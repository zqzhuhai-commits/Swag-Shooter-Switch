#include "android_events.h"
#include "bionic_threads.h"
#include <switch.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Optional Android sensors are deliberately not advertised until a real
 * six-axis backend is connected. Never invent sensor readings. */
typedef struct { Mutex lock; CondVar changed; unsigned refs; int pending; } GuestLooper;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static pthread_key_t looper_key;
static int key_error;
static unsigned live_loopers;
static void looper_release(GuestLooper *loop) {
    if (loop && __atomic_sub_fetch(&loop->refs,1,__ATOMIC_ACQ_REL)==0) {
        __atomic_sub_fetch(&live_loopers,1,__ATOMIC_RELAXED); free(loop);
    }
}
static void looper_destructor(void *loop) { looper_release(loop); }
static void initialize_key(void) { key_error=pthread_key_create(&looper_key,looper_destructor); }
static GuestLooper *looper_for_thread(void) {
    pthread_once(&key_once,initialize_key);
    return key_error ? NULL : pthread_getspecific(looper_key);
}
static GuestLooper *looper_prepare(int options) {
    if (options & ~1) { *bionic_errno_ptr()=22; return NULL; }
    GuestLooper *loop=looper_for_thread();
    if (loop || key_error) return loop;
    loop=calloc(1,sizeof *loop);
    if (!loop) { *bionic_errno_ptr()=12; return NULL; }
    loop->refs=1;
    if (pthread_setspecific(looper_key,loop)) { free(loop); return NULL; }
    __atomic_add_fetch(&live_loopers,1,__ATOMIC_RELAXED);
    return loop;
}
static void looper_acquire(GuestLooper *loop) {
    if (loop) __atomic_add_fetch(&loop->refs,1,__ATOMIC_RELAXED);
}
static void looper_wake(GuestLooper *loop) {
    if (!loop) return;
    mutexLock(&loop->lock); loop->pending=1;
    condvarWakeAll(&loop->changed); mutexUnlock(&loop->lock);
}
/* Only wake/timeout sources are registered in this backend. Sensor queues
 * cannot contribute file descriptors because the sensor list is empty. */
static int looper_poll(int timeout, int *fd, int *events, void **data) {
    if (fd) *fd=-1;
    if (events) *events=0;
    if (data) *data=NULL;
    GuestLooper *loop=looper_for_thread();
    if (!loop) return -4;
    uint64_t started=armGetSystemTick(), budget=timeout>0 ? (uint64_t)timeout*1000000 : 0;
    mutexLock(&loop->lock);
    while (!loop->pending) {
        uint64_t elapsed=armTicksToNs(armGetSystemTick()-started);
        if (timeout==0 || (timeout>0 && elapsed>=budget)) { mutexUnlock(&loop->lock); return -3; }
        Result rc=condvarWaitTimeout(&loop->changed,&loop->lock,timeout<0 ? UINT64_MAX : budget-elapsed);
        if (R_FAILED(rc) && !loop->pending) { mutexUnlock(&loop->lock); return timeout<0 ? -4 : -3; }
    }
    loop->pending=0; mutexUnlock(&loop->lock); return -1;
}
typedef struct { GuestLooper *loop; } SensorQueue;
static const unsigned sensor_manager=0x53454e53;
static void *sensor_instance(void) { return (void *)&sensor_manager; }
static int sensor_list(void *manager, const void ***list) {
    if (list) *list=NULL;
    return manager==&sensor_manager && list ? 0 : -22;
}
static const void *sensor_default(void *manager,int type) { (void)manager; (void)type; return NULL; }
static SensorQueue *sensor_create(void *manager,GuestLooper *loop,int ident,void *callback,void *data) {
    (void)callback; (void)data;
    if (manager!=&sensor_manager || !loop || (!callback && ident<0)) return NULL;
    SensorQueue *queue=malloc(sizeof *queue);
    if (queue) { queue->loop=loop; looper_acquire(loop); }
    return queue;
}
static int sensor_destroy(void *manager,SensorQueue *queue) {
    if (manager!=&sensor_manager || !queue) return -22;
    looper_release(queue->loop); free(queue); return 0;
}
static int sensor_enable(SensorQueue *queue,const void *sensor) { (void)queue; (void)sensor; return -22; }
static int sensor_rate(SensorQueue *queue,const void *sensor,int rate) { (void)rate; return sensor_enable(queue,sensor); }
static int sensor_has(SensorQueue *queue) { return queue ? 0 : -22; }
static int64_t sensor_events(SensorQueue *queue,void *events,size_t count) {
    return queue && (events || !count) ? 0 : -22;
}
static const char *sensor_name(const void *sensor) { (void)sensor; return ""; }
static int sensor_type(const void *sensor) { (void)sensor; return -1; }
static int sensor_delay(const void *sensor) { (void)sensor; return 0; }
static float sensor_resolution(const void *sensor) { (void)sensor; return 0; }
typedef struct { const char *name; uintptr_t function; } Binding;
#define EVENT_BIND(name,fn) {name,(uintptr_t)(fn)}
static const Binding bindings[]={
    EVENT_BIND("ALooper_prepare",looper_prepare), EVENT_BIND("ALooper_forThread",looper_for_thread),
    EVENT_BIND("ALooper_acquire",looper_acquire), EVENT_BIND("ALooper_release",looper_release),
    EVENT_BIND("ALooper_wake",looper_wake), EVENT_BIND("ALooper_pollAll",looper_poll),
    EVENT_BIND("ALooper_pollOnce",looper_poll),
    EVENT_BIND("ASensorManager_getInstance",sensor_instance), EVENT_BIND("ASensorManager_getSensorList",sensor_list),
    EVENT_BIND("ASensorManager_getDefaultSensor",sensor_default), EVENT_BIND("ASensorManager_createEventQueue",sensor_create),
    EVENT_BIND("ASensorManager_destroyEventQueue",sensor_destroy), EVENT_BIND("ASensorEventQueue_enableSensor",sensor_enable),
    EVENT_BIND("ASensorEventQueue_disableSensor",sensor_enable), EVENT_BIND("ASensorEventQueue_setEventRate",sensor_rate),
    EVENT_BIND("ASensorEventQueue_hasEvents",sensor_has), EVENT_BIND("ASensorEventQueue_getEvents",sensor_events),
    EVENT_BIND("ASensor_getName",sensor_name), EVENT_BIND("ASensor_getVendor",sensor_name),
    EVENT_BIND("ASensor_getType",sensor_type), EVENT_BIND("ASensor_getMinDelay",sensor_delay),
    EVENT_BIND("ASensor_getResolution",sensor_resolution)
};
uintptr_t android_events_resolve(const char *name) {
    for (size_t i=0;i<sizeof bindings/sizeof *bindings;++i)
        if (!strcmp(name,bindings[i].name)) return bindings[i].function;
    return 0;
}
typedef struct { GuestLooper *parent; int errors; } WakeProbe;
static void wake_worker(void *arg) {
    WakeProbe *probe=arg;
    probe->errors+=looper_for_thread()!=NULL;
    GuestLooper *own=looper_prepare(0);
    probe->errors+=!own || own==probe->parent;
    svcSleepThread(10000000); looper_wake(probe->parent);
    /* The thread-specific destructor must release this worker's looper. */
}
int android_events_selftest(FILE *log) {
    unsigned baseline=__atomic_load_n(&live_loopers,__ATOMIC_RELAXED);
    int errors=0; GuestLooper *previous=looper_for_thread(), *loop=looper_prepare(1);
    if (!loop) return -1;
    errors+=looper_for_thread()!=loop || looper_prepare(0)!=loop;
    errors+=looper_poll(0,NULL,NULL,NULL)!=-3;
    looper_wake(loop); looper_wake(loop);
    errors+=looper_poll(0,NULL,NULL,NULL)!=-1 || looper_poll(0,NULL,NULL,NULL)!=-3;
    uint64_t start=armGetSystemTick(); errors+=looper_poll(3,NULL,NULL,NULL)!=-3;
    errors+=armTicksToNs(armGetSystemTick()-start)<2000000;
    Thread thread;
    WakeProbe probe={loop,0};
    Result rc=threadCreate(&thread,wake_worker,&probe,NULL,0x8000,0x2c,-2);
    if (R_FAILED(rc)) ++errors;
    else {
        rc=threadStart(&thread);
        if (R_FAILED(rc)) ++errors;
        else { errors+=looper_poll(1000,NULL,NULL,NULL)!=-1; threadWaitForExit(&thread); errors+=probe.errors; }
        threadClose(&thread);
    }
    const void **list=(const void **)(uintptr_t)1;
    errors+=sensor_list(sensor_instance(),&list)!=0 || list!=NULL;
    errors+=sensor_default(sensor_instance(),1)!=NULL;
    SensorQueue *queue=sensor_create(sensor_instance(),loop,1,NULL,NULL);
    if (!queue) ++errors;
    else {
        uint64_t guarded=0x12345678;
        errors+=sensor_has(queue)!=0 || sensor_events(queue,&guarded,1)!=0 || guarded!=0x12345678;
        errors+=sensor_enable(queue,NULL)!=-22 || sensor_destroy(sensor_instance(),queue)!=0;
    }
    if (!previous) { pthread_setspecific(looper_key,NULL); looper_release(loop); }
    errors+=__atomic_load_n(&live_loopers,__ATOMIC_RELAXED)!=baseline;
    fprintf(log,"ANDROID_EVENTS_DONE errors=%d timeout=CHECKED cross_thread_wake=CHECKED thread_isolation_and_cleanup=CHECKED wake_coalescing=CHECKED sensors=NOT_ADVERTISED fd_registration=NOT_IMPLEMENTED\n",errors);
    return errors ? -1 : 0;
}
