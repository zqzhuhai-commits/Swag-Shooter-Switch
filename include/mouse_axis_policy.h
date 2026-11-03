#pragma once
#include <stdint.h>
static inline unsigned mouse_axis_id(const uint16_t *name,int length) {
 const char *prefix="Mouse ";
 if(!name || length!=7)return 0;
 for(int i=0;i<6;++i)if(name[i]!=(unsigned char)prefix[i])return 0;
 return name[6]=='X' ? 1 : name[6]=='Y' ? 2 : 0;
}
static inline int mouse_axis_fresh(uint64_t stamp,uint64_t now,uint64_t frequency) {
 return stamp && now>=stamp && now-stamp<=frequency/5;
}
