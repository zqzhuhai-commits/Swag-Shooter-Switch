#pragma once
#include <stdint.h>
#include <stdio.h>
uintptr_t guest_text_resolve(const char *name);
int guest_text_selftest(FILE *log);
