#pragma once
#include <switch.h>
#include "elf_loader.h"
typedef struct {
    void *staging;
    void *execution;
    size_t size;
    VirtmemReservation *reservation;
    int mapped;
} NxModuleMemory;
int nx_memory_prepare(NxModuleMemory *m, size_t size, FILE *log);
int nx_memory_publish(NxModuleMemory *m, AndroidElf *elf, FILE *log);
int nx_memory_close(NxModuleMemory *m, FILE *log);
int nx_memory_smoke_test(FILE *log);
