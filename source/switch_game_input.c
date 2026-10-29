#include <switch.h>
#include "switch_game_input.h"
#include "jni_shim.h"
extern void *jni_switch_touch_event(int,float,float,long);
extern void *jni_switch_key_event(int,int,long);
static PadState pad;
static FILE *output;
static unsigned char (*inject)(void *,void *,void *);
static unsigned previous_keys;
static int touched;
static u32 finger;
static float last_x,last_y;
static unsigned touch_moves;
int switch_game_input_init(FILE *log) {
    output=log;
    inject=(unsigned char (*)(void *,void *,void *))jni_find_native("nativeInjectEvent");
    if(!inject) { fprintf(log,"GAME_INPUT_BLOCKED missing=nativeInjectEvent\n"); return -1; }
    hidInitializeTouchScreen();
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    fprintf(log,"GAME_INPUT_READY touchscreen=single_pointer menu_navigation=1 game_injection=ENABLED combat_mapping=NOT_VALIDATED\n");
    return 0;
}
static void touch(void *env,void *player,int action,float x,float y,long now) {
    int accepted=inject(env,player,jni_switch_touch_event(action,x,y,now));
    if(action!=2 || ++touch_moves%60==0) { fprintf(output,"GAME_TOUCH action=%d x=%.1f y=%.1f native_return=%d\n",action,x,y,accepted); fflush(output); }
}
void switch_game_input_poll(void *env,void *player) {
    if(!inject) return;
    long now=(long)(armTicksToNs(armGetSystemTick())/1000000);
    HidTouchScreenState state={0};
    if(hidGetTouchScreenStates(&state,1)) {
        int index=-1;
        if(touched) { for(int i=0;i<state.count && i<16;++i) if(state.touches[i].finger_id==finger) { index=i; break; } }
        else if(state.count>0) index=0;
        if(index>=0) {
            float x=state.touches[index].x,y=state.touches[index].y;
            if(x>1279) x=1279;
            if(y>719) y=719;
            if(!touched) { finger=state.touches[index].finger_id; touch(env,player,0,x,y,now); touched=1; }
            else if(x!=last_x || y!=last_y) touch(env,player,2,x,y,now);
            last_x=x; last_y=y;
        } else if(touched) { touch(env,player,1,last_x,last_y,now); touched=0; }
    }
    padUpdate(&pad);
    u64 held=padGetButtons(&pad);
    HidAnalogStickState stick=padGetStickPos(&pad,0);
    unsigned keys=0;
    if((held&HidNpadButton_Up) || stick.y>16000) keys|=1;
    if((held&HidNpadButton_Down) || stick.y<-16000) keys|=2;
    if((held&HidNpadButton_Left) || stick.x<-16000) keys|=4;
    if((held&HidNpadButton_Right) || stick.x>16000) keys|=8;
    if(held&HidNpadButton_A) keys|=16;
    if(held&HidNpadButton_B) keys|=32;
    static const int codes[]={19,20,21,22,66,4};
    for(unsigned i=0;i<6;++i) if((keys^previous_keys)&(1u<<i)) {
        int action=(keys&(1u<<i)) ? 0:1;
        int accepted=inject(env,player,jni_switch_key_event(action,codes[i],now));
        fprintf(output,"GAME_KEY action=%d android_code=%d native_return=%d\n",action,codes[i],accepted);
    }
    previous_keys=keys;
}
