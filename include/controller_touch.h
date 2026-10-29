#pragma once
#include <switch.h>
#include <stdio.h>
typedef struct { int count,ids[16]; float x[16],y[16]; } SwitchTouchSnapshot;
void controller_touch_init(FILE *log);
void controller_touch_update(SwitchTouchSnapshot *touches,PadState *pad,FILE *log);
int controller_touch_cursor(int *x,int *y);
int controller_touch_selftest(FILE *log);

void controller_touch_set_vehicle_state(int state,FILE *log);
