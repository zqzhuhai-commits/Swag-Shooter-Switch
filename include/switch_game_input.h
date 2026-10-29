#pragma once
#include <stdio.h>
int switch_game_input_init(FILE *log);
void switch_game_input_poll(void *env,void *player);
void switch_game_input_set_resolver(void *(*resolve)(const char *));
void switch_game_input_after_frame(void);
