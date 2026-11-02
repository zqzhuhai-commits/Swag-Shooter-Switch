#pragma once
/* Actual complete gait cycles/second in thousandths, not per-foot contacts. */
static inline unsigned gait_target_millihz(int running) {return running ? 650u : 450u;}
static inline unsigned gait_initial_rate(int running) {return running ? 400u : 300u;}
static inline unsigned gait_max_rate(int running) {return running ? 500u : 400u;}
static inline unsigned gait_adjust_rate(unsigned rate,unsigned measured,unsigned target) {
 if(measured<20 || measured>30000)return rate;
 unsigned requested=(unsigned)(((unsigned long long)rate*target)/measured);
 if(requested<20)requested=20;
 if(requested>800)requested=800;
 return (rate+requested+1)/2;
}
