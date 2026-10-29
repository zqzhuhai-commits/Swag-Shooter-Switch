#pragma once
#include <stdint.h>
#include <stdio.h>
#include <time.h>
long guest_futex(uint32_t *address,int operation,int value,const struct timespec *timeout,uint32_t mask);
int guest_futex_selftest(FILE *log);
int futex_gc_lock_free(void);
