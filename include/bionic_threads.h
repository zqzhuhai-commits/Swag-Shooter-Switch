#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
/* Android LP64 ABI; never substitute the host pthread types at these addresses. */
typedef struct { uint32_t words[10]; } AndroidMutex;
typedef struct { uint32_t words[12]; } AndroidCond;
typedef struct { uint32_t words[14]; } AndroidRwlock;
typedef struct { uint32_t words[4]; } AndroidSem;
typedef struct {
    uint32_t flags;
    void *stack_base;
    size_t stack_size, guard_size;
    int32_t sched_policy, sched_priority;
    char reserved[16];
} AndroidThreadAttr;
_Static_assert(sizeof(AndroidThreadAttr) == 56, "Android ARM64 thread attr layout");
uintptr_t bionic_threads_resolve(const char *name);
size_t bionic_threads_count(void);
int bionic_threads_selftest(FILE *log);
int *bionic_errno_ptr(void);
int bionic_error(int native_error);
int bionic_clock_gettime(int android_clock, struct timespec *value);
