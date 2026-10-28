#pragma once
#include "elf_loader.h"
#include <stdio.h>
void unity_host_set_log(FILE *log);
void unity_host_get_vm(void **vm, void **env);
int unity_host_call_onload(uintptr_t entry, void *vm, FILE *log);
int unity_host_bridge_test(AndroidElf *module, FILE *log);

int unity_host_run(FILE *log);
