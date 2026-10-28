/* Executed instead of game code to verify ELF relocation and RX/RW mapping. */
extern int fixture_host_value(void);
extern int __cxa_atexit(void (*)(void *), void *, void *);
extern int snprintf(char *, unsigned long, const char *, ...);
int fixture_format(char *out, unsigned long size) { return snprintf(out, size, "%d %.2f %s", 17, 1.25, "ok"); }
static int initialized, finalized;
static char dso_marker;
static void finish(void *arg) { ++*(int *)arg; }
__attribute__((constructor)) static void start(void) {
    initialized = 1;
    __cxa_atexit(finish, &finalized, &dso_marker);
}
int fixture_initialized(void) { return initialized; }
int fixture_finalized(void) { return finalized; }
int fixture_value = 32;
int *fixture_pointer = &fixture_value;
static volatile int private_value = 7;
static volatile int *volatile private_pointer = &private_value;
int fixture_call(void) { return *fixture_pointer + *private_pointer + fixture_host_value(); }
void fixture_write(int value) { fixture_value = value; }
