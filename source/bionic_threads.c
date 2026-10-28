#define _GNU_SOURCE
#include "bionic_threads.h"
#include "android_tls.h"
#include "guest_threads.h"
#include <switch.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* The SDK's newlib pthread/sem layouts and enum values are NOT Bionic ABI.
 * Sidecars keep native objects out of Android memory (which may be 4-aligned).
 * Registry refs cover blocking calls; held counts cover acquired locks. */
enum Kind { MUTEX, COND, RWLOCK, SEM, ONCE };
typedef struct Object {
    void *key;
    enum Kind kind;
    unsigned refs, held, type;
    pthread_t owner;
    union {
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        pthread_rwlock_t rwlock;
        sem_t sem;
        pthread_once_t once;
    } native;
    struct Object *next;
} Object;
static Mutex registry_lock;
static Object *objects;
static size_t object_count;
/* Basic whitelist calls (malloc/strtol/libm) already use native per-thread errno;
 * their possible errors ENOMEM/EINVAL/ERANGE/EDOM match Android. Sharing that
 * cell preserves those error paths; other adapters translate before returning. */
int *bionic_errno_ptr(void) { return &errno; }

int bionic_error(int e) {
    switch (e) {
    case 0: return 0;
    case EPERM: return 1; case ENOENT: return 2; case ESRCH: return 3;
    case EINTR: return 4; case EIO: return 5; case EBADF: return 9;
    case EAGAIN: return 11; case ENOMEM: return 12; case EACCES: return 13;
    case EBUSY: return 16; case EEXIST: return 17; case EINVAL: return 22;
    case EFAULT: return 14; case ENOTDIR: return 20; case EISDIR: return 21;
    case EMFILE: return 24; case EFBIG: return 27; case ENOSPC: return 28;
    case ESPIPE: return 29; case EROFS: return 30; case ENAMETOOLONG: return 36;
    case ENOTEMPTY: return 39;
    case ENOTSOCK: return 88; case EDESTADDRREQ: return 89; case EMSGSIZE: return 90;
    case EPROTOTYPE: return 91; case ENOPROTOOPT: return 92; case EPROTONOSUPPORT: return 93;
    case EAFNOSUPPORT: return 97; case EADDRINUSE: return 98; case EADDRNOTAVAIL: return 99;
    case ENETDOWN: return 100; case ENETUNREACH: return 101; case ECONNABORTED: return 103;
    case ECONNRESET: return 104; case ENOBUFS: return 105; case EISCONN: return 106;
    case ENOTCONN: return 107; case ECONNREFUSED: return 111; case EHOSTUNREACH: return 113;
    case EALREADY: return 114; case EINPROGRESS: return 115;
    case ERANGE: return 34; case EDEADLK: return 35; case ENOSYS: return 38;
    case EOVERFLOW: return 75; case ENOTSUP: return 95; case ETIMEDOUT: return 110;
    default: return 5; /* Never leak a different SDK errno numbering. */
    }
}
static int minus_one(int native_error) {
    *bionic_errno_ptr() = bionic_error(native_error); return -1;
}
static int native_clock(int android_clock, clockid_t *clock) {
    switch (android_clock) {
    case 0: *clock = CLOCK_REALTIME; return 0;
    case 1: *clock = CLOCK_MONOTONIC; return 0;
    /* This SDK does not advertise per-process/per-thread CPU clocks. */
    case 4: *clock = CLOCK_MONOTONIC_RAW; return 0;
    case 5: *clock = CLOCK_REALTIME_COARSE; return 0;
    case 6: *clock = CLOCK_MONOTONIC_COARSE; return 0;
    case 7: *clock = CLOCK_BOOTTIME; return 0;
    default: return EINVAL;
    }
}
_Static_assert(sizeof(struct timespec) == 16, "Android LP64 timespec layout");
int bionic_clock_gettime(int id, struct timespec *value) {
    clockid_t clock;
    if (!value || native_clock(id, &clock)) return minus_one(EINVAL);
    if (clock_gettime(clock, value)) return minus_one(errno);
    return 0;
}
static int b_clock_getres(int id, struct timespec *value) {
    clockid_t clock;
    if (native_clock(id, &clock)) return minus_one(EINVAL);
    if (clock_getres(clock, value)) return minus_one(errno);
    return 0;
}
static int b_nanosleep(const struct timespec *request, struct timespec *remaining) {
    if (!request || request->tv_sec < 0 || request->tv_nsec < 0 || request->tv_nsec >= 1000000000)
        return minus_one(EINVAL);
    if (nanosleep(request, remaining)) return minus_one(errno);
    return 0;
}
static int b_sched_yield(void) { svcSleepThread(-1); return 0; }

static uint32_t word(const void *raw) { uint32_t n; memcpy(&n, raw, 4); return n; }
static void setword(void *raw, uint32_t n) { memcpy(raw, &n, 4); }
static Object *find(void *key) {
    for (Object *n = objects; n; n = n->next) if (n->key == key) return n;
    return NULL;
}
/* Registry lock must be held. Never runs callbacks or waits on user locks. */
static Object *create(void *key, enum Kind kind, unsigned options, int *error) {
    *error = 0;
    if (object_count >= 8192) { *error = EAGAIN; return NULL; }
    Object *n = calloc(1, sizeof *n);
    if (!n) { *error = ENOMEM; return NULL; }
    n->key = key; n->kind = kind;
    switch (kind) {
    case MUTEX: {
        pthread_mutexattr_t attr;
        *error = pthread_mutexattr_init(&attr);
        if (!*error) {
            n->type = options;
            int native_type = options == 1 ? PTHREAD_MUTEX_RECURSIVE :
                              options == 2 ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL;
            *error = pthread_mutexattr_settype(&attr, native_type);
            if (!*error) *error = pthread_mutex_init(&n->native.mutex, &attr);
            pthread_mutexattr_destroy(&attr);
        }
        break;
    }
    case COND: {
        pthread_condattr_t attr;
        *error = pthread_condattr_init(&attr);
        if (!*error) {
            *error = pthread_condattr_setclock(&attr, options ? CLOCK_MONOTONIC : CLOCK_REALTIME);
            if (!*error) *error = pthread_cond_init(&n->native.cond, &attr);
            pthread_condattr_destroy(&attr);
        }
        break;
    }
    case RWLOCK: *error = pthread_rwlock_init(&n->native.rwlock, NULL); break;
    case SEM:
        if (sem_init(&n->native.sem, 0, options)) *error = errno;
        break;
    case ONCE: {
        pthread_once_t initial = PTHREAD_ONCE_INIT;
        n->native.once = initial;
        break;
    }
    }
    if (*error) { free(n); return NULL; }
    n->next = objects; objects = n; ++object_count;
    return n;
}
static Object *acquire(void *raw, enum Kind kind, int lazy, int *error) {
    *error = 0;
    if (!raw || (uintptr_t)raw % 4) { *error = EINVAL; return NULL; }
    mutexLock(&registry_lock);
    Object *n = find(raw);
    if (n && n->kind != kind) { *error = EINVAL; n = NULL; }
    else if (!n && lazy) {
        uint32_t initial = word(raw);
        unsigned options = 0;
        switch (kind) {
        case MUTEX:
            if (initial != 0 && initial != 0x4000 && initial != 0x8000) *error = EINVAL;
            else options = initial >> 14;
            break;
        case COND:
            if (initial & 1) *error = ENOTSUP;
            else if (initial != 0 && initial != 2) *error = EINVAL;
            else options = initial >> 1;
            break;
        case RWLOCK: case ONCE: if (initial) *error = EINVAL; break;
        case SEM: *error = EINVAL; break;
        }
        if (!*error) n = create(raw, kind, options, error);
    } else if (!n) *error = EINVAL;
    if (n) ++n->refs;
    mutexUnlock(&registry_lock);
    return n;
}
static void release(Object *n) {
    mutexLock(&registry_lock); --n->refs; mutexUnlock(&registry_lock);
}
static int initialize(void *raw, enum Kind kind, unsigned options, uint32_t initial) {
    if (!raw || (uintptr_t)raw % 4) return EINVAL;
    mutexLock(&registry_lock);
    int error = 0;
    if (find(raw)) error = EBUSY;
    else if (create(raw, kind, options, &error)) setword(raw, initial);
    mutexUnlock(&registry_lock);
    return error;
}
static int destroy(void *raw, enum Kind kind, int lazy) {
    int error;
    Object *n = acquire(raw, kind, lazy, &error);
    if (!n) return error;
    mutexLock(&registry_lock);
    if (n->refs != 1 || n->held) error = EBUSY;
    else {
        switch (kind) {
        case MUTEX: error = pthread_mutex_destroy(&n->native.mutex); break;
        case COND: error = pthread_cond_destroy(&n->native.cond); break;
        case RWLOCK: error = pthread_rwlock_destroy(&n->native.rwlock); break;
        case SEM: error = sem_destroy(&n->native.sem) ? errno : 0; break;
        case ONCE: error = EINVAL; break;
        }
        if (!error) {
            Object **link = &objects;
            while (*link != n) link = &(*link)->next;
            *link = n->next; --object_count;
            setword(raw, UINT32_MAX);
            free(n); n = NULL;
        }
    }
    if (n) --n->refs;
    mutexUnlock(&registry_lock);
    return error;
}

static int b_mutexattr_init(long *attr) { if (!attr) return 22; *attr = 0; return 0; }
static int b_attr_scalar_destroy(long *attr) { if (!attr) return 22; *attr = -1; return 0; }
static int b_mutexattr_settype(long *attr, int type) {
    if (!attr || *attr < 0 || type < 0 || type > 2) return 22;
    *attr = (*attr & ~15L) | type; return 0;
}
static int b_condattr_init(long *attr) { if (!attr) return 22; *attr = 0; return 0; }
static int b_condattr_setclock(long *attr, int clock) {
    if (!attr || *attr < 0 || *attr > 3 || (clock != 0 && clock != 1)) return 22;
    *attr = (*attr & ~2L) | (clock << 1); return 0;
}
static int b_mutex_init(void *raw, const long *attr) {
    long options = attr ? *attr : 0;
    if (options < 0 || (options & ~63L) || (options & 15) > 2) return 22;
    if (options & 48) return 95; /* process-shared or priority-inheritance unsupported */
    return bionic_error(initialize(raw, MUTEX, options & 15, (options & 15) << 14));
}
static int b_mutex_destroy(void *raw) { return bionic_error(destroy(raw, MUTEX, 1)); }
static int mutex_lock_impl(void *raw, int try_only) {
    int error;
    Object *n = acquire(raw, MUTEX, 1, &error);
    if (!n) return bionic_error(error);
    pthread_t self = pthread_self();
    mutexLock(&registry_lock);
    int same = n->held && pthread_equal(n->owner, self);
    if (same && n->type == 2) error = try_only ? EBUSY : EDEADLK;
    mutexUnlock(&registry_lock);
    if (!error) {
        error = try_only ? pthread_mutex_trylock(&n->native.mutex) : pthread_mutex_lock(&n->native.mutex);
        if (!error) {
            mutexLock(&registry_lock); n->owner = self; ++n->held; mutexUnlock(&registry_lock);
        }
    }
    release(n); return bionic_error(error);
}
static int b_mutex_lock(void *raw) { return mutex_lock_impl(raw, 0); }
static int b_mutex_trylock(void *raw) { return mutex_lock_impl(raw, 1); }
static int b_mutex_unlock(void *raw) {
    int error;
    Object *n = acquire(raw, MUTEX, 0, &error);
    if (!n) return bionic_error(error);
    mutexLock(&registry_lock);
    if (!n->held || !pthread_equal(n->owner, pthread_self())) error = EPERM;
    else {
        error = pthread_mutex_unlock(&n->native.mutex);
        if (!error && !--n->held) n->owner = (pthread_t)0;
    }
    mutexUnlock(&registry_lock);
    release(n); return bionic_error(error);
}
static int b_cond_init(void *raw, const long *attr) {
    long options = attr ? *attr : 0;
    if (options < 0 || options > 3) return 22;
    if (options & 1) return 95;
    return bionic_error(initialize(raw, COND, (options >> 1) & 1, options));
}
static int b_cond_destroy(void *raw) { return bionic_error(destroy(raw, COND, 1)); }
static int cond_wait_impl(void *cond, void *mutex, const struct timespec *until) {
    if (until && (until->tv_nsec < 0 || until->tv_nsec >= 1000000000)) return 22;
    int error;
    Object *c = acquire(cond, COND, 1, &error);
    if (!c) return bionic_error(error);
    Object *m = acquire(mutex, MUTEX, 1, &error);
    if (!m) { release(c); return bionic_error(error); }
    mutexLock(&registry_lock);
    if (m->held != 1 || !pthread_equal(m->owner, pthread_self())) error = EPERM;
    else { m->held = 0; m->owner = (pthread_t)0; }
    mutexUnlock(&registry_lock);
    if (!error) {
        error = until ? pthread_cond_timedwait(&c->native.cond, &m->native.mutex, until) :
                        pthread_cond_wait(&c->native.cond, &m->native.mutex);
        mutexLock(&registry_lock);
        m->held = 1; m->owner = pthread_self();
        mutexUnlock(&registry_lock);
    }
    release(m); release(c); return bionic_error(error);
}
static int b_cond_wait(void *cond, void *mutex) { return cond_wait_impl(cond, mutex, NULL); }
static int b_cond_timedwait(void *cond, void *mutex, const struct timespec *until) {
    if (!until) return 22;
    return cond_wait_impl(cond, mutex, until);
}
static int cond_wake(void *raw, int all) {
    int error;
    Object *n = acquire(raw, COND, 1, &error);
    if (!n) return bionic_error(error);
    error = all ? pthread_cond_broadcast(&n->native.cond) : pthread_cond_signal(&n->native.cond);
    release(n); return bionic_error(error);
}
static int b_cond_signal(void *raw) { return cond_wake(raw, 0); }
static int b_cond_broadcast(void *raw) { return cond_wake(raw, 1); }
static int b_rwlock_init(void *raw, const long *attr) {
    if (attr && *attr) return 95;
    return bionic_error(initialize(raw, RWLOCK, 0, 0));
}
static int b_rwlock_destroy(void *raw) { return bionic_error(destroy(raw, RWLOCK, 1)); }
static int rwlock_op(void *raw, int op) {
    int error;
    Object *n = acquire(raw, RWLOCK, 1, &error);
    if (!n) return bionic_error(error);
    if (op == 2) {
        mutexLock(&registry_lock);
        if (!n->held) error = EPERM;
        else {
            error = pthread_rwlock_unlock(&n->native.rwlock);
            if (!error) --n->held;
        }
        mutexUnlock(&registry_lock);
    } else {
        error = op == 0 ? pthread_rwlock_rdlock(&n->native.rwlock) : pthread_rwlock_wrlock(&n->native.rwlock);
        if (!error) { mutexLock(&registry_lock); ++n->held; mutexUnlock(&registry_lock); }
    }
    release(n); return bionic_error(error);
}
static int b_rwlock_rdlock(void *raw) { return rwlock_op(raw, 0); }
static int b_rwlock_wrlock(void *raw) { return rwlock_op(raw, 1); }
static int b_rwlock_unlock(void *raw) { return rwlock_op(raw, 2); }
static int b_sem_init(void *raw, int shared, unsigned value) {
    if (shared) return minus_one(ENOTSUP);
    if (value > INT_MAX) return minus_one(EINVAL);
    int error = initialize(raw, SEM, value, 0);
    return error ? minus_one(error) : 0;
}
static int b_sem_destroy(void *raw) {
    int error = destroy(raw, SEM, 0); return error ? minus_one(error) : 0;
}
static int sem_op(void *raw, int op, int *value) {
    int error;
    Object *n = acquire(raw, SEM, 0, &error);
    if (!n) return minus_one(error);
    int rc = op == 0 ? sem_wait(&n->native.sem) : op == 1 ? sem_post(&n->native.sem) :
             op == 2 ? sem_getvalue(&n->native.sem, value) : sem_trywait(&n->native.sem);
    error = rc ? errno : 0;
    release(n); return error ? minus_one(error) : 0;
}
static int b_sem_wait(void *raw) { return sem_op(raw, 0, NULL); }
static int b_sem_post(void *raw) { return sem_op(raw, 1, NULL); }
static int b_sem_getvalue(void *raw, int *value) {
    if (!value) return minus_one(EINVAL);
    return sem_op(raw, 2, value);
}
static int b_sem_trywait(void *raw) { return sem_op(raw, 3, NULL); }

static int b_attr_init(AndroidThreadAttr *attr) {
    if (!attr) return 22;
    memset(attr, 0, sizeof *attr);
    attr->stack_size = 1024 * 1024; attr->guard_size = 4096; return 0;
}
static int b_attr_destroy(AndroidThreadAttr *attr) {
    if (!attr) return 22;
    memset(attr, 0xff, sizeof *attr); return 0;
}
static int b_attr_setdetachstate(AndroidThreadAttr *attr, int state) {
    if (!attr || (state != 0 && state != 1)) return 22;
    attr->flags = (attr->flags & ~1u) | state; return 0;
}
static int b_attr_setstacksize(AndroidThreadAttr *attr, size_t size) {
    if (!attr || size < 16384 || size > INT_MAX) return 22;
    attr->stack_size = size; return 0;
}
static int b_attr_getstack(const AndroidThreadAttr *attr, void **base, size_t *size) {
    if (!attr || !base || !size) return 22;
    *base = attr->stack_base; *size = attr->stack_size; return 0;
}
static int b_create(uintptr_t *id, const AndroidThreadAttr *attr, void *(*start)(void *), void *arg) {
    return guest_thread_create(id, attr, start, arg);
}
static int b_join(uintptr_t id, void **result) { return guest_thread_join(id, result); }
static uintptr_t b_self(void) { return (uintptr_t)pthread_self(); }
static int b_equal(uintptr_t a, uintptr_t b) { return pthread_equal((pthread_t)a, (pthread_t)b); }
static void __attribute__((noreturn)) b_exit(void *result) { guest_thread_exit(result); }
/* Guest keys use a separate namespace; one native key owns each thread's
 * value table. SDK key limits must not become Android's key limit. */
#define GUEST_KEYS 1024
static struct { unsigned generation; int used; void (*destroy)(void *); } guest_keys[GUEST_KEYS];
typedef struct { void *value[GUEST_KEYS]; unsigned generation[GUEST_KEYS]; } KeyValues;
static Mutex key_table_lock;
static pthread_key_t key_values_slot;
static pthread_once_t key_values_once=PTHREAD_ONCE_INIT;
static int key_values_error;
static void destroy_key_values(void *raw) {
    KeyValues *values=raw;
    pthread_setspecific(key_values_slot,values);
    for(unsigned pass=0;pass<4;++pass) {
        int invoked=0;
        for(unsigned i=0;i<GUEST_KEYS;++i) {
            mutexLock(&key_table_lock);
            void (*destroy)(void *)=guest_keys[i].used && guest_keys[i].generation==values->generation[i] ? guest_keys[i].destroy : NULL;
            void *value=values->value[i]; values->value[i]=NULL;
            mutexUnlock(&key_table_lock);
            if(value && destroy) { invoked=1; destroy(value); }
        }
        if(!invoked) break;
    }
    pthread_setspecific(key_values_slot,NULL); free(values);
}
static void initialize_key_values(void) { key_values_error=pthread_key_create(&key_values_slot,destroy_key_values); }
static int b_key_create(int *key,void (*destructor)(void *)) {
    if(!key) return 22;
    mutexLock(&key_table_lock);
    for(unsigned i=0;i<GUEST_KEYS;++i) if(!guest_keys[i].used) {
        unsigned generation=(guest_keys[i].generation+1)&0x1fffff;
        if(!generation) generation=1;
        guest_keys[i].generation=generation; guest_keys[i].used=1; guest_keys[i].destroy=destructor;
        *key=(int)((generation<<10)|i); mutexUnlock(&key_table_lock); return 0;
    }
    mutexUnlock(&key_table_lock); return 11;
}
static int b_key_delete(int key) {
    unsigned index=(unsigned)key&1023, generation=(unsigned)key>>10;
    mutexLock(&key_table_lock);
    int valid=guest_keys[index].used && guest_keys[index].generation==generation;
    if(valid) { guest_keys[index].used=0; guest_keys[index].destroy=NULL; }
    mutexUnlock(&key_table_lock); return valid ? 0 : 22;
}
static int b_setspecific(int key,const void *value) {
    unsigned index=(unsigned)key&1023, generation=(unsigned)key>>10;
    mutexLock(&key_table_lock);
    int valid=guest_keys[index].used && guest_keys[index].generation==generation;
    mutexUnlock(&key_table_lock); if(!valid) return 22;
    pthread_once(&key_values_once,initialize_key_values);
    if(key_values_error) return bionic_error(key_values_error);
    KeyValues *values=pthread_getspecific(key_values_slot);
    if(!values && value) {
        values=calloc(1,sizeof *values); if(!values) return 12;
        int rc=pthread_setspecific(key_values_slot,values);
        if(rc) { free(values); return bionic_error(rc); }
    }
    if(values) { values->value[index]=(void *)value; values->generation[index]=generation; }
    return 0;
}
static void *b_getspecific(int key) {
    unsigned index=(unsigned)key&1023, generation=(unsigned)key>>10;
    mutexLock(&key_table_lock);
    int valid=guest_keys[index].used && guest_keys[index].generation==generation;
    mutexUnlock(&key_table_lock); if(!valid) return NULL;
    pthread_once(&key_values_once,initialize_key_values);
    if(key_values_error) return NULL;
    KeyValues *values=pthread_getspecific(key_values_slot);
    return values && values->generation[index]==generation ? values->value[index] : NULL;
}
static int b_once(void *raw, void (*callback)(void)) {
    if (!raw || (uintptr_t)raw % 4 || !callback) return 22;
    if (__atomic_load_n((uint32_t *)raw, __ATOMIC_ACQUIRE) == 2) return 0;
    int error;
    Object *n = acquire(raw, ONCE, 1, &error);
    if (!n) return bionic_error(error);
    error = pthread_once(&n->native.once, callback);
    if (!error) __atomic_store_n((uint32_t *)raw, 2, __ATOMIC_RELEASE);
    release(n); return bionic_error(error);
}
typedef struct { const char *name; uintptr_t address; } ThreadBinding;
#define THREAD_BIND(name, fn) {name, (uintptr_t)(fn)}
static const ThreadBinding bindings[] = {
    THREAD_BIND("__errno", bionic_errno_ptr),
    THREAD_BIND("clock_gettime", bionic_clock_gettime), THREAD_BIND("clock_getres", b_clock_getres),
    THREAD_BIND("nanosleep", b_nanosleep), THREAD_BIND("sched_yield", b_sched_yield),
    THREAD_BIND("pthread_mutexattr_init", b_mutexattr_init),
    THREAD_BIND("pthread_mutexattr_destroy", b_attr_scalar_destroy),
    THREAD_BIND("pthread_mutexattr_settype", b_mutexattr_settype),
    THREAD_BIND("pthread_mutex_init", b_mutex_init), THREAD_BIND("pthread_mutex_destroy", b_mutex_destroy),
    THREAD_BIND("pthread_mutex_lock", b_mutex_lock), THREAD_BIND("pthread_mutex_trylock", b_mutex_trylock),
    THREAD_BIND("pthread_mutex_unlock", b_mutex_unlock),
    THREAD_BIND("pthread_condattr_init", b_condattr_init),
    THREAD_BIND("pthread_condattr_destroy", b_attr_scalar_destroy),
    THREAD_BIND("pthread_condattr_setclock", b_condattr_setclock),
    THREAD_BIND("pthread_cond_init", b_cond_init), THREAD_BIND("pthread_cond_destroy", b_cond_destroy),
    THREAD_BIND("pthread_cond_wait", b_cond_wait), THREAD_BIND("pthread_cond_timedwait", b_cond_timedwait),
    THREAD_BIND("pthread_cond_signal", b_cond_signal), THREAD_BIND("pthread_cond_broadcast", b_cond_broadcast),
    THREAD_BIND("pthread_rwlock_init", b_rwlock_init), THREAD_BIND("pthread_rwlock_destroy", b_rwlock_destroy),
    THREAD_BIND("pthread_rwlock_rdlock", b_rwlock_rdlock), THREAD_BIND("pthread_rwlock_wrlock", b_rwlock_wrlock),
    THREAD_BIND("pthread_rwlock_unlock", b_rwlock_unlock),
    THREAD_BIND("pthread_attr_init", b_attr_init), THREAD_BIND("pthread_attr_destroy", b_attr_destroy),
    THREAD_BIND("pthread_attr_setdetachstate", b_attr_setdetachstate),
    THREAD_BIND("pthread_getattr_np", guest_thread_getattr),
    THREAD_BIND("pthread_attr_setstacksize", b_attr_setstacksize), THREAD_BIND("pthread_attr_getstack", b_attr_getstack),
    THREAD_BIND("pthread_create", b_create), THREAD_BIND("pthread_join", b_join),
    THREAD_BIND("pthread_self", b_self), THREAD_BIND("pthread_detach", guest_thread_detach),
    THREAD_BIND("pthread_equal", b_equal), THREAD_BIND("pthread_exit", b_exit),
    THREAD_BIND("pthread_once", b_once),
    THREAD_BIND("pthread_key_create", b_key_create), THREAD_BIND("pthread_key_delete", b_key_delete),
    THREAD_BIND("pthread_getspecific", b_getspecific), THREAD_BIND("pthread_setspecific", b_setspecific),
    THREAD_BIND("sem_init", b_sem_init), THREAD_BIND("sem_destroy", b_sem_destroy),
    THREAD_BIND("sem_wait", b_sem_wait), THREAD_BIND("sem_post", b_sem_post),
    THREAD_BIND("sem_getvalue", b_sem_getvalue), THREAD_BIND("sem_trywait", b_sem_trywait)
};
uintptr_t bionic_threads_resolve(const char *name) {
    for (size_t i = 0; i < sizeof bindings / sizeof *bindings; ++i)
        if (!strcmp(name, bindings[i].name)) return bindings[i].address;
    return 0;
}
size_t bionic_threads_count(void) { return sizeof bindings / sizeof *bindings; }
