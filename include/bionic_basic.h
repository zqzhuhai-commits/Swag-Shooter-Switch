#pragma once
#include <stdint.h>
#include <stddef.h>
uintptr_t bionic_basic_resolve(const char *name);
size_t bionic_basic_count(void);
int bionic_basic_selftest(void);
