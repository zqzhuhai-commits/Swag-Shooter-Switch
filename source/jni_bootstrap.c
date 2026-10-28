#include "runtime_services.h"
#include <stdint.h>
#include <string.h>

/* Narrow NativeLoader registration probe, not a Java VM or Unity lifecycle.
 * The supplied libmain.so's JNI_OnLoad uses AttachCurrentThread, FindClass,
 * RegisterNatives (and FatalError on failure). No registered method is called. */
typedef struct { const char *name, *signature; void *code; } NativeMethod;
static struct {
    void *env_table[234], *vm_table[8];
    void **env, **vm;
    AndroidElf *module;
    FILE *log;
    unsigned errors, registrations, load, unload;
    int class_token;
} probe;
static int attach(void *vm, void **env, void *args) {
    (void)vm; (void)args;
    if (!env) return -1;
    *env = &probe.env; return 0;
}
static void *find_class(void *env, const char *name) {
    (void)env;
    if (name && !strcmp(name, "com/unity3d/player/NativeLoader")) return &probe.class_token;
    ++probe.errors; return NULL;
}
static int executable(void *address) {
    uintptr_t pc = (uintptr_t)address;
    if (pc < probe.module->bias) return 0;
    uintptr_t offset = pc - probe.module->bias;
    for (size_t i = 0; i < probe.module->phnum; ++i) {
        const Elf64_Phdr *p = &probe.module->ph[i];
        if (p->p_type == PT_LOAD && (p->p_flags & PF_X) && offset >= p->p_vaddr && offset - p->p_vaddr < p->p_memsz) return 1;
    }
    return 0;
}
static int register_natives(void *env, void *clazz, const NativeMethod *methods, int count) {
    (void)env;
    if (clazz != &probe.class_token || !methods || count != 2) { ++probe.errors; return -1; }
    for (int i = 0; i < count; ++i) {
        if (!methods[i].name || !methods[i].signature || !executable(methods[i].code)) { ++probe.errors; return -1; }
        if (!strcmp(methods[i].name, "load") && !strcmp(methods[i].signature, "(Ljava/lang/String;)Z")) ++probe.load;
        else if (!strcmp(methods[i].name, "unload") && !strcmp(methods[i].signature, "()Z")) ++probe.unload;
        else { ++probe.errors; return -1; }
        fprintf(probe.log, "JNI_BOOTSTRAP_METHOD name=%s signature=%s code=%p\n", methods[i].name, methods[i].signature, methods[i].code);
    }
    ++probe.registrations;
    return 0;
}
static void fatal(void *env, const char *message) {
    (void)env; ++probe.errors;
    fprintf(probe.log, "JNI_BOOTSTRAP_FATAL %s\n", message ? message : "null");
}
int runtime_jni_bootstrap(AndroidElf *elf, FILE *log) {
    if (!elf->published || elf->relocated != 1) return -1;
    uintptr_t entry = elf_export(elf, "JNI_OnLoad");
    if (!entry) return -1;
    /* Pin the narrow probe to the inspected NativeLoader entry variant. */
    int variant = 0;
    for (size_t i = 0; i < elf->sym_count; ++i)
        if (!strcmp(elf->str + elf->sym[i].st_name, "JNI_OnLoad") && elf->sym[i].st_size == 144 && elf->sym[i].st_value == 0x91c) variant = 1;
    if (!variant) { fprintf(log, "JNI_BOOTSTRAP_SKIPPED unknown entry variant\n"); return -1; }
    memset(&probe, 0, sizeof probe);
    probe.log = log; probe.module = elf;
    probe.env = probe.env_table; probe.vm = probe.vm_table;
    probe.vm_table[4] = (void *)attach;
    probe.env_table[6] = (void *)find_class;
    probe.env_table[18] = (void *)fatal;
    probe.env_table[215] = (void *)register_natives;
    fprintf(log, "JNI_BOOTSTRAP_BEGIN actual_libmain=1 registered_methods_not_called=1\n");
    int version = ((int (*)(void *, void *))entry)(&probe.vm, NULL);
    int pass = version == 0x10006 && !probe.errors && probe.registrations == 1 && probe.load == 1 && probe.unload == 1;
    fprintf(log, "JNI_BOOTSTRAP_DONE result=%d version=%x registrations=%u load=%u unload=%u unity_started=0\n",
            pass, version, probe.registrations, probe.load, probe.unload);
    memset(&probe, 0, sizeof probe);
    return pass ? 0 : -1;
}
