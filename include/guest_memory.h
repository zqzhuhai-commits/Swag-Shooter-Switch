#pragma once
#include <stdint.h>
#include <stdio.h>
uintptr_t guest_memory_resolve(const char *name);
int guest_memory_selftest(FILE *log);
