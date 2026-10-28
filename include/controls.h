#pragma once
#include <stdint.h>
#include <stdio.h>
typedef struct { uint64_t previous; int cursor, chord, cancel_grenade, cancel_item; } SwagControls;
uint64_t swag_controls_update(SwagControls *state, uint64_t held);
void swag_controls_log(FILE *log, uint64_t actions, int cursor);
int swag_controls_selftest(FILE *log);
