#ifndef SWAG_ZONE_SAFETY_POLICY_H
#define SWAG_ZONE_SAFETY_POLICY_H
static inline int zone_damage_fallback(unsigned outside_samples,int has_previous,int native_damage_seen) {
 return outside_samples>=2 && has_previous && !native_damage_seen;
}
static inline unsigned zone_damage_percent(unsigned phase) {
 const unsigned values[]={1,2,3,5,8,12};return values[phase<6 ? phase : 5];
}
#endif
