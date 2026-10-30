#ifndef SWAG_GUEST_CPU_POLICY_H
#define SWAG_GUEST_CPU_POLICY_H
/* Keep the main core free where permitted; never request the system core. */
static inline unsigned guest_worker_mask(unsigned allowed) {
 unsigned app=allowed&7u,workers=app&6u;
 return workers ? workers : app;
}
static inline int guest_worker_core(unsigned mask,unsigned sequence) {
 unsigned count=0;for(unsigned i=0;i<3;++i)count+=(mask>>i)&1u;
 if(!count)return -2;
 unsigned pick=sequence%count;
 for(unsigned i=0;i<3;++i)if(mask&(1u<<i)) {if(!pick)return (int)i;--pick;}
 return -2;
}
#endif
