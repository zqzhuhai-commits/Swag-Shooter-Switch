#include <switch.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
/* libnx switches to this dedicated stack before invoking the handler.
 * Open the output ahead of time; avoid heap allocation and stdio FILE locks
 * while reporting a terminal fault. Native filesystem writes remain best effort. */
_Alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size=sizeof(__nx_exception_stack);
static int crash_fd=-1;
static uintptr_t bases[3]; static size_t sizes[3];
extern char _start[];
static void emit(const char *text) {
    if(crash_fd<0) return;
    size_t left=strlen(text);
    while(left) { ssize_t n=write(crash_fd,text,left); if(n<=0) break; text+=n; left-=(size_t)n; }
}
void crash_trace_terminal(const char *operation,int status) {
    char line[128];
    snprintf(line,sizeof line,"GUEST_TERMINAL operation=%s status=%d\n",operation,status); emit(line);
}
void crash_trace_init(FILE *log) {
    crash_fd=open("sdmc:/switch/swag-switch-crash.log",O_WRONLY|O_CREAT|O_TRUNC,0666);
    emit("CRASH_TRACE_ARMED version=0.14.23; no fault recorded yet\n");
    fprintf(log,"CRASH_TRACE_ARMED fd=%d path=/switch/swag-switch-crash.log\n",crash_fd);
}
void crash_trace_module(unsigned index,uintptr_t base,size_t size) {
    if(index<3) { bases[index]=base; sizes[index]=size; }
}
static void address(const char *kind,uintptr_t value) {
    char line[160]; const char *names[]={"unity","il2cpp","main"};
    for(unsigned i=0;i<3;++i) if(bases[i] && value>=bases[i] && value-bases[i]<sizes[i]) {
        snprintf(line,sizeof line,"CRASH_ADDRESS kind=%s module=%s offset=%llx\n",kind,names[i],(unsigned long long)(value-bases[i])); emit(line); return;
    }
    snprintf(line,sizeof line,"CRASH_ADDRESS kind=%s absolute=%llx host_base=%llx\n",kind,(unsigned long long)value,(unsigned long long)(uintptr_t)_start); emit(line);
}
static void dump_words(const char *kind,uintptr_t pointer,unsigned words) {
    MemoryInfo info; u32 page;
    if(!pointer || (pointer&7) || R_FAILED(svcQueryMemory(&info,&page,pointer)) || !(info.perm&Perm_R) || info.size<(u64)words*8 || pointer<info.addr || pointer-info.addr>info.size-(u64)words*8) return;
    for(unsigned i=0;i<words;++i) {
        char line[128]; snprintf(line,sizeof line,"CRASH_MEMORY kind=%s offset=%x value=%llx\n",kind,i*8,(unsigned long long)((uintptr_t *)pointer)[i]); emit(line);
    }
}
void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    char line[256];
    snprintf(line,sizeof line,"NATIVE_EXCEPTION desc=%x esr=%x far=%llx sp=%llx fp=%llx\n",ctx->error_desc,ctx->esr,(unsigned long long)ctx->far.x,(unsigned long long)ctx->sp.x,(unsigned long long)ctx->fp.x); emit(line);
    address("pc",ctx->pc.x); address("lr",ctx->lr.x);
    for(unsigned i=0;i<29;++i) { snprintf(line,sizeof line,"CRASH_REGISTER x%u=%llx\n",i,(unsigned long long)ctx->cpu_gprs[i].x); emit(line); }
    /* Read-only evidence for the verified allocator fault; never patch state. */
    if(bases[0] && ctx->pc.x==bases[0]+0x408b68) dump_words("unity_temp_allocator",ctx->cpu_gprs[19].x,30);
    dump_words("stack",ctx->sp.x,32);
    uintptr_t fp=ctx->fp.x;
    for(unsigned i=0;i<12 && fp && !(fp&15);++i) {
        MemoryInfo info; u32 page;
        if(R_FAILED(svcQueryMemory(&info,&page,fp)) || !(info.perm&Perm_R) || info.size<16 || fp<info.addr || fp-info.addr>info.size-16) break;
        uintptr_t *frame=(uintptr_t *)fp; address("frame",frame[1]);
        uintptr_t next=frame[0]; if(next<=fp || next-fp>0x100000) break; fp=next;
    }
    emit("NATIVE_EXCEPTION_END process_exit=1\n");
    if(crash_fd>=0) close(crash_fd);
    svcExitProcess();
}
