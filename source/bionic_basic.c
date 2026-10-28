#define _GNU_SOURCE
#include "bionic_basic.h"
#include "bionic_threads.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <malloc.h>
#include <errno.h>

/* Only scalar/pointer APIs with matching AArch64 PCS are whitelisted.
 * No FILE, pthread, stat, locale, signal, long double or z_stream layouts. */
typedef struct { const char *name; uintptr_t address; } Binding;
static int *android_errno(void) { return bionic_errno_ptr(); }
static int android_posix_memalign(void **out, size_t alignment, size_t size) {
    if (!out || alignment < sizeof(void *) || alignment % sizeof(void *) ||
        (alignment & (alignment - 1))) return EINVAL;
    int saved_errno = errno;
    void *p = memalign(alignment, size ? size : 1);
    errno = saved_errno;
    if (!p) return ENOMEM;
    *out = p;
    return 0;
}
static void *android_memrchr(const void *data, int c, size_t n) {
    const unsigned char *p = data;
    while (n) { --n; if (p[n] == (unsigned char)c) return (void *)(p + n); }
    return NULL;
}
#define BIND(fn) {#fn, (uintptr_t)(fn)}
static const Binding bindings[] = {
    BIND(malloc), BIND(calloc), BIND(realloc), BIND(free), BIND(memalign),
    {"posix_memalign", (uintptr_t)android_posix_memalign}, BIND(memcpy), BIND(memmove), BIND(memset),
    BIND(memcmp), BIND(memchr), {"memrchr", (uintptr_t)android_memrchr},
    BIND(strlen), BIND(strnlen), BIND(strcmp), BIND(strncmp), BIND(strcpy),
    BIND(strncpy), BIND(strcat), BIND(strchr), BIND(strrchr), BIND(strstr),
    BIND(strpbrk), BIND(strspn), BIND(strdup), BIND(strtok_r), BIND(strlcpy),
    BIND(strtol), BIND(strtoll), BIND(strtoul), BIND(strtoull), BIND(strtod),
    BIND(strtof), BIND(atoi), BIND(atol), BIND(atof), BIND(qsort), BIND(bsearch),
    BIND(acos), BIND(acosf), BIND(asin), BIND(asinf), BIND(atan), BIND(atanf),
    BIND(atan2), BIND(atan2f), BIND(cos), BIND(cosf), BIND(sin), BIND(sinf),
    BIND(tan), BIND(tanf), BIND(exp), BIND(expf), BIND(exp2f), BIND(log),
    BIND(logf), BIND(log10f), BIND(log2f), BIND(logb), BIND(pow), BIND(powf),
    BIND(sqrt), BIND(sqrtf), BIND(fmod), BIND(fmodf), BIND(hypot), BIND(ldexp),
    BIND(ldexpf), BIND(frexpf), BIND(modf), BIND(modff), BIND(nearbyintf),
    BIND(remainderf), BIND(scalbn), {"__errno", (uintptr_t)android_errno}
};
uintptr_t bionic_basic_resolve(const char *name) {
    for (size_t i = 0; i < sizeof bindings / sizeof *bindings; ++i)
        if (!strcmp(name, bindings[i].name)) return bindings[i].address;
    return 0;
}
size_t bionic_basic_count(void) { return sizeof bindings / sizeof *bindings; }
int bionic_basic_selftest(void) {
    void *(*alloc)(size_t) = (void *(*)(size_t))bionic_basic_resolve("malloc");
    void (*release)(void *) = (void (*)(void *))bionic_basic_resolve("free");
    void *(*copy)(void *, const void *, size_t) =
        (void *(*)(void *, const void *, size_t))bionic_basic_resolve("memcpy");
    double (*power)(double, double) = (double (*)(double, double))bionic_basic_resolve("pow");
    char *p = alloc(16);
    if (!p) return -1;
    copy(p, "android-nx", 11);
    int ok = !strcmp(p, "android-nx") && power(3.0, 2.0) == 9.0 &&
             android_memrchr("aba", 'a', 3) != NULL && !bionic_basic_resolve("pthread_create");
    release(p);
    return ok ? 0 : -1;
}
