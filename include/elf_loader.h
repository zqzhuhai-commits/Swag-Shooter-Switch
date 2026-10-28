#pragma once
#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Addresses in image are writable staging addresses. bias is the eventual
 * execution address, never the staging pointer. No game code runs here. */
typedef struct {
    unsigned char *image;
    size_t size;
    uintptr_t bias;
    Elf64_Phdr ph[32];
    size_t phnum;
    Elf64_Sym *sym;
    size_t sym_count;
    char *str;
    size_t str_size;
    uint64_t rela, rela_size, plt, plt_size;
    uint64_t init, init_array, init_array_size;
    size_t applied, unresolved, unsupported;
    int relocated, published, initialized;
} AndroidElf;

typedef uintptr_t (*ElfResolve)(void *ctx, const char *name);
int elf_image_size(const char *path, size_t *size, FILE *log);
int elf_load(AndroidElf *m, const char *path, void *staging, size_t size,
             uintptr_t execution_base, FILE *log);
int elf_relocate(AndroidElf *m, ElfResolve resolve, void *ctx, FILE *log);
uintptr_t elf_export(const AndroidElf *m, const char *name);
int elf_run_initializers(AndroidElf *m, FILE *log);
