#include "native_process.h"
#include "offline_game.h"
#include "ai_profile.h"
#include "diagnostics.h"
#include <switch.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include "elf_loader.h"
#include "nx_memory.h"
#include "bionic_basic.h"
#include "loader_selftest.h"
#include "bionic_threads.h"
#include "android_tls.h"
#include "runtime_services.h"
#include "guest_threads.h"
#include "guest_memory.h"
#include "guest_text.h"
#include "guest_system.h"
#include "unity_host.h"
#include "controls.h"
#include "batch_suite.h"
#include "android_events.h"
#include "startup_probe.h"
#include "gc_bridge.h"
#include "android_graphics.h"

/* Foreign modules are inventoried and patched, but no constructors/JNI/game
 * code run until the remaining imports and lifecycle semantics are ready. */
typedef struct { AndroidElf modules[3]; size_t count; } ModuleSet;
extern void crash_trace_init(FILE *);
extern void crash_trace_module(unsigned,uintptr_t,size_t);
static unsigned batch_stop;
static unsigned engine_watchdog_mode;
static u64 engine_progress_tick;
void startup_engine_progress(void) {
    __atomic_store_n(&engine_progress_tick,armGetSystemTick(),__ATOMIC_RELEASE);
    __atomic_store_n(&engine_watchdog_mode,1,__ATOMIC_RELEASE);
}
static uintptr_t debug_bases[3]; static size_t debug_sizes[3];
extern unsigned guest_thread_debug_snapshot(Handle *,uintptr_t *,unsigned);
static void print_pc(FILE *log,const char *kind,uintptr_t pc) {
    const char *names[]={"unity","il2cpp","main"};
    for(unsigned i=0;i<3;++i) if(pc>=debug_bases[i] && pc-debug_bases[i]<debug_sizes[i]) {
        fprintf(log,"THREAD_ADDRESS kind=%s module=%s offset=%llx\n",kind,names[i],(unsigned long long)(pc-debug_bases[i])); return;
    }
    fprintf(log,"THREAD_ADDRESS kind=%s absolute=%llx\n",kind,(unsigned long long)pc);
}
static void terminal_thread_snapshot(FILE *log) {
    Handle handles[17]; uintptr_t ids[17];
    handles[0]=envGetMainThreadHandle(); ids[0]=0;
    unsigned count=1+guest_thread_debug_snapshot(handles+1,ids+1,16);
    for(unsigned i=0;i<count;++i) {
        ThreadContext context={0}; Result paused=svcSetThreadActivity(handles[i],ThreadActivity_Paused);
        Result read=R_SUCCEEDED(paused) ? svcGetThreadContext3(&context,handles[i]) : paused;
        uintptr_t frames[8]={0}; unsigned frame_count=0;
        if(R_SUCCEEDED(read)) {
            uintptr_t fp=context.fp;
            while(frame_count<8 && fp && !(fp&15)) {
                MemoryInfo info; u32 page;
                if(R_FAILED(svcQueryMemory(&info,&page,fp)) || !(info.perm&Perm_R) || info.size<16 || fp<info.addr || fp-info.addr>info.size-16) break;
                uintptr_t *frame=(uintptr_t *)fp; frames[frame_count++]=frame[1];
                uintptr_t next=frame[0]; if(next<=fp || next-fp>0x100000) break; fp=next;
            }
        }
        Result resumed=R_SUCCEEDED(paused) ? svcSetThreadActivity(handles[i],ThreadActivity_Runnable) : 0;
        fprintf(log,"THREAD_SNAPSHOT index=%u id=%llx pause=%x context=%x resume=%x sp=%llx\n",i,(unsigned long long)ids[i],paused,read,resumed,(unsigned long long)context.sp);
        if(R_SUCCEEDED(read)) { fprintf(log,"THREAD_REGISTERS x19=%llx x20=%llx x21=%llx\n",(unsigned long long)context.cpu_gprs[19].x,(unsigned long long)context.cpu_gprs[20].x,(unsigned long long)context.cpu_gprs[21].x); print_pc(log,"pc",context.pc.x); print_pc(log,"lr",context.lr); for(unsigned j=0;j<frame_count;++j) print_pc(log,"frame",frames[j]); }
        fflush(log);
    }
}
static void batch_watchdog(void *arg) {
    FILE *log = arg;
    u64 begin=armGetSystemTick();
    for (;;) {
        if (__atomic_load_n(&batch_stop, __ATOMIC_ACQUIRE)) return;
        unsigned engine=__atomic_load_n(&engine_watchdog_mode,__ATOMIC_ACQUIRE);
        u64 since=engine ? __atomic_load_n(&engine_progress_tick,__ATOMIC_ACQUIRE) : begin;
        u64 seconds=(armGetSystemTick()-since)/19200000;
        if(seconds>=(engine ? 300u : 120u)) {
            fprintf(log,"BATCH_WATCHDOG_ABORT mode=%s idle_seconds=%llu; retain preceding phase markers\n",engine ? "engine_no_main_thread_progress" : "preflight",(unsigned long long)seconds);
            break;
        }
        svcSleepThread(100000000);
    }
    fflush(log); terminal_thread_snapshot(log); svcExitProcess();
}
static uintptr_t resolve_module(void *ctx, const char *name) {
    ModuleSet *set = ctx;
    for (size_t i = 0; i < set->count; ++i) {
        uintptr_t p = elf_export(&set->modules[i], name);
        if (p) return p;
    }
    uintptr_t thread_api = bionic_threads_resolve(name);
    if (thread_api) return thread_api;
    uintptr_t tls_api = android_tls_resolve(name);
    if (tls_api) return tls_api;
    uintptr_t service = runtime_services_resolve(name);
    if(service) return service;
    uintptr_t basic=bionic_basic_resolve(name);
    return basic ? basic : startup_missing(name);
}
static int fixture_batch(FILE *log) { return loader_selftest(log, 1); }
static PrintConsole *boot_console;
static void boot_progress(unsigned step) {
    char bar[23];
    unsigned filled=step*20/8;
    bar[0]='['; for(unsigned i=0;i<20;++i) bar[i+1]=i<filled ? '=' : ' '; bar[21]=']'; bar[22]=0;
    int width=boot_console->consoleWidth,height=boot_console->consoleHeight;
    consoleClear();
    printf("\x1b[%d;%dHPlease wait...",height/2-1,(width-14)/2+1);
    printf("\x1b[%d;%dH%s",height/2+1,(width-22)/2+1,bar);
    consoleUpdate(NULL);
}
int main(int argc, char **argv) {
    u64 boot_tick=armGetSystemTick();
    runtime_io_set_executable_path(argc>0 ? argv[0] : NULL);
    boot_console=consoleInit(NULL);
    boot_progress(0);
    FILE *log = diagnostics_open();
    if (!log) log = stdout;
    static char log_buffer[65536];
    setvbuf(log,log_buffer,_IOFBF,sizeof log_buffer);
    FILE *full_test_flag=fopen("sdmc:/switch/swag-full-tests.flag","rb");
    int fast_start=full_test_flag==NULL;
    if(full_test_flag) fclose(full_test_flag);
    batch_set_fast_start(fast_start);
    fprintf(log,"STARTUP_MODE fast=%d full_test_flag=/switch/swag-full-tests.flag\n",fast_start);
    crash_trace_init(log);
    if(R_FAILED(runtime_process_initialize(log))) { printf("Startup failed: process handle unavailable.\n"); consoleUpdate(NULL); svcSleepThread(5000000000ULL); svcExitProcess(); }
    Thread batch_watcher;
    int watcher_running = 0;
    Result watcher_rc = threadCreate(&batch_watcher, batch_watchdog, log, NULL, 0x8000, 0x2c, -2);
    if (R_SUCCEEDED(watcher_rc)) {
        watcher_rc = threadStart(&batch_watcher);
        if (R_SUCCEEDED(watcher_rc)) watcher_running = 1;
        else threadClose(&batch_watcher);
    }
    fprintf(log, "SWAG-NX v0.14.58 lazy-memory; applet=%u own-process=%x\n",
            (unsigned)appletGetAppletType(), runtime_process_handle());
    runtime_services_init(log); guest_memory_set_log(log); startup_log(log); android_graphics_set_log(log);
    guest_system_set_log(log); unity_host_set_log(log);
    if(android_graphics_prepare(log)) { fprintf(log,"ENGINE_START_BLOCKED graphics_preflight_failed=1\n"); fflush(log); svcExitProcess(); }
    int memory_ok = batch_run("exec_memory", nx_memory_smoke_test, 1, log);
    int basic_ok = bionic_basic_selftest() == 0;
    fprintf(log, "Basic ABI bindings=%zu selftest=%s\n", bionic_basic_count(), basic_ok ? "PASS" : "FAIL");
    batch_record("basic_abi", basic_ok, log);
    int tls_init_ok = batch_run("tls_initialize", android_tls_initialize, 1, log);
    int threads_ok = batch_run("thread_sync", bionic_threads_selftest, tls_init_ok, log);
    int detached_ok = batch_run("detached_threads", guest_detached_selftest, threads_ok, log);
    Result rfs = romfsInit();
    ModuleSet set = {0};
    NxModuleMemory memory[3] = {0};
    TlsPatchPlan plans[3] = {0};
    const char *names[] = {"libunity.so", "libil2cpp.so", "libmain.so"};
    int failed = 0;
    if (R_FAILED(rfs)) {
        fprintf(log, "romfsInit=%08x\n", rfs); failed = 1;
    }
    boot_progress(1);
    int cache_ok=batch_run("metadata_cache",runtime_io_prepare,R_SUCCEEDED(rfs),log);
    int io_ok = batch_run("file_io", runtime_io_selftest, cache_ok, log);
    int zlib_ok = batch_run("decompression", runtime_zlib_selftest, 1, log);
    int data_memory_ok = batch_run("data_memory", guest_memory_selftest, io_ok, log);
    int text_ok = batch_run("utf8_text", guest_text_selftest, 1, log);
    int system_ok = batch_run("system_calendar", guest_system_selftest, 1, log);
    int events_ok = batch_run("android_events", android_events_selftest, 1, log);
    int controls_ok = batch_run("controls_matrix", swag_controls_selftest, 1, log);
    boot_progress(2);
    int tls_ok = 0;
    if (!failed) {
        int fixture_ok = batch_run("loader_fixture", fixture_batch, memory_ok, log);
        if (!fixture_ok || !basic_ok || !threads_ok) failed = 1;
        if (!failed) {
            tls_ok = android_tls_selftest(log, memory_ok) == 0;
            batch_record("tls_execution", tls_ok, log);
            if (!tls_ok) failed = 1;
        }
    }
    for (size_t i = 0; !failed && i < 3; ++i) {
        char path[128]; size_t size = 0;
        snprintf(path, sizeof path, "romfs:/game/%s", names[i]);
        if (elf_image_size(path, &size, log) || android_tls_plan(path, &plans[i], log) ||
            nx_memory_prepare(&memory[i], size + plans[i].extra_bytes, log) ||
            elf_load(&set.modules[i], path, memory[i].staging, memory[i].size,
                     (uintptr_t)memory[i].execution, log)) { failed = 1; break; }
        debug_sizes[i]=set.modules[i].size; debug_bases[i]=set.modules[i].bias;
        crash_trace_module(i,debug_bases[i],debug_sizes[i]);
        ++set.count;
        boot_progress(3+(unsigned)i);
        consoleUpdate(NULL);
    }
    boot_progress(6);
    int bootstrap_ok = 0, jni_profile_ok = 0, bootstrap_attempted = 0;
    for (size_t i = 0; i < set.count; ++i) {
        int linked = elf_relocate(&set.modules[i], resolve_module, &set, log) == 0;
        int patched = android_tls_apply(&set.modules[i], memory[i].size, &plans[i], log) == 0;
        if (i==1 && linked && patched && offline_game_apply(&set.modules[i],log)) patched=0;
        if (i==1 && linked && patched && ai_profile_prepare(&set.modules[i],log)) {
            fprintf(log,"AI_PROFILE_PREPARE status=FAILED\n"); patched=0;
        }
        if (!patched) { fprintf(log, "TLS_GAME_PATCH_FAIL %s\n", names[i]); failed = 1; }
        fprintf(log, "%s link=%s JNI_OnLoad=%p\n", names[i], linked ? "OK" : "BLOCKED",
                (void *)elf_export(&set.modules[i], "JNI_OnLoad"));
        int ready = 0;
        if (i == 2 && linked && patched && memory_ok && threads_ok && tls_ok && io_ok && zlib_ok && detached_ok && data_memory_ok && text_ok && system_ok && controls_ok && events_ok) {
            if (!nx_memory_publish(&memory[i], &set.modules[i], log) && !elf_run_initializers(&set.modules[i], log)) {
                ready = 1;
                bootstrap_attempted = 1;
                bootstrap_ok = runtime_jni_bootstrap(&set.modules[i], log) == 0;
                jni_profile_ok = unity_host_bridge_test(&set.modules[i], log) == 0;
            }
        }
        runtime_module_register(names[i], &set.modules[i], ready);
        /* Only the inspected libmain registration entry is probed. The
         * Unity engine and IL2CPP constructors are never executed here. */
    }
    /* Diagnostic run: unresolved functions terminate with their names, never
     * return invented values. A trap-filled image is not a completed port. */
    if (!failed && set.count==3 && memory_ok && threads_ok && tls_ok && io_ok && system_ok && jni_profile_ok) {
        fprintf(log,"ENGINE_START_ATTEMPT diagnostic_traps=1 game_running=0\n"); fflush(log);
        fprintf(log,"STARTUP_TIMING before_engine_ms=%llu\n",(unsigned long long)(armTicksToNs(armGetSystemTick()-boot_tick)/1000000));
        startup_engine_progress();
        for(size_t i=0;i<3;++i) {
            if(!set.modules[i].published && nx_memory_publish(&memory[i],&set.modules[i],log)) svcExitProcess();
        }
        for(size_t i=0;i<3;++i) { runtime_module_remove(&set.modules[i]); runtime_module_register(names[i],&set.modules[i],1); }
        for(size_t i=0;i<2;++i) {
            fprintf(log,"ENGINE_CONSTRUCTORS_BEGIN module=%s\n",names[i]); fflush(log);
            if(elf_run_initializers(&set.modules[i],log)) svcExitProcess();
            startup_engine_progress();
        }
        boot_progress(7);
        void *vm,*env; unity_host_get_vm(&vm,&env); (void)env;
        if(gc_bridge_bind(&set.modules[1],log)) { fprintf(log,"GC_BRIDGE_BIND_FAILED\n"); fflush(log); svcExitProcess(); }
        fprintf(log,"ENGINE_UNITY_ONLOAD_BEGIN\n"); fflush(log);
        int version=unity_host_call_onload(elf_export(&set.modules[0],"JNI_OnLoad"),vm,log);
        fprintf(log,"ENGINE_UNITY_ONLOAD_END version=%x\n",version); fflush(log);
        startup_engine_progress();
        if(version==0x10006) {
            boot_progress(8);
            consoleExit(NULL);
            unity_host_set_il2cpp(&set.modules[1]);
            int lifecycle=unity_host_run(log);
            fprintf(log,"ENGINE_LIFECYCLE_RETURN result=%d visual_confirmation=REQUIRED\n",lifecycle); fflush(log);
        }
        svcExitProcess();
    }
    fprintf(log, "COMPAT_BATCH_DONE loaded=%zu load_failed=%d memory_test=%d threads_test=%d tls_test=%d io_test=%d zlib_test=%d bootstrap_test=%d; Unity NOT started\n",
            set.count, failed, memory_ok, threads_ok, tls_ok, io_ok, zlib_ok, bootstrap_ok);
    for (size_t i = 0; i < 3; ++i) {
        runtime_module_remove(&set.modules[i]);
        nx_memory_close(&memory[i], stderr);
        android_tls_plan_free(&plans[i]);
    }
    batch_record("module_load_and_patch", !failed && set.count == 3, log);
    batch_record("libmain_jni_bootstrap", bootstrap_attempted ? bootstrap_ok : -1, log);
    batch_record("jni_profile", bootstrap_attempted ? jni_profile_ok : -1, log);
    /* Three rounds specifically check teardown/reuse and resource leaks. */
    for (unsigned round=0; round<3; ++round) {
        const char *keys[3][6] = {
            {"reuse1_detach","reuse1_memory","reuse1_text","reuse1_calendar","reuse1_controls","reuse1_loader"},
            {"reuse2_detach","reuse2_memory","reuse2_text","reuse2_calendar","reuse2_controls","reuse2_loader"},
            {"reuse3_detach","reuse3_memory","reuse3_text","reuse3_calendar","reuse3_controls","reuse3_loader"}};
        const char *event_keys[]={"reuse1_android_events","reuse2_android_events","reuse3_android_events"};
        batch_run(event_keys[round], android_events_selftest, events_ok, log);
        batch_run(keys[round][0], guest_detached_selftest, detached_ok, log);
        batch_run(keys[round][1], guest_memory_selftest, io_ok && data_memory_ok, log);
        batch_run(keys[round][2], guest_text_selftest, text_ok, log);
        batch_run(keys[round][3], guest_system_selftest, system_ok, log);
        batch_run(keys[round][4], swag_controls_selftest, controls_ok, log);
        batch_run(keys[round][5], fixture_batch, memory_ok && threads_ok && tls_ok && R_SUCCEEDED(rfs), log);
    }
    if (R_SUCCEEDED(rfs)) romfsExit();
    printf("\nNext: 20-second display/input test and a short tone.\nPress buttons and move both sticks during the display test.\n");
    consoleUpdate(NULL); svcSleepThread(1500000000);
    int platform_ok = batch_run("graphics_audio_input", runtime_platform_selftest, 1, log);
    batch_summary(log);
    fprintf(log, "RUNTIME_V10_DONE detached=%d data_memory=%d text=%d system=%d jni_profile=%d game_running=0\n", detached_ok, data_memory_ok, text_ok, system_ok, jni_profile_ok);
    fprintf(log, "BATCH_DONE compat_memory=%d threads=%d tls=%d file_io=%d zlib=%d bootstrap=%d platform=%d; game_running=0\n",
        memory_ok, threads_ok, tls_ok, io_ok, zlib_ok, bootstrap_ok, platform_ok);
    __atomic_store_n(&batch_stop, 1, __ATOMIC_RELEASE);
    if (watcher_running) { threadWaitForExit(&batch_watcher); threadClose(&batch_watcher); }
    if (log != stdout) fclose(log);
    printf("Batch tests complete. Platform: %s\nLog: /switch/swag-switch.log\nPress + to exit.\n", platform_ok ? "PASS" : "CHECK LOG");
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
