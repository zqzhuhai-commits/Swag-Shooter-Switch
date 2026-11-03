#pragma once
#include <stdint.h>
typedef struct {uint64_t sample,ms;int32_t x,y;unsigned primed;} GyroPolicy;
static inline void gyro_policy_reset(GyroPolicy *p) {p->sample=p->ms=0;p->x=p->y=0;p->primed=0;}
static inline int32_t gyro_policy_filter(int32_t current,int32_t raw,unsigned dt) {
 int32_t delta=raw-current;unsigned amount=(unsigned)(((uint64_t)(delta<0 ? -delta : delta)*dt)/(dt+35));
 return delta<0 ? current-(int32_t)amount : current+(int32_t)amount;
}
static inline int32_t gyro_policy_pixels(int32_t rate,unsigned sensitivity,unsigned dt) {
 unsigned magnitude=(unsigned)(rate<0 ? -rate : rate);
 if(magnitude<1500 || sensitivity<100 || sensitivity>5000)return 0;
 uint64_t value=(uint64_t)magnitude*3375*sensitivity*dt/1000000000ULL;
 if(value>45000)value=45000;
 return rate<0 ? -(int32_t)value : (int32_t)value;
}
/* Rates use millionths of raw HID angular-rate units; output is milli-pixels.
 * The real sensor backend supplies fresh data and a gravity-relative yaw. */
static inline int gyro_policy_step(GyroPolicy *p,int active,uint64_t sample,uint64_t now,
 int32_t x,int32_t y,unsigned sx,unsigned sy,int32_t *dx,int32_t *dy) {
 *dx=*dy=0;
 if(!active || !sample || x < -8000000 || x>8000000 || y < -8000000 || y>8000000) {gyro_policy_reset(p);return 0;}
 if(p->primed && (sample==p->sample || now<=p->ms))return 0;
 if(!p->primed || now-p->ms>100) {
  p->sample=sample;p->ms=now;p->x=p->y=0;p->primed=1;return 0;
 }
 unsigned dt=(unsigned)(now-p->ms);if(dt>50)dt=50;
 p->sample=sample;p->ms=now;
 p->x=gyro_policy_filter(p->x,x,dt);p->y=gyro_policy_filter(p->y,y,dt);
 *dx=gyro_policy_pixels(p->x,sx,dt);*dy=gyro_policy_pixels(p->y,sy,dt);return 1;
}
