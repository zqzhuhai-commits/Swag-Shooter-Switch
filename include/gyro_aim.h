#pragma once
#include <switch.h>
#include <stdio.h>
void gyro_aim_init(FILE *log);
void gyro_aim_reset(void);
/* Returns a valid aiming clutch; delta outputs are display pixels. */
int gyro_aim_update(const PadState *pad,int active,float *dx,float *dy);
void gyro_aim_exit(void);
