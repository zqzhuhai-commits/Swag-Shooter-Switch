#include "loader_selftest.h"
#include "nx_memory.h"
#include "bionic_basic.h"
#include "runtime_services.h"
#include <string.h>

static int fixture_host_value(void) { return 3; }
static uintptr_t fixture_resolve(void *ctx, const char *name) {
    (void)ctx;
    return !strcmp(name, "fixture_host_value") ? (uintptr_t)fixture_host_value : runtime_services_resolve(name);
}

int loader_selftest(FILE *log, int execute) {
    const char *invalid[] = {"truncated.so", "bad-magic.so", "bad-phdr.so", "bad-load.so"};
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        char invalid_path[128]; size_t ignored = 0;
        snprintf(invalid_path, sizeof invalid_path, "romfs:/tests/%s", invalid[i]);
        if (!elf_image_size(invalid_path, &ignored, log)) {
            fprintf(log, "REJECTION_TEST_FAIL %s\n", invalid[i]); return -1;
        }
        fprintf(log, "REJECTION_TEST_PASS %s\n", invalid[i]);
    }
    const char *path = "romfs:/tests/fixture.so";
    size_t size;
    NxModuleMemory mem;
    AndroidElf elf;
    if (elf_image_size(path, &size, log) || nx_memory_prepare(&mem, size, log)) return -1;
    int rc = -1;
    if (elf_load(&elf, path, mem.staging, mem.size, (uintptr_t)mem.execution, log)) goto done;
    if (!elf_relocate(&elf, NULL, NULL, log) || !elf.unresolved) {
        fprintf(log, "UNRESOLVED_REJECTION_FAIL\n"); goto done;
    }
    fprintf(log, "UNRESOLVED_REJECTION_PASS\n");
    /* Reload pristine bytes before the real link, never retry a partial image. */
    if (elf_load(&elf, path, mem.staging, mem.size, (uintptr_t)mem.execution, log) ||
        elf_relocate(&elf, fixture_resolve, NULL, log)) goto done;
    uintptr_t fn = elf_export(&elf, "fixture_call");
    uintptr_t write = elf_export(&elf, "fixture_write");
    uintptr_t ptr = elf_export(&elf, "fixture_pointer");
    uintptr_t value = elf_export(&elf, "fixture_value");
    if (!fn || !write || !ptr || !value || elf_export(&elf, "missing_symbol")) goto done;
    uintptr_t relocated_pointer = 0;
    memcpy(&relocated_pointer, elf.image + ptr - elf.bias, sizeof relocated_pointer);
    if (relocated_pointer != value) goto done;
    if (execute) {
        if (nx_memory_publish(&mem, &elf, log)) goto done;
        if (elf_run_initializers(&elf, log)) goto done;
        int (*call)(void) = (int (*)(void))fn;
        void (*set)(int) = (void (*)(int))write;
        if (call() != 42) goto done;
        set(90);
        if (call() != 100) goto done;
        set(32);
        if (runtime_loader_selftest(&elf, log)) goto done;
    }
    fprintf(log, "FIXTURE_PASS: relocation/export%s\n", execute ? "/execute/mutable-data" : " (execution skipped)");
    rc = 0;
done:
    runtime_module_remove(&elf);
    if (nx_memory_close(&mem, log)) rc = -1;
    if (rc) fprintf(log, "FIXTURE_FAIL\n");
    return rc;
}
