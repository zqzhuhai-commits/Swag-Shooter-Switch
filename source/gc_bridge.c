#include "gc_bridge.h"
#include "gc_capture.h"
#include "guest_futex.h"
#include "bionic_threads.h"
#include <pthread.h>
#include <string.h>
/* Offsets derived from this game's Unity 2019.4 libil2cpp, not copied from
 * a different game's binary. Validation precedes all use. */
static uintptr_t base; static size_t image_size; static FILE *output;
static uintptr_t main_id; static Handle main_handle;
typedef struct { uintptr_t next,id,epoch,stack; } GcThread;
_Static_assert(sizeof(GcThread)==32,"Boehm thread prefix");
typedef struct { uintptr_t id; GcCapture capture; } Paused;
static Paused paused[64]; static unsigned paused_count;
static unsigned capture_retries,last_busy_mask;
extern Handle guest_thread_capture_handle(uintptr_t id);
extern int guest_memory_gc_lock_free(void),guest_thread_gc_lock_free(void),bionic_gc_locks_free(void),io_gc_lock_free(void);
static unsigned busy_locks(void) {
    return (!guest_memory_gc_lock_free() ? 1u:0u) | (!futex_gc_lock_free() ? 2u:0u) |
        (!bionic_gc_locks_free() ? 4u:0u) | (!io_gc_lock_free() ? 8u:0u) | (!guest_thread_gc_lock_free() ? 16u:0u);
}
static int readable(uintptr_t p,size_t bytes) {
    MemoryInfo info; u32 page;
    return p && p+bytes>=p && R_SUCCEEDED(svcQueryMemory(&info,&page,p)) && (info.perm&Perm_R) && bytes<=info.size && p>=info.addr && p-info.addr<=info.size-bytes;
}
static void fatal(const char *reason) __attribute__((noreturn));
static void fatal(const char *reason) {
    for(unsigned i=0;i<paused_count;++i) gc_capture_resume(&paused[i].capture);
    fprintf(output,"GC_BRIDGE_ABORT reason=%s paused=%u busy_mask=%x retries=%u no_false_ack=1\n",reason,paused_count,last_busy_mask,capture_retries); fflush(output); svcExitProcess();
}
static GcThread *find_thread(uintptr_t id) {
    uint32_t hash=(uint32_t)((id>>8)^id); hash^=hash>>16;
    uintptr_t *buckets=(uintptr_t *)(base+0x2b3a460);
    uintptr_t address=__atomic_load_n(&buckets[hash&255],__ATOMIC_ACQUIRE);
    for(unsigned count=0;address && count<1024;++count) {
        if(!readable(address,sizeof(GcThread))) fatal("invalid_thread_record");
        GcThread *thread=(GcThread *)address;
        if(thread->id==id) return thread;
        address=thread->next;
    }
    return NULL;
}
int gc_bridge_bind(AndroidElf *module,FILE *log) {
    output=log;
    if(!module->published || module->size<0x2b41840) return -1;
    struct { size_t offset; uint32_t word; } expected[]={
        {0xba7f1c,0xd348fc09},{0xba7f54,0xf9400aa8},{0xba7fa8,0xf9000ea8},
        {0xba80c4,0x97f862d3},{0xba84bc,0x94002966}};
    for(unsigned i=0;i<sizeof expected/sizeof *expected;++i) {
        uint32_t actual; memcpy(&actual,module->image+expected[i].offset,4);
        if(actual!=expected[i].word) { fprintf(log,"GC_BINARY_REJECT offset=%zx actual=%x\n",expected[i].offset,actual); return -1; }
    }
    if(gc_capture_selftest(log)) return -1;
    base=module->bias; image_size=module->size; main_id=(uintptr_t)pthread_self();
    main_handle=envGetMainThreadHandle();
    if(!main_handle || main_handle==CUR_THREAD_HANDLE) { base=0; return -1; }
    fprintf(log,"GC_BRIDGE_BOUND image_bytes=%zu offsets=VERIFIED capture_probe=PASS\n",image_size); return 0;
}
int gc_bridge_accept_signal(int signal,uintptr_t handler) {
    if(!base || handler!=base+0xbb2b1c) return 0;
    int suspend=*(volatile int *)(base+0x2b2feb4),restart=*(volatile int *)(base+0x2b2feb8);
    if(signal!=suspend && signal!=restart) return 0;
    fprintf(output,"GC_SIGNAL_REGISTER signal=%d bridge=PAUSE_CAPTURE_RESUME\n",signal); return 1;
}
int gc_bridge_kill(uintptr_t id,int signal) {
    if(!base) return 95;
    if(!signal) return find_thread(id) ? 0 : 3;
    int suspend=*(volatile int *)(base+0x2b2feb4),restart=*(volatile int *)(base+0x2b2feb8);
    if(signal!=suspend && signal!=restart) return 95;
    if(id==(uintptr_t)pthread_self()) fatal("collector_signalled_itself");
    GcThread *thread=find_thread(id); if(!thread) fatal("target_not_in_gc_table");
    int (*post)(void *)=(int (*)(void *))bionic_threads_resolve("sem_post");
    if(signal==suspend) {
        if(paused_count==64) fatal("pause_table_full");
        for(unsigned i=0;i<paused_count;++i) if(paused[i].id==id) fatal("duplicate_suspend");
        Handle handle=0;
        if(id==main_id) handle=main_handle;
        else handle=guest_thread_capture_handle(id);
        if(!handle) fatal("unknown_kernel_thread_handle");
        Paused *slot=&paused[paused_count]; memset(slot,0,sizeof *slot); slot->id=id;
        ++paused_count;
        Result rc=0;
        for(unsigned attempt=0;;++attempt) {
            rc=gc_capture_pause(&slot->capture,handle);
            if(R_FAILED(rc)) fatal("pause_or_context_failed");
            last_busy_mask=busy_locks();
            if(!last_busy_mask) break;
            rc=gc_capture_resume(&slot->capture);
            if(R_FAILED(rc)) fatal("lock_retry_resume_failed");
            ++capture_retries;
            if(attempt>=499) fatal("shim_lock_retry_timeout");
            svcSleepThread(1000000);
        }
        rc=gc_capture_stage_roots(&slot->capture,&thread->stack);
        if(R_FAILED(rc)) fatal("root_capture_failed");
        uintptr_t epoch=__atomic_load_n((uintptr_t *)(base+0x2b3a440),__ATOMIC_ACQUIRE);
        __atomic_store_n(&thread->epoch,epoch,__ATOMIC_RELEASE);
        if(post((void *)(base+0x2b41830))) fatal("suspend_ack_failed");
        return 0;
    }
    for(unsigned i=0;i<paused_count;++i) if(paused[i].id==id) {
        Result rc=gc_capture_resume(&paused[i].capture);
        if(R_FAILED(rc)) fatal("resume_failed");
        paused[i]=paused[--paused_count];
        if(*(volatile int *)(base+0x2b2feb0)) {
            uintptr_t epoch=__atomic_load_n((uintptr_t *)(base+0x2b3a440),__ATOMIC_ACQUIRE);
            __atomic_store_n(&thread->epoch,epoch|1,__ATOMIC_RELEASE);
            if(post((void *)(base+0x2b41830))) fatal("restart_ack_failed");
        }
        if(!paused_count) { fprintf(output,"GC_COLLECTION_RESUMED captured_roots=1 lock_retries=%u\n",capture_retries); fflush(output); }
        return 0;
    }
    fatal("restart_without_capture");
}
int gc_bridge_sigmask(int how,const uint64_t *set,uint64_t *old) {
    if(old) *old=0;
    if(!set) return 0;
    if(how==1 || ((how==0 || how==2) && !*set)) return 0;
    return 95; /* No general asynchronous signal masking is advertised. */
}
