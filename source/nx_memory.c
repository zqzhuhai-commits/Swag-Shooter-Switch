#include "nx_memory.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

int nx_memory_prepare(NxModuleMemory *m, size_t size, FILE *log) {
    memset(m, 0, sizeof *m);
    if (!size || size > 256u * 1024u * 1024u) return -1;
    m->size = (size + 4095) & ~(size_t)4095;
    m->staging = memalign(4096, m->size);
    if (!m->staging) { fprintf(log, "Staging allocation failed: %zu\n", m->size); return -1; }
    memset(m->staging, 0, m->size);
    virtmemLock();
    m->execution = virtmemFindCodeMemory(m->size, 0x4000);
    if (m->execution) m->reservation = virtmemAddReservation(m->execution, m->size);
    virtmemUnlock();
    if (!m->reservation) {
        fprintf(log, "Code address reservation failed\n");
        free(m->staging); memset(m, 0, sizeof *m); return -1;
    }
    return 0;
}
int nx_memory_publish(NxModuleMemory *m, AndroidElf *elf, FILE *log) {
    Handle proc = envGetOwnProcessHandle();
    if (!proc || !envIsSyscallHinted(0x73) || !envIsSyscallHinted(0x77) ||
        !envIsSyscallHinted(0x78)) {
        fprintf(log, "Code mapping unavailable: process=%x svc73=%d svc77=%d svc78=%d\n",
                proc, envIsSyscallHinted(0x73), envIsSyscallHinted(0x77), envIsSyscallHinted(0x78));
        return -1;
    }
    if (!m->reservation || m->mapped || elf->image != m->staging ||
        elf->bias != (uintptr_t)m->execution || elf->size != m->size || elf->relocated != 1 || elf->published)
        return -1;
    size_t pages = m->size / 4096;
    unsigned char *perms = calloc(pages, 1);
    if (!perms) return -1;
    for (size_t i = 0; i < elf->phnum; ++i) {
        const Elf64_Phdr *p = &elf->ph[i];
        if (p->p_type != PT_LOAD || !p->p_memsz) continue;
        if (p->p_vaddr > m->size || p->p_memsz > m->size - p->p_vaddr) { free(perms); return -1; }
        unsigned char perm = ((p->p_flags & PF_R) ? Perm_R : 0) |
                             ((p->p_flags & PF_W) ? Perm_W : 0) |
                             ((p->p_flags & PF_X) ? Perm_X : 0);
        size_t end = (p->p_vaddr + p->p_memsz + 4095) / 4096;
        for (size_t page = p->p_vaddr / 4096; page < end; ++page) {
            perms[page] |= perm;
            if (perms[page] != 0 && perms[page] != Perm_R &&
                perms[page] != Perm_Rw && perms[page] != Perm_Rx) {
                fprintf(log, "Invalid or overlapping writable/executable ELF pages\n");
                free(perms); return -1;
            }
        }
    }
    armDCacheFlush(m->staging, m->size);
    virtmemLock();
    Result rc = svcMapProcessCodeMemory(proc, (u64)m->execution, (u64)m->staging, m->size);
    if (R_SUCCEEDED(rc)) {
        m->mapped = 1;
        virtmemRemoveReservation(m->reservation);
        m->reservation = NULL;
    }
    virtmemUnlock();
    if (R_FAILED(rc)) { fprintf(log, "MapProcessCodeMemory=%08x\n", rc); free(perms); return -1; }
    /* staging becomes inaccessible after mapping. Only execution is accessed. */
    for (size_t start = 0; start < pages;) {
        size_t end = start + 1;
        while (end < pages && perms[end] == perms[start]) ++end;
        /* Newly mapped pages already have no access; leave holes that way. */
        if (perms[start]) {
            rc = svcSetProcessMemoryPermission(proc, (u64)m->execution + start * 4096,
                                                (end - start) * 4096, perms[start]);
            if (R_FAILED(rc)) {
                fprintf(log, "SetProcessMemoryPermission page=%zu perm=%u rc=%08x\n",
                        start, perms[start], rc);
                free(perms); return -1;
            }
            if (perms[start] & Perm_X)
                armICacheInvalidate((char *)m->execution + start * 4096, (end - start) * 4096);
        }
        start = end;
    }
    free(perms);
    /* The staging alias is inaccessible after publication. Retarget metadata
     * to the readable execution image for later dlsym/export lookups. */
    if (elf->sym) elf->sym = (Elf64_Sym *)((char *)m->execution + ((char *)elf->sym - (char *)m->staging));
    if (elf->str) elf->str = (char *)m->execution + (elf->str - (char *)m->staging);
    elf->image = m->execution;
    elf->published = 1;
    fprintf(log, "Published image=%p bytes=%zu with separate RX/RW pages\n", m->execution, m->size);
    return 0;
}
int nx_memory_close(NxModuleMemory *m, FILE *log) {
    virtmemLock();
    if (m->mapped) {
        Result rc = svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)m->execution,
                                              (u64)m->staging, m->size);
        if (R_FAILED(rc)) {
            virtmemUnlock();
            fprintf(log, "Unmap failed=%08x; backing retained until process exit\n", rc);
            return -1;
        }
    }
    if (m->reservation) virtmemRemoveReservation(m->reservation);
    virtmemUnlock();
    free(m->staging); memset(m, 0, sizeof *m);
    return 0;
}
int nx_memory_smoke_test(FILE *log) {
    NxModuleMemory m;
    if (nx_memory_prepare(&m, 4096, log)) return -1;
    /* AArch64: mov w0,#42; ret. This test executes no Android code. */
    const uint32_t instructions[] = {0x52800540, 0xd65f03c0};
    memcpy(m.staging, instructions, sizeof instructions);
    AndroidElf elf = {0};
    elf.image = m.staging; elf.size = m.size; elf.bias = (uintptr_t)m.execution;
    elf.phnum = 1; elf.ph[0].p_type = PT_LOAD; elf.ph[0].p_memsz = 4096;
    elf.ph[0].p_flags = PF_R | PF_X; elf.relocated = 1;
    int result = -1;
    if (!nx_memory_publish(&m, &elf, log)) {
        int value = ((int (*)(void))m.execution)();
        fprintf(log, "Native executable-memory test: expected=42 actual=%d\n", value);
        if (value == 42) result = 0;
    }
    if (nx_memory_close(&m, log)) result = -1;
    return result;
}
