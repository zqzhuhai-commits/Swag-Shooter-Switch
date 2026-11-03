#pragma once
#include "elf_loader.h"
int ai_profile_prepare(AndroidElf *module,FILE *log);
void ai_profile_poll(void);
void ai_profile_menu(const void *image);

void ai_profile_toggle_camera(void);

void ai_profile_camera_aim(int held);
int ai_profile_gyro_allowed(void);
void ai_profile_mouse_look(float x,float y,int active);
