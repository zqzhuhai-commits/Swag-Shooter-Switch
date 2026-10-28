#pragma once
#include <stdint.h>
#include <stdio.h>
uintptr_t android_events_resolve(const char *name);
int android_events_selftest(FILE *log);
