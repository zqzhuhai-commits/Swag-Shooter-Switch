#pragma once
#include "elf_loader.h"
#include <stdint.h>
#include <stdio.h>
void runtime_services_init(FILE *log);
uintptr_t runtime_services_resolve(const char *name);
uintptr_t runtime_io_resolve(const char *name);
int runtime_io_selftest(FILE *log);
int runtime_module_register(const char *name, AndroidElf *elf, int ready);
void runtime_module_remove(AndroidElf *elf);
int runtime_loader_selftest(AndroidElf *elf, FILE *log);
int runtime_platform_selftest(FILE *log);
int runtime_zlib_selftest(FILE *log);
int runtime_jni_bootstrap(AndroidElf *elf, FILE *log);
int64_t runtime_io_read_at(int fd, void *buffer, size_t size, int64_t offset);

int runtime_io_native_fd(int fd);
