#pragma once
#include <stdio.h>
int batch_run(const char *name, int (*test)(FILE *), int enabled, FILE *log);
void batch_record(const char *name, int passed, FILE *log);
void batch_summary(FILE *log);
