#define _GNU_SOURCE
#include "guest_text.h"
#include "bionic_threads.h"
#include <ctype.h>
#include <wctype.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <limits.h>

/* Android mbstate_t is four bytes; newlib's is not. The state below stores
 * only an incomplete UTF-8 prefix, never a native mbstate_t in guest memory. */
typedef struct { unsigned char bytes[4]; } AndroidMbState;
_Static_assert(sizeof(wchar_t) == 4 && sizeof(long double) == 16, "Android ARM64 scalar text ABI");
static _Thread_local AndroidMbState implicit_state;
static size_t bad_sequence(AndroidMbState *state) {
    memset(state, 0, sizeof *state); *bionic_errno_ptr() = 84; return (size_t)-1;
}
static size_t b_mbrtowc(wchar_t *out, const char *s, size_t length, AndroidMbState *state) {
    if (!state) state = &implicit_state;
    if (!s) { memset(state, 0, sizeof *state); return 0; }
    if (!length) return (size_t)-2;
    size_t pending = 0;
    while (pending < 4 && state->bytes[pending]) ++pending;
    size_t used = 0;
    if (!pending) state->bytes[pending++] = (unsigned char)s[used++];
    unsigned char lead = state->bytes[0];
    unsigned need = lead < 0x80 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 :
                    lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
    if (!need || pending > need) return bad_sequence(state);
    while (pending < need && used < length) state->bytes[pending++] = (unsigned char)s[used++];
    for (size_t i = 1; i < pending; ++i) if ((state->bytes[i] & 0xc0) != 0x80) return bad_sequence(state);
    if (pending < need) return (size_t)-2;
    uint32_t value = need == 1 ? lead : lead & (need == 2 ? 31u : need == 3 ? 15u : 7u);
    for (unsigned i = 1; i < need; ++i) value = (value << 6) | (state->bytes[i] & 63);
    if ((need == 2 && value < 0x80) || (need == 3 && value < 0x800) || (need == 4 && value < 0x10000) ||
        value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return bad_sequence(state);
    memset(state, 0, sizeof *state);
    if (out) *out = (wchar_t)value;
    return value ? used : 0;
}
static size_t b_mbrlen(const char *s, size_t n, AndroidMbState *state) { return b_mbrtowc(NULL, s, n, state); }
static int b_mbtowc(wchar_t *out, const char *s, size_t n) {
    AndroidMbState state = {0}; size_t result = b_mbrtowc(out, s, n, &state);
    if (result == (size_t)-2) { *bionic_errno_ptr() = 84; return -1; }
    return result == (size_t)-1 ? -1 : (int)result;
}
static size_t b_wcrtomb(char *out, wchar_t wc, AndroidMbState *state) {
    AndroidMbState local = {0}; if (!state) state = &local;
    memset(state, 0, sizeof *state); if (!out) return 1;
    uint32_t value = (uint32_t)wc;
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return bad_sequence(state);
    if (value < 0x80) { out[0] = value; return 1; }
    if (value < 0x800) { out[0] = 0xc0 | (value >> 6); out[1] = 0x80 | (value & 63); return 2; }
    if (value < 0x10000) { out[0] = 0xe0 | (value >> 12); out[1] = 0x80 | ((value >> 6) & 63); out[2] = 0x80 | (value & 63); return 3; }
    out[0] = 0xf0 | (value >> 18); out[1] = 0x80 | ((value >> 12) & 63);
    out[2] = 0x80 | ((value >> 6) & 63); out[3] = 0x80 | (value & 63); return 4;
}
static size_t b_mbsnrtowcs(wchar_t *out, const char **source, size_t input_bytes, size_t capacity, AndroidMbState *state) {
    if (!source || !*source) { *bionic_errno_ptr() = 22; return (size_t)-1; }
    if (!state) state = &implicit_state;
    AndroidMbState copy = *state;
    if (!out) state = &copy;
    const char *p = *source; size_t done = 0, remaining = input_bytes;
    while ((!out || done < capacity) && remaining) {
        wchar_t wc;
        size_t n = b_mbrtowc(&wc, p, remaining < 4 ? remaining : 4, state);
        if (n == (size_t)-1) { if (out) *source = p; return n; }
        if (n == (size_t)-2) { p += remaining; remaining = 0; break; }
        if (!n) { if (out) { out[done] = 0; *source = NULL; } return done; }
        if (out) out[done] = wc;
        ++done; p += n; remaining -= n;
    }
    if (out) *source = p;
    return done;
}
static size_t b_mbsrtowcs(wchar_t *out, const char **source, size_t capacity, AndroidMbState *state) {
    return b_mbsnrtowcs(out, source, SIZE_MAX, capacity, state);
}
static size_t b_wcsnrtombs(char *out, const wchar_t **source, size_t input_chars, size_t capacity, AndroidMbState *state) {
    if (!source || !*source) { *bionic_errno_ptr() = 22; return (size_t)-1; }
    const wchar_t *p = *source; size_t used = 0, count = 0;
    while (count < input_chars) {
        char encoded[4]; AndroidMbState local = {0};
        size_t n = b_wcrtomb(encoded, *p, &local);
        if (n == (size_t)-1) { if (out) *source = p; return n; }
        if (out && n > capacity - used) break;
        if (out) memcpy(out + used, encoded, n);
        if (!*p) { if (out) *source = NULL; if (state) memset(state, 0, sizeof *state); return used; }
        used += n; ++p; ++count;
    }
    if (out) *source = p;
    if (state) memset(state, 0, sizeof *state);
    return used;
}
static size_t b_wcsrtombs(char *out, const wchar_t **source, size_t capacity, AndroidMbState *state) {
    return b_wcsnrtombs(out, source, SIZE_MAX, capacity, state);
}
static wint_t b_btowc(int c) { return c >= 0 && c < 128 ? (wint_t)c : WEOF; }
static int b_wctob(wint_t c) { return c < 128 ? (int)c : -1; }
static size_t b_mb_cur_max(void) { return 4; }
static int native_category(int category) {
    const int categories[] = {LC_CTYPE, LC_NUMERIC, LC_TIME, LC_COLLATE, LC_MONETARY, LC_MESSAGES, LC_ALL};
    return category >= 0 && category < 7 ? categories[category] : -1;
}
static char *b_setlocale(int category, const char *name) {
    int native = native_category(category);
    if (native < 0) { *bionic_errno_ptr() = 22; return NULL; }
    if (name && (!*name || !strcmp(name, "C.UTF-8"))) name = "C.UTF-8";
    return setlocale(native, name);
}
static locale_t b_newlocale(int mask, const char *name, locale_t base) {
    if (mask & ~63) { *bionic_errno_ptr() = 22; return NULL; }
    int native_mask = 0;
    for (int c = 0; c < 6; ++c) if (mask & (1 << c)) native_mask |= 1 << native_category(c);
    return newlocale(native_mask, name, base);
}
/* Android's supported numeric locales use the same C/POSIX syntax. */
static long long b_strtoll_l(const char *s, char **end, int base, void *locale) { (void)locale; return strtoll(s, end, base); }
static unsigned long long b_strtoull_l(const char *s, char **end, int base, void *locale) { (void)locale; return strtoull(s, end, base); }
static long double b_strtold_l(const char *s, char **end, void *locale) { (void)locale; return strtold(s, end); }
typedef struct { const char *name; uintptr_t address; } TextBinding;
#define TEXT_BIND(name, fn) {name, (uintptr_t)(fn)}
static const TextBinding bindings[] = {
    TEXT_BIND("mbrtowc", b_mbrtowc), TEXT_BIND("mbrlen", b_mbrlen), TEXT_BIND("mbtowc", b_mbtowc),
    TEXT_BIND("mbsnrtowcs", b_mbsnrtowcs), TEXT_BIND("mbsrtowcs", b_mbsrtowcs), TEXT_BIND("wcrtomb", b_wcrtomb),
    TEXT_BIND("wcsnrtombs", b_wcsnrtombs), TEXT_BIND("wcsrtombs", b_wcsrtombs),
    TEXT_BIND("btowc", b_btowc), TEXT_BIND("wctob", b_wctob), TEXT_BIND("__ctype_get_mb_cur_max", b_mb_cur_max),
    TEXT_BIND("isalnum", isalnum), TEXT_BIND("isalpha", isalpha), TEXT_BIND("isgraph", isgraph),
    TEXT_BIND("islower", islower), TEXT_BIND("isprint", isprint), TEXT_BIND("isspace", isspace),
    TEXT_BIND("isupper", isupper), TEXT_BIND("isxdigit", isxdigit), TEXT_BIND("tolower", tolower), TEXT_BIND("toupper", toupper),
    TEXT_BIND("iswalpha", iswalpha), TEXT_BIND("iswblank", iswblank), TEXT_BIND("iswcntrl", iswcntrl),
    TEXT_BIND("iswdigit", iswdigit), TEXT_BIND("iswlower", iswlower), TEXT_BIND("iswprint", iswprint),
    TEXT_BIND("iswpunct", iswpunct), TEXT_BIND("iswspace", iswspace), TEXT_BIND("iswupper", iswupper),
    TEXT_BIND("iswxdigit", iswxdigit), TEXT_BIND("towlower", towlower), TEXT_BIND("towupper", towupper),
    TEXT_BIND("wmemchr", wmemchr), TEXT_BIND("wmemcmp", wmemcmp), TEXT_BIND("wmemcpy", wmemcpy),
    TEXT_BIND("wmemmove", wmemmove), TEXT_BIND("wmemset", wmemset), TEXT_BIND("wcslen", wcslen),
    TEXT_BIND("wcscoll", wcscoll), TEXT_BIND("wcsxfrm", wcsxfrm), TEXT_BIND("wcstod", wcstod),
    TEXT_BIND("wcstof", wcstof), TEXT_BIND("wcstol", wcstol), TEXT_BIND("wcstold", wcstold),
    TEXT_BIND("wcstoll", wcstoll), TEXT_BIND("wcstoul", wcstoul), TEXT_BIND("wcstoull", wcstoull),
    TEXT_BIND("strcoll", strcoll), TEXT_BIND("strxfrm", strxfrm), TEXT_BIND("setlocale", b_setlocale),
    TEXT_BIND("newlocale", b_newlocale), TEXT_BIND("uselocale", uselocale), TEXT_BIND("freelocale", freelocale),
    TEXT_BIND("localeconv", localeconv), TEXT_BIND("strtoll_l", b_strtoll_l), TEXT_BIND("strtoull_l", b_strtoull_l),
    TEXT_BIND("strtold_l", b_strtold_l)
};
uintptr_t guest_text_resolve(const char *name) {
    for (size_t i = 0; i < sizeof bindings / sizeof *bindings; ++i)
        if (!strcmp(name, bindings[i].name)) return bindings[i].address;
    return 0;
}
int guest_text_selftest(FILE *log) {
    int errors = 0;
    struct { uint32_t before; AndroidMbState state; uint32_t after; } box = {0x12345678, {{0}}, 0x87654321};
    wchar_t wc;
    errors += b_mbrtowc(&wc, "\xe4", 1, &box.state) != (size_t)-2;
    errors += b_mbrtowc(&wc, "\xb8\xad", 2, &box.state) != 2 || wc != 0x4e2d;
    errors += b_mbrtowc(&wc, "\xf0\x9f\x98\x80", 4, &box.state) != 4 || wc != 0x1f600;
    errors += b_mbrtowc(&wc, "\xc0\xaf", 2, &box.state) != (size_t)-1 || *bionic_errno_ptr() != 84;
    errors += box.before != 0x12345678 || box.after != 0x87654321;
    const char sample[] = "A\xe4\xb8\xad\xf0\x9f\x98\x80";
    const char *s = sample; wchar_t wide[8]; AndroidMbState state = {{0}};
    errors += b_mbsrtowcs(wide, &s, 8, &state) != 3 || s != NULL;
    const wchar_t *w = wide; char encoded[32];
    errors += b_wcsrtombs(encoded, &w, sizeof encoded, &state) != 8 || w != NULL || strcmp(encoded, sample);
    fprintf(log, "TEXT_ABI_DONE errors=%d mbstate_bytes=%zu utf8=CHECKED bindings=%zu\n", errors, sizeof(AndroidMbState), sizeof bindings / sizeof *bindings);
    return errors ? -1 : 0;
}
