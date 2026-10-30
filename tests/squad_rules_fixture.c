#include "squad_rules.h"
#include "guest_cpu_policy.h"
#include "health_warning_policy.h"
#include "zone_safety_policy.h"
void *memset(void *p,int v,size_t n) {unsigned char *d=p;for(size_t i=0;i<n;++i)d[i]=(unsigned char)v;return p;}
void *memcpy(void *p,const void *q,size_t n) {unsigned char *d=p;const unsigned char *s=q;for(size_t i=0;i<n;++i)d[i]=s[i];return p;}
int run_squad_rules_tests(void) {
 int result=squad_rules_selftest();if(result)return result;
 { /* Partial spawning must not create 36 phantom survivors on the HUD. */
  SquadRules s;squad_rules_reset(&s);
  if(squad_rules_present_lives(&s,1))return 80;
  squad_rules_join(&s,1);for(unsigned i=1;i<16;++i)squad_rules_join(&s,0);
  unsigned total=0;for(unsigned i=1;i<=13;++i)total+=squad_rules_present_lives(&s,i);
  if(total!=16 || squad_rules_winner(&s) || squad_rules_can_finish(&s))return 81;
  for(unsigned i=4;i<16;++i)squad_rules_death(&s,i);
  total=0;for(unsigned i=1;i<=13;++i)total+=squad_rules_present_lives(&s,i);
  if(total!=4 || squad_rules_winner(&s) || squad_rules_can_finish(&s))return 82;
  for(unsigned i=16;i<52;++i)squad_rules_join(&s,0);
  total=0;for(unsigned i=1;i<=13;++i)total+=squad_rules_present_lives(&s,i);
  if(total!=40 || s.registered!=52)return 83;
 }

 if(zone_damage_fallback(1,1,0) || zone_damage_fallback(2,0,0) || zone_damage_fallback(2,1,1) || !zone_damage_fallback(2,1,0))return 70;
 if(zone_damage_percent(0)!=1 || zone_damage_percent(5)!=12 || zone_damage_percent(100)!=12)return 71;
 {
  SquadRules s;squad_rules_reset(&s);squad_rules_join(&s,1);
  for(unsigned i=1;i<52;++i)squad_rules_join(&s,0);
  for(unsigned i=4;i<20;++i)squad_rules_death(&s,i);
  int hp[52];for(unsigned i=0;i<52;++i)hp[i]=100;
  if(squad_rules_enemy_lives(&s)!=32 || squad_rules_winner(&s))return 72;
  for(unsigned tick=1;tick<=10;++tick)for(unsigned i=20;i<52;++i) {
   if(hp[i]>0 && zone_damage_fallback(tick,tick>1,0)) {
    hp[i]-=(int)zone_damage_percent(5);
    if(hp[i]<=0)squad_rules_death(&s,i);
   }
  }
  if(squad_rules_enemy_lives(&s) || squad_rules_winner(&s)!=1)return 73;
 }
 {
  if(health_warning_alpha(1000) || health_warning_alpha(300) || health_warning_alpha(0)!=450)return 60;
  unsigned previous=450;
  for(unsigned hp=0;hp<=1000;++hp) {
   unsigned alpha=health_warning_alpha(hp);if(alpha>previous || alpha>450)return 61;previous=alpha;
  }
  unsigned opacity=0;
  for(unsigned frame=0;frame<60;++frame) {
   unsigned value=health_warning_fade(opacity,450,16);if(value<opacity || value>450)return 62;opacity=value;
  }
  if(opacity!=450)return 63;
  for(unsigned frame=0;frame<60;++frame) {
   unsigned value=health_warning_fade(opacity,0,16);if(value>opacity)return 64;opacity=value;
  }
  if(opacity || health_warning_fade(200,200,16)!=200 || health_warning_fade(200,450,0)!=200)return 65;
 }
 if(guest_worker_mask(7)!=6 || guest_worker_mask(15)!=6 || guest_worker_mask(1)!=1 || guest_worker_mask(8)!=0)return 30;
 if(guest_worker_core(6,0)!=1 || guest_worker_core(6,1)!=2 || guest_worker_core(6,2)!=1 || guest_worker_core(0,0)!=-2)return 31;
 /* Reproduce 32 alive: five full enemy squads eliminated, eight still alive. */
 {
  SquadRules s;squad_rules_reset(&s);
  if(squad_rules_can_finish(&s) || squad_rules_winner(&s))return 20;
  squad_rules_join(&s,1);for(unsigned i=1;i<52;++i)squad_rules_join(&s,0);
  if(squad_rules_can_finish(&s))return 21;
  for(unsigned i=4;i<24;++i)squad_rules_death(&s,i);
  if(squad_rules_alive(&s)!=8 || squad_rules_enemy_lives(&s)!=28 || squad_rules_can_finish(&s) || squad_rules_winner(&s))return 22;
  /* No victory while any other squad retains even one member. */
  for(unsigned i=24;i<51;++i) {
   squad_rules_death(&s,i);
   if(squad_rules_can_finish(&s) || squad_rules_winner(&s))return 23;
  }
  squad_rules_death(&s,51);
  if(!squad_rules_can_finish(&s) || squad_rules_winner(&s)!=1)return 24;
 }
 for(unsigned human_after=0;human_after<52;++human_after) {
  SquadRules s;squad_rules_reset(&s);
  for(unsigned ai=0;ai<51;++ai) {
   if(ai==human_after && squad_rules_join(&s,1)!=0)return 10;
   if(squad_rules_join(&s,0)!=(int)ai+1)return 11;
  }
  if(human_after==51 && squad_rules_join(&s,1)!=0)return 12;
  if(s.registered!=52)return 13;
  for(unsigned i=0;i<4;++i)if(!squad_rules_death(&s,i))return 14;
  if(s.rank[0]!=13 || squad_rules_alive(&s)!=12 || squad_rules_enemy_lives(&s)!=48)return 15;
  if(!squad_rules_can_finish(&s) || squad_rules_winner(&s))return 25;
  for(unsigned i=4;i<48;++i)squad_rules_death(&s,i);
  if(s.rank[12]!=1 || squad_rules_alive(&s)!=1 || squad_rules_lives(&s,13)!=4)return 16;
  if(!squad_rules_can_finish(&s) || squad_rules_winner(&s)!=13)return 26;
 }
 return 0;
}
