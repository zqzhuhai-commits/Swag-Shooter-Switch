#define _GNU_SOURCE
#include "guest_memory.h"
#include "runtime_services.h"
#include "bionic_threads.h"
#include <switch.h>
#include <switch/kernel/virtmem.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

/* Android mmap data mappings. Executable mappings and MAP_FIXED are rejected;
 * guest ELF code uses the dedicated W^X loader. An extra allocator page keeps
 * allocator headers outside the pages whose access permissions we change. */
typedef struct Mapping {
    unsigned char *base, *permissions;
    size_t size, active;
    int anonymous;
    int lazy;
    VirtmemReservation *reservation;
    struct Mapping *next;
} Mapping;
static Mapping *maps;
static Mutex map_lock;
static int fail(int error) { *bionic_errno_ptr() = bionic_error(error); return -1; }
static Mapping *find(void *address, size_t size) {
    uintptr_t a = (uintptr_t)address;
    for (Mapping *m = maps; m; m = m->next) {
        uintptr_t base = (uintptr_t)m->base;
        if (a >= base && a - base <= m->size && size <= m->size - (a - base)) return m;
    }
    return NULL;
}
static int permissions(int prot) { return (prot & ~3) ? -1 : prot; }
static void *b_mmap(void *hint, size_t length, int prot, int flags, int fd, int64_t offset) {
    (void)hint;
    if (!length || length > 512u * 1024u * 1024u || offset < 0 || (offset & 4095) ||
        permissions(prot) < 0 || (flags & ~(2 | 0x20 | 0x4000 | 0x20000)) || !(flags & 2)) {
        fail(EINVAL); return (void *)UINTPTR_MAX;
    }
    size_t rounded = (length + 4095) & ~(size_t)4095;
    Mapping *m = calloc(1, sizeof *m);
    if (!m) { fail(ENOMEM); return (void *)UINTPTR_MAX; }
    /* A PROT_NONE anonymous mmap is address-space reservation, not a
     * request to allocate and zero hundreds of MiB of Switch heap. */
    if ((flags & 0x20) && prot == 0) {
        virtmemLock();
        void *base = virtmemFindAslr(rounded, 4096);
        VirtmemReservation *rv = base ? virtmemAddReservation(base, rounded) : NULL;
        virtmemUnlock();
        if (!rv) goto oom;
        m->base = base;
        m->reservation = rv;
        m->lazy = 1;
        m->permissions = malloc(rounded / 4096);
        if (!m->permissions) {
            virtmemLock(); virtmemRemoveReservation(rv); virtmemUnlock();
            m->reservation = NULL;
            goto oom;
        }
        memset(m->permissions, 8, rounded / 4096);
        m->size = rounded; m->active = rounded / 4096; m->anonymous = 1;
        mutexLock(&map_lock); m->next = maps; maps = m; mutexUnlock(&map_lock);
        return m->base;
    }
    m->base = memalign(4096, rounded + 4096);
    m->permissions = malloc(rounded / 4096);
    if (!m->base || !m->permissions) goto oom;
    memset(m->base, 0, rounded);
    if (!(flags & 0x20)) {
        struct stat s;
        if (fstat(fd, &s)) goto file_error;
        if (offset > s.st_size || length > ((uint64_t)(s.st_size - offset) + 4095u) / 4096u * 4096u) { errno = EINVAL; goto file_error; }
        size_t bytes = (uint64_t)(s.st_size - offset) < length ? (size_t)(s.st_size - offset) : length;
        size_t got = 0;
        while (got < bytes) {
            int64_t n = runtime_io_read_at(fd, m->base + got, bytes - got, offset + got);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { if (!n) errno = EIO; goto file_error; }
            got += n;
        }
    }
    if (R_FAILED(svcSetMemoryPermission(m->base, rounded, prot))) { errno = ENOTSUP; goto file_error; }
    memset(m->permissions, prot | 8, rounded / 4096);
    m->size = rounded; m->active = rounded / 4096; m->anonymous = !!(flags & 0x20);
    mutexLock(&map_lock); m->next = maps; maps = m; mutexUnlock(&map_lock);
    return m->base;
file_error: {
    int e = errno;
    svcSetMemoryPermission(m->base, rounded, Perm_Rw);
    free(m->base); free(m->permissions); free(m); fail(e); return (void *)UINTPTR_MAX;
}
oom:
    free(m->base); free(m->permissions); free(m); fail(ENOMEM); return (void *)UINTPTR_MAX;
}
static int b_mprotect(void *address, size_t length, int prot) {
    if ((uintptr_t)address % 4096 || !length || length > SIZE_MAX - 4095 || permissions(prot) < 0) return fail(EINVAL);
    size_t size = (length + 4095) & ~(size_t)4095;
    mutexLock(&map_lock);
    Mapping *m = find(address, size);
    if (!m) { mutexUnlock(&map_lock); return fail(ENOMEM); }
    size_t first = ((char *)address - (char *)m->base) / 4096;
    for (size_t p = first; p < first + size / 4096; ++p)
        if (!(m->permissions[p] & 8)) { mutexUnlock(&map_lock); return fail(ENOMEM); }
    if (m->lazy) {
        /* The reservation has no physical backing until a non-NONE protect.
         * Commit only the newly accessible spans. Bit 0x10 tracks backing. */
        if (prot != 0) {
            for (size_t p = first; p < first + size / 4096;) {
                if (m->permissions[p] & 0x10) { ++p; continue; }
                size_t end = p + 1;
                while (end < first + size / 4096 && !(m->permissions[end] & 0x10)) ++end;
                Result mapped = svcMapPhysicalMemory(m->base + p * 4096, (end - p) * 4096);
                if (R_FAILED(mapped)) { mutexUnlock(&map_lock); return fail(ENOMEM); }
                for (size_t j = p; j < end; ++j) m->permissions[j] |= 0x10;
                p = end;
            }
        }
        for (size_t p = first; p < first + size / 4096;) {
            if (!(m->permissions[p] & 0x10)) { ++p; continue; }
            size_t end = p + 1;
            while (end < first + size / 4096 && (m->permissions[end] & 0x10)) ++end;
            Result rc = svcSetMemoryPermission(m->base + p * 4096, (end - p) * 4096, prot);
            if (R_FAILED(rc)) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
            p = end;
        }
        for (size_t p = first; p < first + size / 4096; ++p)
            m->permissions[p] = (m->permissions[p] & 0x10) | 8 | prot;
        mutexUnlock(&map_lock); return 0;
    }
    Result rc = svcSetMemoryPermission(address, size, prot);
    if (R_SUCCEEDED(rc)) memset(m->permissions + first, prot | 8, size / 4096);
    mutexUnlock(&map_lock);
    return R_FAILED(rc) ? fail(ENOTSUP) : 0;
}
static int b_munmap(void *address, size_t length) {
    if ((uintptr_t)address % 4096 || !length || length > SIZE_MAX - 4095) return fail(EINVAL);
    size_t size = (length + 4095) & ~(size_t)4095;
    mutexLock(&map_lock);
    Mapping *m = find(address, size);
    if (!m) { mutexUnlock(&map_lock); return fail(EINVAL); }
    size_t first = ((char *)address - (char *)m->base) / 4096;
    Result rc = 0;
    if (m->lazy) {
        /* Unmap physical backing for just the released spans; keep the
         * virtual reservation until every page has been released. */
        for (size_t p = first; p < first + size / 4096;) {
            if (!(m->permissions[p] & 0x10)) { ++p; continue; }
            size_t end = p + 1;
            while (end < first + size / 4096 && (m->permissions[end] & 0x10)) ++end;
            rc = svcUnmapPhysicalMemory(m->base + p * 4096, (end - p) * 4096);
            if (R_FAILED(rc)) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
            p = end;
        }
    } else {
        rc = svcSetMemoryPermission(address, size, Perm_None);
        if (R_FAILED(rc)) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
    }
    for (size_t p = first; p < first + size / 4096; ++p) {
        if (m->permissions[p] & 8) --m->active;
        m->permissions[p] = 0;
    }
    if (!m->active) {
        if (!m->lazy) {
            rc = svcSetMemoryPermission(m->base, m->size, Perm_Rw);
            if (R_FAILED(rc)) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
        }
        Mapping **link = &maps; while (*link != m) link = &(*link)->next;
        *link = m->next;
        if (m->lazy) {
            virtmemLock(); virtmemRemoveReservation(m->reservation); virtmemUnlock();
        } else free(m->base);
        free(m->permissions); free(m);
    }
    mutexUnlock(&map_lock); return 0;
}
static int b_madvise(void *address, size_t length, int advice) {
    if ((uintptr_t)address % 4096 || !length || length > SIZE_MAX - 4095) return fail(EINVAL);
    if (advice != 0 && advice != 1 && advice != 2 && advice != 3 && advice != 4 && advice != 8) return fail(EINVAL);
    size_t size = (length + 4095) & ~(size_t)4095;
    mutexLock(&map_lock);
    Mapping *m = find(address, size);
    if (!m) { mutexUnlock(&map_lock); return fail(ENOMEM); }
    size_t first = ((char *)address - (char *)m->base) / 4096;
    for (size_t p = first; p < first + size / 4096; ++p)
        if (!(m->permissions[p] & 8)) { mutexUnlock(&map_lock); return fail(ENOMEM); }
    if (advice == 4 || advice == 8) {
        if (!m->anonymous) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
        for (size_t p = first; p < first + size / 4096; ++p) {
            if (m->lazy && !(m->permissions[p] & 0x10)) continue;
            unsigned char *page = m->base + p * 4096;
            if (R_FAILED(svcSetMemoryPermission(page, 4096, Perm_Rw))) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
            memset(page, 0, 4096);
            if (R_FAILED(svcSetMemoryPermission(page, 4096, m->permissions[p] & 3))) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
        }
    }
    mutexUnlock(&map_lock); return 0;
}
typedef struct { const char *name; uintptr_t address; } MemoryBinding;
#define MEM_BIND(name, fn) {name, (uintptr_t)(fn)}
static const MemoryBinding bindings[] = {
    MEM_BIND("mmap", b_mmap), MEM_BIND("mmap64", b_mmap), MEM_BIND("mprotect", b_mprotect),
    MEM_BIND("munmap", b_munmap), MEM_BIND("madvise", b_madvise)
};
uintptr_t guest_memory_resolve(const char *name) {
    for (size_t i = 0; i < sizeof bindings / sizeof *bindings; ++i)
        if (!strcmp(name, bindings[i].name)) return bindings[i].address;
    return 0;
}
int guest_memory_selftest(FILE *log) {
    int errors = 0;
    unsigned char *p = b_mmap(NULL, 12288, 3, 2 | 0x20, -1, 0);
    if (p == (void *)UINTPTR_MAX) { fprintf(log, "MEMORY_ABI_FAIL anonymous errno=%d\n", *bionic_errno_ptr()); return -1; }
    memset(p, 0x6d, 12288);
    errors += b_mprotect(p, 4096, 1) != 0;
    MemoryInfo info; u32 page_info;
    errors += R_FAILED(svcQueryMemory(&info, &page_info, (u64)p)) || info.perm != Perm_R;
    errors += p[0] != 0x6d || b_mprotect(p, 4096, 3) != 0;
    errors += b_madvise(p, 4096, 4) != 0 || p[0] != 0;
    errors += b_munmap(p + 4096, 4096) != 0;
    errors += b_mprotect(p + 4096, 4096, 3) != -1;
    errors += b_munmap(p, 4096) != 0;
    errors += b_munmap(p + 8192, 4096) != 0;
    int (*open_file)(const char *, int, ...) = (int (*)(const char *, int, ...))runtime_io_resolve("open");
    int (*close_file)(int) = (int (*)(int))runtime_io_resolve("close");
    int fd = open_file("libmain.so", 0);
    if (fd < 0) ++errors;
    else {
        p = b_mmap(NULL, 4096, 1, 2, fd, 0);
        if (p == (void *)UINTPTR_MAX) { ++errors; fprintf(log, "MEMORY_FILEMAP_FAIL errno=%d\n", *bionic_errno_ptr()); }
        else { errors += memcmp(p, ELFMAG, 4) != 0; errors += b_munmap(p, 4096) != 0; }
        errors += close_file(fd) != 0;
    }
    /* Regression: multiple Unity-sized PROT_NONE reservations must not
     * allocate multiple 512 MiB backing buffers merely to reserve VA. */
    unsigned char *reserved[6] = {0};
    for (size_t i = 0; i < 6; ++i) {
        reserved[i] = b_mmap(NULL, 536866816, 0, 2 | 0x20, -1, 0);
        if (reserved[i] == (void *)UINTPTR_MAX) { ++errors; reserved[i] = NULL; break; }
    }
    if (reserved[0]) {
        errors += b_mprotect(reserved[0] + 4096, 4096, 3) != 0;
        if (!errors) { reserved[0][4096] = 0x5a; errors += reserved[0][4096] != 0x5a; }
        errors += b_mprotect(reserved[0] + 4096, 4096, 0) != 0;
    }
    for (size_t i = 0; i < 6; ++i) if (reserved[i])
        errors += b_munmap(reserved[i], 536866816) != 0;
    fprintf(log, "MEMORY_LAZY_RESERVE errors=%d reservations=6 bytes_each=536866816\\n", errors);
    errors += b_mmap(NULL, 0, 3, 2 | 0x20, -1, 0) != (void *)UINTPTR_MAX;
    fprintf(log, "MEMORY_ABI_DONE errors=%d protection=CHECKED partial_unmap=CHECKED filemap=CHECKED\n", errors);
    return errors ? -1 : 0;
}
