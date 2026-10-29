#pragma once
#include <switch.h>
#include <stdio.h>
enum { GC_CAPTURE_ROOT_WORDS=98 };
typedef struct {
    Handle handle; ThreadContext context; int paused;
    uintptr_t roots_address, previous_stack;
    uintptr_t *collector_stack_slot;
    uint64_t original_words[GC_CAPTURE_ROOT_WORDS];
} GcCapture;
/* Caller owns the handle and must keep the target thread alive. */
Result gc_capture_pause(GcCapture *capture, Handle handle);
Result gc_capture_resume(GcCapture *capture);
int gc_capture_selftest(FILE *log);

Result gc_capture_stage_roots(GcCapture *capture, uintptr_t *collector_stack_slot);
