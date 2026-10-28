#include "android_tls.h"
#include "bionic_threads.h"
#include "nx_memory.h"
#include <switch.h>
#include <string.h>
#include <stdint.h>

typedef struct { uint64_t x[31], nzcv; unsigned char q[32][16]; } Snapshot;
_Static_assert(sizeof(Snapshot) == 768, "Assembly TLS capture layout");
static struct {
    uintptr_t readers[31];
    void (*capture)(Snapshot *);
    int (*guarded)(int);
    uintptr_t (*discard)(void);
    uintptr_t blocks[4];
    unsigned errors, watchdog_stop, destructors;
    int key;
    FILE *log;
} test;
static void error_if(int condition) {
    if (condition) __atomic_add_fetch(&test.errors, 1, __ATOMIC_RELAXED);
}
static void check_current(void) {
    void *native = armGetTls();
    void *shadow = android_tls_current();
    error_if(!shadow || shadow == native);
    if (!shadow) return;
    uintptr_t *slots = shadow;
    error_if(slots[0] != (uintptr_t)shadow || !slots[5] || (slots[5] & 0xff));
    for (size_t i = 0; i < 31; ++i) {
        uintptr_t (*read)(void) = (uintptr_t (*)(void))test.readers[i];
        error_if(read() != (uintptr_t)shadow);
    }
    Snapshot snapshot;
    memset(&snapshot, 0, sizeof snapshot);
    test.capture(&snapshot);
    for (size_t i = 0; i < 31; ++i)
        error_if(snapshot.x[i] != (i == 9 ? (uintptr_t)shadow : 0x100 + i));
    error_if(snapshot.nzcv != UINT64_C(0x60000000));
    for (size_t i = 0; i < 32; ++i)
        for (size_t n = 0; n < 16; ++n) error_if(snapshot.q[i][n] != i + 1);
    for (unsigned i = 0; i < 100; ++i) error_if(test.guarded(20) != 41);
    error_if(test.discard() != 0);
    error_if(armGetTls() != native);
}
static void destructor(void *value) {
    error_if((uintptr_t)value < 1 || (uintptr_t)value > 4);
    check_current();
    __atomic_add_fetch(&test.destructors, 1, __ATOMIC_RELAXED);
}
static void *tls_worker(void *argument) {
    size_t i = (uintptr_t)argument;
    test.blocks[i] = (uintptr_t)android_tls_current();
    int (*set)(int, const void *) = (int (*)(int, const void *))bionic_threads_resolve("pthread_setspecific");
    error_if(set(test.key, (void *)(i + 1)));
    check_current();
    return argument;
}
static void watchdog(void *arg) {
    (void)arg;
    for (unsigned i = 0; i < 150; ++i) {
        if (__atomic_load_n(&test.watchdog_stop, __ATOMIC_ACQUIRE)) return;
        svcSleepThread(100000000);
    }
    fprintf(test.log, "TLS_WATCHDOG_ABORT: fixture exceeded 15 seconds\n");
    fflush(test.log);
    svcExitProcess();
}
static uintptr_t fixture_resolve(void *ctx, const char *name) {
    (void)ctx; return android_tls_resolve(name);
}
int android_tls_selftest(FILE *log, int execute) {
    memset(&test, 0, sizeof test); test.log = log;
    fprintf(log, "TLS_TEST_BEGIN execute=%d\n", execute);
    const char *path = "romfs:/tests/tls-fixture.so";
    TlsPatchPlan plan = {0};
    AndroidElf elf;
    NxModuleMemory mem = {0};
    size_t image_size;
    size_t blocks_before = android_tls_live_blocks();
    Thread watcher;
    int watcher_started = 0, key_created = 0, result = -1;
    if (android_tls_plan(path, &plan, log) || !plan.count ||
        elf_image_size(path, &image_size, log) ||
        nx_memory_prepare(&mem, image_size + plan.extra_bytes, log) ||
        elf_load(&elf, path, mem.staging, mem.size, (uintptr_t)mem.execution, log) ||
        elf_relocate(&elf, fixture_resolve, NULL, log)) goto done;
    /* A changed instruction must reject the entire plan before any patch. */
    uint32_t original, invalid = 0xd503201f;
    memcpy(&original, elf.image + plan.sites[0].va, 4);
    memcpy(elf.image + plan.sites[0].va, &invalid, 4);
    int rejected = android_tls_apply(&elf, mem.size, &plan, log) != 0;
    memcpy(elf.image + plan.sites[0].va, &original, 4);
    if (!rejected) goto done;
    fprintf(log, "TLS_CHANGED_INSTRUCTION_REJECTION_PASS\n");
    if (android_tls_apply(&elf, mem.size, &plan, log)) goto done;
    for (size_t i = 0; i < 31; ++i) {
        char name[32]; snprintf(name, sizeof name, "tls_read_%zu", i);
        test.readers[i] = elf_export(&elf, name);
        if (!test.readers[i]) goto done;
    }
    test.capture = (void (*)(Snapshot *))elf_export(&elf, "tls_capture");
    test.guarded = (int (*)(int))elf_export(&elf, "tls_guarded");
    test.discard = (uintptr_t (*)(void))elf_export(&elf, "tls_read_discard");
    if (!test.capture || !test.guarded || !test.discard) goto done;
    if (!execute) { fprintf(log, "TLS_TEST_SKIPPED: executable memory unavailable\n"); goto done; }
    if (nx_memory_publish(&mem, &elf, log)) goto done;
    Result rc = threadCreate(&watcher, watchdog, NULL, NULL, 0x8000, 0x2c, -2);
    if (R_FAILED(rc)) goto done;
    rc = threadStart(&watcher);
    if (R_FAILED(rc)) { threadClose(&watcher); goto done; }
    watcher_started = 1;
    int (*key_create)(int *, void (*)(void *)) = (int (*)(int *, void (*)(void *)))bionic_threads_resolve("pthread_key_create");
    if (key_create(&test.key, destructor)) goto done;
    key_created = 1;
    check_current();
    fprintf(log, "TLS_MAIN_CHECK errors=%u\n", test.errors);
    typedef int (*Create)(uintptr_t *, const AndroidThreadAttr *, void *(*)(void *), void *);
    typedef int (*Join)(uintptr_t, void **);
    Create create = (Create)bionic_threads_resolve("pthread_create");
    Join join = (Join)bionic_threads_resolve("pthread_join");
    uintptr_t ids[4] = {0};
    size_t created = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (create(&ids[i], NULL, tls_worker, (void *)i)) { error_if(1); break; }
        ++created;
    }
    /* All four shadows remain alive until join, so allocator reuse cannot
     * make an otherwise correct isolation test appear to share addresses. */
    for (size_t i = 0; i < created; ++i) {
        void *value = NULL;
        error_if(join(ids[i], &value) || value != (void *)i);
    }
    for (size_t i = 0; i < created; ++i) {
        error_if(!test.blocks[i] || test.blocks[i] == (uintptr_t)android_tls_current());
        for (size_t j = 0; j < i; ++j) error_if(test.blocks[i] == test.blocks[j]);
    }
    error_if(created != 4);
    error_if(test.destructors != created);
    error_if(android_tls_live_blocks() != blocks_before);
    fprintf(log, "TLS_TEST_DONE errors=%u registers=31 simd=32 flags=CHECKED workers=%zu destructors=%u guard_calls=%zu live_blocks=%zu\n",
            test.errors, created, test.destructors, (created + test.destructors + 1) * 100,
            android_tls_live_blocks());
    result = test.errors ? -1 : 0;
done:
    if (key_created) {
        int (*key_delete)(int) = (int (*)(int))bionic_threads_resolve("pthread_key_delete");
        if (key_delete(test.key)) result = -1;
    }
    if (watcher_started) {
        __atomic_store_n(&test.watchdog_stop, 1, __ATOMIC_RELEASE);
        threadWaitForExit(&watcher); threadClose(&watcher);
    }
    if (nx_memory_close(&mem, log)) result = -1;
    android_tls_plan_free(&plan);
    if (result) fprintf(log, "TLS_TEST_FAIL\n");
    return result;
}
