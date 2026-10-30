#ifndef SWAG_HEALTH_WARNING_POLICY_H
#define SWAG_HEALTH_WARNING_POLICY_H
/* Input: health/max health in thousandths. Output: alpha in thousandths. */
static inline unsigned health_warning_alpha(unsigned health) {
 return health>=300 ? 0 : (300-health)*450/300;
}
static inline unsigned health_warning_fade(unsigned current,unsigned target,unsigned milliseconds) {
 if(milliseconds>100)milliseconds=100;
 unsigned step=milliseconds*450/(target>current ? 350 : 600);
 if(!step && milliseconds)step=1;
 if(target>current)return target-current<=step ? target : current+step;
 return current-target<=step ? target : current-step;
}
#endif
