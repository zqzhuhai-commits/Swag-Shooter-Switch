#include <switch.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include <time.h>
#include "util.h"
#include "jni_shim.h"
#include "unity_host.h"
#include "startup_probe.h"
#include "guest_threads.h"
#include "switch_game_input.h"
extern int jni_choreo_captured(void);
extern int jni_take_handler_message(void);
extern void jni_handlemessage(void *);
extern int jni_choreo_doframe(void *,long);
extern void *jni_switch_touch_event(int,float,float,long);
extern void *jni_switch_key_event(int,int,long);
static void *(*looper_domain)(void);
static void *(*looper_attach)(void *);
static void (*looper_detach)(void *);
static unsigned looper_stop;
void unity_host_set_il2cpp(AndroidElf *module) {
    looper_domain=(void *(*)(void))elf_export(module,"il2cpp_domain_get");
    looper_attach=(void *(*)(void *))elf_export(module,"il2cpp_thread_attach");
    looper_detach=(void (*)(void *))elf_export(module,"il2cpp_thread_detach");
}
static void *looper_entry(void *argument) {
    void *env=argument;
    while(!__atomic_load_n(&looper_stop,__ATOMIC_ACQUIRE) && !jni_choreo_captured()) svcSleepThread(10000000);
    if(__atomic_load_n(&looper_stop,__ATOMIC_ACQUIRE)) return NULL;
    void *attached=looper_attach(looper_domain());
    debugPrintf("ENGINE_LOOPER_ATTACH result=%p\n",attached);
    if(!attached) return NULL;
    unsigned long long ticks=0;
    int active=0;
    while(!__atomic_load_n(&looper_stop,__ATOMIC_ACQUIRE)) {
        if(jni_take_handler_message()) { jni_handlemessage(env); active=1; debugPrintf("ENGINE_LOOPER_MESSAGE delivered=1\n"); }
        if(active) {
            long nanos=(long)armTicksToNs(armGetSystemTick());
            int delivered=jni_choreo_doframe(env,nanos);
            if(delivered && (++ticks<=3 || ticks%600==0)) debugPrintf("ENGINE_LOOPER_FRAME count=%llu delivered=1\n",ticks);
        }
        svcSleepThread(16000000);
    }
    looper_detach(attached); return NULL;
}

static FILE *unity_log;
static jmp_buf jni_guard;
static int guard_active;
static Handle guard_thread;
void unity_host_set_log(FILE *log) { unity_log = log; }
int debugPrintf(const char *format, ...) {
    if (!unity_log) return 0;
    va_list args; va_start(args, format); int n = vfprintf(unity_log, format, args); va_end(args); return n;
}
const char *hd_game_dir(void) { return "romfs:/game"; }
int hd_game_path(char *out, size_t size, const char *relative) {
    const char *root = hd_game_dir();
    if (!strcmp(relative, "userdata")) { relative = ""; root = "sdmc:/switch/swag-userdata"; }
    else if (!strncmp(relative, "userdata/", 9)) { relative += 9; root = "sdmc:/switch/swag-userdata"; }
    int n = snprintf(out, size, "%s%s%s", root, *relative ? "/" : "", relative);
    return n >= 0 && (size_t)n < size ? 0 : -1;
}
int hd_font_asset_path(char *out, size_t size, const char *relative) {
    int n = snprintf(out, size, "romfs:/game/font-assets/%s", relative);
    return n >= 0 && (size_t)n < size ? 0 : -1;
}
int hd_display_size_detect(int *width, int *height, const char **source) {
    if (width) *width = 1280;
    if (height) *height = 720;
    if (source) *source = "Switch native";
    return 1;
}
void unity_jni_unsupported(unsigned slot) {
    debugPrintf("UNITY_JNI_UNSUPPORTED slot=%u\n", slot);
    if (__atomic_load_n(&guard_active, __ATOMIC_ACQUIRE) && threadGetCurHandle() == guard_thread) longjmp(jni_guard, 1);
    abort();
}
void jni_install_indexed(uintptr_t *table, int count);
void unity_host_get_vm(void **vm, void **env) {
    jni_shim_set_version("1.6", 14);
    jni_shim_init(vm, env);
}
int unity_host_call_onload(uintptr_t entry, void *vm, FILE *log) {
    unity_log = log;
    if (setjmp(jni_guard)) { __atomic_store_n(&guard_active, 0, __ATOMIC_RELEASE); return -1; }
    guard_thread = threadGetCurHandle();
    __atomic_store_n(&guard_active, 1, __ATOMIC_RELEASE);
    int version = ((int (*)(void *, void *))entry)(vm, NULL);
    __atomic_store_n(&guard_active, 0, __ATOMIC_RELEASE);
    return version;
}
static int64_t probe_static_long_v(uintptr_t fn,void *env,void *clazz,void *method,...) {
    va_list ap; va_start(ap,method);
    int64_t result=((int64_t (*)(void *,void *,void *,va_list))fn)(env,clazz,method,ap);
    va_end(ap); return result;
}
int unity_host_bridge_test(AndroidElf *module, FILE *log) {
    void *vm, *env; int errors = 0;
    unity_host_set_log(log); unity_host_get_vm(&vm, &env);
    uintptr_t *table = *(uintptr_t **)env;
    void *(*find_class)(void *,const char *)=(void *(*)(void *,const char *))table[6];
    void *(*get_static)(void *,void *,const char *,const char *)=(void *(*)(void *,void *,const char *,const char *))table[113];
    void *system_class=find_class(env,"java/lang/System");
    void *nano_method=get_static(env,system_class,"nanoTime","()J");
    void *millis_method=get_static(env,system_class,"currentTimeMillis","()J");
    int64_t (*long_plain)(void *,void *,void *,...)=(int64_t (*)(void *,void *,void *,...))table[132];
    int64_t (*long_array)(void *,void *,void *,const void *)=(int64_t (*)(void *,void *,void *,const void *))table[134];
    int64_t n1=long_plain(env,system_class,nano_method);
    svcSleepThread(2000000);
    int64_t n2=probe_static_long_v(table[133],env,system_class,nano_method);
    int64_t n3=long_array(env,system_class,nano_method,NULL);
    int clock_errors=n1<=0 || n2<=n1 || n3<n2 || n2-n1>1000000000;
    struct timespec wall; clock_gettime(CLOCK_REALTIME,&wall);
    int64_t expected=(int64_t)wall.tv_sec*1000+wall.tv_nsec/1000000;
    int64_t millis=probe_static_long_v(table[133],env,system_class,millis_method);
    clock_errors+=millis<expected || millis-expected>2000;
    errors+=clock_errors;
    fprintf(log,"JNI_STATIC_LONG_CLOCK errors=%d slots=132,133,134 monotonic_order=CHECKED realtime_millis=CHECKED\n",clock_errors);
    int field_object, field_id;
    void (*set_int)(void *,void *,void *,int)=(void (*)(void *,void *,void *,int))table[109];
    int (*get_int)(void *,void *,void *)=(int (*)(void *,void *,void *))table[100];
    set_int(env,&field_object,&field_id,12345); errors+=get_int(env,&field_object,&field_id)!=12345;
    fprintf(log,"JNI_FIELD_ROUNDTRIP errors=%d set_index=109 get_index=100\n",errors);

    unsigned char (*exception_check)(void *)=(unsigned char (*)(void *))table[228];
    errors+=exception_check(env)!=0;
    fprintf(log,"JNI_EXCEPTION_SLOT_TEST index=228 errors=%d\n",errors);

    void *(*new_string)(void *, const char *) = (void *(*)(void *, const char *))table[167];
    const char *(*get_string)(void *, void *, unsigned char *) = (const char *(*)(void *, void *, unsigned char *))table[169];
    void (*release_string)(void *, void *, const char *) = (void (*)(void *, void *, const char *))table[170];
    const char *sample = "Switch JNI UTF-8 \xe4\xb8\xad";
    void *string = new_string(env, sample);
    const char *text = get_string(env, string, NULL);
    errors += !text || strcmp(text, sample);
    if (text) release_string(env, string, text);
    void *environment=find_class(env,"android/os/Environment");
    void *(*get_field)(void *,void *,const char *,const char *)=(void *(*)(void *,void *,const char *,const char *))table[144];
    void *(*get_object)(void *,void *,void *)=(void *(*)(void *,void *,void *))table[145];
    void *mounted_id=get_field(env,environment,"MEDIA_MOUNTED","Ljava/lang/String;");
    void *mounted=get_object(env,environment,mounted_id);
    const char *mounted_text=get_string(env,mounted,NULL);
    int storage_errors=!mounted_text || strcmp(mounted_text,"mounted");
    void *string_class=find_class(env,"java/lang/String");
    void *(*get_method)(void *,void *,const char *,const char *)=(void *(*)(void *,void *,const char *,const char *))table[33];
    void *equals=get_method(env,string_class,"equals","(Ljava/lang/Object;)Z");
    unsigned char (*boolean_call)(void *,void *,void *,...)=(unsigned char (*)(void *,void *,void *,...))table[37];
    storage_errors+=!boolean_call(env,mounted,equals,new_string(env,"mounted"));
    storage_errors+=boolean_call(env,mounted,equals,new_string(env,"unmounted"))!=0;
    storage_errors+=boolean_call(env,mounted,equals,NULL)!=0;
    errors+=storage_errors;
    fprintf(log,"JNI_STORAGE_STATE errors=%d mounted_constant=CHECKED string_equals=CHECKED persistent_path_integration=PENDING\n",storage_errors);
    {
        int input_errors=0;
        void *motion_class=find_class(env,"android/view/MotionEvent");
        void *key_class=find_class(env,"android/view/KeyEvent");
        unsigned char (*instance)(void *,void *,void *)=(unsigned char (*)(void *,void *,void *))table[32];
        int (*integer)(void *,void *,void *,...)=(int (*)(void *,void *,void *,...))table[49];
        float (*number)(void *,void *,void *,...)=(float (*)(void *,void *,void *,...))table[55];
        long (*event_time)(void *,void *,void *,...)=(long (*)(void *,void *,void *,...))table[52];
        void *event=jni_switch_touch_event(0,123,456,100);
        input_errors+=!instance(env,event,motion_class) || instance(env,event,key_class);
        void *action=get_method(env,motion_class,"getAction","()I");
        void *count=get_method(env,motion_class,"getPointerCount","()I");
        void *x=get_method(env,motion_class,"getX","(I)F");
        void *y=get_method(env,motion_class,"getY","(I)F");
        void *down=get_method(env,motion_class,"getDownTime","()J");
        input_errors+=integer(env,event,action)!=0 || integer(env,event,count)!=1;
        input_errors+=number(env,event,x,0)!=123 || number(env,event,y,0)!=456;
        jni_switch_touch_event(2,124,457,200);
        input_errors+=integer(env,event,action)!=2 || event_time(env,event,down)!=100;
        jni_switch_touch_event(1,124,457,210);
        input_errors+=integer(env,event,action)!=1;
        event=jni_switch_key_event(0,66,220);
        input_errors+=!instance(env,event,key_class) || instance(env,event,motion_class);
        input_errors+=integer(env,event,get_method(env,key_class,"getKeyCode","()I"))!=66;
        errors+=input_errors;
        fprintf(log,"JNI_GAME_INPUT_TEST errors=%d touch_class_coords_time=CHECKED key_class_code=CHECKED hardware_input=PENDING\n",input_errors);
    }
    void *(*new_array)(void *, int) = (void *(*)(void *, int))table[179];
    int (*array_length)(void *, void *) = (int (*)(void *, void *))table[171];
    int *(*array_data)(void *, void *, unsigned char *) = (int *(*)(void *, void *, unsigned char *))table[187];
    void (*release_array)(void *, void *, int *, int) = (void (*)(void *, void *, int *, int))table[195];
    void *array = new_array(env, 4);
    errors += !array || array_length(env, array) != 4;
    int *data = array ? array_data(env, array, NULL) : NULL;
    if (!data) ++errors;
    else { data[0] = 1234; data[3] = 5678; release_array(env, array, data, 0); data = array_data(env, array, NULL); errors += !data || data[0] != 1234 || data[3] != 5678; }
    uintptr_t entry = elf_export(module, "JNI_OnLoad");
    int version = entry ? unity_host_call_onload(entry, vm, log) : -1;
    errors += version != 0x10006 || !jni_find_native("load") || !jni_find_native("unload");
    fprintf(log, "UNITY_JNI_BRIDGE_DONE errors=%d version=%x strings=CHECKED arrays=CHECKED native_loader=CHECKED engine_started=0\n", errors, version);
    return errors ? -1 : 0;
}

int unity_host_run(FILE *log) {
    startup_engine_progress();
    void *vm,*env; unity_host_get_vm(&vm,&env); (void)vm;
    uintptr_t looper=0;
    if(!looper_domain || !looper_attach || !looper_detach) { fprintf(log,"ENGINE_LOOPER_BLOCKED missing_il2cpp_exports=1\n"); return -1; }
    __atomic_store_n(&looper_stop,0,__ATOMIC_RELEASE);
    int looper_error=guest_thread_create(&looper,NULL,looper_entry,env);
    fprintf(log,"ENGINE_LOOPER_CREATE result=%d\n",looper_error); fflush(log);
    if(looper_error) return -1;
    static unsigned char player[32],activity[32],surface[32];
    unity_host_set_log(log);
    if(setjmp(jni_guard)) { __atomic_store_n(&guard_active,0,__ATOMIC_RELEASE); fprintf(log,"ENGINE_LIFECYCLE_ABORT unsupported_jni=1\n"); goto fail; }
    guard_thread=threadGetCurHandle(); __atomic_store_n(&guard_active,1,__ATOMIC_RELEASE);
    void *fn=jni_find_native("initJni");
    if(!fn) { fprintf(log,"ENGINE_LIFECYCLE_BLOCKED missing_entry=initJni\n"); goto fail; }
    fprintf(log,"ENGINE_INITJNI_BEGIN\n"); fflush(log);
    ((void (*)(void *,void *,void *))fn)(env,player,activity);
    fprintf(log,"ENGINE_INITJNI_END\n"); fflush(log);
    startup_engine_progress();
    fn=jni_find_native("nativeRecreateGfxState");
    if(!fn) { fprintf(log,"ENGINE_LIFECYCLE_BLOCKED missing_entry=nativeRecreateGfxState\n"); goto fail; }
    fprintf(log,"ENGINE_SURFACE_BEGIN\n"); fflush(log);
    ((void (*)(void *,void *,int,void *))fn)(env,player,0,surface);
    fn=jni_find_native("nativeSendSurfaceChangedEvent");
    if(fn) ((void (*)(void *,void *))fn)(env,player);
    fprintf(log,"ENGINE_SURFACE_END\n"); fflush(log);
    fn=jni_find_native("nativeRecreateGfxState");
    if(fn) ((void (*)(void *,void *,int,void *))fn)(env,player,0,surface);
    fn=jni_find_native("nativeSendSurfaceChangedEvent");
    if(fn) { ((void (*)(void *,void *))fn)(env,player); fprintf(log,"ENGINE_SURFACE_CHANGED delivered=1\n"); }
    fn=jni_find_native("nativeResume");
    if(fn) ((void (*)(void *,void *))fn)(env,player);
    fn=jni_find_native("nativeFocusChanged");
    if(fn) { ((void (*)(void *,void *,int))fn)(env,player,1); fprintf(log,"ENGINE_FOCUS_CHANGED focused=1\n"); }
    void *render=jni_find_native("nativeRender");
    if(!render) { fprintf(log,"ENGINE_LIFECYCLE_BLOCKED missing_entry=nativeRender\n"); goto fail; }
    if(switch_game_input_init(log)) goto fail;
    fprintf(log,"ENGINE_LOOP_POLICY frame_limit=NONE idle_timeout_seconds=300 visual_confirmation=REQUIRED\n"); fflush(log);
    startup_engine_progress();
    for(unsigned long long frame=0;appletMainLoop();++frame) {
        if(frame>0) switch_game_input_poll(env,player);
        if(frame<10 || frame%60==0) { fprintf(log,"ENGINE_RENDER_BEGIN frame=%llu\n",frame); fflush(log); }
        int running=((unsigned char (*)(void *,void *))render)(env,player);
        startup_engine_progress();
        if(frame<10 || frame%60==0 || !running) { fprintf(log,"ENGINE_RENDER_END frame=%llu result=%d visual_confirmation=REQUIRED\n",frame,running); fflush(log); }
        if(!running) break;
        jni_poll_java_callbacks(env); startup_engine_progress(); svcSleepThread(16000000);
    }
    __atomic_store_n(&looper_stop,1,__ATOMIC_RELEASE);
    guest_thread_join(looper,NULL);
    __atomic_store_n(&guard_active,0,__ATOMIC_RELEASE); return 0;
fail:
    __atomic_store_n(&looper_stop,1,__ATOMIC_RELEASE);
    guest_thread_join(looper,NULL);
    __atomic_store_n(&guard_active,0,__ATOMIC_RELEASE); return -1;
}
