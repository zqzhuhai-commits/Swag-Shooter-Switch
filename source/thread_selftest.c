#include "bionic_threads.h"
#include <switch.h>
#include <string.h>
#include <stdint.h>

typedef int (*One)(void *);
typedef int (*Init)(void *, const long *);
typedef int (*Attr)(long *);
typedef int (*AttrSet)(long *, int);
typedef int (*Wait)(void *, void *, const struct timespec *);
typedef int (*Create)(uintptr_t *, const AndroidThreadAttr *, void *(*)(void *), void *);
typedef int (*Join)(uintptr_t, void **);
typedef int (*Once)(void *, void (*)(void));
typedef int (*KeyCreate)(int *, void (*)(void *));
typedef int (*KeySet)(int, const void *);
typedef void *(*KeyGet)(int);
typedef int (*KeyDelete)(int);
typedef int (*SemInit)(void *, int, unsigned);
typedef int (*SemValue)(void *, int *);
#define API(name, type) ((type)bionic_threads_resolve(name))

/* Static storage survives diagnostic failures; watchdog ends a stuck process.
 * The 4-byte offset deliberately exercises Android's weaker alignment. */
static struct {
    uint32_t guard_before;
    AndroidMutex mutex;
    uint32_t guard_after;
    AndroidCond cond;
    AndroidRwlock rwlock;
    AndroidSem done;
    uint32_t once;
    int key, start, ready, counter, rw_counter;
    unsigned errors, once_calls, destructors, watchdog_stop;
    FILE *log;
} test;
static void once_callback(void) { __atomic_add_fetch(&test.once_calls, 1, __ATOMIC_RELAXED); }
static void key_destructor(void *value) {
    if ((uintptr_t)value < 1 || (uintptr_t)value > 4) __atomic_add_fetch(&test.errors, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&test.destructors, 1, __ATOMIC_RELAXED);
}
static void fail_if(int value) {
    if (value) __atomic_add_fetch(&test.errors, 1, __ATOMIC_RELAXED);
}
static struct timespec deadline(int clock, long milliseconds) {
    struct timespec t = {0};
    if (bionic_clock_gettime(clock, &t)) fail_if(1);
    t.tv_nsec += milliseconds * 1000000L;
    t.tv_sec += t.tv_nsec / 1000000000L;
    t.tv_nsec %= 1000000000L;
    return t;
}
static void *worker(void *argument) {
    uintptr_t id = (uintptr_t)argument;
    One lock = API("pthread_mutex_lock", One), unlock = API("pthread_mutex_unlock", One);
    One signal = API("pthread_cond_broadcast", One);
    Wait wait = API("pthread_cond_timedwait", Wait);
    fail_if(API("pthread_setspecific", KeySet)(test.key, argument));
    *bionic_errno_ptr() = 100 + (int)id;
    fail_if(lock(&test.mutex));
    ++test.ready;
    fail_if(signal(&test.cond));
    while (!test.start) {
        struct timespec until = deadline(1, 2000);
        if (wait(&test.cond, &test.mutex, &until)) { fail_if(1); break; }
    }
    fail_if(unlock(&test.mutex));
    fail_if(API("pthread_once", Once)(&test.once, once_callback));
    for (unsigned i = 0; i < 2000; ++i) {
        fail_if(lock(&test.mutex));
        ++test.counter;
        fail_if(unlock(&test.mutex));
        if (!(i % 128)) svcSleepThread(-1);
    }
    for (unsigned i = 0; i < 100; ++i) {
        fail_if(API("pthread_rwlock_wrlock", One)(&test.rwlock));
        ++test.rw_counter;
        fail_if(API("pthread_rwlock_unlock", One)(&test.rwlock));
        fail_if(API("pthread_rwlock_rdlock", One)(&test.rwlock));
        fail_if(test.rw_counter < 1 || test.rw_counter > 400);
        fail_if(API("pthread_rwlock_unlock", One)(&test.rwlock));
    }
    fail_if(API("pthread_getspecific", KeyGet)(test.key) != argument);
    fail_if(*bionic_errno_ptr() != 100 + (int)id);
    fail_if(API("sem_post", One)(&test.done));
    if (id == 4) {
        void (*exit_thread)(void *) = (void (*)(void *))bionic_threads_resolve("pthread_exit");
        exit_thread(argument);
    }
    return argument;
}
static void watchdog(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < 150; ++i) {
        if (__atomic_load_n(&test.watchdog_stop, __ATOMIC_ACQUIRE)) return;
        svcSleepThread(100000000);
    }
    fprintf(test.log, "THREADS_WATCHDOG_ABORT: test exceeded 15 seconds\n");
    fflush(test.log);
    svcExitProcess();
}
static void scalar_tests(FILE *log) {
    One lock = API("pthread_mutex_lock", One), unlock = API("pthread_mutex_unlock", One);
    One destroy_mutex = API("pthread_mutex_destroy", One);
    One destroy_cond = API("pthread_cond_destroy", One);
    Init init_mutex = API("pthread_mutex_init", Init), init_cond = API("pthread_cond_init", Init);
    struct { uint32_t before; AndroidMutex value; uint32_t after; } rec = {0};
    rec.before = 0x12345678; rec.after = 0x87654321;
    /* Static recursive initializer and explicit error-check attributes. */
    rec.value.words[0] = 0x4000;
    fail_if(lock(&rec.value)); fail_if(lock(&rec.value));
    fail_if(destroy_mutex(&rec.value) != 16);
    fail_if(unlock(&rec.value)); fail_if(unlock(&rec.value));
    fail_if(destroy_mutex(&rec.value));
    fail_if(rec.before != 0x12345678 || rec.after != 0x87654321);
    long attr;
    fail_if(API("pthread_mutexattr_init", Attr)(&attr));
    fail_if(API("pthread_mutexattr_settype", AttrSet)(&attr, 2));
    fail_if(init_mutex(&rec.value, &attr));
    fail_if(lock(&rec.value));
    fail_if(lock(&rec.value) != 35);
    fail_if(API("pthread_mutex_trylock", One)(&rec.value) != 16);
    fail_if(unlock(&rec.value)); fail_if(unlock(&rec.value) != 1);
    fail_if(destroy_mutex(&rec.value));
    fail_if(API("pthread_mutexattr_destroy", Attr)(&attr));
    for (int clock = 0; clock <= 1; ++clock) {
        AndroidCond cond = {0}; AndroidMutex mutex = {0};
        fail_if(API("pthread_condattr_init", Attr)(&attr));
        fail_if(API("pthread_condattr_setclock", AttrSet)(&attr, clock));
        fail_if(init_cond(&cond, &attr));
        fail_if(lock(&mutex));
        struct timespec until = deadline(clock, 20);
        struct timespec before, after;
        fail_if(bionic_clock_gettime(1, &before));
        int rc = API("pthread_cond_timedwait", Wait)(&cond, &mutex, &until);
        fail_if(bionic_clock_gettime(1, &after));
        int64_t elapsed = (after.tv_sec - before.tv_sec) * 1000000000LL + after.tv_nsec - before.tv_nsec;
        fprintf(log, "THREADS_TIMEOUT clock=%d result=%d elapsed_ns=%lld\n", clock, rc, (long long)elapsed);
        fail_if(rc != 110 || elapsed < 5000000 || elapsed > 1000000000);
        fail_if(API("pthread_mutex_trylock", One)(&mutex) != 16);
        fail_if(unlock(&mutex)); fail_if(destroy_mutex(&mutex)); fail_if(destroy_cond(&cond));
        fail_if(API("pthread_condattr_destroy", Attr)(&attr));
    }
    struct timespec ignored;
    fail_if(bionic_clock_gettime(INT32_MAX, &ignored) != -1 || *bionic_errno_ptr() != 22);
    fprintf(log, "THREADS_SCALAR_DONE errors=%u\n", __atomic_load_n(&test.errors, __ATOMIC_RELAXED));
}
int bionic_threads_selftest(FILE *log) {
    memset(&test, 0, sizeof test);
    test.log = log;
    test.guard_before = 0x11223344; test.guard_after = 0x55667788;
    /* Monotonic static initializer; this uses Android's flag bit, not clock ID. */
    test.cond.words[0] = 2;
    fprintf(log, "THREADS_BEGIN bindings=%zu watchdog=15s\n", bionic_threads_count());
    Thread watcher;
    Result rc = threadCreate(&watcher, watchdog, NULL, NULL, 0x8000, 0x2c, -2);
    if (R_FAILED(rc)) { fprintf(log, "THREADS_FAIL watchdog-create=%08x\n", rc); return -1; }
    rc = threadStart(&watcher);
    if (R_FAILED(rc)) {
        threadClose(&watcher);
        fprintf(log, "THREADS_FAIL watchdog-start=%08x\n", rc); return -1;
    }
    scalar_tests(log);
    fail_if(API("pthread_key_create", KeyCreate)(&test.key, key_destructor));
    fail_if(API("sem_init", SemInit)(&test.done, 0, 0));
    fail_if(API("sem_trywait", One)(&test.done) != -1 || *bionic_errno_ptr() != 11);
    AndroidThreadAttr attr;
    int (*init_attr)(AndroidThreadAttr *) = (int (*)(AndroidThreadAttr *))bionic_threads_resolve("pthread_attr_init");
    int (*stacksize)(AndroidThreadAttr *, size_t) = (int (*)(AndroidThreadAttr *, size_t))bionic_threads_resolve("pthread_attr_setstacksize");
    fail_if(init_attr(&attr)); fail_if(stacksize(&attr, 256 * 1024));
    uintptr_t ids[4] = {0};
    unsigned created = 0;
    for (unsigned i = 0; i < 4; ++i) {
        int error = API("pthread_create", Create)(&ids[i], &attr, worker, (void *)(uintptr_t)(i + 1));
        fprintf(log, "THREADS_CREATE index=%u result=%d\n", i, error);
        if (error) { fail_if(1); break; }
        ++created;
    }
    One lock = API("pthread_mutex_lock", One), unlock = API("pthread_mutex_unlock", One);
    fail_if(lock(&test.mutex));
    while ((unsigned)test.ready < created) {
        struct timespec until = deadline(1, 2000);
        if (API("pthread_cond_timedwait", Wait)(&test.cond, &test.mutex, &until)) { fail_if(1); break; }
    }
    test.start = 1;
    fail_if(API("pthread_cond_broadcast", One)(&test.cond));
    fail_if(unlock(&test.mutex));
    *bionic_errno_ptr() = 777;
    for (unsigned i = 0; i < created; ++i) {
        fail_if(API("sem_wait", One)(&test.done));
        void *value = NULL;
        int error = API("pthread_join", Join)(ids[i], &value);
        fail_if(error || value != (void *)(uintptr_t)(i + 1));
        fprintf(log, "THREADS_JOIN index=%u result=%d value=%p\n", i, error, value);
    }
    int sem_value = -1;
    fail_if(API("sem_getvalue", SemValue)(&test.done, &sem_value) || sem_value);
    fail_if(*bionic_errno_ptr() != 777 || API("pthread_getspecific", KeyGet)(test.key) != NULL);
    fail_if(test.counter != (int)created * 2000 || test.rw_counter != (int)created * 100);
    fail_if(test.once_calls != 1 || test.destructors != created || created != 4);
    fail_if(test.guard_before != 0x11223344 || test.guard_after != 0x55667788);
    fail_if(API("pthread_mutex_destroy", One)(&test.mutex));
    fail_if(API("pthread_cond_destroy", One)(&test.cond));
    fail_if(API("pthread_rwlock_destroy", One)(&test.rwlock));
    fail_if(API("sem_destroy", One)(&test.done));
    fail_if(API("pthread_key_delete", KeyDelete)(test.key));
    fprintf(log, "THREADS_DONE errors=%u increments=%d rw_writes=%d once=%u destructors=%u\n",
            test.errors, test.counter, test.rw_counter, test.once_calls, test.destructors);
    __atomic_store_n(&test.watchdog_stop, 1, __ATOMIC_RELEASE);
    threadWaitForExit(&watcher); threadClose(&watcher);
    return test.errors ? -1 : 0;
}
