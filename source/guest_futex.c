#include "guest_futex.h"
#include "bionic_threads.h"
#include <switch.h>
#include <limits.h>
#include <string.h>
typedef struct FutexWaiter { uint32_t *address; UEvent event; int notified; struct FutexWaiter *next; } FutexWaiter;
static Mutex queue_lock; static FutexWaiter *waiters;
int futex_gc_lock_free(void) { return __atomic_load_n(&queue_lock,__ATOMIC_ACQUIRE)==0; }
static long fail(int error) { *bionic_errno_ptr()=error; return -1; }
long guest_futex(uint32_t *address,int operation,int value,const struct timespec *timeout,uint32_t mask) {
    if(!address || ((uintptr_t)address&3)) return fail(22);
    int command=operation&127;
    if(operation & ~0x1ff) return fail(38);
    if(command==1) {
        if(value<0) return fail(22);
        int count=0; mutexLock(&queue_lock);
        FutexWaiter **link=&waiters;
        while(*link && count<value) {
            FutexWaiter *current=*link;
            if(current->address!=address) { link=&current->next; continue; }
            ueventSignal(&current->event);
            current->notified=1; *link=current->next; ++count;
        }
        mutexUnlock(&queue_lock); return count;
    }
    if(command!=0 && command!=9) return fail(38);
    if(command==9 && mask!=UINT32_MAX) return fail(95);
    int64_t nanoseconds=INT64_MAX;
    if(timeout) {
        if(timeout->tv_sec<0 || timeout->tv_nsec<0 || timeout->tv_nsec>=1000000000 || timeout->tv_sec>INT64_MAX/1000000000) return fail(22);
        nanoseconds=(int64_t)timeout->tv_sec*1000000000;
        if(nanoseconds>INT64_MAX-timeout->tv_nsec) return fail(22);
        nanoseconds+=timeout->tv_nsec;
        if(command==9) {
            struct timespec now;
            if(bionic_clock_gettime((operation&0x100) ? 0 : 1,&now)) return -1;
            int64_t current=(int64_t)now.tv_sec*1000000000+now.tv_nsec;
            nanoseconds=nanoseconds>current ? nanoseconds-current : 0;
        }
    }
    FutexWaiter waiter={.address=address};
    ueventCreate(&waiter.event,true);
    mutexLock(&queue_lock);
    if(__atomic_load_n(address,__ATOMIC_ACQUIRE)!=(uint32_t)value) {
        mutexUnlock(&queue_lock); return fail(11);
    }
    waiter.next=waiters; waiters=&waiter; mutexUnlock(&queue_lock);
    Result rc=waitSingle(waiterForUEvent(&waiter.event),nanoseconds);
    mutexLock(&queue_lock);
    FutexWaiter **link=&waiters; while(*link && *link!=&waiter) link=&(*link)->next;
    if(*link) *link=waiter.next;
    int notified=waiter.notified; mutexUnlock(&queue_lock);
    if(notified) return 0;
    if(R_SUCCEEDED(rc)) return 0;
    return fail((R_DESCRIPTION(rc)==KernelError_TimedOut || rc==MAKERESULT(Module_Libnx,LibnxError_Timeout)) ? 110 : 4);
}
typedef struct { uint32_t word; long result; } Probe;
static void wait_worker(void *arg) { Probe *p=arg; struct timespec timeout={1,0}; p->result=guest_futex(&p->word,0,0,&timeout,0); }
int guest_futex_selftest(FILE *log) {
    Probe probe={0,-2}; struct timespec short_timeout={0,3000000};
    long mismatch=guest_futex(&probe.word,0,1,&short_timeout,0); int mismatch_errno=*bionic_errno_ptr();
    int errors=mismatch!=-1 || mismatch_errno!=11;
    fprintf(log,"FUTEX_CHECK mismatch=%ld errno=%d expected_errno=11\n",mismatch,mismatch_errno);
    long timed=guest_futex(&probe.word,0,0,&short_timeout,0); int timeout_errno=*bionic_errno_ptr();
    errors+=timed!=-1 || timeout_errno!=110;
    fprintf(log,"FUTEX_CHECK timeout=%ld errno=%d expected_errno=110\n",timed,timeout_errno);
    Thread worker;
    Result rc=threadCreate(&worker,wait_worker,&probe,NULL,0x10000,0x2c,-2);
    if(R_FAILED(rc)) return -1;
    rc=threadStart(&worker);
    if(R_FAILED(rc)) { threadClose(&worker); return -1; }
    int queued=0;
    for(unsigned attempt=0;attempt<500 && !queued;++attempt) {
        mutexLock(&queue_lock);
        for(FutexWaiter *p=waiters;p;p=p->next) if(p->address==&probe.word) queued=1;
        mutexUnlock(&queue_lock); if(!queued) svcSleepThread(1000000);
    }
    long woken=queued ? guest_futex(&probe.word,1,1,NULL,0) : -1;
    errors+=!queued || woken!=1;
    fprintf(log,"FUTEX_CHECK queued=%d wake_count=%ld expected=1\n",queued,woken);
    threadWaitForExit(&worker); threadClose(&worker); errors+=probe.result!=0;
    fprintf(log,"FUTEX_TEST_DONE errors=%d wait_wake_count=CHECKED mismatch_timeout=CHECKED unsupported_ops=ENOSYS\n",errors);
    return errors ? -1 : 0;
}
