#include "elf_loader.h"
#include <limits.h>
#include <string.h>

#define MAX_IMAGE (256u * 1024u * 1024u)
static int range(uint64_t off, uint64_t len, uint64_t size) {
    return off <= size && len <= size - off;
}
static int read_at(FILE *f, uint64_t off, void *p, size_t n) {
    return off <= LONG_MAX && fseek(f, (long)off, SEEK_SET) == 0 &&
           fread(p, 1, n, f) == n;
}
static int headers(FILE *f, AndroidElf *m, FILE *log) {
    Elf64_Ehdr h;
    if (fseek(f, 0, SEEK_END)) return -1;
    long end = ftell(f);
    if (end < 0 || !read_at(f, 0, &h, sizeof h)) return -1;
    if (memcmp(h.e_ident, ELFMAG, SELFMAG) || h.e_ident[EI_CLASS] != ELFCLASS64 ||
        h.e_ident[EI_DATA] != ELFDATA2LSB || h.e_ident[EI_VERSION] != EV_CURRENT ||
        h.e_version != EV_CURRENT || h.e_type != ET_DYN ||
        h.e_machine != EM_AARCH64 || h.e_ehsize != sizeof h ||
        h.e_phentsize != sizeof(Elf64_Phdr) || !h.e_phnum || h.e_phnum > 32 ||
        !range(h.e_phoff, h.e_phnum * sizeof(Elf64_Phdr), (uint64_t)end)) return -1;
    m->phnum = h.e_phnum;
    if (!read_at(f, h.e_phoff, m->ph, h.e_phnum * sizeof(Elf64_Phdr))) return -1;
    size_t high = 0;
    unsigned loads = 0;
    for (size_t i = 0; i < m->phnum; ++i) {
        const Elf64_Phdr *p = &m->ph[i];
        if (p->p_type == PT_TLS) {
            fprintf(log, "Unsupported PT_TLS; Android TLS ABI must be implemented first\n");
            return -1;
        }
        if (p->p_type != PT_LOAD) continue;
        if (p->p_filesz > p->p_memsz ||
            !range(p->p_offset, p->p_filesz, (uint64_t)end) ||
            !range(p->p_vaddr, p->p_memsz, MAX_IMAGE) ||
            ((p->p_flags & (PF_W | PF_X)) == (PF_W | PF_X)) ||
            (p->p_align > 1 && ((p->p_align & (p->p_align - 1)) ||
             (p->p_offset % p->p_align) != (p->p_vaddr % p->p_align)))) return -1;
        size_t top = (size_t)(p->p_vaddr + p->p_memsz);
        if (top > high) high = top;
        ++loads;
    }
    if (!loads || !high) return -1;
    m->size = (high + 4095) & ~(size_t)4095;
    return 0;
}
/* Require a range to be inside one PT_LOAD, not just the enclosing allocation. */
static void *at(const AndroidElf *m, uint64_t va, uint64_t n) {
    if (!range(va, n, m->size)) return NULL;
    for (size_t i = 0; i < m->phnum; ++i) {
        const Elf64_Phdr *p = &m->ph[i];
        if (p->p_type == PT_LOAD && va >= p->p_vaddr &&
            range(va - p->p_vaddr, n, p->p_memsz)) return m->image + va;
    }
    return NULL;
}
int elf_image_size(const char *path, size_t *size, FILE *log) {
    AndroidElf m = {0};
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(log, "Cannot open %s\n", path); return -1; }
    int rc = headers(f, &m, log);
    fclose(f);
    if (!rc) *size = m.size;
    else fprintf(log, "Invalid or unsupported ELF: %s\n", path);
    return rc;
}
int elf_load(AndroidElf *m, const char *path, void *staging, size_t size,
             uintptr_t execution_base, FILE *log) {
    memset(m, 0, sizeof *m);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int rc = -1;
    if (headers(f, m, log) || size < m->size || !staging ||
        execution_base > UINTPTR_MAX - m->size) goto done;
    m->image = staging; m->bias = execution_base;
    memset(staging, 0, m->size);
    const Elf64_Phdr *dynamic = NULL;
    for (size_t i = 0; i < m->phnum; ++i) {
        const Elf64_Phdr *p = &m->ph[i];
        if (p->p_type == PT_LOAD &&
            !read_at(f, p->p_offset, m->image + p->p_vaddr, p->p_filesz)) goto done;
        if (p->p_type == PT_DYNAMIC) {
            if (dynamic) goto done;
            dynamic = p;
        }
    }
    if (!dynamic || dynamic->p_memsz % sizeof(Elf64_Dyn)) goto done;
    Elf64_Dyn *d = at(m, dynamic->p_vaddr, dynamic->p_memsz);
    if (!d) goto done;
    uint64_t sym = 0, str = 0, hash = 0, syment = 0, relaent = 0, pltrel = 0;
    int terminated = 0;
    for (size_t i = 0; i < dynamic->p_memsz / sizeof *d; ++i) {
        uint64_t v = d[i].d_un.d_val;
        if (d[i].d_tag == DT_NULL) { terminated = 1; break; }
        switch (d[i].d_tag) {
        case DT_SYMTAB: sym = v; break;
        case DT_STRTAB: str = v; break;
        case DT_STRSZ: m->str_size = v; break;
        case DT_SYMENT: syment = v; break;
        case DT_HASH: hash = v; break;
        case DT_RELA: m->rela = v; break;
        case DT_RELASZ: m->rela_size = v; break;
        case DT_RELAENT: relaent = v; break;
        case DT_JMPREL: m->plt = v; break;
        case DT_PLTRELSZ: m->plt_size = v; break;
        case DT_PLTREL: pltrel = v; break;
        case DT_INIT: m->init = v; break;
        case DT_INIT_ARRAY: m->init_array = v; break;
        case DT_INIT_ARRAYSZ: m->init_array_size = v; break;
        case DT_REL: case DT_RELSZ: case DT_RELR: case DT_RELRSZ:
        case 0x6000000f: case 0x60000010: case 0x60000011: case 0x60000012:
            /* APS2/REL/RELR need their own decoder; never silently skip them. */
            if (v) { fprintf(log, "Unsupported relocation encoding tag %llx\n",
                             (unsigned long long)d[i].d_tag); goto done; }
            break;
        default: break;
        }
    }
    if (!terminated || !hash || !sym || !str || syment != sizeof(Elf64_Sym) ||
        !m->str_size || (m->rela_size && relaent != sizeof(Elf64_Rela)) ||
        (m->plt_size && pltrel != DT_RELA) ||
        m->rela_size % sizeof(Elf64_Rela) || m->plt_size % sizeof(Elf64_Rela)) goto done;
    uint32_t *hash_header = at(m, hash, 8);
    if (!hash_header) goto done;
    m->sym_count = hash_header[1];
    if (m->sym_count > MAX_IMAGE / sizeof(Elf64_Sym)) goto done;
    m->sym = at(m, sym, m->sym_count * sizeof(Elf64_Sym));
    m->str = at(m, str, m->str_size);
    if (!m->sym || !m->str ||
        (m->rela_size && !at(m, m->rela, m->rela_size)) ||
        (m->plt_size && !at(m, m->plt, m->plt_size))) goto done;
    for (size_t i = 0; i < m->sym_count; ++i)
        if (m->sym[i].st_name >= m->str_size ||
            !memchr(m->str + m->sym[i].st_name, 0, m->str_size - m->sym[i].st_name)) goto done;
    fprintf(log, "Loaded %s: image=%zu symbols=%zu execution=%p\n",
            path, m->size, m->sym_count, (void *)m->bias);
    rc = 0;
done:
    fclose(f);
    if (rc) { fprintf(log, "ELF load rejected: %s\n", path); m->image = NULL; }
    return rc;
}
uintptr_t elf_export(const AndroidElf *m, const char *name) {
    for (size_t i = 1; i < m->sym_count; ++i) {
        const Elf64_Sym *s = &m->sym[i];
        unsigned bind = ELF64_ST_BIND(s->st_info);
        if (s->st_shndx != SHN_UNDEF && (bind == STB_GLOBAL || bind == STB_WEAK) &&
            ELF64_ST_TYPE(s->st_info) != STT_TLS &&
            ELF64_ST_VISIBILITY(s->st_other) != STV_HIDDEN &&
            !strcmp(m->str + s->st_name, name)) {
            if (s->st_shndx == SHN_ABS) return s->st_value;
            if (at(m, s->st_value, s->st_size ? s->st_size : 1)) return m->bias + s->st_value;
        }
    }
    return 0;
}
static int relocate_table(AndroidElf *m, uint64_t va, uint64_t bytes,
                          ElfResolve resolve, void *ctx, FILE *log) {
    Elf64_Rela *rs = at(m, va, bytes);
    if (!bytes) return 0;
    if (!rs) return -1;
    for (size_t i = 0; i < bytes / sizeof *rs; ++i) {
        /* Copy before writing: never depend on an alias of the target. */
        Elf64_Rela r = rs[i];
        unsigned type = ELF64_R_TYPE(r.r_info);
        size_t si = ELF64_R_SYM(r.r_info);
        if (type == R_AARCH64_NONE) continue;
        void *target = at(m, r.r_offset, sizeof(uint64_t));
        if (!target || si >= m->sym_count) return -1;
        uint64_t value = 0;
        if (type == R_AARCH64_RELATIVE) {
            if (si) return -1;
            value = m->bias + r.r_addend;
        } else if (type == R_AARCH64_ABS64 || type == R_AARCH64_GLOB_DAT ||
                   type == R_AARCH64_JUMP_SLOT) {
            const Elf64_Sym *s = &m->sym[si];
            if (s->st_shndx == SHN_ABS) value = s->st_value;
            else if (s->st_shndx != SHN_UNDEF) {
                if (ELF64_ST_TYPE(s->st_info) == STT_TLS || !at(m, s->st_value, 1)) return -1;
                value = m->bias + s->st_value;
            } else {
                const char *name = m->str + s->st_name;
                value = resolve ? resolve(ctx, name) : 0;
                if (!value && ELF64_ST_BIND(s->st_info) != STB_WEAK) {
                    ++m->unresolved;
                    fprintf(log, "MISSING %s relocation=%u offset=%llx\n", name, type,
                            (unsigned long long)r.r_offset);
                    continue;
                }
            }
            value += r.r_addend;
        } else {
            ++m->unsupported;
            fprintf(log, "UNSUPPORTED relocation=%u offset=%llx\n", type,
                    (unsigned long long)r.r_offset);
            continue;
        }
        memcpy(target, &value, sizeof value);
        ++m->applied;
    }
    return 0;
}
int elf_relocate(AndroidElf *m, ElfResolve resolve, void *ctx, FILE *log) {
    if (!m->image || m->relocated || m->published) return -1;
    /* One attempt only: partial writes must never be retried as a linked image. */
    m->relocated = -1;
    if (relocate_table(m, m->rela, m->rela_size, resolve, ctx, log) ||
        relocate_table(m, m->plt, m->plt_size, resolve, ctx, log)) return -1;
    fprintf(log, "Relocations: applied=%zu unresolved=%zu unsupported=%zu\n",
            m->applied, m->unresolved, m->unsupported);
    if (m->unresolved || m->unsupported) return -1;
    m->relocated = 1;
    return 0;
}
static int executable_address(const AndroidElf *m, uintptr_t fn) {
    if (fn < m->bias) return 0;
    uintptr_t va = fn - m->bias;
    for (size_t i = 0; i < m->phnum; ++i) {
        const Elf64_Phdr *p = &m->ph[i];
        if (p->p_type == PT_LOAD && (p->p_flags & PF_X) && va >= p->p_vaddr && va - p->p_vaddr < p->p_memsz) return 1;
    }
    return 0;
}
int elf_run_initializers(AndroidElf *m, FILE *log) {
    if (!m->published || m->relocated != 1 || m->initialized || m->init_array_size % sizeof(uintptr_t)) return -1;
    uintptr_t *array = m->init_array_size ? at(m, m->init_array, m->init_array_size) : NULL;
    if (m->init_array_size && !array) return -1;
    if (m->init && !executable_address(m, m->bias + m->init)) return -1;
    for (size_t i = 0; i < m->init_array_size / sizeof(uintptr_t); ++i)
        if (array[i] && array[i] != UINTPTR_MAX && !executable_address(m, array[i])) return -1;
    m->initialized = 1;
    if (m->init) {
        fprintf(log,"ELF_INIT_BEGIN offset=%llx\n",(unsigned long long)m->init); fflush(log);
        ((void (*)(void))(m->bias + m->init))();
        fprintf(log,"ELF_INIT_END\n"); fflush(log);
    }
    for (size_t i = 0; i < m->init_array_size / sizeof(uintptr_t); ++i)
        if (array[i] && array[i] != UINTPTR_MAX) {
            fprintf(log,"ELF_CTOR_BEGIN index=%zu offset=%llx address=%p\n",i,(unsigned long long)(array[i]-m->bias),(void *)array[i]); fflush(log);
            ((void (*)(void))array[i])();
            fprintf(log,"ELF_CTOR_END index=%zu\n",i); fflush(log);
        }
    fprintf(log, "ELF_CONSTRUCTORS_PASS count=%zu\n", m->init_array_size / sizeof(uintptr_t));
    return 0;
}
