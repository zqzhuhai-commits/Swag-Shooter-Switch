#include "guest_threads.h"
#include "android_tls.h"
#include <switch.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

/* Public libnx Thread is the first member. Native pthread_self returns that
 * public Thread pointer; join/exit never use the SDK's private pthread record. */
typedef struct GuestThread {
    Thread native;
    void *(*start)(void *);
    void *arg, *result;
    AndroidTlsBlock *tls;
    unsigned references, detached, claimed, running;
    struct GuestThread *next;
} GuestThread;
static Mutex thread_lock;
static GuestThread *threads;
static _Thread_local GuestThread *current;
static Thread reaper;
static int reaper_running;
static unsigned reaped_count;
static GuestThread *find(uintptr_t id) {
    for (GuestThread *p = threads; p; p = p->next) if ((uintptr_t)p == id) return p;
    return NULL;
}
int guest_thread_getattr(uintptr_t id, AndroidThreadAttr *attr) {
    if (!attr) return 22;
    mutexLock(&thread_lock);
    GuestThread *guest=find(id);
    Thread *native=guest ? &guest->native : id==(uintptr_t)pthread_self() ? (Thread *)id : NULL;
    if (!native) { mutexUnlock(&thread_lock); return 3; }
    memset(attr,0,sizeof *attr);
    attr->flags=guest && guest->detached ? 1 : 0;
    attr->stack_base=native->stack_mirror ? native->stack_mirror : native->stack_mem;
    attr->stack_size=native->stack_sz;
    mutexUnlock(&thread_lock);
    return attr->stack_base && attr->stack_size ? 0 : 95;
}
static void release_locked(GuestThread *p) { if (!--p->references) free(p); }
static int collect(GuestThread *p, void **result) {
    Result rc = threadWaitForExit(&p->native);
    if (R_FAILED(rc)) return 5;
    if (result) *result = p->result;
    rc = threadClose(&p->native);
    if (R_FAILED(rc)) return 5;
    android_tls_discard(p->tls);
    mutexLock(&thread_lock);
    GuestThread **link = &threads;
    while (*link && *link != p) link = &(*link)->next;
    if (*link) *link = p->next;
    if (p->detached) ++reaped_count;
    release_locked(p);
    mutexUnlock(&thread_lock);
    return 0;
}
static void reaper_entry(void *arg) {
    (void)arg;
    for (;;) {
        GuestThread *ready = NULL;
        mutexLock(&thread_lock);
        for (GuestThread *p = threads; p; p = p->next) {
            if (p->detached && !p->claimed && p->running && R_SUCCEEDED(waitSingle(waiterForThread(&p->native), 0))) {
                p->claimed = 1; ready = p; break;
            }
        }
        mutexUnlock(&thread_lock);
        if (ready) {
            if (collect(ready, NULL)) {
                /* Preserve the record on kernel failure; never free a stack
                 * whose thread may still be active. The drain test reports it. */
                mutexLock(&thread_lock); ready->claimed = 0; mutexUnlock(&thread_lock);
                svcSleepThread(10000000);
            }
        } else svcSleepThread(10000000);
    }
}
static int ensure_reaper(void) {
    mutexLock(&thread_lock);
    int error = 0;
    if (!reaper_running) {
        Result rc = threadCreate(&reaper, reaper_entry, NULL, NULL, 0x10000, 0x3b, -2);
        if (R_FAILED(rc)) error = 11;
        else {
            rc = threadStart(&reaper);
            if (R_FAILED(rc)) { threadClose(&reaper); error = 11; }
            else reaper_running = 1;
        }
    }
    mutexUnlock(&thread_lock);
    return error;
}
void guest_thread_exit(void *result) {
    if (!current) { fprintf(stderr, "GUEST_THREAD_EXIT outside a guest worker\n"); abort(); }
    current->result = result;
    threadExit();
}
static void entry(void *arg) {
    GuestThread *p = arg;
    current = p;
    if (android_tls_bind(p->tls)) guest_thread_exit(NULL);
    guest_thread_exit(p->start(p->arg));
}
int guest_thread_create(uintptr_t *id, const AndroidThreadAttr *attr, void *(*start)(void *), void *arg) {
    if (!id || !start) return 22;
    size_t size = attr ? attr->stack_size : 1024 * 1024;
    unsigned detached = attr ? attr->flags & 1 : 0;
    if (attr && ((attr->flags & ~1u) || attr->stack_base || attr->sched_policy || attr->sched_priority ||
                 (attr->guard_size != 0 && attr->guard_size != 4096))) return 95;
    if (size < 16384 || size > INT_MAX - 4095) return 22;
    if (detached) { int e = ensure_reaper(); if (e) return e; }
    GuestThread *p = calloc(1, sizeof *p);
    if (!p) return 12;
    p->tls = android_tls_allocate();
    if (!p->tls) { free(p); return 12; }
    p->start = start; p->arg = arg; p->detached = detached; p->references = 2;
    Result rc = threadCreate(&p->native, entry, p, NULL, (size + 4095) & ~(size_t)4095, 0x3b, -2);
    if (R_FAILED(rc)) { android_tls_discard(p->tls); free(p); return 11; }
    android_tls_record_owner(p->tls, (uintptr_t)p);
    mutexLock(&thread_lock); p->next = threads; threads = p; mutexUnlock(&thread_lock);
    rc = threadStart(&p->native);
    if (R_FAILED(rc)) {
        mutexLock(&thread_lock);
        GuestThread **link = &threads; while (*link != p) link = &(*link)->next;
        *link = p->next; mutexUnlock(&thread_lock);
        threadClose(&p->native); android_tls_discard(p->tls); free(p); return 11;
    }
    mutexLock(&thread_lock);
    p->running = 1;
    *id = (uintptr_t)p;
    release_locked(p); /* Creating thread reference; registry owns the other. */
    mutexUnlock(&thread_lock);
    return 0;
}
int guest_thread_join(uintptr_t id, void **result) {
    if (id == (uintptr_t)pthread_self()) return 35;
    mutexLock(&thread_lock);
    GuestThread *p = find(id);
    if (!p) { mutexUnlock(&thread_lock); return 3; }
    if (p->detached || p->claimed) { mutexUnlock(&thread_lock); return 22; }
    p->claimed = 1;
    mutexUnlock(&thread_lock);
    int e = collect(p, result);
    if (e) { mutexLock(&thread_lock); p->claimed = 0; mutexUnlock(&thread_lock); }
    return e;
}
int guest_thread_detach(uintptr_t id) {
    int e = ensure_reaper(); if (e) return e;
    mutexLock(&thread_lock);
    GuestThread *p = find(id);
    if (!p) e = 3;
    else if (p->detached || p->claimed) e = 22;
    else p->detached = 1;
    mutexUnlock(&thread_lock);
    return e;
}
int guest_threads_drain(FILE *log) {
    for (unsigned i = 0; i < 300; ++i) {
        unsigned live = 0;
        mutexLock(&thread_lock);
        for (GuestThread *p = threads; p; p = p->next) if (p->detached) ++live;
        unsigned reaped = reaped_count;
        mutexUnlock(&thread_lock);
        if (!live) { fprintf(log, "DETACHED_DRAIN_PASS reaped=%u\n", reaped); return 0; }
        svcSleepThread(10000000);
    }
    fprintf(log, "DETACHED_DRAIN_FAIL workers still active\n"); return -1;
}
static struct {
    AndroidSem gate, ready, done;
    int key;
    unsigned errors, destructors;
} detached_test;
typedef int (*SemOp)(void *);
static void detached_destructor(void *value) {
    if (!value || !android_tls_current()) __atomic_add_fetch(&detached_test.errors, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&detached_test.destructors, 1, __ATOMIC_RELAXED);
}
static void *detached_worker(void *arg) {
    uintptr_t index = (uintptr_t)arg;
    AndroidThreadAttr observed;
    int attr_rc=guest_thread_getattr((uintptr_t)pthread_self(),&observed);
    uintptr_t local=(uintptr_t)&observed, low=(uintptr_t)observed.stack_base;
    if(attr_rc || local<low || local-low>=observed.stack_size)
        __atomic_add_fetch(&detached_test.errors,1,__ATOMIC_RELAXED);
    int (*set)(int, const void *) = (int (*)(int, const void *))bionic_threads_resolve("pthread_setspecific");
    if (set(detached_test.key, (void *)(index + 1))) __atomic_add_fetch(&detached_test.errors, 1, __ATOMIC_RELAXED);
    if (index == 2 && guest_thread_detach((uintptr_t)pthread_self())) __atomic_add_fetch(&detached_test.errors, 1, __ATOMIC_RELAXED);
    ((SemOp)bionic_threads_resolve("sem_post"))(&detached_test.ready);
    ((SemOp)bionic_threads_resolve("sem_wait"))(&detached_test.gate);
    ((SemOp)bionic_threads_resolve("sem_post"))(&detached_test.done);
    if (index == 3) guest_thread_exit(arg);
    return arg;
}
int guest_detached_selftest(FILE *log) {
    memset(&detached_test, 0, sizeof detached_test);
    int (*sem_init)(void *, int, unsigned) = (int (*)(void *, int, unsigned))bionic_threads_resolve("sem_init");
    int (*key_create)(int *, void (*)(void *)) = (int (*)(int *, void (*)(void *)))bionic_threads_resolve("pthread_key_create");
    if (sem_init(&detached_test.gate, 0, 0) || sem_init(&detached_test.ready, 0, 0) || sem_init(&detached_test.done, 0, 0) ||
        key_create(&detached_test.key, detached_destructor)) return -1;
    size_t before = android_tls_live_blocks();
    uintptr_t ids[4] = {0};
    unsigned created = 0;
    for (uintptr_t i = 0; i < 4; ++i) {
        AndroidThreadAttr attr = {.stack_size = 256 * 1024, .guard_size = 4096, .flags = i == 0 || i == 3 ? 1 : 0};
        int e = guest_thread_create(&ids[i], &attr, detached_worker, (void *)i);
        if (e) { ++detached_test.errors; break; }
        ++created;
        if (i == 1 && guest_thread_detach(ids[i])) ++detached_test.errors;
    }
    for (unsigned i = 0; i < created; ++i) ((SemOp)bionic_threads_resolve("sem_wait"))(&detached_test.ready);
    for (unsigned i = 0; i < created; ++i) {
        mutexLock(&thread_lock);
        GuestThread *p = find(ids[i]);
        if (p && !p->detached) { ++detached_test.errors; p->detached = 1; }
        mutexUnlock(&thread_lock);
        if (guest_thread_join(ids[i], NULL) != 22 || guest_thread_detach(ids[i]) != 22) ++detached_test.errors;
    }
    for (unsigned i = 0; i < created; ++i) ((SemOp)bionic_threads_resolve("sem_post"))(&detached_test.gate);
    for (unsigned i = 0; i < created; ++i) ((SemOp)bionic_threads_resolve("sem_wait"))(&detached_test.done);
    if (guest_threads_drain(log)) ++detached_test.errors;
    if (created != 4 || detached_test.destructors != created || android_tls_live_blocks() != before) ++detached_test.errors;
    ((SemOp)bionic_threads_resolve("sem_destroy"))(&detached_test.gate);
    ((SemOp)bionic_threads_resolve("sem_destroy"))(&detached_test.ready);
    ((SemOp)bionic_threads_resolve("sem_destroy"))(&detached_test.done);
    ((int (*)(int))bionic_threads_resolve("pthread_key_delete"))(detached_test.key);
    fprintf(log, "DETACHED_TEST_DONE errors=%u workers=%u destructors=%u live_blocks=%zu\n",
        detached_test.errors, created, detached_test.destructors, android_tls_live_blocks());
    return detached_test.errors ? -1 : 0;
}

Handle guest_thread_capture_handle(uintptr_t id) {
    Handle result=0;
    mutexLock(&thread_lock);
    GuestThread *thread=find(id);
    if(thread) result=thread->native.handle;
    mutexUnlock(&thread_lock); return result;
}
int guest_thread_gc_lock_free(void) { return __atomic_load_n(&thread_lock,__ATOMIC_ACQUIRE)==0; }

unsigned guest_thread_debug_snapshot(Handle *handles,uintptr_t *ids,unsigned capacity) {
    if(!mutexTryLock(&thread_lock)) return 0;
    unsigned count=0;
    for(GuestThread *p=threads;p && count<capacity;p=p->next) { handles[count]=p->native.handle; ids[count]=(uintptr_t)p; ++count; }
    mutexUnlock(&thread_lock); return count;
}
