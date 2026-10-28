#pragma once
#include "bionic_threads.h"
int guest_thread_create(uintptr_t *id, const AndroidThreadAttr *attr, void *(*start)(void *), void *arg);
int guest_thread_join(uintptr_t id, void **result);
int guest_thread_detach(uintptr_t id);
void guest_thread_exit(void *result) __attribute__((noreturn));
int guest_threads_drain(FILE *log);
int guest_detached_selftest(FILE *log);

int guest_thread_getattr(uintptr_t id, AndroidThreadAttr *attr);
