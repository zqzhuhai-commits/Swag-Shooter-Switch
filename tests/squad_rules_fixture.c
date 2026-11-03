#include "squad_rules.h"
#include "guest_cpu_policy.h"
#include "health_warning_policy.h"
#include "gait_policy.h"
#include "gyro_policy.h"
#include "performance_policy.h"
#include "mouse_axis_policy.h"
#include "movement_policy.h"
#include "zone_safety_policy.h"
void *memset(void *p,int v,size_t n) {unsigned char *d=p;for(size_t i=0;i<n;++i)d[i]=(unsigned char)v;return p;}
void *memcpy(void *p,const void *q,size_t n) {unsigned char *d=p;const unsigned char *s=q;for(size_t i=0;i<n;++i)d[i]=s[i];return p;}
int run_squad_rules_tests(void) {
 const uint16_t mouse_x[]={'M','o','u','s','e',' ','X'},mouse_y[]={'M','o','u','s','e',' ','Y'},other[]={'M','o','u','s','e',' ','Z'};
 if(mouse_axis_id(mouse_x,7)!=1 || mouse_axis_id(mouse_y,7)!=2 || mouse_axis_id(other,7))return 107;
 if(mouse_axis_id(mouse_x,6) || mouse_axis_id(0,7))return 108;
 if(!mouse_axis_fresh(1000,1100,1000) || !mouse_axis_fresh(1000,1200,1000))return 109;
 if(mouse_axis_fresh(1000,1201,1000) || mouse_axis_fresh(1000,999,1000) || mouse_axis_fresh(0,100,1000))return 110;
 if(movement_goal_milli(10000,16000,0)!=10000 || movement_goal_milli(10000,16000,1)!=16000)return 111;
 if(movement_speed_milli(16000,10000,0,1)!=10000)return 112;
 if(movement_speed_milli(0,10000,0,1)!=10000 || movement_speed_milli(10000,16000,0,1)!=16000)return 113;
 if(movement_speed_milli(3000,10000,1,1)!=3000 || movement_speed_milli(16000,10000,1,1)!=10000)return 114;
 if(movement_speed_milli(0,10000,0,0)!=0 || movement_running(0,0) || movement_running(1,1) || !movement_running(1,0))return 115;
 uint32_t rates[]={1020000000u,1785000000u,2040000000u,1224000000u};
 if(performance_select_rate(rates,4,1,1785000000u)!=1785000000u)return 100;
 if(performance_select_rate(rates,4,1,1100000000u)!=1020000000u)return 101;
 if(performance_select_rate(rates,4,0,1785000000u) || performance_select_rate(rates,65,1,1785000000u))return 102;
 if(performance_select_rate(rates,4,1,800000000u) || performance_select_rate(0,4,1,1785000000u))return 103;
 if(performance_gpu_ceiling(0)!=460800000u || performance_gpu_ceiling(1)!=768000000u || performance_gpu_ceiling(2)!=921600000u)return 104;
 GyroPolicy gp={0};int32_t gx=0,gy=0;
 if(gyro_policy_step(&gp,1,1,100,100000,0,1000,1000,&gx,&gy) || gx || gy)return 90;
 if(!gyro_policy_step(&gp,1,2,116,100000,0,1000,1000,&gx,&gy) || gx<=0 || gy)return 91;
 if(gyro_policy_step(&gp,1,2,132,100000,0,1000,1000,&gx,&gy) || gx || gy)return 92;
 if(gyro_policy_step(&gp,0,3,148,100000,0,1000,1000,&gx,&gy) || gp.primed || gx || gy)return 93;
 gyro_policy_step(&gp,1,4,164,-100000,100000,1000,1000,&gx,&gy);
 if(!gyro_policy_step(&gp,1,5,180,-100000,100000,1000,1000,&gx,&gy) || gx>=0 || gy<=0)return 94;
 if(gyro_policy_step(&gp,1,6,400,8000001,0,1000,1000,&gx,&gy) || gp.primed)return 95;
 if(gyro_policy_pixels(100000,1000,16)!=5400 || gyro_policy_pixels(-100000,1000,16)!=-5400)return 98;
 if(gyro_policy_pixels(8000000,5000,50)!=45000 || gyro_policy_pixels(1000,1000,16))return 96;
 gyro_policy_step(&gp,1,7,500,100000,0,1000,1000,&gx,&gy);
 if(gyro_policy_step(&gp,1,8,800,100000,0,1000,1000,&gx,&gy) || gx || gy)return 97;

 if(gait_target_millihz(0)!=450 || gait_target_millihz(1)!=650)return 80;
 if(gait_adjust_rate(600,6000,750)>=600 || gait_adjust_rate(600,600,750)<=600)return 81;
 if(gait_adjust_rate(300,450,450)!=300 || gait_adjust_rate(400,650,650)!=400)return 82;
 if(gait_adjust_rate(550,0,750)!=550 || gait_adjust_rate(550,31000,750)!=550)return 83;
 if(gait_max_rate(0)!=400 || gait_max_rate(1)!=500)return 85;
 unsigned gait_rate=400;
 for(unsigned i=0;i<12;++i)gait_rate=gait_adjust_rate(gait_rate,12000*gait_rate/400,650);
 if(gait_rate<18 || gait_rate>28)return 84;

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
  if(health_warning_pulse_hz(299)!=2 || health_warning_pulse_hz(300)!=1 || health_warning_pulse_hz(499)!=1)return 66;
  if(health_warning_alpha(1000) || health_warning_alpha(500) || health_warning_alpha(0)!=450 || !health_warning_alpha(499))return 60;
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
