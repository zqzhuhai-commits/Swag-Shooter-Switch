#include <stdint.h>
#include <stddef.h>
void *memset(void *p,int value,size_t n) {for(size_t i=0;i<n;++i)((unsigned char *)p)[i]=(unsigned char)value;return p;}
typedef struct {void *sprite,*override;int type;unsigned char aspect;uint32_t color[4];unsigned calls;} Image;
static int same(const char *a,const char *b) {while(*a && *a==*b) {++a;++b;}return *a==*b;}
static int squad_invoke(void *object,const char *name,int count,void **args,void **out) {
 (void)count;(void)out;Image *image=object;++image->calls;
 if(same(name,"set_sprite"))image->sprite=args[0];
 else if(same(name,"set_overrideSprite"))image->override=args[0];
 else if(same(name,"set_type"))image->type=*(int *)args[0];
 else if(same(name,"set_preserveAspect"))image->aspect=*(unsigned char *)args[0];
 else if(same(name,"set_color"))for(unsigned i=0;i<16;++i)((unsigned char *)image->color)[i]=((unsigned char *)args[0])[i];
 else return -1;
 return 0;
}
/* PRODUCTION_PREVIEW */
int run_preview_tests(void) {
 Image images[4]={0};
 for(unsigned i=0;i<4;++i) {
  images[i].sprite=(void *)99;images[i].override=(void *)99;images[i].type=1;
  crosshair_preview_image(&images[i],(void *)(uintptr_t)(i+1),0);
  if(images[i].sprite!=(void *)(uintptr_t)(i+1) || images[i].override!=images[i].sprite)return 1;
  if(images[i].calls!=5 || images[i].type || !images[i].aspect)return 2;
  for(unsigned k=0;k<4;++k)if(images[i].color[k]!=0x3f800000)return 3;
 }
 crosshair_preview_image(&images[0],(void *)4,1);
 if(images[0].sprite!=(void *)4 || images[0].override!=(void *)4)return 4;
 if(images[0].color[0]!=0x3f800000 || images[0].color[1]!=0x3f4ccccd || images[0].color[2]!=0x3e4ccccd || images[0].color[3]!=0x3f800000)return 5;
 unsigned calls=images[0].calls;crosshair_preview_image(&images[0],NULL,0);crosshair_preview_image(NULL,(void *)1,0);
 if(images[0].calls!=calls)return 6;
 return 0;
}
