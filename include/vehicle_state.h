#pragma once
#include <stdio.h>
#include "elf_loader.h"
void vehicle_state_bind(AndroidElf *module);
void vehicle_state_poll(FILE *log);
int vehicle_state_selftest(FILE *log);
