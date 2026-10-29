#include "native_process.h"
#define _GNU_SOURCE
#include "guest_memory.h"
#include "runtime_services.h"
#include "bionic_threads.h"
#include <switch.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

/* Android mmap data mappings. PROT_NONE anonymous reserves use lazy RW data
 * aliases. Executable mappings and MAP_FIXED are rejected;
 * guest ELF code uses the dedicated W^X loader. An extra allocator page keeps
 * allocator headers outside the pages whose access permissions we change. */
typedef struct Backing {
    unsigned char *source; size_t first,size;
    struct Backing *next;
} Backing;
typedef struct Mapping {
    unsigned char *base, *permissions;
    size_t size, active;
    int anonymous,lazy,shared_readonly;
    VirtmemReservation *reservation;
    Backing *backing;
    struct Mapping *next;
} Mapping;
static Mapping *maps;
static Mutex map_lock;
int guest_memory_gc_lock_free(void) { return __atomic_load_n(&map_lock,__ATOMIC_RELAXED)==0; }
static FILE *memory_log;
void guest_memory_set_log(FILE *log) { memory_log=log; }
static void memory_budget(const char *phase) {
    if(!memory_log) return;
    u64 total=0,used=0;
    Result tr=svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);
    Result ur=svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);
    struct mallinfo heap=mallinfo();
    size_t reserved=0,live=0,backed=0; unsigned count=0;
    mutexLock(&map_lock);
    for(Mapping *m=maps;m;m=m->next) { reserved+=m->size; live+=m->active*4096; ++count; if(!m->lazy) backed+=m->size; else for(Backing *b=m->backing;b;b=b->next) backed+=b->size; }
    mutexUnlock(&map_lock);
    fprintf(memory_log,"MEMORY_BUDGET phase=%s total=%llu used=%llu total_rc=%x used_rc=%x mappings=%u backing_bytes=%zu live_bytes=%zu\n",phase,(unsigned long long)total,(unsigned long long)used,tr,ur,count,backed,live);
    fprintf(memory_log,"MEMORY_VIRTUAL reserved_bytes=%zu\n",reserved);
    fprintf(memory_log,"MEMORY_HEAP arena_bytes=%zu allocated_bytes=%zu free_chunks_bytes=%zu top_chunk_bytes=%zu kernel_used_includes_reserved_heap=1\n",heap.arena,heap.uordblks,heap.fordblks,heap.keepcost);
}

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
static void *mmap_impl(void *hint, size_t length, int prot, int flags, int fd, int64_t offset) {
    (void)hint;
    if (!length || length > 512u * 1024u * 1024u || offset < 0 || (offset & 4095) ||
        permissions(prot) < 0 || (flags & ~(3 | 0x20 | 0x4000 | 0x20000)) ||
        ((flags&3)!=1 && (flags&3)!=2) ||
        ((flags&3)==1 && ((flags&0x20) || prot!=1))) {
        if(memory_log) fprintf(memory_log,"MMAP_REJECT length=%zu prot=%x flags=%x offset=%lld limit_bytes=%u\n",length,prot,flags,(long long)offset,512u*1024u*1024u);
        fail(EINVAL); return (void *)UINTPTR_MAX;
    }
    size_t rounded = (length + 4095) & ~(size_t)4095;
    Mapping *m = calloc(1, sizeof *m);
    if (!m) { fail(ENOMEM); return (void *)UINTPTR_MAX; }
    /* Unity reserves hundreds of MiB with PROT_NONE. Do not allocate heap
     * backing for a virtual reservation. Reuse the proven own-process code
     * mapping syscalls as RW data aliases; never grant execute permission. */
    if((flags&0x20) && !prot) {
        if(!runtime_process_handle() || !envIsSyscallHinted(0x73) || !envIsSyscallHinted(0x77) || !envIsSyscallHinted(0x78)) { free(m); fail(ENOTSUP); return (void *)UINTPTR_MAX; }
        m->permissions=malloc(rounded/4096);
        if(!m->permissions) { free(m); fail(ENOMEM); return (void *)UINTPTR_MAX; }
        virtmemLock();
        m->base=virtmemFindCodeMemory(rounded,4096);
        if(m->base) m->reservation=virtmemAddReservation(m->base,rounded);
        virtmemUnlock();
        if(!m->reservation) { free(m->permissions); free(m); fail(ENOMEM); return (void *)UINTPTR_MAX; }
        memset(m->permissions,8,rounded/4096);
        m->lazy=1; m->anonymous=1; m->size=rounded; m->active=rounded/4096;
        mutexLock(&map_lock); m->next=maps; maps=m; mutexUnlock(&map_lock);
        if(memory_log) fprintf(memory_log,"MMAP_LAZY_RESERVE address=%p bytes=%zu backing_bytes=0\n",m->base,rounded);
        return m->base;
    }
    m->base = memalign(4096, rounded + 4096);
    m->permissions = malloc(rounded / 4096);
    if (!m->base || !m->permissions) goto oom;
    memset(m->base, 0, rounded);
    if (!(flags & 0x20)) {
        struct stat s;
        if (fstat(runtime_io_native_fd(fd), &s)) goto file_error;
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
    Result rc=svcSetMemoryPermission(m->base, rounded, prot);
    if (R_FAILED(rc)) { if(memory_log) fprintf(memory_log,"MMAP_PERMISSION_FAIL result=%x address=%p size=%zu prot=%x\n",rc,m->base,rounded,prot); errno = ENOTSUP; goto file_error; }
    memset(m->permissions, prot | 8, rounded / 4096);
    m->size = rounded; m->active = rounded / 4096; m->anonymous = !!(flags & 0x20);
    /* Immutable file snapshot supports metadata's read-only MAP_SHARED.
     * Writable shared mappings need writeback/coherence and remain rejected. */
    m->shared_readonly=(flags&3)==1;
    mutexLock(&map_lock); m->next = maps; maps = m; mutexUnlock(&map_lock);
    return m->base;
file_error: {
    int e = errno;
    svcSetMemoryPermission(m->base, rounded, Perm_Rw);
    free(m->base); free(m->permissions); free(m); fail(e); return (void *)UINTPTR_MAX;
}
oom:
    if(memory_log) fprintf(memory_log,"MMAP_ALLOC_FAIL rounded=%zu base=%p permissions=%p\n",rounded,m->base,m->permissions);
    free(m->base); free(m->permissions); free(m); fail(ENOMEM); return (void *)UINTPTR_MAX;
}
static void *b_mmap(void *hint,size_t length,int prot,int flags,int fd,int64_t offset) {
    if(memory_log) { fprintf(memory_log,"MMAP_BEGIN caller=%p hint=%p length=%zu prot=%x flags=%x fd=%d offset=%lld\n",__builtin_return_address(0),hint,length,prot,flags,fd,(long long)offset); fflush(memory_log); }
    void *result=mmap_impl(hint,length,prot,flags,fd,offset);
    int saved=*bionic_errno_ptr();
    if(memory_log) {
        fprintf(memory_log,"MMAP_END result=%p guest_errno=%d failure=%d\n",result,saved,result==(void *)UINTPTR_MAX);
        if(length>=0x100000 || result==(void *)UINTPTR_MAX) memory_budget("mmap_end");
        fflush(memory_log);
    }
    *bionic_errno_ptr()=saved; return result;
}
/* map_lock held. Failed permission publication retains mapped backing so
 * the kernel can never refer to freed source pages. A later unmap cleans it. */
static Result commit_lazy_pages(Mapping *m,size_t first,size_t pages) {
    for(size_t p=first;p<first+pages;) {
        if(m->permissions[p]&16) { ++p; continue; }
        Backing *existing=m->backing;
        while(existing && !(p>=existing->first && p-existing->first<existing->size/4096)) existing=existing->next;
        if(existing) {
            size_t end=p+1;
            while(end<first+pages && end<existing->first+existing->size/4096 && !(m->permissions[end]&16)) ++end;
            size_t bytes=(end-p)*4096;
            void *destination=m->base+p*4096;
            void *source=existing->source+(p-existing->first)*4096;
            virtmemLock();
            Result rc=svcMapProcessCodeMemory(runtime_process_handle(),(u64)destination,(u64)source,bytes);
            virtmemUnlock();
            if(R_FAILED(rc)) return rc;
            for(size_t i=p;i<end;++i) m->permissions[i]|=16;
            rc=svcSetProcessMemoryPermission(runtime_process_handle(),(u64)destination,bytes,Perm_Rw);
            if(R_FAILED(rc)) return rc;
            for(size_t i=p;i<end;++i) m->permissions[i]=(m->permissions[i]&~3)|3;
            p=end; continue;
        }
        size_t end=p+1; while(end<first+pages && !(m->permissions[end]&16)) ++end;
        for(Backing *b=m->backing;b;b=b->next) if(b->first>p && b->first<end) end=b->first;
        size_t bytes=(end-p)*4096;
        Backing *b=calloc(1,sizeof *b);
        if(!b) return MAKERESULT(Module_Libnx,LibnxError_OutOfMemory);
        b->source=memalign(4096,bytes+4096); b->first=p; b->size=bytes;
        if(!b->source) { free(b); return MAKERESULT(Module_Libnx,LibnxError_OutOfMemory); }
        memset(b->source,0,bytes);
        void *destination=m->base+p*4096;
        virtmemLock();
        Result rc=svcMapProcessCodeMemory(runtime_process_handle(),(u64)destination,(u64)b->source,bytes);
        virtmemUnlock();
        if(R_FAILED(rc)) { if(memory_log) fprintf(memory_log,"MMAP_LAZY_MAP_FAIL address=%p bytes=%zu result=%x\n",destination,bytes,rc); free(b->source); free(b); return rc; }
        b->next=m->backing; m->backing=b;
        for(size_t i=p;i<end;++i) m->permissions[i]|=16;
        rc=svcSetProcessMemoryPermission(runtime_process_handle(),(u64)destination,bytes,Perm_Rw);
        if(R_FAILED(rc)) { if(memory_log) fprintf(memory_log,"MMAP_LAZY_PUBLISH_FAIL address=%p bytes=%zu result=%x\n",destination,bytes,rc); return rc; }
        for(size_t i=p;i<end;++i) m->permissions[i]=(m->permissions[i]&~3)|3;
        if(memory_log) fprintf(memory_log,"MMAP_LAZY_COMMIT address=%p bytes=%zu\n",destination,bytes);
        p=end;
    }
    return 0;
}
/* Code-memory aliases reject Perm_None. Remove their destination mapping
 * for guards while retaining the source allocation for a later recommit. */
static Result unmap_lazy_pages(Mapping *m,size_t first,size_t pages) {
    for(Backing *b=m->backing;b;b=b->next) {
        size_t start=first>b->first ? first:b->first;
        size_t end=first+pages;
        if(end>b->first+b->size/4096) end=b->first+b->size/4096;
        for(size_t p=start;p<end;) {
            if(!(m->permissions[p]&16)) { ++p; continue; }
            size_t next=p+1; while(next<end && (m->permissions[next]&16)) ++next;
            virtmemLock();
            Result rc=svcUnmapProcessCodeMemory(runtime_process_handle(),(u64)(m->base+p*4096),(u64)(b->source+(p-b->first)*4096),(next-p)*4096);
            virtmemUnlock();
            if(R_FAILED(rc)) return rc;
            for(size_t i=p;i<next;++i) m->permissions[i]&=~19;
            p=next;
        }
    }
    return 0;
}
static Result release_lazy_backing(Mapping *m) {
    Result rc=unmap_lazy_pages(m,0,m->size/4096);
    if(R_FAILED(rc)) return rc;
    while(m->backing) {
        Backing *b=m->backing;
        m->backing=b->next; free(b->source); free(b);
    }
    return 0;
}
/* Horizon permission operations must stay within a uniform kernel memory
 * region. A Unity reservation contains committed pages, guards and trimmed
 * pages; query and change each current region independently. map_lock held.
 * Keep the guest page table synchronized even on a later partial failure. */
static Result set_mapping_permissions(Mapping *m,size_t first,size_t pages,int prot) {
    if(m->lazy && !prot) return unmap_lazy_pages(m,first,pages);
    size_t end=first+pages;
    for(size_t page=first;page<end;) {
        uintptr_t address=(uintptr_t)m->base+page*4096;
        MemoryInfo info; u32 page_info;
        Result rc=svcQueryMemory(&info,&page_info,address);
        if(R_FAILED(rc)) return rc;
        if(info.addr>address || info.size<4096 || address-info.addr>info.size-4096) return MAKERESULT(Module_Libnx,LibnxError_BadInput);
        size_t count=(info.size-(address-info.addr))/4096;
        if(count>end-page) count=end-page;
        if(info.perm!=(unsigned)prot) {
            rc=m->lazy ? svcSetProcessMemoryPermission(runtime_process_handle(),address,count*4096,prot) : svcSetMemoryPermission((void *)address,count*4096,prot);
            if(R_FAILED(rc)) {
                if(memory_log) fprintf(memory_log,"MEMORY_PERMISSION_RUN_FAIL address=%p bytes=%zu old_perm=%x new_perm=%x result=%x\n",(void *)address,count*4096,info.perm,prot,rc);
                return rc;
            }
        }
        for(size_t p=page;p<page+count;++p) m->permissions[p]=(m->permissions[p]&~3)|prot;
        page+=count;
    }
    return 0;
}
static int protect_impl(void *address, size_t length, int prot) {
    if ((uintptr_t)address % 4096 || !length || length > SIZE_MAX - 4095 || permissions(prot) < 0) return fail(EINVAL);
    size_t size = (length + 4095) & ~(size_t)4095;
    mutexLock(&map_lock);
    Mapping *m = find(address, size);
    if (!m) { mutexUnlock(&map_lock); return fail(ENOMEM); }
    if(m->shared_readonly && (prot&2)) { mutexUnlock(&map_lock); return fail(EACCES); }
    size_t first = ((char *)address - (char *)m->base) / 4096;
    for (size_t p = first; p < first + size / 4096; ++p)
        if (!(m->permissions[p] & 8)) { mutexUnlock(&map_lock); return fail(ENOMEM); }
    Result rc=0;
    if(m->lazy && prot) rc=commit_lazy_pages(m,first,size/4096);
    if(R_SUCCEEDED(rc)) rc=set_mapping_permissions(m,first,size/4096,prot);
    mutexUnlock(&map_lock);
    return R_FAILED(rc) ? fail(rc==MAKERESULT(Module_Libnx,LibnxError_OutOfMemory) ? ENOMEM : ENOTSUP) : 0;
}
static int unmap_impl(void *address, size_t length) {
    if ((uintptr_t)address % 4096 || !length || length > SIZE_MAX - 4095) return fail(EINVAL);
    size_t size = (length + 4095) & ~(size_t)4095;
    mutexLock(&map_lock);
    Mapping *m = find(address, size);
    if (!m) { mutexUnlock(&map_lock); return fail(EINVAL); }
    size_t first = ((char *)address - (char *)m->base) / 4096;
    Result rc = set_mapping_permissions(m,first,size/4096,Perm_None);
    if (R_FAILED(rc)) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
    for (size_t p = first; p < first + size / 4096; ++p) {
        if (m->permissions[p] & 8) --m->active;
        m->permissions[p] &= 16;
    }
    if (!m->active) {
        rc=m->lazy ? release_lazy_backing(m) : set_mapping_permissions(m,0,m->size/4096,Perm_Rw);
        if (R_FAILED(rc)) { if(memory_log) fprintf(memory_log,"MMAP_RELEASE_FAIL result=%x\n",rc); mutexUnlock(&map_lock); return fail(ENOTSUP); }
        if(m->reservation) { virtmemLock(); virtmemRemoveReservation(m->reservation); virtmemUnlock(); }
        Mapping **link = &maps; while (*link != m) link = &(*link)->next;
        *link = m->next;
        if(!m->lazy) free(m->base);
        free(m->permissions); free(m);
    }
    mutexUnlock(&map_lock); return 0;
}
static int advise_impl(void *address, size_t length, int advice) {
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
            unsigned char *page = m->base + p * 4096;
            int old_prot=m->permissions[p]&3;
            if(m->lazy && !(m->permissions[p]&16)) {
                Backing *b=m->backing;
                while(b && !(p>=b->first && p-b->first<b->size/4096)) b=b->next;
                if(!b) continue; /* never committed: already zero */
                if(R_FAILED(commit_lazy_pages(m,p,1))) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
            }
            if (R_FAILED(set_mapping_permissions(m,p,1,Perm_Rw))) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
            memset(page, 0, 4096);
            if (R_FAILED(set_mapping_permissions(m,p,1,old_prot))) { mutexUnlock(&map_lock); return fail(ENOTSUP); }
        }
    }
    mutexUnlock(&map_lock); return 0;
}
static int b_mprotect(void *address,size_t length,int prot) {
    int result=protect_impl(address,length,prot),saved=*bionic_errno_ptr();
    if(memory_log) { fprintf(memory_log,"MPROTECT address=%p size=%zu prot=%x result=%d guest_errno=%d caller=%p\n",address,length,prot,result,saved,__builtin_return_address(0)); fflush(memory_log); }
    *bionic_errno_ptr()=saved; return result;
}
static int b_munmap(void *address,size_t length) {
    int result=unmap_impl(address,length),saved=*bionic_errno_ptr();
    if(memory_log) { fprintf(memory_log,"MUNMAP address=%p size=%zu result=%d guest_errno=%d caller=%p\n",address,length,result,saved,__builtin_return_address(0)); fflush(memory_log); }
    *bionic_errno_ptr()=saved; return result;
}
static int b_madvise(void *address,size_t length,int advice) {
    int result=advise_impl(address,length,advice),saved=*bionic_errno_ptr();
    if(memory_log) { fprintf(memory_log,"MADVISE address=%p size=%zu advice=%d result=%d guest_errno=%d caller=%p\n",address,length,advice,result,saved,__builtin_return_address(0)); fflush(memory_log); }
    *bionic_errno_ptr()=saved; return result;
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
        int shared_errors=0;
        int (*dup_file)(int)=(int (*)(int))runtime_io_resolve("dup");
        int alias=dup_file(fd);
        if(alias<0) ++shared_errors;
        else {
            p=b_mmap(NULL,4096,1,1,alias,0);
            shared_errors+=close_file(alias)!=0;
            if(p==(void *)UINTPTR_MAX) ++shared_errors;
            else {
                shared_errors+=memcmp(p,ELFMAG,4)!=0;
                shared_errors+=R_FAILED(svcQueryMemory(&info,&page_info,(u64)p)) || info.perm!=Perm_R;
                shared_errors+=b_mprotect(p,4096,3)!=-1 || *bionic_errno_ptr()!=13;
                shared_errors+=b_munmap(p,4096)!=0;
            }
        }
        shared_errors+=b_mmap(NULL,4096,3,1,fd,0)!=(void *)UINTPTR_MAX;
        shared_errors+=b_mmap(NULL,4096,1,3,fd,0)!=(void *)UINTPTR_MAX;
        errors+=shared_errors;
        fprintf(log,"MEMORY_SHARED_READONLY errors=%d dup_closed_after_map=CHECKED writable_shared=REJECTED external_write_coherence=UNSUPPORTED\n",shared_errors);
        errors += close_file(fd) != 0;
    }
    errors += b_mmap(NULL, 0, 3, 2 | 0x20, -1, 0) != (void *)UINTPTR_MAX;
    int metadata_errors=0;
    fd=open_file("/il2cpp/Metadata/global-metadata.dat",0);
    struct stat metadata_stat;
    if(fd<0) ++metadata_errors;
    else {
        if(fstat(runtime_io_native_fd(fd),&metadata_stat) || metadata_stat.st_size<=0) ++metadata_errors;
        else {
            size_t size=(metadata_stat.st_size+4095)&~(size_t)4095;
            p=b_mmap(NULL,size,1,1,fd,0);
            if(p==(void *)UINTPTR_MAX) ++metadata_errors;
            else {
                unsigned char block[512];
                for(size_t off=0;off<(size_t)metadata_stat.st_size;) {
                    size_t n=(size_t)metadata_stat.st_size-off; if(n>sizeof block) n=sizeof block;
                    if(runtime_io_read_at(fd,block,n,off)!=(int64_t)n || memcmp(p+off,block,n)) { ++metadata_errors; break; }
                    off+=n;
                }
                for(size_t off=metadata_stat.st_size;off<size;++off) if(p[off]) { ++metadata_errors; break; }
                metadata_errors+=b_munmap(p,size)!=0;
            }
        }
        metadata_errors+=close_file(fd)!=0;
    }
    errors+=metadata_errors;
    fprintf(log,"MEMORY_METADATA_SHARED errors=%d full_file_comparison=CHECKED eof_padding=CHECKED\n",metadata_errors);
    /* Mixed permissions in both directions and munmap over a trimmed hole. */
    int mixed_errors=0;
    p=b_mmap(NULL,24576,3,0x22,-1,0);
    if(p==(void *)UINTPTR_MAX) ++mixed_errors;
    else {
        p[0]=0x41; p[24575]=0x72;
        mixed_errors+=b_mprotect(p+4096,4096,1)!=0;
        mixed_errors+=b_mprotect(p+12288,4096,0)!=0;
        int restored=b_mprotect(p,24576,3)==0;
        if(!restored) ++mixed_errors;
        else {
            mixed_errors+=p[0]!=0x41 || p[24575]!=0x72;
            mixed_errors+=b_mprotect(p+8192,8192,0)!=0;
        }
        mixed_errors+=b_munmap(p+4096,4096)!=0;
        mixed_errors+=b_mprotect(p,24576,3)!=-1;
        mixed_errors+=b_munmap(p,24576)!=0;
    }
    errors+=mixed_errors;
    fprintf(log,"MEMORY_MIXED_SEQUENCE errors=%d permission_runs_and_holes=CHECKED\n",mixed_errors);
    /* Reproduce Unity's reserve / alignment trim / commit / guard lifecycle
     * at a modest size so the test does not consume its 512-MiB reserve. */
    const size_t alignment=0x400000, reserve=alignment*2-4096;
    unsigned char *reservation=b_mmap(NULL,reserve,0,0x22,-1,0);
    int vm_errors=0;
    if(reservation==(void *)UINTPTR_MAX) ++vm_errors;
    else {
        uintptr_t aligned=((uintptr_t)reservation+alignment-1)&~(uintptr_t)(alignment-1);
        size_t prefix=aligned-(uintptr_t)reservation,suffix=reserve-prefix-alignment;
        if(prefix) vm_errors+=b_munmap(reservation,prefix)!=0;
        if(suffix) vm_errors+=b_munmap((void *)(aligned+alignment),suffix)!=0;
        unsigned char *committed=(void *)aligned;
        vm_errors+=b_madvise(committed,16384,0)!=0;
        int writable=b_mprotect(committed,16384,3)==0;
        if(!writable) ++vm_errors;
        else {
            committed[0]=0x6a; committed[16383]=0x9b;
            int guarded=b_mprotect(committed,16384,0)==0;
            vm_errors+=!guarded;
            if(guarded) vm_errors+=R_FAILED(svcQueryMemory(&info,&page_info,(u64)committed)) || info.perm!=Perm_None;
            int restored=b_mprotect(committed,16384,3)==0;
            if(!restored) ++vm_errors;
            else {
                vm_errors+=committed[0]!=0x6a || committed[16383]!=0x9b;
                vm_errors+=b_madvise(committed,16384,4)!=0;
                vm_errors+=committed[0]!=0 || committed[16383]!=0;
                committed[4096]=0x45;
                int middle_guard=b_mprotect(committed+4096,4096,0)==0;
                vm_errors+=!middle_guard;
                if(middle_guard) {
                    vm_errors+=committed[0]!=0 || committed[16383]!=0;
                    vm_errors+=b_madvise(committed+4096,4096,4)!=0;
                    int middle_rw=b_mprotect(committed+4096,4096,3)==0;
                    vm_errors+=!middle_rw;
                    if(middle_rw) vm_errors+=committed[4096]!=0;
                }
            }
        }
        vm_errors+=b_munmap(committed,alignment)!=0;
    }
    errors+=vm_errors;
    fprintf(log,"MEMORY_UNITY_SEQUENCE errors=%d reserve_trim_commit_guard_discard=CHECKED\n",vm_errors);
    /* Six simultaneous reservations exceed the old eager backing capacity.
     * Match Unity's real 256-MiB alignment and 512-MiB-minus-page requests. */
    unsigned char *large[6]={0}; unsigned char *aligned_large[6]={0};
    const size_t large_alignment=0x10000000,large_size=0x1ffff000;
    int lazy_errors=0;
    for(unsigned i=0;i<6;++i) {
        large[i]=b_mmap(NULL,large_size,0,0x22,-1,0);
        if(large[i]==(void *)UINTPTR_MAX) { large[i]=NULL; ++lazy_errors; break; }
        uintptr_t a=((uintptr_t)large[i]+large_alignment-1)&~(uintptr_t)(large_alignment-1);
        size_t prefix=a-(uintptr_t)large[i],suffix=large_size-prefix-large_alignment;
        if(prefix) lazy_errors+=b_munmap(large[i],prefix)!=0;
        if(suffix) lazy_errors+=b_munmap((void *)(a+large_alignment),suffix)!=0;
        aligned_large[i]=(void *)a;
        if(b_mprotect((void *)a,0x42000,3)) { ++lazy_errors; break; }
        aligned_large[i][0]=(unsigned char)(0x31+i);
        aligned_large[i][0x41fff]=(unsigned char)(0x71+i);
        if(b_mprotect((void *)(a+large_alignment-4096),4096,3)) { ++lazy_errors; break; }
        aligned_large[i][large_alignment-1]=(unsigned char)(0xa1+i);
    }
    for(unsigned i=0;i<6;++i) if(large[i]) {
        if(!lazy_errors) {
            lazy_errors+=aligned_large[i][0]!=(unsigned char)(0x31+i);
            lazy_errors+=aligned_large[i][0x41fff]!=(unsigned char)(0x71+i);
            lazy_errors+=aligned_large[i][large_alignment-1]!=(unsigned char)(0xa1+i);
        }
        lazy_errors+=b_munmap(large[i],large_size)!=0;
    }
    errors+=lazy_errors;
    fprintf(log,"MEMORY_LAZY_SEQUENCE errors=%d reservations=6 requested_virtual_bytes=%zu expected_backing_bytes=%u live_device_validation=REQUIRED\n",lazy_errors,large_size*6,6*(0x42000+4096));
    fprintf(log, "MEMORY_ABI_DONE errors=%d protection=CHECKED partial_unmap=CHECKED filemap=CHECKED\n", errors);
    return errors ? -1 : 0;
}
