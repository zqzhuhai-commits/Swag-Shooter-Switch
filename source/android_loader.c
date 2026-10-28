#include "runtime_services.h"
#include "bionic_basic.h"
#include "bionic_threads.h"
#include "android_tls.h"
#include "guest_memory.h"
#include "guest_text.h"
#include "guest_system.h"
#include "android_events.h"
#include "android_graphics.h"
#include <switch.h>
#include <zlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { char name[96]; AndroidElf *elf; unsigned refs; int ready, used; } Module;
static Module modules[24];
static Mutex module_lock, finalizer_lock;
static _Thread_local char dl_error_buffer[192];
static _Thread_local int dl_error_pending;
typedef struct Finalizer { void (*fn)(void *); void *arg, *dso; struct Finalizer *next; } Finalizer;
static Finalizer *finalizers;
static size_t finalizer_count;
static void error(const char *message, const char *name) {
    snprintf(dl_error_buffer, sizeof dl_error_buffer, "%s: %s", message, name ? name : "(null)");
    dl_error_pending = 1;
}
static int b_cxa_atexit(void (*fn)(void *), void *arg, void *dso) {
    if (!fn) return -1;
    Finalizer *n = malloc(sizeof *n);
    if (!n) return -1;
    *n = (Finalizer){fn, arg, dso, NULL};
    mutexLock(&finalizer_lock);
    if (finalizer_count >= 4096) { mutexUnlock(&finalizer_lock); free(n); return -1; }
    n->next = finalizers; finalizers = n; ++finalizer_count;
    mutexUnlock(&finalizer_lock); return 0;
}
static void finalize(void *dso, uintptr_t low, uintptr_t high) {
    for (;;) {
        mutexLock(&finalizer_lock);
        Finalizer **link = &finalizers;
        while (*link) {
            uintptr_t owner = (uintptr_t)(*link)->dso;
            if ((low && owner >= low && owner < high) || (!low && (!dso || (*link)->dso == dso))) break;
            link = &(*link)->next;
        }
        Finalizer *n = *link;
        if (n) { *link = n->next; --finalizer_count; }
        mutexUnlock(&finalizer_lock);
        if (!n) break;
        void (*fn)(void *) = n->fn; void *arg = n->arg; free(n); fn(arg);
    }
}
static void b_cxa_finalize(void *dso) { finalize(dso, 0, 0); }
int runtime_module_register(const char *name, AndroidElf *elf, int ready) {
    if (!name || strlen(name) >= sizeof modules[0].name || (ready && (!elf || !elf->published))) return -1;
    mutexLock(&module_lock);
    for (size_t i = 0; i < 24; ++i) if (!modules[i].used) {
        modules[i] = (Module){.elf = elf, .ready = ready, .used = 1};
        strcpy(modules[i].name, name); mutexUnlock(&module_lock); return 0;
    }
    mutexUnlock(&module_lock); return -1;
}
void runtime_module_remove(AndroidElf *elf) {
    uintptr_t low = 0, high = 0;
    mutexLock(&module_lock);
    for (size_t i = 0; i < 24; ++i) if (modules[i].used && modules[i].elf == elf) {
        low = elf->bias; high = low + elf->size;
        memset(&modules[i], 0, sizeof modules[i]);
    }
    mutexUnlock(&module_lock);
    if (low) finalize(NULL, low, high);
}
static int virtual_library(const char *name) {
    const char *names[] = {"libc.so", "libm.so", "libdl.so", "liblog.so", "libz.so", "libandroid.so", "libEGL.so", "libGLESv2.so"};
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i) if (!strcmp(name, names[i])) return 1;
    return 0;
}
static void *b_dlopen(const char *name, int flags) {
    if (flags & ~(1 | 2 | 4 | 0x100)) { error("unsupported dlopen flags", name); return NULL; }
    if (!name) return (void *)(uintptr_t)1; /* Main/global scope token. */
    const char *base = strrchr(name, '/'); base = base ? base + 1 : name;
    mutexLock(&module_lock);
    for (size_t i = 0; i < 24; ++i) if (modules[i].used && !strcmp(modules[i].name, base)) {
        if (!modules[i].ready) { mutexUnlock(&module_lock); error("module not executable/fully linked", name); return NULL; }
        ++modules[i].refs; mutexUnlock(&module_lock); return &modules[i];
    }
    if (virtual_library(base)) {
        for (size_t i = 0; i < 24; ++i) if (!modules[i].used) {
            modules[i] = (Module){.refs = 1, .ready = 1, .used = 1};
            strcpy(modules[i].name, base); mutexUnlock(&module_lock); return &modules[i];
        }
    }
    mutexUnlock(&module_lock); error("library unavailable", name); return NULL;
}
static Module *valid_handle(void *handle) {
    for (size_t i = 0; i < 24; ++i)
        if (handle == &modules[i] && modules[i].used && modules[i].refs) return &modules[i];
    return NULL;
}
static uintptr_t host_symbol(const char *name) {
    uintptr_t p = runtime_services_resolve(name);
    if (!p) p = bionic_threads_resolve(name);
    if (!p) p = android_tls_resolve(name);
    if (!p) p = bionic_basic_resolve(name);
    return p;
}
static void *b_dlsym(void *handle, const char *name) {
    if (!name || !*name) { error("invalid symbol", name); return NULL; }
    uintptr_t p = 0;
    mutexLock(&module_lock);
    if (!handle || handle == (void *)(uintptr_t)1) {
        for (size_t i = 0; i < 24 && !p; ++i)
            if (modules[i].used && modules[i].ready && modules[i].elf) p = elf_export(modules[i].elf, name);
    } else {
        Module *m = valid_handle(handle);
        if (!m) { mutexUnlock(&module_lock); error("invalid handle", name); return NULL; }
        if (m->elf) p = elf_export(m->elf, name);
    }
    mutexUnlock(&module_lock);
    if (!p) p = host_symbol(name);
    if (!p) error("symbol unavailable", name);
    return (void *)p;
}
static char *b_dlerror(void) {
    if (!dl_error_pending) return NULL;
    dl_error_pending = 0; return dl_error_buffer;
}
static int b_dlclose(void *handle) {
    if (handle == (void *)(uintptr_t)1) return 0;
    uintptr_t low = 0, high = 0;
    mutexLock(&module_lock);
    Module *m = valid_handle(handle);
    if (!m) { mutexUnlock(&module_lock); error("invalid close handle", "dlclose"); return -1; }
    --m->refs;
    if (!m->refs && m->elf) { low = m->elf->bias; high = low + m->elf->size; m->ready = 0; }
    mutexUnlock(&module_lock);
    if (low) finalize(NULL, low, high);
    /* Memory belongs to the host module owner, not to this pseudo handle. */
    return 0;
}
/* Android arm64 link.h ABI. Callback storage is a snapshot: callbacks may
 * re-enter the loader without holding module_lock. Host modules are excluded. */
typedef struct { const char *file; void *base; const char *symbol; void *address; } GuestDlInfo;
typedef struct {
    uintptr_t address; const char *name; const Elf64_Phdr *headers;
    uint16_t count;
} GuestPhdrInfo;
_Static_assert(sizeof(GuestDlInfo) == 32 && sizeof(GuestPhdrInfo) == 32, "Android linker ABI");
static int b_dladdr(const void *address, GuestDlInfo *out) {
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    uintptr_t target = (uintptr_t)address;
    mutexLock(&module_lock);
    for (size_t i = 0; i < 24; ++i) {
        Module *m = &modules[i]; AndroidElf *e = m->elf;
        if (!m->used || !m->ready || !e || target < e->bias || target - e->bias >= e->size) continue;
        out->file = m->name; out->base = (void *)e->bias;
        uintptr_t nearest = 0;
        for (size_t j = 1; j < e->sym_count; ++j) {
            const Elf64_Sym *s = &e->sym[j];
            if (s->st_shndx == SHN_UNDEF || s->st_shndx == SHN_ABS ||
                ELF64_ST_TYPE(s->st_info) == STT_TLS || !s->st_name || s->st_name >= e->str_size ||
                s->st_value >= e->size) continue;
            uintptr_t start = e->bias + s->st_value;
            if (start > target || (s->st_size && target - start >= s->st_size) || start < nearest) continue;
            nearest = start; out->symbol = e->str + s->st_name; out->address = (void *)start;
        }
        mutexUnlock(&module_lock); return 1;
    }
    mutexUnlock(&module_lock); return 0;
}
static int b_iterate_phdr(int (*callback)(GuestPhdrInfo *, size_t, void *), void *arg) {
    if (!callback) return -1;
    typedef struct { uintptr_t bias; char name[96]; Elf64_Phdr ph[32]; uint16_t count; } Snapshot;
    Snapshot *snapshot = calloc(24, sizeof *snapshot);
    if (!snapshot) return -1;
    size_t count = 0;
    mutexLock(&module_lock);
    for (size_t i = 0; i < 24; ++i) {
        Module *m = &modules[i];
        if (!m->used || !m->ready || !m->elf) continue;
        Snapshot *s = &snapshot[count++]; s->bias = m->elf->bias; s->count = m->elf->phnum;
        memcpy(s->name, m->name, sizeof s->name); memcpy(s->ph, m->elf->ph, s->count * sizeof *s->ph);
    }
    mutexUnlock(&module_lock);
    int result = 0;
    for (size_t i = 0; i < count; ++i) {
        Snapshot *s = &snapshot[i]; GuestPhdrInfo info = {s->bias, s->name, s->ph, s->count};
        result = callback(&info, sizeof info, arg); if (result) break;
    }
    free(snapshot); return result;
}
typedef struct { const char *name; uintptr_t address; } RuntimeBinding;
#define RT_BIND(name, fn) {name, (uintptr_t)(fn)}
static const RuntimeBinding bindings[] = {
    RT_BIND("dlopen", b_dlopen), RT_BIND("dlsym", b_dlsym), RT_BIND("dlclose", b_dlclose),
    RT_BIND("dlerror", b_dlerror), RT_BIND("__cxa_atexit", b_cxa_atexit), RT_BIND("__cxa_finalize", b_cxa_finalize),
    RT_BIND("dladdr", b_dladdr), RT_BIND("dl_iterate_phdr", b_iterate_phdr),
    RT_BIND("inflate", inflate), RT_BIND("inflateEnd", inflateEnd), RT_BIND("inflateInit_", inflateInit_),
    RT_BIND("inflateInit2_", inflateInit2_), RT_BIND("zlibVersion", zlibVersion)
};
uintptr_t runtime_services_resolve(const char *name) {
    uintptr_t p = runtime_io_resolve(name); if (p) return p;
    p = guest_memory_resolve(name); if (p) return p;
    p = guest_text_resolve(name); if (p) return p;
    p = guest_system_resolve(name); if (p) return p;
    p = android_events_resolve(name); if (p) return p;
    p = android_graphics_resolve(name); if (p) return p;
    for (size_t i = 0; i < sizeof bindings / sizeof *bindings; ++i)
        if (!strcmp(name, bindings[i].name)) return bindings[i].address;
    return 0;
}
static int finalizer_trace;
static int phdr_probe(GuestPhdrInfo *info, size_t bytes, void *arg) {
    int *seen = arg;
    if (bytes != 32 || !info->headers || !info->count || strcmp(info->name, "fixture.so")) return -7;
    if (!b_dlsym((void *)(uintptr_t)1, "fixture_call")) return -8;
    ++*seen; return 17;
}
static void trace(void *arg) { finalizer_trace = finalizer_trace * 10 + (int)(uintptr_t)arg; }
static void *error_worker(void *arg) {
    (void)arg; int error_count = b_dlerror() != NULL;
    error_count += b_dlsym(NULL, "worker_missing_symbol") != NULL;
    const char *message = b_dlerror(); error_count += !message || !strstr(message, "worker_missing_symbol");
    return (void *)(uintptr_t)error_count;
}
int runtime_loader_selftest(AndroidElf *elf, FILE *log) {
    int errors = 0;
    if (runtime_module_register("fixture.so", elf, 1)) return -1;
    void *h = b_dlopen("fixture.so", 2);
    errors += !h;
    void *math = b_dlopen("libm.so", 2);
    double (*root)(double) = math ? (double (*)(double))b_dlsym(math, "sqrt") : NULL;
    errors += !root || root(25.0) != 5.0;
    if (math) errors += b_dlclose(math) != 0;
    errors += b_dlopen("unavailable-library.so", 2) != NULL || b_dlerror() == NULL;
    int (*call)(void) = (int (*)(void))b_dlsym(h, "fixture_call");
    errors += !call || call() != 42;
    GuestDlInfo info; int seen = 0;
    errors += !b_dladdr((void *)call, &info) || !info.symbol || strcmp(info.symbol, "fixture_call") || info.address != (void *)call;
    errors += b_dladdr((void *)(uintptr_t)1, &info) != 0 || info.base != NULL;
    errors += b_iterate_phdr(phdr_probe, &seen) != 17 || seen != 1;
    fprintf(log, "LOADER_QUERY_DONE dladdr_and_phdr_errors=%d callback_reentry=CHECKED early_stop=CHECKED\n", errors);
    errors += b_dlsym(h, "main_missing_symbol") != NULL;
    typedef int (*Create)(uintptr_t *, const AndroidThreadAttr *, void *(*)(void *), void *);
    typedef int (*Join)(uintptr_t, void **);
    uintptr_t child; void *value = NULL;
    int rc = ((Create)bionic_threads_resolve("pthread_create"))(&child, NULL, error_worker, NULL);
    if (rc) ++errors;
    else errors += ((Join)bionic_threads_resolve("pthread_join"))(child, &value) != 0 || value != NULL;
    const char *message = b_dlerror(); errors += !message || !strstr(message, "main_missing_symbol") || b_dlerror() != NULL;
    int (*initialized)(void) = (int (*)(void))b_dlsym(h, "fixture_initialized");
    int (*finalized)(void) = (int (*)(void))b_dlsym(h, "fixture_finalized");
    int (*format)(char *, size_t) = (int (*)(char *, size_t))b_dlsym(h, "fixture_format");
    char formatted[64];
    errors += !format || format(formatted, sizeof formatted) != 10 || strcmp(formatted, "17 1.25 ok");
    errors += !initialized || initialized() != 1 || !finalized || finalized() != 0;
    errors += b_dlclose(h) != 0;
    errors += !finalized || finalized() != 1;
    errors += b_dlsym(h, "fixture_call") != NULL || b_dlerror() == NULL;
    int dso;
    finalizer_trace = 0;
    errors += b_cxa_atexit(trace, (void *)1, &dso) != 0;
    errors += b_cxa_atexit(trace, (void *)2, &dso) != 0;
    b_cxa_finalize(&dso); b_cxa_finalize(&dso);
    errors += finalizer_trace != 21;
    runtime_module_remove(elf);
    fprintf(log, "LOADER_TEST_DONE errors=%d dlerror_tls=CHECKED finalizer_order=%d\n", errors, finalizer_trace);
    return errors ? -1 : 0;
}
int runtime_zlib_selftest(FILE *log) {
    _Static_assert(sizeof(z_stream) == 112, "Android ARM64 z_stream ABI");
    const char input[] = "SWAG Switch compressed game asset probe";
    unsigned char compressed[128], output[128]; uLongf n = sizeof compressed;
    int errors = compress2(compressed, &n, (const Bytef *)input, sizeof input, 6) != Z_OK;
    z_stream stream = {0};
    int rc = inflateInit_(&stream, ZLIB_VERSION, sizeof stream);
    if (rc != Z_OK) ++errors;
    else {
        stream.next_in = compressed; stream.avail_in = n;
        stream.next_out = output; stream.avail_out = sizeof output;
        errors += inflate(&stream, Z_FINISH) != Z_STREAM_END;
        errors += stream.total_out != sizeof input || memcmp(output, input, sizeof input);
        errors += inflateEnd(&stream) != Z_OK;
    }
    fprintf(log, "ZLIB_TEST_DONE errors=%d version=%s stream_bytes=%zu\n", errors, zlibVersion(), sizeof stream);
    return errors ? -1 : 0;
}
