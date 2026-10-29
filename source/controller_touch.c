#include "controller_touch.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
/* Pixel centers measured on the user's actual 1280x720 Switch screenshots. */
enum { JUMP,CROUCH,PRONE,RELOAD,MAP,PRIMARY,PISTOL,PLANE,CYCLE,GRENADE,AIM,FIRE,BAG,SPRINT,INTERACT,HEAL,MENU,FORWARD,REVERSE,LEFT,RIGHT,EXIT,PICKUP,TARGET_COUNT };
typedef struct { const char *name; float x,y; } Target;
static const Target defaults[TARGET_COUNT]={
 {"jump",1226,500},{"crouch",1080,674},{"prone",1204,642},{"reload",973,674},
 {"map",1192,88},{"primary_smg",553,638},{"pistol",726,638},{"plane_jump",335,360},
 {"cycle_item",880,625},{"grenade",879,678},{"aim",1228,379},{"fire",85,369},
 {"inventory",45,677},{"sprint",298,198},{"interact",1008,308},{"heal",1239,249},
 {"menu",23,21},{"drive_forward",210,382},{"drive_reverse",212,561},
 {"drive_left",866,499},{"drive_right",1066,494},{"drive_exit",1158,305},{"pickup",880,236}};
static Target targets[TARGET_COUNT];
static float move_x=185,move_y=560,look_x=850,look_y=330;
static u64 previous,crouch_begin,reload_begin,interact_begin;
static int interact_long;
static int crouch_long,reload_long,alternate_weapon,vehicle,chord,grenade_cancel;
static int look_active,look_reset,move_active;
static int vehicle_auto;
static int action_id[TARGET_COUNT];
static unsigned action_seen[TARGET_COUNT];
static int sprint_enabled,aim_active,aim_close,aim_last_pulse,modifier_used,interaction_modified;
static float clamp(float v,float lo,float hi) { return v<lo ? lo : v>hi ? hi : v; }
static float axis(int v) { return abs(v)<6000 ? 0 : v/32767.0f; }
static void append(SwitchTouchSnapshot *s,int id,float x,float y) {
 if(s->count>=16) return;
 int i=s->count++; s->ids[i]=id; s->x[i]=clamp(x,0,1279); s->y[i]=clamp(y,0,719);
}
static void reset(void) {
 previous=0; crouch_begin=reload_begin=interact_begin=0; interact_long=0; crouch_long=reload_long=0;
 alternate_weapon=vehicle=chord=grenade_cancel=look_active=look_reset=move_active=0;
 vehicle_auto=0;
 look_x=850; look_y=330; move_x=185; move_y=560;
 memcpy(targets,defaults,sizeof targets);
 for(unsigned i=0;i<TARGET_COUNT;++i) action_id[i]=-1;
 memset(action_seen,0,sizeof action_seen);
 sprint_enabled=aim_active=aim_close=aim_last_pulse=modifier_used=interaction_modified=0;
}
void controller_touch_init(FILE *log) {
 reset();
 FILE *file=fopen("sdmc:/switch/swag-controls-v3.cfg","r");
 fprintf(log,"CONTROL_CONFIG revision=3 loaded=%d path=/switch/swag-controls-v3.cfg legacy_config=IGNORED\n",file!=NULL);
 if(file) {
  char line[160],name[64]; float x,y;
  while(fgets(line,sizeof line,file)) {
   if(line[0]=='#' || sscanf(line,"%63s %f %f",name,&x,&y)!=3 || !isfinite(x) || !isfinite(y) || x<0 || x>1279 || y<0 || y>719) continue;
   for(unsigned i=0;i<TARGET_COUNT;++i) if(!strcmp(name,targets[i].name)) { targets[i].x=x; targets[i].y=y; }
   if(!strcmp(name,"move_center")) { move_x=x; move_y=y; }
  }
  fclose(file);
 }
 for(unsigned i=0;i<TARGET_COUNT;++i) fprintf(log,"CONTROL_EFFECTIVE action=%s x=%.1f y=%.1f\n",targets[i].name,targets[i].x,targets[i].y);
 fprintf(log,"CONTROLLER_PROFILE revision=3 cursor=REMOVED mode=FOOT vehicle_auto=GAME_STATE manual_fallback=L+R source=Switch_1280x720_screenshots\n");
}
void controller_touch_set_vehicle_state(int state,FILE *log) {
 if(state<0) { vehicle_auto=0; return; }
 vehicle_auto=1;
 if(vehicle!=!!state) {
  vehicle=!!state; look_active=look_reset=move_active=0;
  aim_active=aim_close=aim_last_pulse=0;
  fprintf(log,"CONTROL_MODE vehicle=%d source=verified_game_field\n",vehicle);
 }
}
int controller_touch_cursor(int *x,int *y) { (void)x; (void)y; return 0; }
static void emit(SwitchTouchSnapshot *s,unsigned action,FILE *log,u64 pressed) {
 if(action_id[action]<0) {
  for(int id=18;id<32;++id) {
   int used=0; for(unsigned j=0;j<TARGET_COUNT;++j) used|=action_id[j]==id;
   if(!used) { action_id[action]=id; break; }
  }
 }
 if(action_id[action]<0) { fprintf(log,"CONTROL_POINTER_CAPACITY action=%s\n",targets[action].name); return; }
 action_seen[action]=1;
 append(s,action_id[action],targets[action].x,targets[action].y);
 if(pressed) fprintf(log,"GAME_CONTROL mode=%s action=%s x=%.1f y=%.1f\n",vehicle ? "vehicle" : "foot",targets[action].name,targets[action].x,targets[action].y);
}
static void update_at(SwitchTouchSnapshot *s,PadState *pad,FILE *log,u64 now) {
 u64 held=padGetButtons(pad),pressed=held&~previous,released=previous&~held;
 memset(action_seen,0,sizeof action_seen);
 if(pressed&HidNpadButton_L) modifier_used=0;
 if(pressed&HidNpadButton_Y) interaction_modified=!!(held&HidNpadButton_L);
 int door_modifier=interaction_modified && ((held|released)&HidNpadButton_Y);
 if(door_modifier) modifier_used=1;
 int combined=(held&(HidNpadButton_R|HidNpadButton_L))==(HidNpadButton_R|HidNpadButton_L);
 if(combined) {
  grenade_cancel=1;
  if(!chord && !vehicle_auto) { aim_active=aim_close=aim_last_pulse=0; vehicle=!vehicle; look_active=look_reset=move_active=0; fprintf(log,"CONTROL_MODE vehicle=%d source=manual_chord\n",vehicle); }
 }
 if(combined || (chord && (held&(HidNpadButton_R|HidNpadButton_L)))) {
  for(unsigned i=0;i<TARGET_COUNT;++i) action_id[i]=-1;
  previous=held; chord=1; return;
 }
 chord=0;
 if(pressed&HidNpadButton_A) { crouch_begin=now; crouch_long=0; }
 if(pressed&HidNpadButton_Y) { reload_begin=now; reload_long=0; }
 if(pressed&HidNpadButton_StickR) { interact_begin=now; interact_long=0; }
 HidAnalogStickState left=padGetStickPos(pad,0),right=padGetStickPos(pad,1);
 float lx=axis(left.x),ly=axis(left.y),rx=axis(right.x),ry=axis(right.y);
 if(vehicle) {
  int forward=(held&(HidNpadButton_ZR|HidNpadButton_Up)) || ly>0;
  int reverse=(held&(HidNpadButton_ZL|HidNpadButton_Down)) || ly<0;
  int steer_left=(held&HidNpadButton_Left) || rx<0;
  int steer_right=(held&HidNpadButton_Right) || rx>0;
  if(forward && !reverse) emit(s,FORWARD,log,pressed&(HidNpadButton_ZR|HidNpadButton_Up));
  if(reverse && !forward) emit(s,REVERSE,log,pressed&(HidNpadButton_ZL|HidNpadButton_Down));
  if(steer_left && !steer_right) emit(s,LEFT,log,0);
  if(steer_right && !steer_left) emit(s,RIGHT,log,0);
  if(pressed&HidNpadButton_A) emit(s,EXIT,log,1);
 } else {
  if(lx || ly) { append(s,16,move_x+(move_active ? lx*(sprint_enabled ? 75 : 40) : 0),move_y-(move_active ? ly*(sprint_enabled ? 75 : 40) : 0)); move_active=1; } else move_active=0;
  if(held&HidNpadButton_ZR) emit(s,FIRE,log,pressed&HidNpadButton_ZR);
  /* Translate held trigger into paired clicks on the mobile toggle.
   * Always leave a touch-up frame between opening and closing clicks. */
  int aim_pulse=0;
  if((pressed&HidNpadButton_ZL) && !aim_active) { emit(s,AIM,log,1); aim_active=1; aim_close=0; aim_pulse=1; }
  if((released&HidNpadButton_ZL) && aim_active) aim_close=1;
  if((held&HidNpadButton_ZL) && aim_active) aim_close=0;
  if(aim_close && !aim_last_pulse && !aim_pulse) { emit(s,AIM,log,1); aim_active=0; aim_close=0; aim_pulse=1; }
  aim_last_pulse=aim_pulse;
  if(held&HidNpadButton_B) emit(s,JUMP,log,pressed&HidNpadButton_B);
  if((held&HidNpadButton_A) && !crouch_long && now-crouch_begin>=500) { emit(s,PRONE,log,1); crouch_long=1; }
  if((released&HidNpadButton_A) && !crouch_long) emit(s,CROUCH,log,1);
  if((held&HidNpadButton_StickR) && !interact_long && now-interact_begin>=500) { emit(s,INTERACT,log,1); interact_long=1; }
  if((released&HidNpadButton_StickR) && !interact_long) emit(s,PRONE,log,1);
  if((held&HidNpadButton_Y) && !reload_long && now-reload_begin>=500) { emit(s,RELOAD,log,1); reload_long=1; }
  if((released&HidNpadButton_Y) && !reload_long) emit(s,door_modifier ? INTERACT : PICKUP,log,1);
  if(pressed&HidNpadButton_X) { emit(s,alternate_weapon ? PISTOL : PRIMARY,log,1); alternate_weapon=!alternate_weapon; }
  if(pressed&HidNpadButton_Left) emit(s,PRIMARY,log,1);
  if(pressed&HidNpadButton_Right) emit(s,PISTOL,log,1);
  if(pressed&HidNpadButton_Up) emit(s,PLANE,log,1);
  if(pressed&HidNpadButton_Down) emit(s,CYCLE,log,1);
  if((released&HidNpadButton_R) && !grenade_cancel) emit(s,GRENADE,log,1);
  if((released&HidNpadButton_L) && !modifier_used) emit(s,HEAL,log,1);
  if(pressed&HidNpadButton_StickL) {
   sprint_enabled=!sprint_enabled; emit(s,SPRINT,log,1);
   fprintf(log,"CONTROL_SPRINT enabled=%d\n",sprint_enabled);
  }
 }
 if(!vehicle && (rx || ry)) {
  if(look_reset) { look_reset=0; look_active=0; }
  else {
   if(!look_active) { look_x=850; look_y=330; look_active=1; }
   look_x+=rx*14; look_y-=ry*14;
   if(look_x<650 || look_x>1050 || look_y<160 || look_y>500) look_reset=1;
   append(s,17,look_x,look_y);
  }
 } else look_active=0;
 if(pressed&HidNpadButton_Plus) emit(s,MAP,log,1);
 if(pressed&HidNpadButton_Minus) emit(s,BAG,log,1);
 /* After issuing exit, retain vehicle mode for the touch-up frame, then foot. */
 if(vehicle && !vehicle_auto && (released&HidNpadButton_A)) { vehicle=0; fprintf(log,"CONTROL_MODE vehicle=0 source=exit_key_unconfirmed\n"); }
 if(!(held&HidNpadButton_R)) grenade_cancel=0;
 for(unsigned i=0;i<TARGET_COUNT;++i) if(!action_seen[i]) action_id[i]=-1;
 previous=held;
}
void controller_touch_update(SwitchTouchSnapshot *s,PadState *pad,FILE *log) { update_at(s,pad,log,armTicksToNs(armGetSystemTick())/1000000); }
static int has(const SwitchTouchSnapshot *s,int action) {
 for(int i=0;i<s->count;++i) if(s->ids[i]==action_id[action] && s->x[i]==targets[action].x && s->y[i]==targets[action].y) return 1;
 return 0;
}
int controller_touch_selftest(FILE *log) {
 reset(); int errors=0; PadState pad={0}; SwitchTouchSnapshot s={0};
 #define STEP(buttons,time) do { pad.buttons_cur=(buttons); s.count=0; update_at(&s,&pad,log,(time)); for(int ti=0;ti<s.count;++ti) { errors+=s.ids[ti]<0 || s.ids[ti]>31; for(int tj=0;tj<ti;++tj) errors+=s.ids[ti]==s.ids[tj]; } } while(0)
 STEP(HidNpadButton_ZL,1); errors+=s.count!=1 || !has(&s,AIM) || has(&s,JUMP);
 STEP(HidNpadButton_ZL,2); errors+=s.count!=0;
 STEP(0,3); errors+=!has(&s,AIM);
 STEP(HidNpadButton_ZR,4); errors+=!has(&s,FIRE);
 STEP(HidNpadButton_ZR,5); errors+=!has(&s,FIRE);
 STEP(0,6); errors+=s.count!=0;
 STEP(HidNpadButton_Plus,7); errors+=!has(&s,MAP);
 STEP(0,8); STEP(HidNpadButton_Minus,9); errors+=!has(&s,BAG);
 STEP(0,10);
 STEP(HidNpadButton_A,100); STEP(0,200); errors+=!has(&s,CROUCH);
 STEP(HidNpadButton_A,300); STEP(HidNpadButton_A,800); errors+=!has(&s,PRONE);
 STEP(0,900); errors+=s.count!=0;
 STEP(HidNpadButton_Y,1000); STEP(0,1100); errors+=!has(&s,PICKUP);
 STEP(HidNpadButton_Y,1200); STEP(HidNpadButton_Y,1700); errors+=!has(&s,RELOAD);
 STEP(0,1800); errors+=s.count!=0;
 STEP(HidNpadButton_StickL,1810); errors+=!sprint_enabled;
 STEP(0,1820); STEP(HidNpadButton_StickL,1830); errors+=sprint_enabled;
 STEP(0,1840);
 STEP(HidNpadButton_StickR,1841); errors+=s.count!=0;
 STEP(0,1842); errors+=!has(&s,PRONE) || has(&s,INTERACT);
 STEP(HidNpadButton_L|HidNpadButton_Y,1850);
 STEP(HidNpadButton_L,1860); errors+=!has(&s,INTERACT);
 STEP(0,1870); errors+=has(&s,HEAL);
 STEP(HidNpadButton_R|HidNpadButton_L,1900); errors+=!vehicle || s.count!=0;
 STEP(HidNpadButton_R|HidNpadButton_L,2000); errors+=!vehicle;
 STEP(HidNpadButton_R,2100); errors+=s.count!=0;
 STEP(0,2200); errors+=s.count!=0;
 pad.sticks[1].x=-22000; STEP(HidNpadButton_ZR,2300); errors+=!has(&s,FORWARD) || !has(&s,LEFT) || has(&s,FIRE) || has(&s,AIM);
 pad.sticks[1].x=22000; STEP(HidNpadButton_ZL,2400); errors+=!has(&s,REVERSE) || !has(&s,RIGHT);
 pad.sticks[1].x=0; STEP(HidNpadButton_ZL|HidNpadButton_ZR,2500); errors+=s.count!=0;
 STEP(HidNpadButton_A,2600); errors+=!has(&s,EXIT) || has(&s,CROUCH);
 STEP(0,2700); errors+=vehicle || s.count!=0;
 pad.sticks[0].x=20000; pad.sticks[1].x=15000;
 STEP(HidNpadButton_ZR,2800); errors+=s.count!=3 || !has(&s,FIRE);
 STEP(0,2900); /* physical fingers coexist with controller fire */
 s=(SwitchTouchSnapshot){.count=1,.ids={0},.x={100},.y={100}}; pad.buttons_cur=HidNpadButton_ZR; update_at(&s,&pad,log,3000);
 errors+=s.count!=4 || s.ids[0]!=0 || !has(&s,FIRE);
 for(int i=0;i<s.count;++i) { errors+=s.ids[i]<0 || s.ids[i]>31; for(int j=0;j<i;++j) errors+=s.ids[i]==s.ids[j]; }
 pad.sticks[0].x=pad.sticks[1].x=0;
 controller_touch_set_vehicle_state(1,log);
 STEP(HidNpadButton_A,3100); errors+=!has(&s,EXIT);
 STEP(0,3200); errors+=!vehicle; /* request alone must not switch modes */
 STEP(HidNpadButton_R|HidNpadButton_L,3300); errors+=!vehicle;
 STEP(0,3400);
 controller_touch_set_vehicle_state(0,log); errors+=vehicle || !vehicle_auto;
 STEP(HidNpadButton_ZR,3500); errors+=!has(&s,FIRE) || has(&s,FORWARD);
 controller_touch_set_vehicle_state(-1,log); errors+=vehicle_auto;
 STEP(0,4000);
 STEP(HidNpadButton_StickR,4100); errors+=s.count!=0;
 STEP(HidNpadButton_StickR,4599); errors+=s.count!=0;
 STEP(HidNpadButton_StickR,4600); errors+=!has(&s,INTERACT) || has(&s,PRONE);
 STEP(HidNpadButton_StickR,4700); errors+=s.count!=0;
 STEP(0,4800); errors+=s.count!=0;
 STEP(HidNpadButton_R,4900); STEP(0,4910); errors+=!has(&s,GRENADE) || has(&s,HEAL);
 STEP(HidNpadButton_L,4920); STEP(0,4930); errors+=!has(&s,HEAL) || has(&s,GRENADE);
 controller_touch_set_vehicle_state(1,log);
 pad.sticks[1].x=22000; pad.sticks[0].y=22000;
 STEP(0,5000); errors+=!has(&s,FORWARD) || !has(&s,RIGHT);
 pad.sticks[1].x=-22000; pad.sticks[0].y=-22000;
 STEP(0,5100); errors+=!has(&s,REVERSE) || !has(&s,LEFT);
 pad.sticks[0].x=pad.sticks[0].y=pad.sticks[1].x=0;
 STEP(HidNpadButton_Up|HidNpadButton_Left,5200); errors+=!has(&s,FORWARD) || !has(&s,LEFT);
 STEP(HidNpadButton_Down|HidNpadButton_Right,5300); errors+=!has(&s,REVERSE) || !has(&s,RIGHT);
 STEP(HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right,5400); errors+=s.count!=0;
 STEP(0,5500); errors+=s.count!=0;
 pad.sticks[0].x=22000; pad.sticks[1].y=22000;
 STEP(0,5600); errors+=s.count!=0; /* unused driving axes must not steer/look */
 #undef STEP
 fprintf(log,"CONTROLLER_TOUCH_TEST revision=3 errors=%d held_scope=CHECKED sprint_toggle=CHECKED pickup=CHECKED pointer_id_range=CHECKED continuous_fire=CHECKED tap_hold=CHECKED vehicle_controls=CHECKED chord_isolation=CHECKED simultaneous_inputs=CHECKED actual_ui_response=PENDING\n",errors);
 reset(); return errors ? -1 : 0;
}
