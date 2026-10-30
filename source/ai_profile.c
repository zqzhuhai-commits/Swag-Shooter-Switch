#include "ai_profile.h"
#include "nx_memory.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "squad_rules.h"
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
} AiApi;
static AiApi api;
static FILE *output;
static int difficulty=1,dirty,updating_ui;
static unsigned bot_count;
static void *object_field(void *object,const char *name);
static const void *profile_game_image;
static void squad_update_gun(void *gun);
static void *ui_strings[10];
static uint32_t ui_handles[10];
static void *ui_string(unsigned index) {
 static const char *words[]={"简单AI","普通AI","困难AI","精英AI","经典模式","死亡竞赛","狙击模式","斧头模式","匕首模式","自由模式"};
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
static unsigned zone_phase;
static const unsigned zone_wait_seconds[]={180,120,90,60,30};
static const unsigned zone_shrink_seconds[]={60,45,40,30,30};
typedef struct {float x,y,z;} ZoneVector;
static const float zone_shrink_fraction[]={0.25f,0.35f,0.40f,0.45f,0.50f};
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
 if(!zone_coroutine || api.target(zone_coroutine)!=coroutine) {
  if(zone_coroutine)api.release(zone_coroutine);
  zone_coroutine=api.weak(coroutine,0);zone_phase=0;
  if(!zone_coroutine)return;
 }
 unsigned phase=zone_phase<5 ? zone_phase : 4;
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
 const float accuracy[]={1.35f,1,0.75f,0.55f};
 const float pause[]={1.3f,1,0.75f,0.55f};
 const float tactical[]={1.15f,1,0.8f,0.65f};
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
void ai_profile_poll(void) {
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
 #undef API
 if(!api.klass || !api.field || !api.flags || !api.type || !api.kind || !api.read || !api.write || !api.method || !api.invoke || !api.weak || !api.target || !api.release)return -1;
 const struct {size_t offset;uint32_t words[4];} guards[]={
  {0x169e36c,{0xf81c0ff7,0xa90157f6,0xa9024ff4,0xa9037bfd}},
  {0x1987fc8,{0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd}},
  {0x19882ec,{0xa9be4ff4,0xa9017bfd,0x910043fd,0xb0008db4}},
  {0x1986b90,{0xa9bd57f6,0xa9014ff4,0xa9027bfd,0x910083fd}},
  {0x19864dc,{0xa9be4ff4,0xa9017bfd,0x910043fd,0xf0008db4}},
  {0x1608b74,{0xaa1403f5,0xf80c8ea0,0xbc5c82a0,0xaa1503e0}}
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
 AndroidElf trampoline={0};trampoline.image=trampoline_memory.staging;trampoline.size=trampoline_memory.size;
 trampoline.bias=(uintptr_t)trampoline_memory.execution;trampoline.relocated=1;trampoline.phnum=1;
 trampoline.ph[0].p_type=PT_LOAD;trampoline.ph[0].p_flags=PF_R|PF_X;trampoline.ph[0].p_memsz=4096;
 if(nx_memory_publish(&trampoline_memory,&trampoline,log))return -1;
 original_gun_changed=(void *)trampoline_memory.execution;
 zone_phase_resume=module->bias+0x1608b84;
 jump(module->image+0x1608b74,(uintptr_t)zone_phase_bridge);
 jump(module->image+0x169e36c,(uintptr_t)gun_changed);
 jump(module->image+0x1987fc8,(uintptr_t)settings_changed);
 jump(module->image+0x19882ec,(uintptr_t)settings_initialize);
 jump(module->image+0x1986b90,(uintptr_t)settings_changed);
 jump(module->image+0x19864dc,(uintptr_t)settings_initialize);
 const uint32_t ret=0xd65f03c0;memcpy(module->image+0x198876c,&ret,4);
 memcpy(module->image+0x1986c7c,&ret,4);
 fprintf(log,"AI_PROFILE_READY difficulty=%s levels=4 registry=weak_fields hook=GunScript.OnGunChanged\n",names[difficulty]);
 return 0;
}
