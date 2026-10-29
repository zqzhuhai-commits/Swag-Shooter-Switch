#include <stdio.h>
#include <string.h>
#include "gc_capture.h"
static int writable_region(uintptr_t address,size_t bytes) {
    MemoryInfo info; u32 page;
    if(!address || address+bytes<address || R_FAILED(svcQueryMemory(&info,&page,address))) return 0;
    return (info.perm & (Perm_R|Perm_W))==(Perm_R|Perm_W) && address>=info.addr && bytes<=info.size && address-info.addr<=info.size-bytes;
}
Result gc_capture_stage_roots(GcCapture *capture,uintptr_t *collector_stack_slot) {
    Result invalid=MAKERESULT(Module_Libnx,LibnxError_BadInput);
    if(!capture->paused || capture->roots_address || !collector_stack_slot) return invalid;
    const size_t bytes=sizeof capture->original_words;
    if(capture->context.sp<bytes) return invalid;
    uintptr_t address=(capture->context.sp-bytes)&~(uintptr_t)15;
    if(!writable_region(address,bytes) || !writable_region((uintptr_t)collector_stack_slot,sizeof(uintptr_t))) return invalid;
    memcpy(capture->original_words,(void *)address,bytes);
    uint64_t roots[GC_CAPTURE_ROOT_WORDS];
    for(unsigned i=0;i<29;++i) roots[i]=capture->context.cpu_gprs[i].x;
    roots[29]=capture->context.fp; roots[30]=capture->context.lr; roots[31]=capture->context.sp;
    roots[32]=capture->context.pc.x; roots[33]=capture->context.tpidr;
    memcpy(roots+34,capture->context.fpu_gprs,sizeof capture->context.fpu_gprs);
    capture->previous_stack=__atomic_load_n(collector_stack_slot,__ATOMIC_ACQUIRE);
    capture->collector_stack_slot=collector_stack_slot; capture->roots_address=address;
    memcpy((void *)address,roots,sizeof roots);
    __atomic_store_n(collector_stack_slot,address,__ATOMIC_RELEASE); return 0;
}
Result gc_capture_resume(GcCapture *capture) {
    if(!capture->paused) return 0;
    if(capture->roots_address) {
        memcpy((void *)capture->roots_address,capture->original_words,sizeof capture->original_words);
        __atomic_store_n(capture->collector_stack_slot,capture->previous_stack,__ATOMIC_RELEASE);
        capture->roots_address=0;
    }
    Result result=svcSetThreadActivity(capture->handle,ThreadActivity_Runnable);
    if(R_SUCCEEDED(result)) capture->paused=0;
    return result;
}
Result gc_capture_pause(GcCapture *capture,Handle handle) {
    memset(capture,0,sizeof *capture); capture->handle=handle;
    Result result=svcSetThreadActivity(handle,ThreadActivity_Paused);
    if(R_FAILED(result)) return result;
    capture->paused=1;
    result=svcGetThreadContext3(&capture->context,handle);
    if(R_FAILED(result) || (capture->context.psr&0x10) || capture->context.sp<4096) {
        Result resumed=gc_capture_resume(capture);
        return R_FAILED(resumed) ? resumed : R_FAILED(result) ? result : MAKERESULT(Module_Libnx,LibnxError_BadInput);
    }
    return 0;
}
static unsigned worker_ready,worker_stop;
static void capture_worker(void *argument) {
    (void)argument;
    __atomic_store_n(&worker_ready,1,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&worker_stop,__ATOMIC_ACQUIRE)) svcSleepThread(YieldType_ToAnyThread);
}
int gc_capture_selftest(FILE *log) {
    Thread worker; worker_ready=worker_stop=0;
    fprintf(log,"GC_STEP worker_create\n"); fflush(log);
    Result result=threadCreate(&worker,capture_worker,NULL,NULL,0x10000,0x30,-2);
    if(R_FAILED(result)) return -1;
    result=threadStart(&worker);
    if(R_FAILED(result)) { threadClose(&worker); return -1; }
    unsigned attempts=0;
    while(!__atomic_load_n(&worker_ready,__ATOMIC_ACQUIRE) && attempts++<1000) svcSleepThread(1000000);
    GcCapture capture={0};
    fprintf(log,"GC_STEP pause_begin ready=%u\n",worker_ready); fflush(log);
    result=gc_capture_pause(&capture,worker.handle);
    fprintf(log,"GC_STEP pause_end result=%x paused=%d\n",result,capture.paused); fflush(log);
    uintptr_t collector_stack=0;
    int roots_bad=0;
    if(R_SUCCEEDED(result)) {
        fprintf(log,"GC_STEP roots_begin\n"); fflush(log);
        result=gc_capture_stage_roots(&capture,&collector_stack);
        fprintf(log,"GC_STEP roots_end result=%x\n",result); fflush(log);
        if(R_SUCCEEDED(result)) {
            uint64_t *roots=(uint64_t *)collector_stack;
            roots_bad=roots[31]!=capture.context.sp || roots[32]!=capture.context.pc.x;
            for(unsigned i=0;i<29;++i) roots_bad|=roots[i]!=capture.context.cpu_gprs[i].x;
            roots_bad|=memcmp(roots+34,capture.context.fpu_gprs,sizeof capture.context.fpu_gprs)!=0;
        }
    }
    fprintf(log,"GC_STEP resume_begin\n"); fflush(log);
    Result resumed=gc_capture_resume(&capture);
    fprintf(log,"GC_STEP resume_end result=%x\n",resumed); fflush(log);
    roots_bad|=collector_stack!=0;
    __atomic_store_n(&worker_stop,1,__ATOMIC_RELEASE);
    /* Never wait for a thread that could not be resumed. */
    if(capture.paused) {
        fprintf(log,"GC_CAPTURE_FATAL resume=%x\n",resumed); fflush(log); svcExitProcess();
    }
    fprintf(log,"GC_STEP join_begin\n"); fflush(log);
    threadWaitForExit(&worker); threadClose(&worker);
    fprintf(log,"GC_STEP join_end\n"); fflush(log);
    fprintf(log,"GC_CAPTURE_PRIMITIVE result=%x resumed=%x roots_errors=%d sp=%llx collector_integration=NOT_ENABLED\n",
        result,resumed,roots_bad,(unsigned long long)capture.context.sp);
    return R_SUCCEEDED(result) && R_SUCCEEDED(resumed) && !roots_bad && attempts<1000 ? 0 : -1;
}
