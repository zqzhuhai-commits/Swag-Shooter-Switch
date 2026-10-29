#include "diagnostics.h"
#include <switch.h>
#include "switch_game_input.h"
#include "jni_shim.h"
#include "controller_touch.h"
#include "vehicle_state.h"
extern void *jni_switch_touch_event(int,float,float,long);
extern void *jni_switch_key_event(int,int,long);
extern void jni_switch_release_event(void *);
extern void *jni_switch_motion_event(int,int,const int *,const float *,const float *,long);
static PadState pad;
static FILE *output;
static unsigned char (*inject)(void *,void *,void *);
static unsigned previous_keys;
static int pointer_count,pointer_ids[16];
static float pointer_x[16],pointer_y[16];
static unsigned touch_moves;
static int inject_with_frame(void *env,void *player,void *event) {
    uintptr_t *table=*(uintptr_t **)env;
    int (*push)(void *,int)=(int (*)(void *,int))table[19];
    void *(*pop)(void *,void *)=(void *(*)(void *,void *))table[20];
    if(push(env,64)) { fprintf(output,"GAME_INPUT_FRAME_FAIL\n"); jni_switch_release_event(event); return -1; }
    int result=inject(env,player,event);
    pop(env,NULL);
    jni_switch_release_event(event);
    return result;
}
static void *(*resolve_input)(const char *);
static int (*unity_touch_count)(void);
static unsigned char (*unity_touch_supported)(void),(*unity_mouse_button)(int),(*unity_key)(int);
typedef struct { int id; float position[2],raw[2],delta[2],dt; int taps,phase,type; float pressure,max_pressure,radius,variance,altitude,azimuth; } UnityTouch;
_Static_assert(sizeof(UnityTouch)==68,"Unity 2019 Touch ABI");
static void (*unity_get_touch)(int,UnityTouch *);
static int pending_probe,configured;
void switch_game_input_set_resolver(void *(*resolve)(const char *)) { resolve_input=resolve; }
static void configure_unity_input(void) {
    if(configured || !resolve_input) return;
    configured=1;
    unity_touch_count=(int (*)(void))resolve_input("UnityEngine.Input::get_touchCount");
    unity_touch_supported=(unsigned char (*)(void))resolve_input("UnityEngine.Input::get_touchSupported");
    unity_get_touch=(void (*)(int,UnityTouch *))resolve_input("UnityEngine.Input::GetTouch_Injected");
    unity_mouse_button=(unsigned char (*)(int))resolve_input("UnityEngine.Input::GetMouseButton");
    unity_key=(unsigned char (*)(int))resolve_input("UnityEngine.Input::GetKeyInt");
    void (*multitouch)(unsigned char)=(void (*)(unsigned char))resolve_input("UnityEngine.Input::set_multiTouchEnabled");
    void (*mouse_touch)(unsigned char)=(void (*)(unsigned char))resolve_input("UnityEngine.Input::set_simulateMouseWithTouches");
    if(multitouch) multitouch(1);
    if(mouse_touch) mouse_touch(1);
    fprintf(output,"UNITY_INPUT_CONFIG touch_count_api=%d get_touch_api=%d multitouch_enabled=%d mouse_from_touch=%d\n",unity_touch_count!=NULL,unity_get_touch!=NULL,multitouch!=NULL,mouse_touch!=NULL);
}
void switch_game_input_after_frame(void) {
    if(!diagnostics_enabled() || !pending_probe) return;
    --pending_probe;
    int count=unity_touch_count ? unity_touch_count() : -1;
    fprintf(output,"UNITY_INPUT_STATE hid_pointers=%d touches=%d supported=%d mouse0=%d return_key=%d\n",pointer_count,count,unity_touch_supported ? unity_touch_supported() : -1,unity_mouse_button ? unity_mouse_button(0) : -1,unity_key ? unity_key(13) : -1);
    if(unity_get_touch && count>0 && count<=16) {
        UnityTouch touch={0}; unity_get_touch(0,&touch);
        fprintf(output,"UNITY_INPUT_TOUCH id=%d phase=%d x=%.1f y=%.1f pressure=%.2f\n",touch.id,touch.phase,touch.position[0],touch.position[1],touch.pressure);
    }
}
int switch_game_input_init(FILE *log) {
    output=log;
    inject=(unsigned char (*)(void *,void *,void *))jni_find_native("nativeInjectEvent");
    if(!inject) { fprintf(log,"GAME_INPUT_BLOCKED missing=nativeInjectEvent\n"); return -1; }
    hidInitializeTouchScreen();
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    if(controller_touch_selftest(log)) return -1;
    controller_touch_init(log);
    if(vehicle_state_selftest(log)) return -1;
    fprintf(log,"GAME_INPUT_READY touchscreen=multi_pointer immutable_events=1 menu_navigation=1 game_injection=ENABLED combat_mapping=NOT_VALIDATED\n");
    return 0;
}
static void touch(void *env,void *player,int action,long now) {
    configure_unity_input(); if(diagnostics_enabled() && action!=2) pending_probe=3;
    void *event=jni_switch_motion_event(action,pointer_count,pointer_ids,pointer_x,pointer_y,now);
    if(!event) return;
    int accepted=inject_with_frame(env,player,event);
    if(diagnostics_enabled() && (action!=2 || ++touch_moves%300==0)) { fprintf(output,"GAME_TOUCH action=%d pointers=%d x=%.1f y=%.1f native_return=%d\n",action,pointer_count,pointer_x[0],pointer_y[0],accepted); fflush(output); }
}
void switch_game_input_poll(void *env,void *player) {
    if(!inject) return;
    long now=(long)(armTicksToNs(armGetSystemTick())/1000000);
    HidTouchScreenState state={0}; SwitchTouchSnapshot desired={0};
    if(hidGetTouchScreenStates(&state,1)) {
        for(int i=0;i<state.count && i<16;++i) {
            int n=desired.count++; desired.ids[n]=state.touches[i].finger_id;
            desired.x[n]=state.touches[i].x>1279 ? 1279 : state.touches[i].x;
            desired.y[n]=state.touches[i].y>719 ? 719 : state.touches[i].y;
        }
    } else {
        for(int i=0;i<pointer_count;++i) if(pointer_ids[i]<16) {
            int n=desired.count++; desired.ids[n]=pointer_ids[i]; desired.x[n]=pointer_x[i]; desired.y[n]=pointer_y[i];
        }
    }
    padUpdate(&pad);
    vehicle_state_poll(output);
    controller_touch_update(&desired,&pad,output);
    {
        int moved=0;
        for(int i=pointer_count-1;i>=0;--i) {
            int found=-1;
            for(int j=0;j<desired.count;++j) if(desired.ids[j]==pointer_ids[i]) { found=j; break; }
            if(found<0) {
                touch(env,player,pointer_count==1 ? 1 : 6|(i<<8),now);
                for(int j=i;j<pointer_count-1;++j) { pointer_ids[j]=pointer_ids[j+1]; pointer_x[j]=pointer_x[j+1]; pointer_y[j]=pointer_y[j+1]; }
                --pointer_count;
            } else {
                float x=desired.x[found],y=desired.y[found];
                if(x>1279) x=1279;
                if(y>719) y=719;
                moved|=pointer_x[i]!=x || pointer_y[i]!=y; pointer_x[i]=x; pointer_y[i]=y;
            }
        }
        for(int i=0;i<desired.count;++i) {
            int known=0;
            for(int j=0;j<pointer_count;++j) known|=pointer_ids[j]==desired.ids[i];
            if(known || pointer_count==16) continue;
            int index=pointer_count++;
            pointer_ids[index]=desired.ids[i]; pointer_x[index]=desired.x[i]; pointer_y[index]=desired.y[i];
            touch(env,player,index==0 ? 0 : 5|(index<<8),now);
        }
        int virtual_held=0;
        for(int i=0;i<pointer_count;++i) virtual_held|=pointer_ids[i]>=16;
        if(pointer_count && (moved || virtual_held)) touch(env,player,2,now);
    }
    u64 held=padGetButtons(&pad);
    int cursor_x,cursor_y;
    unsigned keys=controller_touch_cursor(&cursor_x,&cursor_y) && (held&HidNpadButton_B) ? 32 : 0;
    static const int codes[]={19,20,21,22,66,4};
    for(unsigned i=0;i<6;++i) if((keys^previous_keys)&(1u<<i)) {
        configure_unity_input(); if(diagnostics_enabled()) pending_probe=3;
        int action=(keys&(1u<<i)) ? 0:1;
        void *event=jni_switch_key_event(action,codes[i],now);
        if(!event) continue;
        int accepted=inject_with_frame(env,player,event);
        fprintf(output,"GAME_KEY action=%d android_code=%d native_return=%d\n",action,codes[i],accepted);
    }
    previous_keys=keys;
}
