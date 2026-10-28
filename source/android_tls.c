#include "android_tls.h"
#include <switch.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

/* Shadow the well-known stack-guard and self slots only. This is not a
 * replacement for Bionic's pthread_internal_t or general ELF TLS support. */
struct AndroidTlsBlock {
    uintptr_t slots[64];
    uintptr_t owner;
    size_t references;
    struct AndroidTlsBlock *next;
};
static int tls_slot = -1;
static size_t slot_offset;
static Mutex blocks_lock;
static AndroidTlsBlock *blocks;
static FILE *runtime_log;
static void restore_shadow_during_destructors(void *block) {
    /* Installed libnx clears each native slot immediately before invoking
     * its destructor, in ascending slot order. Rebind this non-owning slot
     * so later Android key destructors can still execute guarded code. The
     * backing block is released only after native join completes. */
    threadTlsSet(tls_slot, block);
}

AndroidTlsBlock *android_tls_allocate(void) {
    if (tls_slot < 0) return NULL;
    AndroidTlsBlock *b = calloc(1, sizeof *b);
    if (!b) return NULL;
    b->references = 1;
    b->slots[0] = (uintptr_t)b;
    b->slots[5] = randomGet64() & ~(uintptr_t)0xff;
    if (!b->slots[5]) b->slots[5] = UINT64_C(0x9f3e64d70ac81200);
    mutexLock(&blocks_lock); b->next = blocks; blocks = b; mutexUnlock(&blocks_lock);
    return b;
}
int android_tls_bind(AndroidTlsBlock *b) {
    Thread *self = threadGetSelf();
    if (!b || !self || !self->tls_array || tls_slot < 0) return -1;
    uintptr_t offset = (uintptr_t)&self->tls_array[tls_slot] - (uintptr_t)armGetTls();
    if (offset != slot_offset) return -1;
    mutexLock(&blocks_lock); b->owner = (uintptr_t)pthread_self(); mutexUnlock(&blocks_lock);
    threadTlsSet(tls_slot, b);
    return 0;
}
void android_tls_retain(AndroidTlsBlock *b) {
    mutexLock(&blocks_lock); ++b->references; mutexUnlock(&blocks_lock);
}
void android_tls_discard(AndroidTlsBlock *b) {
    if (!b) return;
    mutexLock(&blocks_lock);
    AndroidTlsBlock **link = &blocks;
    while (*link && *link != b) link = &(*link)->next;
    if (*link && !--b->references) { *link = b->next; free(b); }
    mutexUnlock(&blocks_lock);
}
void android_tls_record_owner(AndroidTlsBlock *b, uintptr_t native_thread) {
    mutexLock(&blocks_lock); b->owner = native_thread; mutexUnlock(&blocks_lock);
}
AndroidTlsBlock *android_tls_for_thread(uintptr_t native_thread) {
    /* Retain before join; release only after exit/key destructors finish.
     * Looking up by a freed native handle after join risks address reuse. */
    mutexLock(&blocks_lock);
    AndroidTlsBlock *result = NULL;
    for (AndroidTlsBlock *b = blocks; b; b = b->next)
        if (b->owner == native_thread) { ++b->references; result = b; break; }
    mutexUnlock(&blocks_lock);
    return result;
}
void *android_tls_current(void) { return tls_slot < 0 ? NULL : threadTlsGet(tls_slot); }
size_t android_tls_slot_offset(void) { return slot_offset; }
size_t android_tls_live_blocks(void) {
    size_t count = 0;
    mutexLock(&blocks_lock);
    for (AndroidTlsBlock *b = blocks; b; b = b->next) ++count;
    mutexUnlock(&blocks_lock);
    return count;
}
int android_tls_initialize(FILE *log) {
    runtime_log = log;
    if (tls_slot >= 0) return android_tls_current() ? 0 : -1;
    /* The destructor restores the slot; join, never this callback, frees it. */
    tls_slot = threadTlsAlloc(restore_shadow_during_destructors);
    if (tls_slot < 0) { fprintf(log, "TLS_INIT_FAIL no native TLS slot\n"); return -1; }
    Thread *self = threadGetSelf();
    if (!self || !self->tls_array) return -1;
    slot_offset = (uintptr_t)&self->tls_array[tls_slot] - (uintptr_t)armGetTls();
    if (slot_offset % 8 || slot_offset >= 512) {
        fprintf(log, "TLS_INIT_FAIL unexpected SDK TLS layout\n"); return -1;
    }
    AndroidTlsBlock *b = android_tls_allocate();
    if (!b || android_tls_bind(b)) {
        android_tls_discard(b); fprintf(log, "TLS_INIT_FAIL allocation/bind\n"); return -1;
    }
    fprintf(log, "TLS_INIT_PASS native_slot=%d offset=%zu shadow=%p guard_slot=5\n",
            tls_slot, slot_offset, (void *)b);
    return 0;
}
static void __attribute__((noreturn)) android_stack_chk_fail(void) {
    if (runtime_log) {
        fprintf(runtime_log, "TLS_STACK_CHK_FAIL: guarded code detected stack corruption\n");
        fflush(runtime_log);
    }
    fprintf(stderr, "SWAG-NX: Android stack protector detected corruption\n");
    abort();
}
uintptr_t android_tls_resolve(const char *name) {
    if (!strcmp(name, "__stack_chk_fail")) return (uintptr_t)android_stack_chk_fail;
    return 0;
}

static int read_at(FILE *f, uint64_t offset, void *out, size_t bytes, uint64_t limit) {
    return offset <= LONG_MAX && offset <= limit && bytes <= limit - offset &&
           !fseek(f, (long)offset, SEEK_SET) && fread(out, 1, bytes, f) == bytes;
}
static int compare_sites(const void *a, const void *b) {
    uint64_t x = ((const TlsSite *)a)->va, y = ((const TlsSite *)b)->va;
    return (x > y) - (x < y);
}
int android_tls_plan(const char *path, TlsPatchPlan *plan, FILE *log) {
    memset(plan, 0, sizeof *plan);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    Elf64_Ehdr h;
    int result = -1;
    if (fseek(f, 0, SEEK_END)) goto done;
    long bytes = ftell(f);
    if (bytes < 0 || !read_at(f, 0, &h, sizeof h, bytes) ||
        memcmp(h.e_ident, ELFMAG, SELFMAG) || h.e_ident[EI_CLASS] != ELFCLASS64 ||
        h.e_ident[EI_DATA] != ELFDATA2LSB || h.e_machine != EM_AARCH64 || h.e_type != ET_DYN ||
        h.e_shentsize != sizeof(Elf64_Shdr) || !h.e_shnum || h.e_shnum > 4096 ||
        h.e_shoff > (uint64_t)bytes ||
        (uint64_t)h.e_shnum * sizeof(Elf64_Shdr) > (uint64_t)bytes - h.e_shoff) goto done;
    /* Scan SHF_EXECINSTR sections, never rodata/unwind bytes in an RX PT_LOAD. */
    for (size_t i = 0; i < h.e_shnum; ++i) {
        Elf64_Shdr s;
        if (!read_at(f, h.e_shoff + i * sizeof s, &s, sizeof s, bytes)) goto done;
        if (s.sh_type != SHT_PROGBITS || !(s.sh_flags & SHF_EXECINSTR)) continue;
        if (!(s.sh_flags & SHF_ALLOC) || s.sh_addr % 4 || s.sh_offset % 4 || s.sh_size % 4 ||
            s.sh_size > 128u * 1024u * 1024u || s.sh_offset > (uint64_t)bytes ||
            s.sh_size > (uint64_t)bytes - s.sh_offset || s.sh_addr > UINT64_MAX - s.sh_size) goto done;
        /* A small fixed chunk avoids a second complete copy of the text. */
        uint32_t chunk[1024];
        for (uint64_t offset = 0; offset < s.sh_size;) {
            size_t count = (s.sh_size - offset) / 4;
            if (count > 1024) count = 1024;
            if (!read_at(f, s.sh_offset + offset, chunk, count * 4, bytes)) goto done;
            for (size_t n = 0; n < count; ++n) {
                if ((chunk[n] & 0xffffffe0u) != 0xd53bd040u) continue;
                if (plan->count >= 65536) goto done;
                TlsSite *grown = realloc(plan->sites, (plan->count + 1) * sizeof *grown);
                if (!grown) goto done;
                plan->sites = grown;
                plan->sites[plan->count++] = (TlsSite){s.sh_addr + offset + n * 4, chunk[n]};
            }
            offset += count * 4;
        }
    }
    plan->extra_bytes = (plan->count * 12 + 4095) & ~(size_t)4095;
    if (plan->count) qsort(plan->sites, plan->count, sizeof *plan->sites, compare_sites);
    fprintf(log, "TLS_PLAN %s reads=%zu trampoline_bytes=%zu\n", path, plan->count, plan->extra_bytes);
    result = 0;
done:
    fclose(f);
    if (result) { fprintf(log, "TLS_PLAN_FAIL %s\n", path); android_tls_plan_free(plan); }
    return result;
}
void android_tls_plan_free(TlsPatchPlan *plan) { free(plan->sites); memset(plan, 0, sizeof *plan); }
static int encode_branch(uint64_t from, uint64_t to, uint32_t *instruction) {
    if (from > INT64_MAX || to > INT64_MAX) return -1;
    int64_t delta = (int64_t)to - (int64_t)from;
    if (delta % 4 || delta < -134217728 || delta > 134217724) return -1;
    *instruction = 0x14000000u | ((uint32_t)(delta / 4) & 0x03ffffffu);
    return 0;
}
static int executable_site(const AndroidElf *m, uint64_t va) {
    for (size_t i = 0; i < m->phnum; ++i) {
        const Elf64_Phdr *p = &m->ph[i];
        if (p->p_type == PT_LOAD && (p->p_flags & PF_X) && va >= p->p_vaddr &&
            p->p_memsz >= 4 && va - p->p_vaddr <= p->p_memsz - 4) return 1;
    }
    return 0;
}
int android_tls_apply(AndroidElf *m, size_t allocation_size, const TlsPatchPlan *plan, FILE *log) {
    if (tls_slot < 0 || !android_tls_current() || !m->image || m->published || m->size % 4096 ||
        allocation_size != m->size + plan->extra_bytes ||
        (plan->count && m->phnum >= 32)) return -1;
    size_t base = m->size;
    for (size_t i = 0; i < plan->count; ++i) {
        const TlsSite *site = &plan->sites[i];
        uint32_t word, branch;
        if (!executable_site(m, site->va) || site->va % 4) return -1;
        memcpy(&word, m->image + site->va, 4);
        if (word != site->instruction || (word & 0xffffffe0u) != 0xd53bd040u ||
            encode_branch(site->va, base + i * 12, &branch) ||
            encode_branch(base + i * 12 + 8, site->va + 4, &branch)) return -1;
        if (i && plan->sites[i-1].va >= site->va) return -1;
    }
    /* Every stub touches only the destination register of the replaced MRS.
     * No BL/call, no scratch GPR, no NZCV/FP/SIMD changes, no SP changes:
     * mrs Rt,tpidrro_el0; ldr Rt,[Rt,#native_slot_offset]; b original+4. */
    for (size_t i = 0; i < plan->count; ++i) {
        const TlsSite *site = &plan->sites[i];
        uint32_t r = site->instruction & 31u;
        uint32_t stub[3], branch;
        stub[0] = 0xd53bd060u | r;
        stub[1] = 0xf9400000u | ((slot_offset / 8) << 10) | (r << 5) | r;
        if (encode_branch(base + i * 12 + 8, site->va + 4, &stub[2]) ||
            encode_branch(site->va, base + i * 12, &branch)) return -1;
        if (r == 31) branch = 0xd503201fu; /* Discarded MRS result is a NOP. */
        memcpy(m->image + base + i * 12, stub, sizeof stub);
        memcpy(m->image + site->va, &branch, 4);
    }
    if (plan->count) {
        m->ph[m->phnum++] = (Elf64_Phdr){.p_type = PT_LOAD, .p_flags = PF_R | PF_X,
            .p_vaddr = base, .p_memsz = plan->extra_bytes, .p_align = 4096};
    }
    m->size = allocation_size;
    fprintf(log, "TLS_PATCH_PASS sites=%zu image=%zu native_tls_register_unchanged=1\n", plan->count, m->size);
    return 0;
}
