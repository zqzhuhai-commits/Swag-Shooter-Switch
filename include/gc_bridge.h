#pragma once
#include "elf_loader.h"
int gc_bridge_bind(AndroidElf *module,FILE *log);
int gc_bridge_accept_signal(int signal,uintptr_t handler);
int gc_bridge_kill(uintptr_t thread,int signal);
int gc_bridge_sigmask(int how,const uint64_t *set,uint64_t *old);
