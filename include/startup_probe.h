#pragma once
#include <stdio.h>
#include <stdint.h>
uintptr_t startup_missing(const char *name);
void startup_log(FILE *log);
void startup_engine_progress(void);
