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

/* Foreign modules are inventoried and patched, but no constructors/JNI/game
 * code run until the remaining imports and lifecycle semantics are ready. */
typedef struct { AndroidElf modules[3]; size_t count; } ModuleSet;
static unsigned batch_stop;
static void batch_watchdog(void *arg) {
    FILE *log = arg;
    for (unsigned i = 0; i < 1200; ++i) {
        if (__atomic_load_n(&batch_stop, __ATOMIC_ACQUIRE)) return;
        svcSleepThread(100000000);
    }
    fprintf(log, "BATCH_WATCHDOG_ABORT after=120s; retain preceding phase markers\n");
    fflush(log); svcExitProcess();
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
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    consoleInit(NULL);
    printf("SWAG Switch - batch compatibility bring-up 0.10\n");
    printf("This build probes the Android loader; gameplay is not started.\n\n");
    FILE *log = fopen("sdmc:/switch/swag-switch.log", "w");
    if (!log) log = stdout;
    setvbuf(log, NULL, _IOLBF, 0);
    Thread batch_watcher;
    int watcher_running = 0;
    Result watcher_rc = threadCreate(&batch_watcher, batch_watchdog, log, NULL, 0x8000, 0x2c, -2);
    if (R_SUCCEEDED(watcher_rc)) {
        watcher_rc = threadStart(&batch_watcher);
        if (R_SUCCEEDED(watcher_rc)) watcher_running = 1;
        else threadClose(&batch_watcher);
    }
    fprintf(log, "SWAG-NX stage12-lifecycle; applet=%u own-process=%x\n",
            (unsigned)appletGetAppletType(), envGetOwnProcessHandle());
    runtime_services_init(log); startup_log(log);
    guest_system_set_log(log); unity_host_set_log(log);
    int memory_ok = batch_run("exec_memory", nx_memory_smoke_test, 1, log);
    printf("Executable memory: %s\n", memory_ok ? "PASS" : "FAIL (see log)");
    int basic_ok = bionic_basic_selftest() == 0;
    fprintf(log, "Basic ABI bindings=%zu selftest=%s\n", bionic_basic_count(), basic_ok ? "PASS" : "FAIL");
    batch_record("basic_abi", basic_ok, log);
    int tls_init_ok = batch_run("tls_initialize", android_tls_initialize, 1, log);
    int threads_ok = batch_run("thread_sync", bionic_threads_selftest, tls_init_ok, log);
    printf("Thread/sync bridge: %s\n", threads_ok ? "PASS" : "FAIL (see log)");
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
    int io_ok = batch_run("file_io", runtime_io_selftest, R_SUCCEEDED(rfs), log);
    int zlib_ok = batch_run("decompression", runtime_zlib_selftest, 1, log);
    int data_memory_ok = batch_run("data_memory", guest_memory_selftest, io_ok, log);
    int text_ok = batch_run("utf8_text", guest_text_selftest, 1, log);
    int system_ok = batch_run("system_calendar", guest_system_selftest, 1, log);
    int events_ok = batch_run("android_events", android_events_selftest, 1, log);
    int controls_ok = batch_run("controls_matrix", swag_controls_selftest, 1, log);
    printf("File bridge: %s; decompression: %s\n", io_ok ? "PASS" : "FAIL", zlib_ok ? "PASS" : "FAIL");
    int tls_ok = 0;
    if (!failed) {
        int fixture_ok = batch_run("loader_fixture", fixture_batch, memory_ok, log);
        printf("ELF fixture: %s\n", fixture_ok ? "PASS" : "FAIL");
        if (!fixture_ok || !basic_ok || !threads_ok) failed = 1;
        if (!failed) {
            tls_ok = android_tls_selftest(log, memory_ok) == 0;
            batch_record("tls_execution", tls_ok, log);
            printf("Android TLS bridge: %s\n", tls_ok ? "PASS" : "FAIL (see log)");
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
        ++set.count;
        printf("Loaded %s (%zu KiB)\n", names[i], size / 1024);
        consoleUpdate(NULL);
    }
    int bootstrap_ok = 0, jni_profile_ok = 0, bootstrap_attempted = 0;
    for (size_t i = 0; i < set.count; ++i) {
        int linked = elf_relocate(&set.modules[i], resolve_module, &set, log) == 0;
        int patched = android_tls_apply(&set.modules[i], memory[i].size, &plans[i], log) == 0;
        if (!patched) { fprintf(log, "TLS_GAME_PATCH_FAIL %s\n", names[i]); failed = 1; }
        printf("%s: %zu imports unresolved; %zu unsupported relocations\n", names[i],
                set.modules[i].unresolved, set.modules[i].unsupported);
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
        for(size_t i=0;i<3;++i) {
            if(!set.modules[i].published && nx_memory_publish(&memory[i],&set.modules[i],log)) svcExitProcess();
        }
        for(size_t i=0;i<3;++i) { runtime_module_remove(&set.modules[i]); runtime_module_register(names[i],&set.modules[i],1); }
        for(size_t i=0;i<2;++i) {
            fprintf(log,"ENGINE_CONSTRUCTORS_BEGIN module=%s\n",names[i]); fflush(log);
            if(elf_run_initializers(&set.modules[i],log)) svcExitProcess();
        }
        void *vm,*env; unity_host_get_vm(&vm,&env); (void)env;
        fprintf(log,"ENGINE_UNITY_ONLOAD_BEGIN\n"); fflush(log);
        int version=unity_host_call_onload(elf_export(&set.modules[0],"JNI_OnLoad"),vm,log);
        fprintf(log,"ENGINE_UNITY_ONLOAD_END version=%x\n",version); fflush(log);
        if(version==0x10006) {
            consoleExit(NULL);
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
