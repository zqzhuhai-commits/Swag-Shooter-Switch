#include <stdint.h>
#define NULL ((void *)0)
#define fprintf(...) (0)
static void *output;
static int online=14,offline=15,active=1,spawn_calls,reset_calls,fill_calls,life_calls;
static void *bound_list;static int shown_lives;
static uint32_t squad_population_data;
static int squad_population_original,squad_population_offline_original,squad_population_changed;
static struct {int ready;struct {unsigned char dead[52];} rules;} squads;
static int equal(const char *a,const char *b) {while(*a && *a==*b){++a;++b;}return *a==*b;}
static void *target(uint32_t handle) {return handle==(uint32_t)0x100 ? (void *)0x100 : NULL;}
static uint32_t weak(void *p,int ignored) {(void)ignored;return (uint32_t)(uintptr_t)p;}
static void release(uint32_t handle) {(void)handle;}
static struct {void *(*target)(uint32_t);uint32_t (*weak)(void *,int);void (*release)(uint32_t);} api={target,weak,release};
static void *squad_singleton(const char *ns,const char *name,const char *member) {(void)ns;(void)name;(void)member;return (void *)0x100;}
static int squad_int(void *o,const char *name,int *v,int write) {
 if(o!=(void *)0x100)return -1;
 int *p=equal(name,"noOfPlayersOnline") ? &online : equal(name,"noOfAiInOffline") ? &offline : NULL;
 if(!p)return -1;if(write)*p=*v;else *v=*p;return 0;
}
static int squad_mode_active(void) {return active;}
static void squad_begin_new_roster(void) {++reset_calls;}
static void *spawn(void *s,void *m) {(void)s;(void)m;++spawn_calls;return (void *)0x400;}
static int spawn_bots(void *s,void *m) {(void)s;(void)m;++spawn_calls;return online==51 && offline==51;}
static void fill(void *s,void *list,void *m) {(void)s;(void)m;bound_list=list;++fill_calls;}
static void life(void *s,int count,void *m) {(void)s;(void)m;shown_lives=count;++life_calls;}
static void *(*squad_spawn_original)(void *,void *)=spawn;
static int (*squad_spawn_bots_original)(void *,void *)=spawn_bots;
static void (*squad_hud_fill_original)(void *,void *,void *)=fill;
static void (*squad_hud_life_original)(void *,int,void *)=life;
static void *squad_team(unsigned id) {return id==1 ? (void *)0x200 : NULL;}
static void *squad_ref(void *o,const char *name) {
 if(o==(void *)0x200 && equal(name,"<PlayerList>k__BackingField"))return (void *)0x300;
 if(equal(name,"player"))return o;
 return NULL;
}
static int squad_find_player(void *p) {return (int)(uintptr_t)p-0x1000;}
/* PRODUCTION_WRAPPERS */
int run_population_tests(void) {
 if(squad_spawn_players(NULL,NULL)!=(void *)0x400 || online!=51 || offline!=51 || reset_calls!=1)return 90;
 /* Simulate deferred native setup overwriting both defaults before MoveNext. */
 for(int i=0;i<51;++i) {online=14;offline=15;if(!squad_spawn_bots(NULL,NULL))return 91;}
 active=0;squad_population_apply();if(online!=14 || offline!=15)return 92;
 squads.ready=1;active=1;
 squad_hud_fill(NULL,(void *)0xdead,NULL);if(bound_list!=(void *)0x300)return 93;
 for(int slot=0;slot<4;++slot) {
  squads.rules.dead[slot]=0;squad_hud_life((void *)(uintptr_t)(0x1000+slot),0,NULL);if(shown_lives!=1)return 94;
  squads.rules.dead[slot]=1;squad_hud_life((void *)(uintptr_t)(0x1000+slot),9,NULL);if(shown_lives)return 95;
 }
 squad_hud_life((void *)0x1004,7,NULL);if(shown_lives!=7)return 96;
 active=0;squad_hud_fill(NULL,(void *)0xdead,NULL);if(bound_list!=(void *)0xdead)return 97;
 squad_hud_life((void *)0x1000,3,NULL);if(shown_lives!=3 || fill_calls!=2 || life_calls!=10)return 98;
 return 0;
}
