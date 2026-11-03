#include <stdint.h>
#include <stddef.h>
void *memset(void *p,int v,size_t n) {for(size_t i=0;i<n;++i)((unsigned char *)p)[i]=(unsigned char)v;return p;}
typedef struct {void *sprite,*override,*material,*alpha;int type;unsigned char aspect;uint32_t color[4];unsigned clear,dirty,rebuild,clock,clear_at,dirty_at,rebuild_at;int phase;} State;
static State state;static int renderer;
static int same(const char *a,const char *b) {while(*a && *a==*b){++a;++b;}return *a==*b;}
static int squad_invoke(void *object,const char *name,int n,void **args,void **out) {
 (void)n;++state.clock;
 if(object==&renderer) {
  if(same(name,"SetAlphaTexture"))state.alpha=args[0];
  else if(same(name,"Clear")){++state.clear;state.clear_at=state.clock;}
  else return -1;
  return 0;
 }
 if(object!=&state)return -1;
 if(same(name,"get_sprite"))*out=state.sprite;
 else if(same(name,"get_overrideSprite"))*out=state.override;
 else if(same(name,"get_material"))*out=state.material;
 else if(same(name,"get_canvasRenderer"))*out=&renderer;
 else if(same(name,"set_sprite"))state.sprite=args[0];
 else if(same(name,"set_overrideSprite"))state.override=args[0];
 else if(same(name,"set_material"))state.material=args[0];
 else if(same(name,"set_type"))state.type=*(int *)args[0];
 else if(same(name,"set_preserveAspect"))state.aspect=*(unsigned char *)args[0];
 else if(same(name,"set_color"))for(unsigned i=0;i<16;++i)((unsigned char *)state.color)[i]=((unsigned char *)args[0])[i];
 else if(same(name,"SetAllDirty")){++state.dirty;state.dirty_at=state.clock;}
 else if(same(name,"Rebuild")){++state.rebuild;state.rebuild_at=state.clock;state.phase=*(int *)args[0];}
 else return -1;
 return 0;
}
/* PRODUCTION_BATTLE */
int run_battle_tests(void) {
 state.sprite=(void *)99;state.override=(void *)98;state.material=(void *)97;state.alpha=(void *)96;state.type=1;
 crosshair_battle_image(&state,(void *)1,(void *)2);
 if(state.sprite!=(void *)1 || state.override!=(void *)1 || state.material!=(void *)2 || state.alpha)return 1;
 if(state.clear!=1 || state.dirty!=1 || state.rebuild!=1 || state.phase!=3 || state.type || !state.aspect)return 2;
 if(!(state.clear_at<state.dirty_at && state.dirty_at<state.rebuild_at))return 3;
 for(unsigned i=0;i<4;++i)if(state.color[i]!=0x3f800000)return 4;
 crosshair_battle_image(&state,(void *)1,(void *)2);if(state.clear!=1 || state.rebuild!=1)return 5;
 state.alpha=(void *)77;crosshair_battle_image(&state,(void *)3,(void *)2);
 if(state.alpha || state.clear!=2)return 6;
 state.alpha=(void *)78;crosshair_battle_image(&state,(void *)1,(void *)2);
 if(state.alpha || state.clear!=3 || state.sprite!=(void *)1)return 7;
 crosshair_battle_image(&state,(void *)4,NULL);if(state.material || state.clear!=4)return 8;
 unsigned calls=state.clock;crosshair_battle_image(NULL,(void *)1,NULL);crosshair_battle_image(&state,NULL,NULL);
 if(state.clock!=calls)return 9;
 return 0;
}
