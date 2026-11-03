#pragma once
#include <stdint.h>
static inline int movement_goal_milli(int normal,int run,int sprint) {
 return sprint ? run : normal;
}
static inline int movement_speed_milli(int current,int goal,int busy,int final) {
 return busy || !final ? (current>goal ? goal : current) : goal;
}
static inline int movement_running(int sprint,int busy) {return sprint && !busy;}
