#pragma once
#include "elf_loader.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

typedef struct AndroidTlsBlock AndroidTlsBlock;
typedef struct { uint64_t va; uint32_t instruction; } TlsSite;
typedef struct { TlsSite *sites; size_t count, extra_bytes; } TlsPatchPlan;
int android_tls_initialize(FILE *log);
AndroidTlsBlock *android_tls_allocate(void);
void android_tls_retain(AndroidTlsBlock *block);
int android_tls_bind(AndroidTlsBlock *block);
void android_tls_discard(AndroidTlsBlock *block);
void android_tls_record_owner(AndroidTlsBlock *block, uintptr_t native_thread);
AndroidTlsBlock *android_tls_for_thread(uintptr_t native_thread);
void *android_tls_current(void);
size_t android_tls_slot_offset(void);
size_t android_tls_live_blocks(void);
uintptr_t android_tls_resolve(const char *name);
int android_tls_plan(const char *path, TlsPatchPlan *plan, FILE *log);
void android_tls_plan_free(TlsPatchPlan *plan);
int android_tls_apply(AndroidElf *elf, size_t allocation_size, const TlsPatchPlan *plan, FILE *log);
int android_tls_selftest(FILE *log, int execute);
