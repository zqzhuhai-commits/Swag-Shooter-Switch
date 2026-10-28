#include "batch_suite.h"
#include <switch.h>
#include <stdint.h>
typedef struct { const char *name; int state; uint64_t milliseconds; } Check;
static Check checks[96];
static unsigned count;
int batch_run(const char *name, int (*test)(FILE *), int enabled, FILE *log) {
    fprintf(log,"TEST_BEGIN name=%s enabled=%d\n",name,enabled); fflush(log);
    uint64_t start=armGetSystemTick();
    int state=enabled ? (test(log)==0 ? 1 : 0) : -1;
    uint64_t elapsed=armTicksToNs(armGetSystemTick()-start)/1000000;
    if (count<96) checks[count++]=(Check){name,state,elapsed};
    fprintf(log,"TEST_RESULT name=%s status=%s elapsed_ms=%llu\n",name,
        state<0 ? "SKIP_DEPENDENCY" : state ? "PASS" : "FAIL",(unsigned long long)elapsed);
    return state==1;
}
void batch_record(const char *name, int passed, FILE *log) {
    if (count<96) checks[count++]=(Check){name,passed<0 ? -1 : !!passed,0};
    fprintf(log,"TEST_RESULT name=%s status=%s\n",name,passed<0 ? "SKIP_DEPENDENCY" : passed ? "PASS" : "FAIL");
}
void batch_summary(FILE *log) {
    unsigned passed=0,failed=0,skipped=0;
    for (unsigned i=0;i<count;++i) {
        Check *c=&checks[i];
        if (c->state<0) ++skipped; else if(c->state) ++passed; else ++failed;
        fprintf(log,"SUITE_CHECK name=%s status=%s elapsed_ms=%llu\n",c->name,
            c->state<0 ? "SKIP_DEPENDENCY" : c->state ? "PASS" : "FAIL",(unsigned long long)c->milliseconds);
    }
    fprintf(log,"SUITE_DONE checks=%u passed=%u failed=%u skipped=%u implemented_tests_complete=%d game_running=0 full_game_validation=BLOCKED_MISSING_APIS\n",
        count,passed,failed,skipped,failed==0 && skipped==0);
}
