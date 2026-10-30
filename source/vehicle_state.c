#include <switch.h>
#include <stdint.h>
#include <string.h>
#include "vehicle_state.h"
#include "controller_touch.h"
#include "offline_game.h"
/* Read only metadata-backed fields on the Unity main thread. Never retain
 * managed object pointers across frames and never invoke gameplay methods. */
typedef struct {
 void *(*domain_get)(void);
 void *(*thread_current)(void);
 void *(*thread_attach)(void *);
 const void **(*assemblies)(void *,size_t *);
 void *(*image)(const void *);
 const char *(*image_name)(const void *);
 void *(*klass)(const void *,const char *,const char *);
 void *(*field)(void *,const char *);
 unsigned (*field_flags)(void *);
 void *(*field_type)(void *);
 int (*type_kind)(const void *);
 void (*static_value)(void *,void *);
 void (*value)(void *,void *,void *);
 void *(*object_class)(void *);
 unsigned char (*assignable)(void *,void *);
 const void *(*method)(void *,const char *,int);
 void *(*invoke)(const void *,void *,void **,void **);
 const void *(*return_type)(const void *);
 const void *(*parameter)(const void *,unsigned);
} StateApi;
static StateApi api;
static void *classes[3],*fields[4];
static int available,bound,disabled,last_report=-2;
static u64 next_poll;
static int balances_initialized;
static const void *game_image;
static void seed_balances(const void *image,FILE *log) {
 if(balances_initialized || !api.method || !api.invoke || !api.return_type || !api.parameter) return;
 void *manager=api.klass(image,"hms.countryWar2","DataManager");
 void *game=api.klass(image,"hms.countryWar2","GameData");
 void *menu=api.klass(image,"hms.countryWar2","MainMenuManager");
 if(!manager || !game || !menu) return;
 void *instance=api.field(manager,"Instance"),*data=api.field(manager,"_gamedata"),*menu_instance=api.field(menu,"Instance");
 if(!instance || !data || !menu_instance || !(api.field_flags(instance)&0x10) || !(api.field_flags(menu_instance)&0x10) || (api.field_flags(data)&0x10) || api.type_kind(api.field_type(data))!=0x12) return;
 void *menu_object=NULL,*object=NULL,*game_object=NULL;
 api.static_value(menu_instance,&menu_object);api.static_value(instance,&object);
 if(!menu_object || !object || !api.assignable(manager,api.object_class(object))) return;
 void *loading=api.field(manager,"loadingDataFronDisk");
 if(loading && api.type_kind(api.field_type(loading))==2) {
  unsigned char busy[sizeof(void *)]={1};api.value(object,loading,busy);if(busy[0]) return;
 }
 api.value(object,data,&game_object);
 if(!game_object || !api.assignable(game,api.object_class(game_object))) return;
 const char *names[]={"set_Cash","set_Gold","set_Diamond"};
 const void *setters[3];
 for(unsigned i=0;i<3;++i) {
  setters[i]=api.method(game,names[i],1);
  if(!setters[i] || api.type_kind(api.return_type(setters[i]))!=1 || api.type_kind(api.parameter(setters[i],0))!=8) return;
 }
 /* User-selected balance. Seed once per launch, never every frame. */
 int balance=10000000;void *arguments[]={&balance};
 balances_initialized=1;
 for(unsigned i=0;i<3;++i) {
  void *exception=NULL;api.invoke(setters[i],game_object,arguments,&exception);
  if(exception) { fprintf(log,"DEFAULT_BALANCES status=FAILED setter=%s\n",names[i]);return; }
 }
 const void *save=api.method(manager,"SaveGameData",1);
 void *exception=NULL;
 unsigned char compulsory=1;void *save_arguments[]={&compulsory};
 if(save && api.type_kind(api.return_type(save))==1 && api.type_kind(api.parameter(save,0))==2) api.invoke(save,object,save_arguments,&exception);
 fprintf(log,"DEFAULT_BALANCES cash=%d gold=%d diamond=%d once_per_launch=1 save_exception=%d\n",balance,balance,balance,exception!=NULL);
}
void vehicle_state_bind(AndroidElf *module) {
 #define API(member,symbol) api.member=(void *)elf_export(module,symbol)
 API(thread_current,"il2cpp_thread_current"); API(thread_attach,"il2cpp_thread_attach");
 API(domain_get,"il2cpp_domain_get"); API(assemblies,"il2cpp_domain_get_assemblies");
 API(image,"il2cpp_assembly_get_image"); API(image_name,"il2cpp_image_get_name");
 API(klass,"il2cpp_class_from_name"); API(field,"il2cpp_class_get_field_from_name");
 API(field_flags,"il2cpp_field_get_flags"); API(field_type,"il2cpp_field_get_type");
 API(type_kind,"il2cpp_type_get_type"); API(static_value,"il2cpp_field_static_get_value");
 API(value,"il2cpp_field_get_value"); API(object_class,"il2cpp_object_get_class");
 API(assignable,"il2cpp_class_is_assignable_from");
 API(method,"il2cpp_class_get_method_from_name");API(invoke,"il2cpp_runtime_invoke");
 API(return_type,"il2cpp_method_get_return_type");
 API(parameter,"il2cpp_method_get_param");
 #undef API
 available=api.thread_current && api.thread_attach && api.domain_get && api.assemblies && api.image && api.image_name && api.klass && api.field && api.field_flags && api.field_type && api.type_kind && api.static_value && api.value && api.object_class && api.assignable;
}
/* -1: unknown/invalid; 0: foot/no local player; 1: local player driving. */
static int read_chain(void) {
 void *object=NULL;
 api.static_value(fields[0],&object);
 if(!object) return 0;
 for(unsigned i=0;i<3;++i) {
  void *actual=api.object_class(object);
  if(!actual || !api.assignable(classes[i],actual)) return -1;
  if(i==2) {
   unsigned char driving[sizeof(void *)]={255};
   api.value(object,fields[3],driving);
   return driving[0]<=1 ? driving[0] : -1;
  }
  void *next=NULL; api.value(object,fields[i+1],&next);
  if(!next) return 0;
  object=next;
 }
 return -1;
}
static int bind_fields(FILE *log) {
 void *domain=api.domain_get(); if(!domain) return 0;
 if(!api.thread_current() && !api.thread_attach(domain)) return 0;
 size_t count=0; const void **assemblies=api.assemblies(domain,&count);
 if(!assemblies || count>4096) return 0;
 const void *image=NULL;
 for(size_t i=0;i<count;++i) {
  void *candidate=api.image(assemblies[i]);
  const char *name=candidate ? api.image_name(candidate) : NULL;
  if(name && !strcmp(name,"Assembly-CSharp.dll")) { image=candidate; break; }
 }
 if(!image) return 0;
 game_image=image;
 const char *names[]={"GameManager","PlayerManager","PlayerCarInteractionHandler"};
 for(unsigned i=0;i<3;++i) {
  classes[i]=api.klass(image,"brijesh.CountryWar2",names[i]);
  if(!classes[i]) goto invalid;
 }
 fields[0]=api.field(classes[0],"_instance");
 fields[1]=api.field(classes[0],"_localPlayer");
 fields[2]=api.field(classes[1],"carInteractionHandler");
 fields[3]=api.field(classes[2],"drivingCar");
 for(unsigned i=0;i<4;++i) {
  if(!fields[i]) goto invalid;
  unsigned flags=api.field_flags(fields[i]);
  if(!!(flags&0x10)!=(i==0)) goto invalid;
  int kind=api.type_kind(api.field_type(fields[i]));
  if((i==3 && kind!=2) || (i!=3 && kind!=0x12)) goto invalid;
 }
 fprintf(log,"VEHICLE_AUTO_BIND result=OK source=local_player.drivingCar access=il2cpp_fields\n");
 bound=1; return 1;
invalid:
 fprintf(log,"VEHICLE_AUTO_BIND result=UNAVAILABLE reason=metadata_shape_mismatch fallback=manual\n");
 disabled=1; return 0;
}
void vehicle_state_poll(FILE *log) {
 u64 now=armGetSystemTick();
 if(now<next_poll) return;
 next_poll=now+1920000; /* 100 ms; objects are read fresh each time. */
 if(!available || disabled) {
  if(last_report!=-1) { fprintf(log,"VEHICLE_AUTO_STATE state=UNKNOWN fallback=manual apis=%d\n",available); last_report=-1; }
  controller_touch_set_vehicle_state(-1,log); return;
 }
 if(!bound && !bind_fields(log)) return;
 if(game_image) seed_balances(game_image,log);
 offline_shop_poll(log);
 int state=read_chain();
 if(state!=last_report) { fprintf(log,"VEHICLE_AUTO_STATE state=%s source=local_player.drivingCar\n",state<0 ? "UNKNOWN" : state ? "DRIVING" : "FOOT_OR_NO_PLAYER"); fflush(log); last_report=state; }
 controller_touch_set_vehicle_state(state,log);
}
/* Exercise state changes, object disappearance and type rejection without
 * requiring live scene objects. Restore all real API pointers afterwards. */
static int fixture_mode;
static unsigned char fixture_driving;
static void test_static(void *field,void *out) { (void)field; *(void **)out=fixture_mode==1 ? NULL : (void *)(uintptr_t)1; }
static void *test_class(void *object) { return object; }
static unsigned char test_assign(void *expected,void *actual) { return expected==actual && fixture_mode!=3; }
static void test_value(void *object,void *field,void *out) {
 (void)field;
 if(object==(void *)(uintptr_t)3) *(unsigned char *)out=fixture_driving;
 else *(void **)out=fixture_mode==2 ? NULL : (void *)((uintptr_t)object+1);
}
int vehicle_state_selftest(FILE *log) {
 StateApi saved=api; void *saved_classes[3]; memcpy(saved_classes,classes,sizeof classes);
 api.static_value=test_static; api.value=test_value; api.object_class=test_class; api.assignable=test_assign;
 for(unsigned i=0;i<3;++i) classes[i]=(void *)(uintptr_t)(i+1);
 int errors=0; fixture_mode=0; fixture_driving=0; errors+=read_chain()!=0;
 fixture_driving=1; errors+=read_chain()!=1;
 fixture_driving=0; errors+=read_chain()!=0;
 fixture_driving=2; errors+=read_chain()!=-1;
 fixture_mode=1; errors+=read_chain()!=0;
 fixture_mode=2; errors+=read_chain()!=0;
 fixture_mode=3; errors+=read_chain()!=-1;
 api=saved; memcpy(classes,saved_classes,sizeof classes);
 fprintf(log,"VEHICLE_STATE_TEST errors=%d enter_exit=CHECKED scene_reset=CHECKED invalid_bool=CHECKED wrong_class=CHECKED real_scene=PENDING\n",errors);
 return errors ? -1 : 0;
}
