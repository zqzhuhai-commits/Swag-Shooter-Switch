#include <switch.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "util.h"
#include "jni_shim.h"
#include "unity_host.h"

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
int unity_host_bridge_test(AndroidElf *module, FILE *log) {
    void *vm, *env; int errors = 0;
    unity_host_set_log(log); unity_host_get_vm(&vm, &env);
    uintptr_t *table = *(uintptr_t **)env;
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
    void *vm,*env; unity_host_get_vm(&vm,&env); (void)vm;
    static unsigned char player[32],activity[32],surface[32];
    unity_host_set_log(log);
    if(setjmp(jni_guard)) { __atomic_store_n(&guard_active,0,__ATOMIC_RELEASE); fprintf(log,"ENGINE_LIFECYCLE_ABORT unsupported_jni=1\n"); return -1; }
    guard_thread=threadGetCurHandle(); __atomic_store_n(&guard_active,1,__ATOMIC_RELEASE);
    void *fn=jni_find_native("initJni");
    if(!fn) { fprintf(log,"ENGINE_LIFECYCLE_BLOCKED missing_entry=initJni\n"); goto fail; }
    fprintf(log,"ENGINE_INITJNI_BEGIN\n"); fflush(log);
    ((void (*)(void *,void *,void *))fn)(env,player,activity);
    fprintf(log,"ENGINE_INITJNI_END\n"); fflush(log);
    fn=jni_find_native("nativeRecreateGfxState");
    if(!fn) { fprintf(log,"ENGINE_LIFECYCLE_BLOCKED missing_entry=nativeRecreateGfxState\n"); goto fail; }
    fprintf(log,"ENGINE_SURFACE_BEGIN\n"); fflush(log);
    ((void (*)(void *,void *,int,void *))fn)(env,player,0,surface);
    fn=jni_find_native("nativeSendSurfaceChangedEvent");
    if(fn) ((void (*)(void *,void *))fn)(env,player);
    fprintf(log,"ENGINE_SURFACE_END\n"); fflush(log);
    fn=jni_find_native("nativeResume");
    if(fn) ((void (*)(void *,void *))fn)(env,player);
    void *render=jni_find_native("nativeRender");
    if(!render) { fprintf(log,"ENGINE_LIFECYCLE_BLOCKED missing_entry=nativeRender\n"); goto fail; }
    for(unsigned frame=0;frame<300 && appletMainLoop();++frame) {
        if(frame<10) { fprintf(log,"ENGINE_RENDER_BEGIN frame=%u\n",frame); fflush(log); }
        int running=((unsigned char (*)(void *,void *))render)(env,player);
        if(frame<10 || !running) { fprintf(log,"ENGINE_RENDER_END frame=%u result=%d visual_confirmation=REQUIRED\n",frame,running); fflush(log); }
        if(!running) break;
        jni_poll_java_callbacks(env); svcSleepThread(16000000);
    }
    __atomic_store_n(&guard_active,0,__ATOMIC_RELEASE); return 0;
fail:
    __atomic_store_n(&guard_active,0,__ATOMIC_RELEASE); return -1;
}
