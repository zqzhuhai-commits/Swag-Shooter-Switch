#pragma once
#include <stdint.h>
#include <stdio.h>
void guest_system_set_log(FILE *log);
uintptr_t guest_system_resolve(const char *name);
int guest_system_selftest(FILE *log);
