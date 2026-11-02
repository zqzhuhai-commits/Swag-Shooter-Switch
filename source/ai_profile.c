#include "ai_profile.h"
#include "nx_memory.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "squad_rules.h"
#include "health_warning.h"
#include "gait_policy.h"
#include "zone_safety_policy.h"
typedef struct {
 void *(*klass)(void *);
 void *(*field)(void *,const char *);
 unsigned (*flags)(void *);
 void *(*type)(void *);
 int (*kind)(const void *);
 void (*read)(void *,void *,void *);
 void (*write)(void *,void *,void *);
 const void *(*method)(void *,const char *,int);
 void *(*invoke)(const void *,void *,void **,void **);
 void *(*unbox)(void *);
 uint32_t (*weak)(void *,unsigned char);
 void *(*target)(uint32_t);
 void (*release)(uint32_t);
 void *(*class_named)(const void *,const char *,const char *);
 void (*static_read)(void *,void *);
 void *(*new_string)(const char *);
 uint32_t (*strong)(void *,unsigned char);
 int (*string_length)(void *);
 const uint16_t *(*string_chars)(void *);
 void *(*object_new)(void *);
 void *(*array_new)(void *,uintptr_t);
 uintptr_t (*array_length)(void *);
 const void *(*corlib)(void);
 size_t (*field_offset)(void *);
 void (*barrier)(void *,void **,void *);
 void *(*class_from_type)(const void *);
 unsigned char (*assignable)(void *,void *);
 void *(*resolve_icall)(const char *);
} AiApi;
static AiApi api;
static FILE *output;
static int difficulty=1,dirty,updating_ui;
static unsigned bot_count;
static void *object_field(void *object,const char *name);
static const void *profile_game_image;
static void squad_update_gun(void *gun);
static void *ui_strings[11];
static uint32_t ui_handles[11];
static void *ui_string(unsigned index) {
 static const char *words[]={"简单AI","普通AI","困难AI","精英AI","经典模式","死亡竞赛","狙击模式","斧头模式","匕首模式","自由模式","游戏难度"};
 if(!ui_strings[index]) {
  void *s=api.new_string(words[index]);if(!s)return NULL;
  ui_handles[index]=api.strong(s,0);if(ui_handles[index])ui_strings[index]=s;
 }
 return ui_strings[index];
}
static void set_ui_text(void *text,unsigned index) {
 if(!text)return;
 void *desired=ui_string(index),*current=object_field(text,"m_Text");
 if(!desired || current==desired)return;
 const void *setter=api.method(api.klass(text),"set_text",1);
 if(setter) {void *args[]={desired},*exception=NULL;api.invoke(setter,text,args,&exception);}
}
static uint32_t loading_panel;
static void (*original_loading_enable)(void *,void *);
static void loading_tip_author(void *panel) {
 void *text=object_field(panel,"textToShowText"),*value=object_field(text,"m_Text");
 if(!value || !api.string_length || !api.string_chars)return;
 int n=api.string_length(value);const uint16_t *chars=api.string_chars(value);
 if(!chars || n<7 || n>2048)return;
 /* The game prepends an ASCII speaker name ("Alexa : ") to its tips. */
 int colon=0;
 while(colon<n && colon<24 && chars[colon]!=':') {
  uint16_t c=chars[colon];
  if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || c==' '))return;
  ++colon;
 }
 if(colon<2 || colon>=n || colon>=24 || chars[colon]!=':')return;
 char localized[8192];size_t used=0;
 const char *prefix="小帆：";used=strlen(prefix);memcpy(localized,prefix,used);
 int start=colon+1;while(start<n && chars[start]==' ')++start;
 for(int i=start;i<n;++i) {
  unsigned c=chars[i];
  if(c>=0xd800 && c<=0xdbff && i+1<n && chars[i+1]>=0xdc00 && chars[i+1]<=0xdfff)
   c=0x10000+((c-0xd800)<<10)+(chars[++i]-0xdc00);
  if(c<0x80)localized[used++]=(char)c;
  else if(c<0x800) {localized[used++]=(char)(0xc0|(c>>6));localized[used++]=(char)(0x80|(c&63));}
  else if(c<0x10000) {localized[used++]=(char)(0xe0|(c>>12));localized[used++]=(char)(0x80|((c>>6)&63));localized[used++]=(char)(0x80|(c&63));}
  else {localized[used++]=(char)(0xf0|(c>>18));localized[used++]=(char)(0x80|((c>>12)&63));localized[used++]=(char)(0x80|((c>>6)&63));localized[used++]=(char)(0x80|(c&63));}
 }
 localized[used]=0;
 const void *setter=api.method(api.klass(text),"set_text",1);
 void *replacement=api.new_string(localized),*ex=NULL,*args[]={replacement};
 if(setter && replacement)api.invoke(setter,text,args,&ex);
}
static void loading_difficulty_refresh(void) {
 void *panel=loading_panel ? api.target(loading_panel) : NULL;if(!panel)return;
 loading_tip_author(panel);
 set_ui_text(object_field(panel,"serverText"),(unsigned)difficulty);
 set_ui_text(object_field(panel,"serverTitleText"),10);
}
static void loading_enable(void *panel,void *method) {
 original_loading_enable(panel,method);
 if(loading_panel)api.release(loading_panel);
 loading_panel=api.weak(panel,0);loading_difficulty_refresh();
 if(output)fprintf(output,"AI_LOADING_DIFFICULTY selected=%d source=saved_ai_setting\n",difficulty);
}
void ai_profile_menu(const void *image) {
 profile_game_image=image;
 if(!api.class_named || !api.static_read || !api.new_string || !api.strong || !api.string_chars || !api.string_length)return;
 void *klass=api.class_named(image,"hms.countryWar2","MainMenuManager");if(!klass)return;
 void *field=api.field(klass,"Instance"),*menu=NULL;
 if(!field || !(api.flags(field)&0x10))return;
 api.static_read(field,&menu);if(!menu)return;
 set_ui_text(object_field(menu,"regionNameText"),(unsigned)difficulty);
 void *text=object_field(menu,"gameModeNameText"),*value=object_field(text,"m_Text");
 if(value) {
  const char *english[]={"CLASSIC MODE","DEATH MATCH","SNIPER MODE","AXE MODE","KNIFE MODE","FREE MODE"};
  int n=api.string_length(value);const uint16_t *chars=api.string_chars(value);
  for(unsigned i=0;i<6;++i) {
   if((int)strlen(english[i])!=n)continue;
   int same=1;for(int j=0;j<n;++j)if(chars[j]!=(unsigned char)english[i][j])same=0;
   if(same){set_ui_text(text,4+i);break;}
  }
 }
}
/* Squad size is independent of match population. Do not cap the entire
 * battle royale to seven bots to imitate a four-player squad. The stock
 * two-team backend needs a separate multi-squad implementation. */
static uint32_t difficulty_controls[8];
static void register_difficulty_control(void *dropdown) {
 for(unsigned i=0;i<8;++i)if(difficulty_controls[i] && api.target(difficulty_controls[i])==dropdown)return;
 for(unsigned i=0;i<8;++i)if(!difficulty_controls[i] || !api.target(difficulty_controls[i])) {
  if(difficulty_controls[i])api.release(difficulty_controls[i]);
  difficulty_controls[i]=api.weak(dropdown,0);return;
 }
}
typedef struct {const char *name;int kind;float floor;} Spec;
static const Spec gun_specs[]={
 {"inaccuracy",12,0.2f},{"minPauseTime",12,0.07f},{"randomPauseTimeAdd",12,0.1f}
};
static const Spec base_specs[]={
 {"maxTimeToWaitAtEachSkirmishPoint",12,0.3f},
 {"maxTimeInCover",12,0.5f},{"timeBetweenLoSDodges",12,0.6f},
 {"canCrossBehindTarget",2,0},{"shouldTryAndDodge",2,0},
 {"shouldRunFromGrenades",2,0},{"shouldFireFromCover",2,0}
};
typedef union {float f;unsigned char b[sizeof(void *)];} Value;
typedef struct {
 uint32_t gun,base;float personality;
 Value original_gun[3],original_base[7];
 unsigned gun_present,base_present;
 uint64_t route_next_tick;float route_position[3];int route_forced;
} Bot;
static Bot bots[128];
static NxModuleMemory trampoline_memory;
static void (*original_gun_changed)(void *,void *);
static const char *names[]={"easy","normal","hard","veteran"};
static void load_setting(void) {
 FILE *f=fopen("sdmc:/switch/swag-ai.cfg","r");int value=1;
 if(f) { if(fscanf(f,"difficulty=%d",&value)!=1 || value<0 || value>3)value=1;fclose(f); }
 difficulty=value;
}
static void save_setting(void) {
 FILE *f=fopen("sdmc:/switch/swag-ai.cfg","w");
 if(f) {fprintf(f,"difficulty=%d\n",difficulty);fclose(f);}
}
static void *checked_field(void *object,const Spec *spec) {
 if(!object)return NULL;
 void *field=api.field(api.klass(object),spec->name);
 if(!field || (api.flags(field)&0x10) || api.kind(api.type(field))!=spec->kind)return NULL;
 return field;
}
/* Runs once at the native coroutine's next-circle deadline boundary, not
 * once per render frame. A newly allocated coroutine resets the schedule. */
uintptr_t zone_phase_resume;
extern void zone_phase_bridge(void);
static uint32_t zone_coroutine;
static uint32_t zone_area_handle;
static unsigned zone_phase;
static unsigned zone_round_serial;
/* Livik-inspired 52-player pacing; custom adaptation, not an official table. */
static const unsigned zone_wait_seconds[]={180,120,90,75,60,45};
static const unsigned zone_shrink_seconds[]={90,75,60,45,30,30};
typedef struct {float x,y,z;} ZoneVector;
static const float zone_shrink_fraction[]={0.25f,0.35f,0.40f,0.45f,0.50f,0.50f};
static int zone_vector_property(void *object,const char *name,ZoneVector *value,int write) {
 if(!object || !api.unbox)return -1;
 const void *method=api.method(api.klass(object),name,write ? 1 : 0);
 if(!method)return -1;
 void *exception=NULL,*args[]={value};
 void *result=api.invoke(method,object,write ? args : NULL,&exception);
 if(exception)return -1;
 if(!write) {if(!result)return -1;void *unboxed=api.unbox(result);if(!unboxed)return -1;memcpy(value,unboxed,sizeof *value);}
 return 0;
}
static int zone_resize_next(void *area,unsigned phase) {
 const Spec current_spec={"_currentScale",17,0},next_spec={"_currentNextAreaScale",17,0};
 const Spec delta_spec={"_targetDecrement",12,0},minimum_spec={"minScale",12,0};
 const Spec radius_spec={"radiusSquare",12,0},percent_spec={"percentageDecrement",12,0};
 const Spec world_spec={"worldIconSizePercentages",12,0},map_spec={"minimapIconSizePercentages",12,0};
 void *cf=checked_field(area,&current_spec),*nf=checked_field(area,&next_spec);
 void *df=checked_field(area,&delta_spec),*mf=checked_field(area,&minimum_spec);
 void *rf=checked_field(area,&radius_spec),*pf=checked_field(area,&percent_spec);
 void *wf=checked_field(area,&world_spec),*mapf=checked_field(area,&map_spec);
 if(!cf || !nf || !df || !mf || !rf || !pf || !wf || !mapf)return -1;
 ZoneVector current;float minimum=0,radius_squared=0,world_ratio=0,map_ratio=0;
 api.read(area,cf,&current);api.read(area,mf,&minimum);api.read(area,rf,&radius_squared);
 api.read(area,wf,&world_ratio);api.read(area,mapf,&map_ratio);
 if(!isfinite(current.x) || !isfinite(current.z) || !isfinite(minimum) ||
    !isfinite(radius_squared) || !isfinite(world_ratio) || !isfinite(map_ratio) ||
    current.x<=0 || minimum<=0 || radius_squared<=0)return -1;
 float target=fmaxf(minimum,current.x*(1.0f-zone_shrink_fraction[phase]));
 target=fminf(target,current.x);
 float decrement=current.x-target,percent=zone_shrink_fraction[phase]*100.0f;
 void *next=object_field(area,"nextAreaTransform"),*icon=object_field(area,"nextAreaIcon");
 const Spec icon_world={"worldmapIconSize",12,0},icon_map={"minimapIconSize",12,0};
 void *iw=checked_field(icon,&icon_world),*im=checked_field(icon,&icon_map);
 const void *get_transform=api.method(api.klass(area),"get_transform",0);
 if(!next || !iw || !im || !get_transform)return -1;
 void *exception=NULL,*transform=api.invoke(get_transform,area,NULL,&exception);
 if(exception || !transform)return -1;
 ZoneVector center,next_position,old_position,old_scale;
 if(zone_vector_property(transform,"get_position",&center,0) ||
    zone_vector_property(next,"get_position",&next_position,0) ||
    zone_vector_property(next,"get_localScale",&old_scale,0))return -1;
 old_position=next_position;
 if(!isfinite(old_scale.y))return -1;
 if(!isfinite(center.x) || !isfinite(center.z) || !isfinite(next_position.x) || !isfinite(next_position.z))return -1;
 /* Keep the next circle fully inside the current one in the horizontal plane. */
 float dx=next_position.x-center.x,dz=next_position.z-center.z;
 float distance=hypotf(dx,dz),allowance=sqrtf(radius_squared)*(1.0f-target/current.x)*0.98f;
 if(!isfinite(distance) || !isfinite(allowance))return -1;
 if(distance>allowance && distance>0) {
  next_position.x=center.x+dx*(allowance/distance);
  next_position.z=center.z+dz*(allowance/distance);
 }
 ZoneVector next_scale={target,old_scale.y,target};
 if(zone_vector_property(next,"set_position",&next_position,1))return -1;
 if(zone_vector_property(next,"set_localScale",&next_scale,1)) {
  zone_vector_property(next,"set_position",&old_position,1);
  zone_vector_property(next,"set_localScale",&old_scale,1);return -1;
 }
 api.write(area,nf,&next_scale);api.write(area,df,&decrement);api.write(area,pf,&percent);
 float world_size=world_ratio*target,map_size=map_ratio*target;
 api.write(icon,iw,&world_size);api.write(icon,im,&map_size);
 if(output)fprintf(output,"ZONE_SIZE phase=%u shrink_current_percent=%.0f current_scale=%.3f target_scale=%.3f center_limit=%.3f preview_sync=1\n",zone_phase+1,percent,current.x,target,allowance);
 return 0;
}
void zone_phase_prepare(void *area,void *coroutine) {
 if(!area || !coroutine)return;
 const Spec wait_spec={"timeIntervalInMin",12,0},shrink_spec={"decrementIntervalSec",12,0};
 const Spec online_spec={"_isOnlineScene",2,0};
 void *wait=checked_field(area,&wait_spec),*shrink=checked_field(area,&shrink_spec);
 void *online=checked_field(area,&online_spec);unsigned char network=0;
 if(!wait || !shrink || !online)return;
 api.read(area,online,&network);if(network)return;
 if(!zone_area_handle || api.target(zone_area_handle)!=area) {
  if(zone_area_handle)api.release(zone_area_handle);
  zone_area_handle=api.weak(area,0);
 }
 if(!zone_coroutine || api.target(zone_coroutine)!=coroutine) {
  if(zone_coroutine)api.release(zone_coroutine);
  zone_coroutine=api.weak(coroutine,0);zone_phase=0;++zone_round_serial;
  if(!zone_coroutine)return;
 }
 unsigned phase=zone_phase<6 ? zone_phase : 5;
 float minutes=(float)zone_wait_seconds[phase]/60.0f;
 float seconds=(float)zone_shrink_seconds[phase];
 api.write(area,wait,&minutes);api.write(area,shrink,&seconds);
 if(zone_resize_next(area,phase) && output)fprintf(output,"ZONE_SIZE phase=%u status=UNCHANGED reason=field_or_transform_unavailable\n",zone_phase+1);
 if(output)fprintf(output,"ZONE_PHASE phase=%u wait_seconds=%u shrink_seconds=%u size_profile=current_scale reset_on_new_round=1\n",zone_phase+1,zone_wait_seconds[phase],zone_shrink_seconds[phase]);
 if(zone_phase<1000)++zone_phase;
}
static void *object_field(void *object,const char *name) {
 if(!object)return NULL;
 void *field=api.field(api.klass(object),name),*value=NULL;
 if(!field || (api.flags(field)&0x10))return NULL;
 int kind=api.kind(api.type(field));if(kind!=0x12 && kind!=0x1c && kind!=0x0e)return NULL;
 api.read(object,field,&value);return value;
}
#include "squad_mode.inc"
#include "zone_ai.inc"
static void health_warning_poll(void) {
 void *game=squad_singleton("brijesh.CountryWar2","GameManager","_instance");
 void *state=game ? api.field(api.klass(game),"gameState") : NULL;int playing=-1;
 if(state && !(api.flags(state)&0x10) && api.kind(api.type(state))==17)api.read(game,state,&playing);
 if(playing!=1) {health_warning_set(-1);return;}
 void *player=object_field(game,"_localPlayer");int alive=0,local=0;
 if(!player || squad_bool(player,"_isAlive",&alive) || !alive ||
    squad_bool(player,"_isLocalPlayer",&local) || !local) {health_warning_set(-1);return;}
 void *health=object_field(player,"playerHealth");
 const Spec hp={"_health",12,0},maximum={"maxHealth",12,0};
 void *hf=checked_field(health,&hp),*mf=checked_field(health,&maximum);float value=0,max=0;
 if(!hf || !mf) {health_warning_set(-1);return;}
 api.read(health,hf,&value);api.read(health,mf,&max);
 if(!isfinite(value) || !isfinite(max) || value<=0 || max<=0) {health_warning_set(-1);return;}
 float ratio=fminf(value/max,1.0f);
 health_warning_set((int)(ratio*1000.0f));
}
static void capture(void *object,const Spec *spec,unsigned count,Value *values,unsigned *present) {
 *present=0;
 for(unsigned i=0;i<count;++i) {
  void *field=checked_field(object,&spec[i]);if(!field)continue;
  memset(&values[i],0,sizeof values[i]);api.read(object,field,&values[i]);
  if(spec[i].kind==12 && (!isfinite(values[i].f) || values[i].f<0 || values[i].f>10000))continue;
  if(spec[i].kind==2 && values[i].b[0]>1)continue;
  *present|=1u<<i;
 }
}
static float scale_value(float original,float factor,float floor) {
 if(!isfinite(original) || original<=0)return original;
 float value=original*factor;
 if(factor<1) {value=fmaxf(value,floor);value=fminf(value,original);}
 return value;
}
static void apply_bot(Bot *bot) {
 void *gun=api.target(bot->gun),*base=bot->base ? api.target(bot->base) : NULL;
 if(!gun)return;
 const float accuracy[]={1.35f,1,0.60f,0.25f};
 const float pause[]={1.3f,1,0.60f,0.25f};
 const float tactical[]={1.15f,1,0.65f,0.32f};
 float variation=difficulty>=2 ? bot->personality : 1;
 for(unsigned i=0;i<3;++i) {
  if(!(bot->gun_present&(1u<<i)))continue;
  void *field=checked_field(gun,&gun_specs[i]);if(!field)continue;
  float factor=(i==0 ? accuracy[difficulty] : pause[difficulty])*variation;
  float value=scale_value(bot->original_gun[i].f,factor,gun_specs[i].floor);
  api.write(gun,field,&value);
 }
 if(!base)return;
 for(unsigned i=0;i<7;++i) {
  if(!(bot->base_present&(1u<<i)))continue;
  void *field=checked_field(base,&base_specs[i]);if(!field)continue;
  Value value=bot->original_base[i];
  if(i<3)value.f=scale_value(value.f,tactical[difficulty]*variation,base_specs[i].floor);
  else if(difficulty>=2) {
   int supported=object_field(base,"navI")!=NULL;
   if(i==4)supported=supported && object_field(base,"animationScript")!=NULL;
   if(i==6)supported=supported && object_field(base,"coverFinderScript")!=NULL;
   if(supported)value.b[0]=1;
  }
  api.write(base,field,&value);
 }
}
static void tune_gun(void *object) {
 if(!object)return;
 squad_update_gun(object);
 Bot *slot=NULL;
 for(unsigned i=0;i<128;++i) {
  if(bots[i].gun && api.target(bots[i].gun)==object) {slot=&bots[i];break;}
 }
 if(!slot) {
  for(unsigned i=0;i<128;++i) {
   if(bots[i].gun && api.target(bots[i].gun))continue;
   slot=&bots[i];if(slot->gun)api.release(slot->gun);if(slot->base)api.release(slot->base);
   memset(slot,0,sizeof *slot);slot->gun=api.weak(object,0);if(!slot->gun)return;
   uintptr_t seed=(uintptr_t)object;seed^=seed>>13;seed^=seed<<7;
   slot->personality=0.9f+(float)(seed%201)/1000.0f;
   capture(object,gun_specs,3,slot->original_gun,&slot->gun_present);
   ++bot_count;break;
  }
 }
 if(!slot)return;
 void *base=object_field(object,"myAIBaseScript");
 if(base && (!slot->base || api.target(slot->base)!=base)) {
  if(slot->base)api.release(slot->base);
  slot->base=api.weak(base,0);
  capture(base,base_specs,7,slot->original_base,&slot->base_present);
 }
 apply_bot(slot);
 dirty=1; /* One deferred pass after the component initialization frame. */
 if(bot_count<=3 && output)fprintf(output,"AI_TUNE profile=%s gun_fields=%x base_fields=%x variation=%.3f\n",names[difficulty],slot->gun_present,slot->base_present,slot->personality);
}
static void gun_changed(void *object,void *method) {
 original_gun_changed(object,method);tune_gun(object);
}
static void settings_changed(void *object,void *method) {
 (void)method;if(updating_ui)return;
 void *dropdown=object_field(object,"regionSelectDropdown");if(!dropdown)return;
 register_difficulty_control(dropdown);
 const Spec value_spec={"m_Value",8,0};void *field=checked_field(dropdown,&value_spec);
 int index=-1;if(field)api.read(dropdown,field,&index);
 if(index<0 || index>3)return;
 difficulty=index;dirty=1;save_setting();
 if(output)fprintf(output,"AI_DIFFICULTY selected=%s saved=1\n",names[index]);
}
static void settings_initialize(void *object,void *method) {
 (void)method;void *dropdown=object_field(object,"regionSelectDropdown");if(!dropdown)return;
 register_difficulty_control(dropdown);
 const void *setter=api.method(api.klass(dropdown),"SetValueWithoutNotify",1);
 if(!setter)setter=api.method(api.klass(dropdown),"set_value",1);
 if(!setter)return;
 void *arguments[]={&difficulty},*exception=NULL;
 updating_ui=1;api.invoke(setter,dropdown,arguments,&exception);updating_ui=0;
 if(exception && output)fprintf(output,"AI_DIFFICULTY_UI result=EXCEPTION\n");
}
/* Experimental shoulder camera, retaining FPS input/HUD semantics. */
#ifdef SWAG_THIRD_PERSON_TEST
static unsigned camera_toggle_pending,camera_aim_held;
void ai_profile_toggle_camera(void) {__atomic_fetch_xor(&camera_toggle_pending,1,__ATOMIC_RELAXED);}
void ai_profile_camera_aim(int held) {__atomic_store_n(&camera_aim_held,held!=0,__ATOMIC_RELAXED);}
typedef struct {uint32_t handle;unsigned char enabled;} ShoulderRenderer;
static ShoulderRenderer shoulder_renderers[96];static unsigned shoulder_renderer_count;
static uint32_t shoulder_body;static unsigned char shoulder_body_active;
static ZoneVector shoulder_original_angles;static int shoulder_have_angles;
static uint32_t shoulder_animation,shoulder_lens,shoulder_ik,shoulder_controller;
static unsigned char shoulder_ik_enabled;
static void (*original_third_animation_late)(void *,void *);
static void (*original_player_update)(void *,void *);
static void shoulder_align_pose(void) {
 void *body=shoulder_body ? api.target(shoulder_body) : NULL;
 void *lens=shoulder_lens ? api.target(shoulder_lens) : NULL;
 void *animation=shoulder_animation ? api.target(shoulder_animation) : NULL;
 void *body_transform=NULL;ZoneVector angles;
 if(!body || !lens || !animation || squad_invoke(body,"get_transform",0,NULL,&body_transform) || !body_transform ||
    zone_vector_property(lens,"get_eulerAngles",&angles,0))return;
 float pitch=angles.x;if(pitch>180)pitch-=360;
 ZoneVector heading={0,angles.y,0};
 if(zone_vector_property(body_transform,"set_eulerAngles",&heading,1))return;
 /* Native FPS view disables the third-person aim solver. Showing its mesh
  * alone leaves the animated weapon in its unaimed pose. Re-enable only
  * this local model's solver and feed it the same pitch as the firing view. */
 void *pitch_args[]={&pitch};squad_invoke(animation,"OnVerticalRotationReceived",1,pitch_args,NULL);
 void *targets=object_field(animation,"aimTargets");ZoneVector target_angles={pitch,0,0};
 if(targets)zone_vector_property(targets,"set_localEulerAngles",&target_angles,1);
 void *ik=shoulder_ik ? api.target(shoulder_ik) : NULL;
 unsigned char yes=1;void *enable[]={&yes};
 if(ik)squad_invoke(ik,"set_enabled",1,enable,NULL);
}
/* Local third-person locomotion stabilization. Skeleton pose only: the
 * controller, animation state, movement and lower-body bones remain native. */
typedef struct {float x,y,z,w;} ShoulderQuaternion;
typedef struct {uint32_t transform;ShoulderQuaternion rotation;} ShoulderBone;
static ShoulderBone shoulder_bones[13];static unsigned shoulder_bone_count;
static int shoulder_pose_valid,shoulder_pose_weapon=-1;
static uint32_t shoulder_pose_animator;static float shoulder_original_speed;
static float shoulder_original_state_speed;static int shoulder_state_speed_saved;
static unsigned shoulder_gait_rate,shoulder_gait_mode;static uint64_t shoulder_gait_tick,shoulder_gait_log;
static float shoulder_gait_phase;
/* Unity 2019 AnimatorStateInfo, independently checked against metadata. */
typedef struct {uint32_t name,path,full_path;float phase,length,speed,multiplier;uint32_t tag;int loop;} ShoulderStateInfo;
_Static_assert(sizeof(ShoulderStateInfo)==36,"AnimatorStateInfo layout");
static ZoneVector shoulder_spine_anchor;static ShoulderQuaternion shoulder_spine_rotation;
static ShoulderQuaternion shoulder_multiply(ShoulderQuaternion a,ShoulderQuaternion b) {
 return (ShoulderQuaternion){a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
  a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
  a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
static int shoulder_rotation(void *transform,const char *name,ShoulderQuaternion *q,int write) {
 void *boxed=NULL,*args[]={q};
 if(squad_invoke(transform,name,write ? 1 : 0,write ? args : NULL,&boxed))return -1;
 if(!write) {if(!boxed || !api.unbox(boxed))return -1;memcpy(q,api.unbox(boxed),sizeof *q);}
 return 0;
}
static void shoulder_restore_speed(void) {
 shoulder_gait_tick=0;shoulder_gait_rate=0;
 if(shoulder_pose_animator) {
  void *animator=api.target(shoulder_pose_animator),*args[]={&shoulder_original_speed};
  if(animator) {
   squad_invoke(animator,"set_speed",1,args,NULL);
   if(shoulder_state_speed_saved) {
    int id=254213878;void *state_args[]={&id,&shoulder_original_state_speed};
    squad_invoke(animator,"SetFloatID",2,state_args,NULL);
   }
  }
 }
}
static void shoulder_clear_pose(void) {
 shoulder_restore_speed();
 if(shoulder_pose_animator) {api.release(shoulder_pose_animator);shoulder_pose_animator=0;}

 for(unsigned i=0;i<shoulder_bone_count;++i)api.release(shoulder_bones[i].transform);
 shoulder_bone_count=0;shoulder_pose_valid=0;shoulder_pose_weapon=-1;shoulder_state_speed_saved=0;
}
static ShoulderQuaternion shoulder_blend_rotation(ShoulderQuaternion current,ShoulderQuaternion stable,float weight) {
 float dot=current.x*stable.x+current.y*stable.y+current.z*stable.z+current.w*stable.w;
 if(dot<0) {stable.x=-stable.x;stable.y=-stable.y;stable.z=-stable.z;stable.w=-stable.w;}
 ShoulderQuaternion q={current.x+(stable.x-current.x)*weight,current.y+(stable.y-current.y)*weight,
  current.z+(stable.z-current.z)*weight,current.w+(stable.w-current.w)*weight};
 float length=sqrtf(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
 if(length<0.00001f)return current;
 q.x/=length;q.y/=length;q.z/=length;q.w/=length;return q;
}
typedef struct {void *player,*controller,*animator,*root;int moving,running,grounded;} ShoulderMotion;
static int shoulder_motion(void *animation,ShoulderMotion *motion) {
 memset(motion,0,sizeof *motion);
 motion->player=object_field(animation,"player");motion->controller=object_field(motion->player,"fpsController");
 motion->animator=object_field(animation,"animator");
 void *body=shoulder_body ? api.target(shoulder_body) : NULL;
 if(!motion->controller || !motion->animator || !body ||
    squad_invoke(body,"get_transform",0,NULL,&motion->root) || !motion->root)return 0;
 void *character=object_field(motion->controller,"controller"),*grounded=NULL;ZoneVector velocity;
 if(!character || squad_invoke(character,"get_isGrounded",0,NULL,&grounded) ||
    !grounded || !api.unbox(grounded) ||
    zone_vector_property(character,"get_velocity",&velocity,0))return 0;
 motion->grounded=*(unsigned char *)api.unbox(grounded)!=0;
 /* Use real movement input as well: a final gravity-only Move can report
  * zero horizontal CharacterController velocity despite ongoing walking. */
 float horizontal=0,vertical=0;
 const Spec hs={"_hor",12,0},vs={"_ver",12,0};
 void *hf=checked_field(motion->controller,&hs),*vf=checked_field(motion->controller,&vs);
 if(hf)api.read(motion->controller,hf,&horizontal);
 if(vf)api.read(motion->controller,vf,&vertical);
 motion->moving=horizontal*horizontal+vertical*vertical>0.0025f || velocity.x*velocity.x+velocity.z*velocity.z>0.0025f;
 const char *blocked[]={"isReloading","jumping","climbladder","crouching"};
 for(unsigned i=0;i<sizeof blocked/sizeof blocked[0];++i) {
  int value=0;if(squad_bool(motion->controller,blocked[i],&value) || value)return 0;
 }
 if(squad_bool(motion->controller,"running",&motion->running))return 0;
 return 1;
}
static void shoulder_cadence(void *animation) {
 ShoulderMotion motion;
 if(!shoulder_motion(animation,&motion) || !motion.moving) {shoulder_restore_speed();return;}
 void *animator=motion.animator,*boxed=NULL;int layer=0;void *state_args[]={&layer};
 if(squad_invoke(animator,"GetCurrentAnimatorStateInfo",1,state_args,&boxed) || !boxed || !api.unbox(boxed)) {
  shoulder_restore_speed();return;
 }
 ShoulderStateInfo state;memcpy(&state,api.unbox(boxed),sizeof state);
 /* Never slow reload, jump, death or other base-layer states. */
 if(state.full_path!=1294380448u || !isfinite(state.phase) || !isfinite(state.length) || state.length<=0) {
  shoulder_restore_speed();return;
 }
 if(!shoulder_pose_animator) {
  boxed=NULL;if(squad_invoke(animator,"get_speed",0,NULL,&boxed) || !boxed || !api.unbox(boxed))return;
  memcpy(&shoulder_original_speed,api.unbox(boxed),4);
  if(!isfinite(shoulder_original_speed) || shoulder_original_speed<=0)return;
  shoulder_pose_animator=api.weak(animator,0);if(!shoulder_pose_animator)return;
 }
 int speed_id=254213878;void *get_args[]={&speed_id};
 if(!shoulder_state_speed_saved) {
  boxed=NULL;if(squad_invoke(animator,"GetFloatID",1,get_args,&boxed) || !boxed || !api.unbox(boxed))return;
  memcpy(&shoulder_original_state_speed,api.unbox(boxed),4);shoulder_state_speed_saved=1;
 }
 /* WalkStates has a second speed multiplier named speed. Cap that actual
  * state parameter, after the native movement update and before evaluation. */
 float state_speed=1.0f;void *set_args[]={&speed_id,&state_speed};
 if(squad_invoke(animator,"SetFloatID",2,set_args,NULL))return;
 uint64_t now=armGetSystemTick(),freq=armGetSystemTickFreq();unsigned measured=0;
 if(!shoulder_gait_tick || shoulder_gait_mode!=(unsigned)motion.running) {
  shoulder_gait_tick=now;shoulder_gait_phase=state.phase;
  shoulder_gait_mode=(unsigned)motion.running;shoulder_gait_rate=gait_initial_rate(motion.running);
 } else if(now-shoulder_gait_tick>=freq/4) {
  float delta=state.phase-shoulder_gait_phase;
  if(delta>=0 && delta<30) {
   measured=(unsigned)(delta*1000.0f*(float)freq/(float)(now-shoulder_gait_tick));
   shoulder_gait_rate=gait_adjust_rate(shoulder_gait_rate,measured,gait_target_millihz(motion.running));
  }
  shoulder_gait_tick=now;shoulder_gait_phase=state.phase;
 }
 unsigned maximum=gait_max_rate(motion.running);
 if(shoulder_gait_rate>maximum)shoulder_gait_rate=maximum;
 float rate=(float)shoulder_gait_rate/1000.0f;void *rate_args[]={&rate};
 squad_invoke(animator,"set_speed",1,rate_args,NULL);
 if(output && measured && now>=shoulder_gait_log) {
  boxed=NULL;float readback=-1;
  if(!squad_invoke(animator,"get_speed",0,NULL,&boxed) && boxed && api.unbox(boxed))memcpy(&readback,api.unbox(boxed),4);
  fprintf(output,"GAIT_CONTROL running=%d state=%u measured_millihz=%u target_millihz=%u requested=%.3f readback=%.3f state_multiplier=1\n",
   motion.running,state.full_path,measured,gait_target_millihz(motion.running),rate,readback);
  shoulder_gait_log=now+freq*2;
 }
}
static void player_controller_update(void *controller,void *method) {
 original_player_update(controller,method);
 if(shoulder_controller && api.target(shoulder_controller)==controller) {
  void *animation=shoulder_animation ? api.target(shoulder_animation) : NULL;
  if(animation)shoulder_cadence(animation);
 }
}
static ShoulderQuaternion shoulder_small_sway(ShoulderQuaternion current,ShoulderQuaternion stable,float weight,float degrees) {
 float dot=fabsf(current.x*stable.x+current.y*stable.y+current.z*stable.z+current.w*stable.w);
 if(dot>1)dot=1;
 float angle=2.0f*acosf(dot),limit=degrees*0.01745329252f;
 if(angle>0.0001f && angle*(1.0f-weight)>limit)weight=1.0f-limit/angle;
 return shoulder_blend_rotation(current,stable,weight);
}
static void shoulder_stabilize_walk(void *animation) {
 shoulder_cadence(animation);
 ShoulderMotion motion;if(!shoulder_motion(animation,&motion) || !motion.grounded) {shoulder_pose_valid=0;return;}
 void *player=motion.player,*controller=motion.controller,*animator=motion.animator,*root=motion.root;
 int moving=motion.moving,running=motion.running,firing=0;
 if(!squad_bool(controller,"isFiring",&firing) && firing)return;
 float stability=running ? 0.84f : 0.88f;
 void *manager=object_field(player,"weaponManager");
 void *weapon_field=manager ? api.field(api.klass(manager),"currentWeaponId") : NULL;int weapon=-1;
 if(!weapon_field)return;
 api.read(manager,weapon_field,&weapon);
 if(weapon!=shoulder_pose_weapon) {shoulder_pose_valid=0;shoulder_pose_weapon=weapon;}
 if(!shoulder_bone_count) {
  /* HumanBodyBones: spine/chest/upper chest, head chain and both arms.
   * Hips, legs and feet are deliberately excluded. UpperChest is optional. */
  const int ids[]={7,8,54,9,10,11,12,13,14,15,16,17,18};
  void *human=NULL;
  if(squad_invoke(animator,"get_isHuman",0,NULL,&human) || !human || !api.unbox(human) || !*(unsigned char *)api.unbox(human))return;
  for(unsigned i=0;i<sizeof ids/sizeof ids[0];++i) {
   int id=ids[i];void *bone=NULL,*args[]={&id};
   if(squad_invoke(animator,"GetBoneTransform",1,args,&bone) || !bone) {if(!i)break;else continue;}
   uint32_t handle=api.weak(bone,0);if(!handle)continue;
   shoulder_bones[shoulder_bone_count++].transform=handle;
  }
  if(!shoulder_bone_count)return;
 }
 void *spine=api.target(shoulder_bones[0].transform);if(!spine)return;
 ShoulderQuaternion root_rotation;
 if(shoulder_rotation(root,"get_rotation",&root_rotation,0))return;
 if(!moving) {
  for(unsigned i=0;i<shoulder_bone_count;++i) {
   void *bone=api.target(shoulder_bones[i].transform);
   if(!bone || shoulder_rotation(bone,"get_localRotation",&shoulder_bones[i].rotation,0)) {shoulder_pose_valid=0;return;}
  }
  ZoneVector position;void *boxed=NULL,*args[]={&position};ShoulderQuaternion rotation;
  if(zone_vector_property(spine,"get_position",&position,0) ||
     squad_invoke(root,"InverseTransformPoint",1,args,&boxed) || !boxed || !api.unbox(boxed) ||
     shoulder_rotation(spine,"get_rotation",&rotation,0))return;
  memcpy(&shoulder_spine_anchor,api.unbox(boxed),sizeof shoulder_spine_anchor);
  ShoulderQuaternion inverse={-root_rotation.x,-root_rotation.y,-root_rotation.z,root_rotation.w};
  shoulder_spine_rotation=shoulder_multiply(inverse,rotation);shoulder_pose_valid=1;return;
 }
 if(!shoulder_pose_valid)return; /* Capture a settled standing pose first. */
 for(unsigned i=1;i<shoulder_bone_count;++i) {
  void *bone=api.target(shoulder_bones[i].transform);
  if(bone) {
   ShoulderQuaternion current;
   if(!shoulder_rotation(bone,"get_localRotation",&current,0)) {
    ShoulderQuaternion blended=shoulder_small_sway(current,shoulder_bones[i].rotation,stability,i<3 ? 2.0f : 4.0f);
    shoulder_rotation(bone,"set_localRotation",&blended,1);
   }
  }
 }
 void *boxed=NULL,*args[]={&shoulder_spine_anchor};
 if(!squad_invoke(root,"TransformPoint",1,args,&boxed) && boxed && api.unbox(boxed)) {
  ZoneVector anchor;memcpy(&anchor,api.unbox(boxed),sizeof anchor);
  ZoneVector current;
  if(!zone_vector_property(spine,"get_position",&current,0)) {
   float remainder=1.0f-stability;
   float dx=(current.x-anchor.x)*remainder,dy=(current.y-anchor.y)*remainder,dz=(current.z-anchor.z)*remainder;
   anchor.x+=fmaxf(-0.01f,fminf(0.01f,dx));
   anchor.y+=fmaxf(-0.01f,fminf(0.01f,dy));
   anchor.z+=fmaxf(-0.01f,fminf(0.01f,dz));
   zone_vector_property(spine,"set_position",&anchor,1);
  }
 }
 ShoulderQuaternion rotation=shoulder_multiply(root_rotation,shoulder_spine_rotation);
 ShoulderQuaternion current;
 if(!shoulder_rotation(spine,"get_rotation",&current,0)) {
  rotation=shoulder_small_sway(current,rotation,stability,2.0f);
  shoulder_rotation(spine,"set_rotation",&rotation,1);
 }
 /* Re-solve the weapon aim after stabilizing the pose; keep the direction
  * alignment that was confirmed on hardware in 0.14.68. */
 void *ik=shoulder_ik ? api.target(shoulder_ik) : NULL,*solver=object_field(ik,"solver");
 if(solver)squad_invoke(solver,"Update",0,NULL,NULL);
}
static void third_animation_late(void *animation,void *method) {
 original_third_animation_late(animation,method);
 if(shoulder_animation && api.target(shoulder_animation)==animation) {
  shoulder_align_pose();shoulder_stabilize_walk(animation);
 }
}
static void shoulder_restore_models(void) {
 shoulder_clear_pose();
 if(shoulder_controller) {api.release(shoulder_controller);shoulder_controller=0;}
 if(shoulder_ik) {
  void *ik=api.target(shoulder_ik),*args[]={&shoulder_ik_enabled};
  if(ik)squad_invoke(ik,"set_enabled",1,args,NULL);
  api.release(shoulder_ik);shoulder_ik=0;
 }
 if(shoulder_animation) {
  void *animation=api.target(shoulder_animation);
  if(animation)squad_invoke(animation,"ResetVerticalRotation",0,NULL,NULL);
  api.release(shoulder_animation);shoulder_animation=0;
 }
 if(shoulder_lens) {api.release(shoulder_lens);shoulder_lens=0;}

 for(unsigned i=0;i<shoulder_renderer_count;++i) {
  void *renderer=api.target(shoulder_renderers[i].handle);
  void *args[]={&shoulder_renderers[i].enabled};
  if(renderer)squad_invoke(renderer,"set_enabled",1,args,NULL);
  api.release(shoulder_renderers[i].handle);
 }
 shoulder_renderer_count=0;
 if(shoulder_body) {
  void *body=api.target(shoulder_body);void *args[]={&shoulder_body_active};
  if(body) {
   void *transform=NULL;
   if(shoulder_have_angles && !squad_invoke(body,"get_transform",0,NULL,&transform) && transform)
    zone_vector_property(transform,"set_localEulerAngles",&shoulder_original_angles,1);
   squad_invoke(body,"SetActive",1,args,NULL);
  }
  shoulder_have_angles=0;
  api.release(shoulder_body);shoulder_body=0;
 }
}
static int shoulder_show_model(void *view,void *fps) {
 void *first=object_field(view,"fpsCharacter"),*third=object_field(view,"thirdPersonCharacter");
 if(!first || !third || first==third)return 0;
 if(shoulder_body && api.target(shoulder_body)==third)return 1;
 shoulder_restore_models();
 const void *corlib=api.corlib();void *type_class=corlib ? api.class_named(corlib,"System","Type") : NULL;
 const void *get_type=type_class ? api.method(type_class,"GetType",1) : NULL;
 void *name=api.new_string("UnityEngine.Renderer, UnityEngine.CoreModule"),*renderer_type=NULL,*exception=NULL;
 void *type_args[]={name};
 if(get_type && name)renderer_type=api.invoke(get_type,NULL,type_args,&exception);
 if(exception || !renderer_type)return 0;
 unsigned char yes=1,no=0;void *third_array=NULL;
 void *collect_args[]={renderer_type,&yes,&yes,&yes,&no,NULL};
 if(squad_invoke(third,"GetComponentsInternal",6,collect_args,&third_array) || !third_array)return 0;
 uintptr_t third_count=api.array_length(third_array);if(!third_count || third_count>96)return 0;
 void *third_renderers[96]={0};
 for(uintptr_t i=0;i<third_count;++i)memcpy(&third_renderers[i],(unsigned char *)third_array+32+8*i,8);
 void *roots[]={first,object_field(fps,"weaponCamera")};
 for(unsigned root=0;root<2;++root)if(roots[root]) {
  void *args[]={renderer_type,&yes,&yes,&yes,&no,NULL},*array=NULL,*object=roots[root];
  if(!api.method(api.klass(object),"GetComponentsInternal",6)) {
   object=NULL;if(squad_invoke(roots[root],"get_gameObject",0,NULL,&object) || !object)goto fail;
  }
  if(squad_invoke(object,"GetComponentsInternal",6,args,&array) || !array)goto fail;
  uintptr_t count=api.array_length(array);if(count>96)goto fail;
  for(uintptr_t i=0;i<count;++i) {
   void *renderer=NULL;memcpy(&renderer,(unsigned char *)array+32+8*i,8);if(!renderer)continue;
   unsigned duplicate=0;
   for(uintptr_t j=0;j<third_count;++j)if(third_renderers[j]==renderer)duplicate=1;
   for(unsigned j=0;j<shoulder_renderer_count;++j)
    if(api.target(shoulder_renderers[j].handle)==renderer)duplicate=1;
   if(duplicate)continue;
   if(shoulder_renderer_count==96)goto fail;
   void *boxed=NULL;if(squad_invoke(renderer,"get_enabled",0,NULL,&boxed) || !boxed || !api.unbox(boxed))goto fail;
   uint32_t handle=api.weak(renderer,0);if(!handle)goto fail;
   shoulder_renderers[shoulder_renderer_count].handle=handle;
   shoulder_renderers[shoulder_renderer_count++].enabled=*(unsigned char *)api.unbox(boxed);
  }
 }
 void *boxed=NULL;
 if(squad_invoke(third,"get_activeSelf",0,NULL,&boxed) || !boxed || !api.unbox(boxed))goto fail;
 shoulder_body_active=*(unsigned char *)api.unbox(boxed);shoulder_body=api.weak(third,0);
 if(!shoulder_body)goto fail;
 void *body_transform=NULL;
 if(!squad_invoke(third,"get_transform",0,NULL,&body_transform) && body_transform)
  shoulder_have_angles=!zone_vector_property(body_transform,"get_localEulerAngles",&shoulder_original_angles,0);
 if(!shoulder_have_angles)goto fail;
 void *animation=object_field(view,"thirdPersonAnimationController");
 void *ik=object_field(animation,"aimIk"),*lens=NULL;
 if(!animation || !ik || squad_invoke(object_field(fps,"playerCamera"),"get_transform",0,NULL,&lens) || !lens)goto fail;
 boxed=NULL;if(squad_invoke(ik,"get_enabled",0,NULL,&boxed) || !boxed || !api.unbox(boxed))goto fail;
 shoulder_ik_enabled=*(unsigned char *)api.unbox(boxed);
 shoulder_ik=api.weak(ik,0);shoulder_animation=api.weak(animation,0);shoulder_lens=api.weak(lens,0);
 shoulder_controller=api.weak(object_field(object_field(animation,"player"),"fpsController"),0);
 if(!shoulder_ik || !shoulder_animation || !shoulder_lens || !shoulder_controller)goto fail;
 void *enable[]={&yes};if(squad_invoke(third,"SetActive",1,enable,NULL))goto fail;
 for(unsigned i=0;i<shoulder_renderer_count;++i) {
  void *renderer=api.target(shoulder_renderers[i].handle),*disable[]={&no};
  if(renderer && squad_invoke(renderer,"set_enabled",1,disable,NULL))goto fail;
 }
 if(output)fprintf(output,"SHOULDER_MODEL renderers_hidden=%u fps_gameobject=KEPT_ACTIVE\n",shoulder_renderer_count);
 return 1;
fail:
 if(output)fprintf(output,"SHOULDER_MODEL_FAILED safe_restore=1\n");
 shoulder_restore_models();return 0;
}
static void third_person_test_poll(void) {
 static uint64_t next;static int preferred=1,manual,shifted,original_mask,have_mask;
 static uint32_t transform_handle,camera_handle;static ZoneVector original_position;
 uint64_t now=armGetSystemTick();if(now<next)return;
 next=now+armGetSystemTickFreq()/60;
 void *game=zone_playing_game(),*player=game ? object_field(game,"_localPlayer") : NULL;
 int alive=0,local=0;
 int playable=player && !squad_bool(player,"_isLocalPlayer",&local) && local &&
              !squad_bool(player,"_isAlive",&alive) && alive;
 void *old_transform=transform_handle ? api.target(transform_handle) : NULL;
 if(!playable) {
  shoulder_restore_models();
  if(shifted && old_transform)zone_vector_property(old_transform,"set_localPosition",&original_position,1);
  if(shifted && have_mask && camera_handle) {
   void *old_camera=api.target(camera_handle);void *args[]={&original_mask};
   if(old_camera)squad_invoke(old_camera,"set_cullingMask",1,args,NULL);
  }
  shifted=0;manual=0;preferred=1;__atomic_store_n(&camera_toggle_pending,0,__ATOMIC_RELAXED);return;
 }
 if(__atomic_exchange_n(&camera_toggle_pending,0,__ATOMIC_RELAXED)) {preferred=3-preferred;manual=1;}
 if(!manual)return;
 void *controller=object_field(player,"fpsController"),*view=object_field(player,"playerViewManager");
 void *fps=object_field(player,"fpsCamera"),*camera_object=object_field(fps,"playerCamera");
 if(!controller || !view || !camera_object)return;
 void *camera=squad_singleton("brijesh.CountryWar2","CameraManager","Instance"),*boxed=NULL;
 if(!camera || squad_invoke(camera,"get_cameraMode",0,NULL,&boxed) || !boxed)return;
 int *mode=api.unbox(boxed);if(!mode || (*mode!=1 && *mode!=2))return;
 int driving=object_field(object_field(player,"carInteractionHandler"),"drivingCar")!=NULL;
 if(driving) {
  shoulder_restore_models();
  if(shifted && old_transform)zone_vector_property(old_transform,"set_localPosition",&original_position,1);
  if(shifted && have_mask) {void *args[]={&original_mask};squad_invoke(camera_object,"set_cullingMask",1,args,NULL);}
  shifted=0;return;
 }
 if(*mode!=1) {
  int logical_fps=1;unsigned char check=1,weapon=254;void *args[]={&logical_fps,&check,&weapon};
  if(squad_invoke(game,"SetCameraMode",3,args,NULL))return;
  boxed=NULL;if(squad_invoke(camera,"get_cameraMode",0,NULL,&boxed) || !boxed)return;
  mode=api.unbox(boxed);if(!mode || *mode!=1)return;
 }
 void *transform=NULL;
 if(squad_invoke(camera_object,"get_transform",0,NULL,&transform) || !transform)return;
 if(!transform_handle || old_transform!=transform) {
  if(transform_handle)api.release(transform_handle);
  transform_handle=api.weak(transform,0);
  if(camera_handle)api.release(camera_handle);
  camera_handle=api.weak(camera_object,0);shifted=0;have_mask=0;
 }
 int aiming=0;squad_bool(controller,"isAiming",&aiming);
 int third=preferred==2 && !aiming && !__atomic_load_n(&camera_aim_held,__ATOMIC_RELAXED);
 if(third) {
  if(!shoulder_show_model(view,fps))return;
  shoulder_align_pose();
  if(!shifted) {
   if(zone_vector_property(transform,"get_localPosition",&original_position,0))return;
   boxed=NULL;
   if(!squad_invoke(camera_object,"get_cullingMask",0,NULL,&boxed) && boxed && api.unbox(boxed)) {
    memcpy(&original_mask,api.unbox(boxed),4);have_mask=1;
   }
  }
  /* Keep the active FPS camera and weapon-camera scripts alive. Offset only
   * the viewing lens; logical FPS keeps normal hip-fire and reticle code. */
  ZoneVector shoulder={original_position.x+0.18f,original_position.y+0.15f,original_position.z-1.8f};
  if(zone_vector_property(transform,"set_localPosition",&shoulder,1))return;
  if(have_mask) {
   void *tp_camera=object_field(object_field(player,"thirdPersonCamera"),"playerCamera");boxed=NULL;
   if(!squad_invoke(tp_camera,"get_cullingMask",0,NULL,&boxed) && boxed && api.unbox(boxed)) {
    int mask=0;memcpy(&mask,api.unbox(boxed),4);void *args[]={&mask};
    squad_invoke(camera_object,"set_cullingMask",1,args,NULL);
   }
  }
  void *tp_camera=object_field(object_field(player,"thirdPersonCamera"),"playerCamera");
  if(tp_camera && tp_camera!=camera_object) {
   unsigned char no=0;void *args[]={&no};squad_invoke(tp_camera,"set_enabled",1,args,NULL);
  }
  if(!shifted && output)fprintf(output,"SHOULDER_CAMERA third=1 offset=0.18,0.15,-1.8 logical_camera=FPS weapon_camera=UNCHANGED\n");
  shifted=1;
 } else if(shifted) {
  zone_vector_property(transform,"set_localPosition",&original_position,1);
  if(have_mask) {void *args[]={&original_mask};squad_invoke(camera_object,"set_cullingMask",1,args,NULL);}
  shoulder_restore_models();shifted=0;
  if(output)fprintf(output,"SHOULDER_CAMERA third=0 aiming=%d\n",aiming);
 }
}

#else
void ai_profile_toggle_camera(void) {}
void ai_profile_camera_aim(int held) {(void)held;}
#endif
/* Suppress the actual runtime panel as well as its native loader entries.
 * Other popup sequencers may activate a panel directly through SetActive. */
static void suppress_startup_promotion(void) {
 void *menu=squad_singleton("hms.countryWar2","MainMenuManager","Instance");if(!menu)return;
 void *shown=api.field(api.klass(menu),"isPurchasedBannerShowed");unsigned char yes=1;
 if(shown)api.write(menu,shown,&yes);
 void *panel=object_field(menu,"purchasedBannerAds"),*go=NULL;
 if(!panel || squad_invoke(panel,"get_gameObject",0,NULL,&go) || !go)return;
 void *boxed=NULL;
 if(squad_invoke(go,"get_activeSelf",0,NULL,&boxed) || !boxed || !api.unbox(boxed))return;
 int active=*(unsigned char *)api.unbox(boxed);
 if(active) {
  /* Use the original close action so its normal resource cleanup also runs. */
  squad_invoke(panel,"OnCloseButtonClicked",0,NULL,NULL);
  unsigned char no=0;void *args[]={&no};squad_invoke(go,"SetActive",1,args,NULL);
  if(output)fprintf(output,"STARTUP_PROMOTION suppressed_runtime=1\n");
 }
}
static void permanent_network_locks(void) {
 static uint64_t next;uint64_t now=armGetSystemTick();if(now<next)return;
 next=now+armGetSystemTickFreq()/4;
 void *menu=squad_singleton("hms.countryWar2","MainMenuManager","Instance");if(!menu)return;
 void *panel=object_field(menu,"selectGamemodeScreen");
 void *buttons[]={object_field(menu,"privateServer"),object_field(panel,"onlineButton"),object_field(panel,"createButton")};
 unsigned char no=0,yes=1;void *disable[]={&no},*enable[]={&yes};
 for(unsigned i=0;i<3;++i)if(buttons[i]) {
  const void *method=api.method(api.klass(buttons[i]),"set_interactable",1);
  if(method) {void *ex=NULL;api.invoke(method,buttons[i],disable,&ex);}
 }
 void *locks[]={object_field(menu,"privateServerLock"),object_field(panel,"onlineLockImage")};
 for(unsigned i=0;i<2;++i)if(locks[i]) {
  void *go=NULL;
  if(api.method(api.klass(locks[i]),"SetActive",1))go=locks[i];
  else squad_invoke(locks[i],"get_gameObject",0,NULL,&go);
  if(go)squad_invoke(go,"SetActive",1,enable,NULL);
 }
}
void ai_profile_poll(void) {
 suppress_startup_promotion();
#ifdef SWAG_THIRD_PERSON_TEST
 third_person_test_poll();
#endif
 permanent_network_locks();
 health_warning_poll();
 zone_ai_poll();
 zone_actors_poll();
 loading_difficulty_refresh();
 squad_poll();
 /* Old region refreshes can run after panel initialization. Restore the saved
  * value without emitting selection events, and refresh the caption too. */
 for(unsigned i=0;i<8;++i) {
  void *dropdown=difficulty_controls[i] ? api.target(difficulty_controls[i]) : NULL;
  if(!dropdown)continue;
  const Spec spec={"m_Value",8,0};void *field=checked_field(dropdown,&spec);
  int current=-1;if(field)api.read(dropdown,field,&current);
  if(current!=difficulty) {
   const void *setter=api.method(api.klass(dropdown),"SetValueWithoutNotify",1);
   if(setter) {void *args[]={&difficulty},*exception=NULL;updating_ui=1;api.invoke(setter,dropdown,args,&exception);updating_ui=0;}
  }
  const void *refresh=api.method(api.klass(dropdown),"RefreshShownValue",0);
  if(refresh) {void *exception=NULL;api.invoke(refresh,dropdown,NULL,&exception);}
 }
 if(!dirty)return;
 dirty=0;
 for(unsigned i=0;i<128;++i)if(bots[i].gun)apply_bot(&bots[i]);
}
static void jump(unsigned char *dest,uintptr_t target) {
 const uint32_t code[]={0x58000050,0xd61f0200};memcpy(dest,code,8);memcpy(dest+8,&target,8);
}
int ai_profile_prepare(AndroidElf *module,FILE *log) {
 if(!module || module->published || !module->relocated)return -1;
 output=log;load_setting();
 #define API(member,symbol) api.member=(void *)elf_export(module,symbol)
 API(klass,"il2cpp_object_get_class");API(field,"il2cpp_class_get_field_from_name");
 API(flags,"il2cpp_field_get_flags");API(type,"il2cpp_field_get_type");API(kind,"il2cpp_type_get_type");
 API(read,"il2cpp_field_get_value");API(write,"il2cpp_field_set_value");
 API(method,"il2cpp_class_get_method_from_name");API(invoke,"il2cpp_runtime_invoke");
 API(unbox,"il2cpp_object_unbox");API(weak,"il2cpp_gchandle_new_weakref");
 API(target,"il2cpp_gchandle_get_target");API(release,"il2cpp_gchandle_free");
 API(class_named,"il2cpp_class_from_name");API(static_read,"il2cpp_field_static_get_value");
 API(new_string,"il2cpp_string_new");API(strong,"il2cpp_gchandle_new");
 API(string_length,"il2cpp_string_length");API(string_chars,"il2cpp_string_chars");
 API(object_new,"il2cpp_object_new");API(array_new,"il2cpp_array_new");
 API(array_length,"il2cpp_array_length");
 API(corlib,"il2cpp_get_corlib");API(field_offset,"il2cpp_field_get_offset");
 API(barrier,"il2cpp_gc_wbarrier_set_field");
 API(class_from_type,"il2cpp_class_from_type");API(assignable,"il2cpp_class_is_assignable_from");
 API(resolve_icall,"il2cpp_resolve_icall");
 #undef API
 if(!api.klass || !api.field || !api.flags || !api.type || !api.kind || !api.read || !api.write || !api.method || !api.invoke || !api.weak || !api.target || !api.release)return -1;
 const struct {size_t offset;uint32_t words[4];} guards[]={
  {0xc3c4e4,{0xd10243ff,0x6d0233ed,0x6d032beb,0x6d0423e9}},
  {0x17578bc,{0xd10243ff,0x6d013bef,0x6d0233ed,0x6d032beb}},
  {0x16c9c58,{0xfc1a0fe8,0xa90167fc,0xa9025ff8,0xa90357f6}},
  {0x16c9914,{0xa9be4ff4,0xa9017bfd,0x910043fd,0xd000a394}},
  {0x173aa0c,{0xa9bd57f6,0xa9014ff4,0xa9027bfd,0x910083fd}},
  {0x17481a4,{0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd}},
  {0x1741ecc,{0xa9bd57f6,0xa9014ff4,0xa9027bfd,0x910083fd}},
  {0x1742048,{0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd}},
  {0x1986794,{0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd}},
  {0x169e36c,{0xf81c0ff7,0xa90157f6,0xa9024ff4,0xa9037bfd}},
  {0x1987fc8,{0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd}},
  {0x19882ec,{0xa9be4ff4,0xa9017bfd,0x910043fd,0xb0008db4}},
  {0x1986b90,{0xa9bd57f6,0xa9014ff4,0xa9027bfd,0x910083fd}},
  {0x19864dc,{0xa9be4ff4,0xa9017bfd,0x910043fd,0xf0008db4}},
  {0x1608b74,{0xaa1403f5,0xf80c8ea0,0xbc5c82a0,0xaa1503e0}},
  {0x16a8fec,{0x6dbb33ed,0x6d012beb,0x6d0223e9,0xa9034ff4}},
  {0x16a9330,{0xf81e0ff3,0xa9017bfd,0x910043fd,0xaa0003f3}},
  {0x163a8e4,{0xf81c0ff7,0xa90157f6,0xa9024ff4,0xa9037bfd}},
  {0x16a9374,{0xfc1d0fe8,0xa9014ff4,0xa9027bfd,0x910083fd}}
 };
 for(unsigned i=0;i<sizeof guards/sizeof guards[0];++i)if(guards[i].offset+16>module->size || memcmp(module->image+guards[i].offset,guards[i].words,16))return -1;
 uint32_t mode_highlight;memcpy(&mode_highlight,module->image+0x1986c7c,4);if(mode_highlight!=0xf81c0ff7)return -1;
 uint32_t highlight;memcpy(&highlight,module->image+0x198876c,4);if(highlight!=0xf81b0ff9)return -1;
 if(squad_prepare(module,log))return -1;
 /* Only the GunScript hook needs its original prologue: four stack-save
  * instructions without PC-relative operands, followed by an absolute jump. */
 if(nx_memory_prepare(&trampoline_memory,4096,log))return -1;
 memcpy(trampoline_memory.staging,module->image+0x169e36c,16);
 jump((unsigned char *)trampoline_memory.staging+16,module->bias+0x169e37c);
 memcpy((unsigned char *)trampoline_memory.staging+64,module->image+0x16a8fec,16);
 jump((unsigned char *)trampoline_memory.staging+80,module->bias+0x16a8ffc);
 memcpy((unsigned char *)trampoline_memory.staging+128,module->image+0x163a8e4,16);
 jump((unsigned char *)trampoline_memory.staging+144,module->bias+0x163a8f4);
 memcpy((unsigned char *)trampoline_memory.staging+192,module->image+0x16a9374,16);
 jump((unsigned char *)trampoline_memory.staging+208,module->bias+0x16a9384);
#ifdef SWAG_THIRD_PERSON_TEST
 memcpy((unsigned char *)trampoline_memory.staging+320,module->image+0xc3c4e4,16);
 jump((unsigned char *)trampoline_memory.staging+336,module->bias+0xc3c4f4);
 memcpy((unsigned char *)trampoline_memory.staging+256,module->image+0x17578bc,16);
 jump((unsigned char *)trampoline_memory.staging+272,module->bias+0x17578cc);
#endif
 AndroidElf trampoline={0};trampoline.image=trampoline_memory.staging;trampoline.size=trampoline_memory.size;
 trampoline.bias=(uintptr_t)trampoline_memory.execution;trampoline.relocated=1;trampoline.phnum=1;
 trampoline.ph[0].p_type=PT_LOAD;trampoline.ph[0].p_flags=PF_R|PF_X;trampoline.ph[0].p_memsz=4096;
 if(nx_memory_publish(&trampoline_memory,&trampoline,log))return -1;
#ifdef SWAG_THIRD_PERSON_TEST
 original_player_update=(void *)((uintptr_t)trampoline_memory.execution+320);
 jump(module->image+0xc3c4e4,(uintptr_t)player_controller_update);
 original_third_animation_late=(void *)((uintptr_t)trampoline_memory.execution+256);
 jump(module->image+0x17578bc,(uintptr_t)third_animation_late);
#endif
 original_gun_changed=(void *)trampoline_memory.execution;
 zone_move_original=(void *)((uintptr_t)trampoline_memory.execution+64);
 zone_sprint=(void *)(module->bias+0x16a9330);
 original_loading_enable=(void *)((uintptr_t)trampoline_memory.execution+128);
 jump(module->image+0x163a8e4,(uintptr_t)loading_enable);
 zone_late_original=(void *)((uintptr_t)trampoline_memory.execution+192);
 jump(module->image+0x16a9374,(uintptr_t)zone_ai_late);
 jump(module->image+0x16a8fec,(uintptr_t)zone_ai_move);
 zone_phase_resume=module->bias+0x1608b84;
 jump(module->image+0x1608b74,(uintptr_t)zone_phase_bridge);
 jump(module->image+0x169e36c,(uintptr_t)gun_changed);
 jump(module->image+0x1987fc8,(uintptr_t)settings_changed);
 jump(module->image+0x19882ec,(uintptr_t)settings_initialize);
 jump(module->image+0x1986b90,(uintptr_t)settings_changed);
 jump(module->image+0x19864dc,(uintptr_t)settings_initialize);
 const size_t locked_entries[]={0x173aa0c,0x17481a4,0x1741ecc,0x1742048,0x1986794};
 const uint32_t blocked=0xd65f03c0;
 memcpy(module->image+0x16c9c58,&blocked,4);
 memcpy(module->image+0x16c9914,&blocked,4); /* User-approved fallback: no automatic startup promotion. */
 for(unsigned i=0;i<sizeof locked_entries/sizeof locked_entries[0];++i)memcpy(module->image+locked_entries[i],&blocked,4);
 const uint32_t ret=0xd65f03c0;memcpy(module->image+0x198876c,&ret,4);
 memcpy(module->image+0x1986c7c,&ret,4);
 fprintf(log,"AI_PROFILE_READY difficulty=%s levels=4 registry=weak_fields hook=GunScript.OnGunChanged\n",names[difficulty]);
 return 0;
}
