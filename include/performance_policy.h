#pragma once
#include <stdint.h>
static inline uint32_t performance_gpu_ceiling(int profile) {
 return profile==2 ? 921600000u : profile==1 ? 768000000u : 460800000u;
}
static inline uint32_t performance_select_rate(const uint32_t *rates,int count,int discrete,uint32_t ceiling) {
 if(!rates || !discrete || count<=0 || count>64)return 0;
 uint32_t chosen=0;
 for(int i=0;i<count;++i)if(rates[i]<=ceiling && rates[i]>chosen)chosen=rates[i];
 return chosen;
}
