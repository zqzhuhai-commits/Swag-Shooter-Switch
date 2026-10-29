#pragma once
#include <stdint.h>
#include <stdio.h>
uintptr_t android_graphics_resolve(const char *name);
int android_graphics_selftest(FILE *log);

void android_graphics_set_log(FILE *log);

int android_graphics_prepare(FILE *log);
