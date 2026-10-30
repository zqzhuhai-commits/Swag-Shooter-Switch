#pragma once
#include "elf_loader.h"
int ai_profile_prepare(AndroidElf *module,FILE *log);
void ai_profile_poll(void);
void ai_profile_menu(const void *image);
