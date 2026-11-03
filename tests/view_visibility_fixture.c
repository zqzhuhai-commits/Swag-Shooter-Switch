#include <stdint.h>
#include <stddef.h>
static unsigned bad_calls;
void *memset(void *p,int value,size_t size) {unsigned char *b=p;while(size--)*b++=value;return p;}
typedef struct {unsigned char visible,alive;} Object;
static Object objects[5];
static void *handles[32];static unsigned next_handle=1;
static int same(const char *a,const char *b) {while(*a && *a==*b){++a;++b;}return *a==*b;}
static uint32_t weak(void *o,unsigned char track) {(void)track;handles[next_handle]=o;return next_handle++;}
static void *target(uint32_t h) {Object *o=handles[h];return o && o->alive ? o : 0;}
static void release(uint32_t h) {handles[h]=0;}
static void *unbox(void *o) {return o;}
static struct {uint32_t (*weak)(void *,unsigned char);void *(*target)(uint32_t);void (*release)(uint32_t);void *(*unbox)(void *);} api={weak,target,release,unbox};
static int squad_invoke(void *p,const char *name,int count,void **args,void **result) {
 Object *o=p;if(!o || !o->alive)return -1;
 if(same(name,"get_enabled") || same(name,"get_activeSelf")) {if(count!=0)++bad_calls;*result=&o->visible;return 0;}
 if(same(name,"set_enabled") || same(name,"SetActive")) {if(count!=1)++bad_calls;o->visible=*(unsigned char *)args[0];return 0;}
 ++bad_calls;return -1;
}
/* PRODUCTION_VISIBILITY */
int run_visibility_tests(void) {
 for(unsigned i=0;i<5;++i)objects[i].alive=objects[i].visible=1;
 view_visibility_set(5,&objects[0],1,1);
 if(objects[0].visible || !view_visibility[5].handle)return 1;
 objects[0].visible=1;view_visibility_refresh();if(objects[0].visible)return 2;
 view_visibility_set(5,&objects[0],1,1);view_visibility_set(5,0,0,1);
 if(!objects[0].visible || view_visibility[5].handle)return 3;
 view_visibility_set(5,&objects[0],1,1);view_visibility_set(5,&objects[1],1,1);
 if(!objects[0].visible || objects[1].visible)return 4;
 view_visibility_set(5,0,0,1);if(!objects[1].visible)return 5;
 objects[2].visible=0;view_visibility_set(0,&objects[2],1,0);
 objects[2].visible=1;view_visibility_set(0,0,0,0);
 if(!objects[2].visible)return 6; /* Do not overwrite native dismount restoration. */
 view_visibility_set(1,&objects[3],1,0);objects[3].alive=0;
 view_visibility_refresh();view_visibility_set(1,0,0,0);
 if(view_visibility[1].handle)return 7;
 view_visibility_set(0,&objects[4],1,0);if(objects[4].visible)return 8;
 for(unsigned i=0;i<6;++i)view_visibility_set(i,0,0,0);
 if(!objects[4].visible || bad_calls)return 9;
 return 0;
}
