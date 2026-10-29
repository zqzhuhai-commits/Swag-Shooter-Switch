#define _GNU_SOURCE
#include "runtime_services.h"
#include "bionic_threads.h"
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>

/* LP64 Bionic __sFILE is 152 bytes. Only documented flags/descriptor offsets
 * are mirrored; native FILE internals stay in the native C library. */
typedef struct { _Alignas(8) unsigned char bytes[152]; } AndroidFile;
_Static_assert(sizeof(AndroidFile) == 152, "Bionic LP64 __sFILE stride");
typedef struct Stream { AndroidFile proxy; FILE *native; int readable; char path[1024]; struct Stream *next; } Stream;
typedef struct FdPath { int fd, native_fd, readable; char path[1024]; struct FdPath *next; } FdPath;
static FdPath *fd_paths;
static AndroidFile standards[3];
static FILE *standard_native[3], *game_log;
static Stream *streams;
static Mutex io_lock;
static unsigned next_alias=0x10000;
static Mutex random_lock;
static int random_initialized;
static Result random_ready(void) {
    mutexLock(&random_lock);
    Result rc=0;
    if(!random_initialized) { rc=csrngInitialize(); if(R_SUCCEEDED(rc)) random_initialized=1; }
    mutexUnlock(&random_lock); return rc;
}
static char executable_path[1024];
void runtime_io_set_executable_path(const char *path) {
    executable_path[0]=0;
    if(path && strlen(path)<sizeof executable_path) strcpy(executable_path,path);
}
static int fail(int e) { *bionic_errno_ptr() = bionic_error(e); return -1; }
int runtime_io_native_fd(int fd) {
    int result=fd; mutexLock(&io_lock);
    for(FdPath *p=fd_paths;p;p=p->next) if(p->fd==fd) { result=p->native_fd; break; }
    mutexUnlock(&io_lock); return result;
}
static int b_dup(int fd) {
    FdPath *copy=malloc(sizeof *copy); if(!copy) return fail(ENOMEM);
    mutexLock(&io_lock); FdPath *original=fd_paths;
    while(original && original->fd!=fd) original=original->next;
    if(!original) { mutexUnlock(&io_lock); free(copy); return fail(EBADF); }
    *copy=*original; copy->fd=(int)next_alias++; copy->next=fd_paths; fd_paths=copy;
    mutexUnlock(&io_lock);
    fprintf(game_log,"IO_DUP source=%d alias=%d shared_native=%d\n",fd,copy->fd,copy->native_fd); return copy->fd;
}
static void flags(AndroidFile *p, FILE *f) {
    uint32_t v = (feof(f) ? 32u : 0) | (ferror(f) ? 64u : 0);
    uint32_t *word = (uint32_t *)(p->bytes + 16);
    __atomic_store_n(word, (__atomic_load_n(word, __ATOMIC_RELAXED) & ~96u) | v, __ATOMIC_RELAXED);
}
static FILE *native_stream(void *raw) {
    for (size_t i = 0; i < 3; ++i) if (raw == &standards[i]) return standard_native[i];
    mutexLock(&io_lock);
    Stream *s = streams;
    while (s && raw != &s->proxy) s = s->next;
    FILE *f = s ? s->native : NULL;
    mutexUnlock(&io_lock);
    if (!f) fail(EBADF);
    return f;
}
static int map_path(const char *path, int writing, char out[1024]) {
    static unsigned trace_count;
    if(game_log && __atomic_fetch_add(&trace_count,1,__ATOMIC_RELAXED)<1000)
        fprintf(game_log,"IO_PATH_REQUEST path=%s writing=%d\n",path ? path : "(null)",writing);
    if (!path || !*path) return fail(ENOENT);
    const char *tail = path;
    int save = 0;
    if (!strcmp(tail, "userdata") || !strcmp(tail, "/sdcard") || !strcmp(tail, "sdmc:/switch/swag-userdata")) { tail = ""; save = 1; }
    else if (!strcmp(tail, "romfs:/game") || !strcmp(tail, "/roms/ports/swag")) tail = "";
    else if (!strncmp(tail, "userdata/", 9)) { tail += 9; save = 1; }
    else if (!strncmp(tail, "/sdcard/", 8)) { tail += 8; save = 1; }
    /* Unity's extracted IL2CPP cache is a dedicated virtual mount. */
    else if (!strcmp(tail,"/il2cpp") || !strncmp(tail,"/il2cpp/",8)) { tail += 1; save = 1; }
    else if (!strncmp(tail, "/data/data/", 11)) { tail += 11; save = 1; }
    else if (!strncmp(tail, "/data/user/0/", 13)) { tail += 13; save = 1; }
    else if (!strncmp(tail, "sdmc:/switch/swag-userdata/", 27)) { tail += 27; save = 1; }
    else if (!strncmp(tail, "romfs:/game/", 12)) tail += 12;
    else if (!strncmp(tail, "/roms/ports/swag/", 17)) tail += 17;
    else if (*tail == '/' || strchr(tail, ':')) return fail(ENOENT);
    if (writing && !save) return fail(EROFS);
    /* Canonicalize inside the selected root; never cross between roots. */
    char normalized[768]; size_t length = 0, starts[128], depth = 0;
    while (*tail) {
        while (*tail == '/') ++tail;
        const char *end = strchr(tail, '/');
        size_t n = end ? (size_t)(end - tail) : strlen(tail);
        if (!n) break;
        if (n == 1 && tail[0] == '.') { tail += n; continue; }
        if (n == 2 && !memcmp(tail, "..", 2)) {
            if (!depth) return fail(EACCES);
            length = starts[--depth];
        } else {
            if (depth >= 128 || length + n + 2 > sizeof normalized || memchr(tail, ':', n)) return fail(ENAMETOOLONG);
            starts[depth++] = length;
            if (length) normalized[length++] = '/';
            memcpy(normalized + length, tail, n); length += n;
        }
        tail += n;
    }
    normalized[length] = 0;
    if (!save && !strcmp(normalized,"assets")) normalized[0]=0;
    if (!save && !strncmp(normalized,"assets/",7)) memmove(normalized,normalized+7,strlen(normalized+7)+1);
    snprintf(out, 1024, "%s/%s", save ? "sdmc:/switch/swag-userdata" : "romfs:/game", normalized);
    return 0;
}
void runtime_services_init(FILE *log) {
    game_log = log;
    standard_native[0] = stdin; standard_native[1] = log; standard_native[2] = log;
    for (int i = 0; i < 3; ++i) {
        int mode = i ? 10 : 4;
        memcpy(standards[i].bytes + 16, &mode, 4);
        memcpy(standards[i].bytes + 20, &i, 4);
    }
    if (mkdir("sdmc:/switch/swag-userdata", 0777) && errno != EEXIST)
        fprintf(log, "IO_SAVE_ROOT_FAIL native_errno=%d\n", errno);
}
/* Per-session POSIX permission metadata for the guest-owned SD files.
 * Horizon's devoptab has no chmod syscall. Enforce new write opens rather
 * than returning success without changing guest-visible behaviour. */
typedef struct { char path[1024]; unsigned mode; } GuestMode;
static GuestMode guest_modes[128]; static unsigned guest_mode_count;
static int writable_mode(const char *path) {
    int allowed=1; mutexLock(&io_lock);
    for(unsigned i=0;i<guest_mode_count;++i) if(!strcmp(path,guest_modes[i].path)) { allowed=!!(guest_modes[i].mode&0200); break; }
    mutexUnlock(&io_lock);
    if(!allowed) {
        struct stat current;
        if(stat(path,&current) && errno==ENOENT) {
            mutexLock(&io_lock);
            for(unsigned i=0;i<guest_mode_count;++i) if(!strcmp(path,guest_modes[i].path)) { guest_modes[i]=guest_modes[--guest_mode_count]; break; }
            mutexUnlock(&io_lock); allowed=1;
        }
        if(!allowed) fprintf(game_log,"IO_PERMISSION_DENIED path=%s\n",path);
    }
    return allowed;
}
static void *b_fopen(const char *path, const char *mode) {
    char real[1024];
    if (!mode || !*mode || map_path(path, mode[0] != 'r' || strchr(mode, '+'), real)) return NULL;
    if ((mode[0]!='r' || strchr(mode,'+')) && !writable_mode(real)) { fail(EACCES); return NULL; }
    FILE *f = fopen(real, mode);
    if (!f) { int e=errno; fprintf(game_log,"IO_FOPEN_FAIL path=%s native_errno=%d\n",real,e); fail(e); return NULL; }
    Stream *s = calloc(1, sizeof *s);
    if (!s) { fclose(f); fail(ENOMEM); return NULL; }
    s->native = f;
    s->readable = mode[0] == 'r' || strchr(mode, '+') != NULL;
    strcpy(s->path, real);
    int descriptor = fileno(f), mode_bits = strchr(mode, '+') ? 16 : mode[0] == 'r' ? 4 : 8;
    memcpy(s->proxy.bytes + 16, &mode_bits, 4); memcpy(s->proxy.bytes + 20, &descriptor, 4);
    mutexLock(&io_lock); s->next = streams; streams = s; mutexUnlock(&io_lock);
    return &s->proxy;
}
static int b_fclose(void *raw) {
    mutexLock(&io_lock);
    Stream **link = &streams;
    while (*link && raw != &(*link)->proxy) link = &(*link)->next;
    Stream *s = *link;
    if (s) *link = s->next;
    mutexUnlock(&io_lock);
    if (!s) return fail(EBADF);
    int rc = fclose(s->native), e = errno; free(s);
    return rc ? fail(e) : 0;
}
static size_t b_fread(void *ptr, size_t size, size_t count, void *raw) {
    FILE *f = native_stream(raw); if (!f) return 0;
    size_t n = fread(ptr, size, count, f); flags(raw, f);
    if (ferror(f)) fail(errno);
    return n;
}
static size_t b_fwrite(const void *ptr, size_t size, size_t count, void *raw) {
    FILE *f = native_stream(raw); if (!f) return 0;
    size_t n = fwrite(ptr, size, count, f); flags(raw, f);
    if (ferror(f)) fail(errno);
    return n;
}
static int b_fseek(void *raw, int64_t offset, int origin) {
    FILE *f = native_stream(raw); if (!f) return -1;
    int rc = fseeko(f, offset, origin); flags(raw, f); return rc ? fail(errno) : 0;
}
static int64_t b_ftell(void *raw) {
    FILE *f = native_stream(raw); if (!f) return -1;
    off_t n = ftello(f); if (n < 0) fail(errno); return n;
}
static int b_fflush(void *raw) {
    FILE *f = raw ? native_stream(raw) : NULL;
    if (raw && !f) return -1;
    return fflush(f) ? fail(errno) : 0;
}
static int b_feof(void *raw) { FILE *f = native_stream(raw); return f ? feof(f) : 0; }
static int b_ferror(void *raw) { FILE *f = native_stream(raw); return f ? ferror(f) : 1; }
static void b_clearerr(void *raw) { FILE *f = native_stream(raw); if (f) { clearerr(f); flags(raw, f); } }
static int b_getc(void *raw) { FILE *f = native_stream(raw); if (!f) return -1; int n = getc(f); flags(raw, f); return n; }
static int b_fputc(int c, void *raw) { FILE *f = native_stream(raw); if (!f) return -1; int n = fputc(c, f); flags(raw, f); return n; }
static int b_fputs(const char *s, void *raw) { FILE *f = native_stream(raw); return f ? fputs(s, f) : -1; }
static char *b_fgets(char *s, int n, void *raw) { FILE *f = native_stream(raw); if (!f) return NULL; char *p = fgets(s, n, f); flags(raw, f); return p; }
static int b_vfprintf(void *raw, const char *fmt, va_list args) {
    FILE *f = native_stream(raw); return f ? vfprintf(f, fmt, args) : -1;
}
static int b_fprintf(void *raw, const char *fmt, ...) {
    va_list args; va_start(args, fmt); int n = b_vfprintf(raw, fmt, args); va_end(args); return n;
}
static int b_printf(const char *fmt, ...) {
    va_list args; va_start(args, fmt); int n = vfprintf(game_log, fmt, args); va_end(args); return n;
}
static int b_vprintf(const char *fmt, va_list args) { return vfprintf(game_log, fmt, args); }
static int b_puts(const char *s) { return fprintf(game_log, "%s\n", s) < 0 ? -1 : 0; }
static int b_fscanf(void *raw, const char *fmt, ...) {
    FILE *f = native_stream(raw); if (!f) return -1;
    va_list args; va_start(args, fmt); int n = vfscanf(f, fmt, args); va_end(args); return n;
}
static int b_setvbuf(void *raw, char *buffer, int mode, size_t n) {
    FILE *f = native_stream(raw); if (!f || mode < 0 || mode > 2) return fail(EINVAL);
    return setvbuf(f, buffer, mode == 0 ? _IOFBF : mode == 1 ? _IOLBF : _IONBF, n);
}
static int b_fileno(void *raw) {
    for (int i = 0; i < 3; ++i) if (raw == &standards[i]) return i;
    FILE *f = native_stream(raw); return f ? fileno(f) : -1;
}
static int open_flags(int android, int *native) {
    if (android & ~(3 | 0x40 | 0x80 | 0x200 | 0x400 | 0x800 | 0x8000 | 0x80000 | 0x101000 | 0x10000 | 0x20000)) {
        fprintf(game_log,"IO_OPEN_FLAGS_UNSUPPORTED flags=%x\n",android); return fail(ENOTSUP);
    }
    if ((android & 3) == 3) return fail(EINVAL);
    *native = (android & 3) == 0 ? O_RDONLY : (android & 3) == 1 ? O_WRONLY : O_RDWR;
    if (android & 0x40) *native |= O_CREAT;
    if (android & 0x80) *native |= O_EXCL;
    if (android & 0x200) *native |= O_TRUNC;
    if (android & 0x400) *native |= O_APPEND;
    if (android & 0x800) *native |= O_NONBLOCK;
    if (android & 0x101000) *native |= O_SYNC;
    if (android & 0x10000) *native |= O_DIRECTORY;
    if (android & 0x20000) *native |= O_NOFOLLOW;
    return 0;
}
static int b_open(const char *path, int options, ...) {
    char real[1024]; int native;
    if(path && (!strcmp(path,"/dev/urandom") || !strcmp(path,"/dev/random"))) {
        if(options & ~(0x800|0x80000)) return fail(EINVAL);
        Result rc=random_ready();
        if(R_FAILED(rc)) { fprintf(game_log,"IO_RANDOM_INIT_FAIL result=%x\n",rc); return fail(EIO); }
        FdPath *record=calloc(1,sizeof *record); if(!record) return fail(ENOMEM);
        record->native_fd=-2; record->readable=1; strcpy(record->path,path);
        mutexLock(&io_lock); record->fd=next_alias++; record->next=fd_paths; fd_paths=record; mutexUnlock(&io_lock);
        fprintf(game_log,"IO_RANDOM_OPEN fd=%d path=%s source=csrng\n",record->fd,path);
        return record->fd;
    }
    if (open_flags(options, &native) || map_path(path, (options & 3) != 0 || (options & 0x240), real)) return -1;
    fprintf(game_log,"IO_OPEN_TRANSLATED path=%s flags=%x native_flags=%x\n",real,options,native);
    unsigned mode = 0666;
    if (options & 0x40) { va_list args; va_start(args, options); mode = va_arg(args, unsigned); va_end(args); }
    if (((options&3)!=0 || (options&0x240)) && !writable_mode(real)) return fail(EACCES);
    int fd = open(real, native, mode);
    if (fd < 0) { int e=errno; fprintf(game_log,"IO_OPEN_FAIL path=%s native_errno=%d\n",real,e); return fail(e); }
    FdPath *record = malloc(sizeof *record);
    if (!record) { close(fd); return fail(ENOMEM); }
    record->fd = fd; record->native_fd=fd; record->readable = (options & 3) != 1; strcpy(record->path, real);
    mutexLock(&io_lock); record->next = fd_paths; fd_paths = record; mutexUnlock(&io_lock);
    return fd;
}
static ssize_t b_read(int fd, void *out, size_t n) {
    int native=runtime_io_native_fd(fd);
    if(native==-2) {
        if(n>(size_t)SSIZE_MAX) return fail(EINVAL);
        if(!n) return 0;
        if(!out) return fail(EFAULT);
        Result rc=csrngGetRandomBytes(out,n);
        if(R_FAILED(rc)) { fprintf(game_log,"IO_RANDOM_READ_FAIL result=%x bytes=%zu\n",rc,n); return fail(EIO); }
        return n;
    }
    ssize_t r=read(native,out,n); return r<0 ? fail(errno) : r;
}
static ssize_t b_write(int fd, const void *data, size_t n) {
    if (fd == 1 || fd == 2) return fwrite(data, 1, n, game_log);
    ssize_t r = write(runtime_io_native_fd(fd), data, n); if(r<0) { int e=errno; fprintf(game_log,"IO_WRITE_FAIL fd=%d bytes=%zu errno=%d\n",fd,n,e); return fail(e); } return r;
}
static int b_close(int fd) {
    if(fd<3) return fail(EBADF);
    mutexLock(&io_lock);
    FdPath **link=&fd_paths; while(*link && (*link)->fd!=fd) link=&(*link)->next;
    int native=fd,remaining=0;
    if(*link) {
        FdPath *old=*link; native=old->native_fd; *link=old->next; free(old);
        for(FdPath *p=fd_paths;p;p=p->next) if(p->native_fd==native) { remaining=1; break; }
    } else if(fd>=0x10000) { mutexUnlock(&io_lock); return fail(EBADF); }
    int rc=(remaining || native==-2) ? 0 : close(native), saved=errno;
    mutexUnlock(&io_lock);
    fprintf(game_log,"IO_CLOSE fd=%d native=%d result=%d retained=%d\n",fd,native,rc,remaining);
    return rc ? fail(saved) : 0;
}
int64_t runtime_io_read_at(int fd, void *buffer, size_t size, int64_t offset) {
    if (offset < 0) { errno = EINVAL; return -1; }
    char path[1024] = {0}; int readable = 0;
    mutexLock(&io_lock);
    for (FdPath *p = fd_paths; p; p = p->next) if (p->fd == fd) { strcpy(path, p->path); readable = p->readable; break; }
    if (!*path) for (Stream *s = streams; s; s = s->next)
        if (fileno(s->native) == fd) { strcpy(path, s->path); readable = s->readable; break; }
    mutexUnlock(&io_lock);
    if (!*path) { errno = ENOTSUP; return -1; }
    if (!readable) { errno = EACCES; return -1; }
    /* Reopen a known resource path rather than changing the caller's file
     * position. The SDK has no positioned-read syscall. Unlinked/renamed-open
     * files are not emulated by this extracted-resource backend. */
    FILE *file = fopen(path, "rb"); if (!file) return -1;
    if (fseeko(file, offset, SEEK_SET)) { int e = errno; fclose(file); errno = e; return -1; }
    size_t n = fread(buffer, 1, size, file); int e = ferror(file) ? errno : 0;
    fclose(file); if (e) { errno = e; return -1; } return n;
}
static int64_t b_lseek(int fd, int64_t off, int origin) { int native=runtime_io_native_fd(fd); if(native==-2) return fail(ESPIPE); off_t r = lseek(native, off, origin); return r < 0 ? fail(errno) : r; }
static int b_ftruncate(int fd, int64_t n) { return ftruncate(runtime_io_native_fd(fd), n) ? fail(errno) : 0; }
static int b_truncate(const char *path,int64_t length) {
    if (length<0) { *bionic_errno_ptr()=22; return -1; }
    char real[1024]; if(map_path(path,1,real)) return -1;
    if(!writable_mode(real)) return fail(EACCES);
    int fd=open(real,O_WRONLY);
    if(fd<0) return fail(errno);
    int rc=ftruncate(fd,length), saved=errno;
    int closed=close(fd), close_error=errno;
    if(rc) return fail(saved);
    return closed ? fail(close_error) : 0;
}
static void b_perror(const char *prefix) {
    int saved=*bionic_errno_ptr();
    char *(*message)(int)=(char *(*)(int))runtime_services_resolve("strerror");
    const char *text=message ? message(saved) : "Android runtime error";
    fprintf(stderr,"%s%s%s\n",prefix && *prefix ? prefix : "",prefix && *prefix ? ": " : "",text);
    *bionic_errno_ptr()=saved;
}

static int path_op(const char *path, int op, int mode) {
    char real[1024]; if (map_path(path, op != 0, real)) return -1;
    if(op==0 && (mode&2) && !writable_mode(real)) return fail(EACCES);
    int rc = op == 0 ? access(real, mode) : op == 1 ? mkdir(real, mode) : op == 2 ? unlink(real) : rmdir(real);
    if(rc) { int e=errno; fprintf(game_log,"IO_PATH_OP_FAIL path=%s op=%d mode=%d native_errno=%d\n",real,op,mode,e); return fail(e); }
    fprintf(game_log,"IO_PATH_OP_OK path=%s op=%d\n",real,op);
    if(op==2 || op==3) {
        mutexLock(&io_lock);
        for(unsigned i=0;i<guest_mode_count;++i) if(!strcmp(real,guest_modes[i].path)) { guest_modes[i]=guest_modes[--guest_mode_count]; break; }
        mutexUnlock(&io_lock);
    }
    return 0;
}
/* Horizon may reject opening a second handle while a writer is active.
 * POSIX stat/chmod must still work on an open file, so use its owned handle. */
static int stat_guest_path(const char *path,struct stat *status) {
    mutexLock(&io_lock);
    for(FdPath *p=fd_paths;p;p=p->next) if(!strcmp(path,p->path)) {
        int rc=fstat(p->native_fd,status), saved=errno;
        fprintf(game_log,"IO_STAT_OPEN_HANDLE path=%s fd=%d result=%d errno=%d\n",path,p->fd,rc,rc ? saved : 0);
        mutexUnlock(&io_lock); errno=saved; return rc;
    }
    for(Stream *p=streams;p;p=p->next) if(!strcmp(path,p->path)) {
        int rc=fstat(fileno(p->native),status), saved=errno;
        mutexUnlock(&io_lock); errno=saved; return rc;
    }
    mutexUnlock(&io_lock); return stat(path,status);
}
static int b_chmod(const char *path,unsigned mode) {
    char real[1024]; if(map_path(path,1,real)) return -1;
    struct stat status; if(stat_guest_path(real,&status)) { int e=errno; fprintf(game_log,"IO_CHMOD_STAT_FAIL path=%s errno=%d\n",real,e); return fail(e); }
    mutexLock(&io_lock); unsigned index=0;
    while(index<guest_mode_count && strcmp(real,guest_modes[index].path)) ++index;
    if(index==128) { mutexUnlock(&io_lock); return fail(ENOMEM); }
    if(index==guest_mode_count) { strcpy(guest_modes[index].path,real); ++guest_mode_count; }
    guest_modes[index].mode=mode&0777; mutexUnlock(&io_lock);
    fprintf(game_log,"IO_CHMOD_VIRTUAL path=%s mode=%o scope=SESSION new_write_opens=ENFORCED\n",real,mode&0777); return 0;
}
static int b_access(const char *path, int mode) { return path_op(path, 0, mode); }
static int b_mkdir(const char *path, unsigned mode) { return path_op(path, 1, mode); }
static int b_unlink(const char *path) { return path_op(path, 2, 0); }
static int b_rmdir(const char *path) { return path_op(path, 3, 0); }
static int b_rename(const char *old, const char *name) {
    char a[1024], b[1024]; if (map_path(old, 1, a) || map_path(name, 1, b)) return -1;
    int rc=rename(a,b), e=errno; fprintf(game_log,"IO_RENAME old=%s new=%s result=%d errno=%d\n",a,b,rc,rc ? e : 0); return rc ? fail(e) : 0;
}
typedef struct {
    uint64_t dev, ino;
    uint32_t mode, nlink, uid, gid;
    uint64_t rdev, pad1;
    int64_t size;
    int32_t blksize, pad2;
    int64_t blocks, atime;
    uint64_t atime_nsec;
    int64_t mtime;
    uint64_t mtime_nsec;
    int64_t ctime;
    uint64_t ctime_nsec;
    uint32_t unused[2];
} AndroidStat;
typedef struct { uint64_t ino; int64_t off; uint16_t reclen; uint8_t type; char name[256]; } AndroidDirent;
typedef struct { DIR *native; AndroidDirent current; int64_t serial; } AndroidDir;
_Static_assert(sizeof(AndroidStat) == 128 && offsetof(AndroidStat, size) == 48, "Android ARM64 stat layout");
_Static_assert(sizeof(AndroidDirent) == 280 && offsetof(AndroidDirent, name) == 19, "Android dirent layout");
_Static_assert(S_IFREG == 0100000 && S_IFDIR == 0040000 && DT_REG == 8 && DT_DIR == 4, "native file type numbering");
static int convert_stat(const struct stat *s, AndroidStat *out) {
    if (!out) return fail(EFAULT);
    if (s->st_blksize > INT_MAX) return fail(EOVERFLOW);
    *out = (AndroidStat){.dev=s->st_dev, .ino=s->st_ino, .mode=s->st_mode,
        .nlink=s->st_nlink, .uid=s->st_uid, .gid=s->st_gid, .rdev=s->st_rdev,
        .size=s->st_size, .blksize=s->st_blksize, .blocks=s->st_blocks,
        .atime=s->st_atim.tv_sec, .atime_nsec=s->st_atim.tv_nsec,
        .mtime=s->st_mtim.tv_sec, .mtime_nsec=s->st_mtim.tv_nsec,
        .ctime=s->st_ctim.tv_sec, .ctime_nsec=s->st_ctim.tv_nsec};
    return 0;
}
static int b_stat(const char *path, AndroidStat *out) {
    char real[1024]; struct stat s;
    if (map_path(path, 0, real)) return -1;
    int rc=stat_guest_path(real,&s);
    if(rc) { int e=errno; fprintf(game_log,"IO_STAT_FAIL path=%s native_errno=%d\n",real,e); return fail(e); }
    int converted=convert_stat(&s,out);
    if(!converted) {
        mutexLock(&io_lock);
        for(unsigned i=0;i<guest_mode_count;++i) if(!strcmp(real,guest_modes[i].path)) { out->mode=(out->mode&~0777u)|guest_modes[i].mode; break; }
        mutexUnlock(&io_lock);
    }
    return converted;
}
static int b_lstat(const char *path, AndroidStat *out) {
    char real[1024]; struct stat s;
    if (map_path(path, 0, real)) return -1;
    return lstat(real, &s) ? fail(errno) : convert_stat(&s, out);
}
static int b_fstat(int fd, AndroidStat *out) {
    int native=runtime_io_native_fd(fd); struct stat s;
    if(native==-2) { memset(&s,0,sizeof s); s.st_mode=S_IFCHR|0444; s.st_nlink=1; s.st_blksize=4096; return convert_stat(&s,out); }
    return fstat(native,&s) ? fail(errno) : convert_stat(&s,out);
}
static void *b_opendir(const char *path) {
    char real[1024]; if (map_path(path, 0, real)) return NULL;
    DIR *d = opendir(real); if (!d) { fail(errno); return NULL; }
    AndroidDir *out = calloc(1, sizeof *out);
    if (!out) { closedir(d); fail(ENOMEM); return NULL; }
    out->native = d; return out;
}
static AndroidDirent *b_readdir(void *raw) {
    if (!raw) { fail(EBADF); return NULL; }
    AndroidDir *d = raw; errno = 0;
    struct dirent *entry = readdir(d->native);
    if (!entry) { if (errno) fail(errno); return NULL; }
    size_t n = strlen(entry->d_name);
    if (n > 255) { fail(ENAMETOOLONG); return NULL; }
    memset(&d->current, 0, sizeof d->current);
    d->current.ino = entry->d_ino; d->current.off = ++d->serial;
    d->current.type = entry->d_type; d->current.reclen = sizeof d->current;
    memcpy(d->current.name, entry->d_name, n + 1); return &d->current;
}
static int b_closedir(void *raw) {
    if (!raw) return fail(EBADF);
    AndroidDir *d = raw; int rc = closedir(d->native), e = errno;
    free(d); return rc ? fail(e) : 0;
}
static char *b_getcwd(char *out, size_t size) {
    const char *cwd = "/roms/ports/swag";
    if (!out && !size) size = strlen(cwd) + 1;
    if (size <= strlen(cwd)) { fail(ERANGE); return NULL; }
    if (!out) { out = malloc(size); if (!out) { fail(ENOMEM); return NULL; } }
    strcpy(out, cwd); return out;
}
static char *b_realpath(const char *path, char *out) {
    char real[1024], virtual_path[1024];
    if (map_path(path, 0, real)) return NULL;
    if (access(real, F_OK)) { fail(errno); return NULL; }
    int resource = !strncmp(real, "romfs:/game/", 12);
    const char *prefix = resource ? "/roms/ports/swag/" : "/sdcard/";
    const char *tail = real + (resource ? 12 : 27);
    size_t prefix_size = strlen(prefix), tail_size = strlen(tail);
    if (prefix_size + tail_size + 1 > sizeof virtual_path) { fail(ENAMETOOLONG); return NULL; }
    memcpy(virtual_path, prefix, prefix_size); memcpy(virtual_path + prefix_size, tail, tail_size + 1);
    if (!out) { out = malloc(strlen(virtual_path) + 1); if (!out) { fail(ENOMEM); return NULL; } }
    strcpy(out, virtual_path); return out;
}
static ssize_t b_readlink(const char *path,char *buffer,size_t size) {
    if(!path || !buffer) return fail(EFAULT);
    if(!size || size>(size_t)SSIZE_MAX) return fail(EINVAL);
    char target[1024]={0};
    if(!strcmp(path,"/proc/self/exe")) {
        if(!*executable_path) return fail(ENOENT);
        strcpy(target,executable_path);
    } else if(!strncmp(path,"/proc/self/fd/",14)) {
        const char *number=path+14; unsigned long descriptor=0;
        if(!*number) return fail(ENOENT);
        for(const char *p=number;*p;++p) {
            if(*p<'0' || *p>'9' || descriptor>((unsigned long)INT_MAX-(*p-'0'))/10) return fail(ENOENT);
            descriptor=descriptor*10+(*p-'0');
        }
        mutexLock(&io_lock);
        for(FdPath *p=fd_paths;p;p=p->next) if(p->fd==(int)descriptor) { strcpy(target,p->path); break; }
        mutexUnlock(&io_lock);
        if(!*target) return fail(ENOENT);
        const char *prefix=NULL,*tail=NULL;
        if(!strncmp(target,"romfs:/game/",12)) { prefix="/roms/ports/swag/"; tail=target+12; }
        else if(!strncmp(target,"sdmc:/switch/swag-userdata/",27)) { prefix="/sdcard/"; tail=target+27; }
        if(prefix) {
            char translated[1024];
            if(strlen(prefix)+strlen(tail)>=sizeof translated) return fail(ENAMETOOLONG);
            strcpy(translated,prefix); strcat(translated,tail); strcpy(target,translated);
        }
    } else {
        char real[1024]; struct stat status;
        if(map_path(path,0,real)) return -1;
        if(stat_guest_path(real,&status)) return fail(errno);
        return fail(EINVAL); /* Mounted Horizon files are not symbolic links. */
    }
    size_t count=strlen(target); if(count>size) count=size;
    memcpy(buffer,target,count); /* POSIX readlink never appends a terminator. */
    if(game_log) { fprintf(game_log,"IO_READLINK path=%s target=%s returned=%zu truncated=%d\n",path,target,count,count<strlen(target)); fflush(game_log); }
    return count;
}
typedef struct { const char *name; uintptr_t address; } IoBinding;
#define IO_BIND(name, fn) {name, (uintptr_t)(fn)}
static const IoBinding bindings[] = {
    IO_BIND("__sF", standards), IO_BIND("fopen", b_fopen), IO_BIND("fclose", b_fclose),
    IO_BIND("fread", b_fread), IO_BIND("fwrite", b_fwrite), IO_BIND("fseek", b_fseek),
    IO_BIND("fseeko", b_fseek), IO_BIND("ftell", b_ftell), IO_BIND("ftello", b_ftell),
    IO_BIND("fflush", b_fflush), IO_BIND("feof", b_feof), IO_BIND("ferror", b_ferror),
    IO_BIND("clearerr", b_clearerr), IO_BIND("getc", b_getc), IO_BIND("fputc", b_fputc),
    IO_BIND("fputs", b_fputs), IO_BIND("fgets", b_fgets), IO_BIND("fileno", b_fileno),
    IO_BIND("fprintf", b_fprintf), IO_BIND("vfprintf", b_vfprintf), IO_BIND("fscanf", b_fscanf),
    IO_BIND("printf", b_printf), IO_BIND("vprintf", b_vprintf), IO_BIND("puts", b_puts),
    IO_BIND("snprintf", snprintf), IO_BIND("sprintf", sprintf), IO_BIND("vsnprintf", vsnprintf),
    IO_BIND("sscanf", sscanf), IO_BIND("vsscanf", vsscanf), IO_BIND("vasprintf", vasprintf),
    IO_BIND("setvbuf", b_setvbuf), IO_BIND("open", b_open), IO_BIND("read", b_read),
    IO_BIND("write", b_write), IO_BIND("close", b_close), IO_BIND("lseek", b_lseek), IO_BIND("lseek64", b_lseek),
    IO_BIND("dup", b_dup), IO_BIND("chmod", b_chmod), IO_BIND("truncate", b_truncate), IO_BIND("truncate64", b_truncate), IO_BIND("perror", b_perror),
    IO_BIND("ftruncate", b_ftruncate), IO_BIND("access", b_access), IO_BIND("mkdir", b_mkdir),
    IO_BIND("unlink", b_unlink), IO_BIND("remove", b_unlink), IO_BIND("rmdir", b_rmdir), IO_BIND("rename", b_rename),
    IO_BIND("stat", b_stat), IO_BIND("stat64", b_stat), IO_BIND("lstat", b_lstat), IO_BIND("fstat", b_fstat),
    IO_BIND("opendir", b_opendir), IO_BIND("readdir", b_readdir), IO_BIND("closedir", b_closedir),
    IO_BIND("getcwd", b_getcwd), IO_BIND("realpath", b_realpath), IO_BIND("readlink", b_readlink)
};
uintptr_t runtime_io_resolve(const char *name) {
    for (size_t i = 0; i < sizeof bindings / sizeof *bindings; ++i)
        if (!strcmp(name, bindings[i].name)) return bindings[i].address;
    return 0;
}
static int prepare_metadata_cache(FILE *log) {
    const char *source="romfs:/game/bin/Data/Managed/Metadata/global-metadata.dat";
    const char *target="sdmc:/switch/swag-userdata/il2cpp/Metadata/global-metadata.dat";
    const char *temp="sdmc:/switch/swag-userdata/il2cpp/Metadata/global-metadata.dat.nx-tmp";
    const char *dirs[]={"sdmc:/switch/swag-userdata/il2cpp","sdmc:/switch/swag-userdata/il2cpp/Metadata"};
    for(unsigned i=0;i<2;++i) if(mkdir(dirs[i],0777) && errno!=EEXIST) { fprintf(log,"CACHE_DIRECTORY_FAIL path=%s errno=%d\n",dirs[i],errno); return -1; }
    FILE *input=fopen(source,"rb"),*output=NULL;
    if(!input) return -1;
    output=fopen(temp,"wb");
    if(!output) { fclose(input); return -1; }
    unsigned char buffer[32768]; size_t count,total=0; int error=0;
    while((count=fread(buffer,1,sizeof buffer,input))) {
        if(fwrite(buffer,1,count,output)!=count) { error=1; break; } total+=count;
    }
    error|=ferror(input)!=0; fclose(input); error|=fclose(output)!=0;
    /* Verify the new copy before replacing the derived metadata cache. */
    if(!error) {
        FILE *verify_source=fopen(source,"rb"),*verify_temp=fopen(temp,"rb");
        if(!verify_source || !verify_temp) error=1;
        else {
            unsigned char compared[32768]; size_t verified_bytes=0;
            while((count=fread(buffer,1,sizeof buffer,verify_source))) {
                if(fread(compared,1,count,verify_temp)!=count || memcmp(buffer,compared,count)) { error=1; break; }
                verified_bytes+=count;
            }
            error|=ferror(verify_source)!=0 || verified_bytes!=total || fgetc(verify_temp)!=EOF;
        }
        if(verify_source) fclose(verify_source);
        if(verify_temp) fclose(verify_temp);
    }
    if(!error && rename(temp,target)) {
        int reason=errno;
        if(reason==EEXIST) {
            struct stat previous;
            if(stat(target,&previous) || !S_ISREG(previous.st_mode)) error=1;
            else {
                fprintf(log,"CACHE_REPLACE_EXISTING verified_temp_bytes=%zu atomic_replace=UNAVAILABLE\n",total);
                error=remove(target)!=0 || rename(temp,target)!=0;
            }
        } else error=1;
    }
    if(error) { remove(temp); fprintf(log,"CACHE_COPY_FAIL errno=%d\n",errno); return -1; }
    input=fopen(source,"rb"); output=fopen(target,"rb");
    if(!input || !output) { if(input) fclose(input); if(output) fclose(output); return -1; }
    unsigned char check[32768]; size_t verified=0;
    while((count=fread(buffer,1,sizeof buffer,input))) {
        if(fread(check,1,count,output)!=count || memcmp(check,buffer,count)) { error=1; break; } verified+=count;
    }
    error|=ferror(input)!=0 || verified!=total || fgetc(output)!=EOF;
    fclose(input); fclose(output);
    if(!error) error=b_chmod("/il2cpp/Metadata/global-metadata.dat",0444)!=0;
    fprintf(log,"CACHE_METADATA_PREPARE result=%s bytes=%zu byte_comparison=COMPLETE\n",error ? "FAIL" : "PASS",total);
    return error ? -1 : 0;
}
int runtime_io_selftest(FILE *log) {
    int errors = prepare_metadata_cache(log)!=0;
    int random_errors=0,random_fd=b_open("/dev/urandom",0);
    if(random_fd<0) ++random_errors;
    else {
        unsigned char a[32]={0},b[32]={0}; AndroidStat status;
        random_errors+=b_read(random_fd,a,sizeof a)!=(ssize_t)sizeof a;
        random_errors+=b_fstat(random_fd,&status)!=0 || (status.mode&S_IFMT)!=S_IFCHR;
        random_errors+=b_lseek(random_fd,0,SEEK_SET)!=-1 || *bionic_errno_ptr()!=29;
        int alias=b_dup(random_fd); random_errors+=alias<0;
        random_errors+=b_close(random_fd)!=0;
        if(alias>=0) {
            random_errors+=b_read(alias,b,sizeof b)!=(ssize_t)sizeof b;
            random_errors+=!memcmp(a,b,sizeof a);
            random_errors+=b_read(alias,NULL,0)!=0;
            random_errors+=b_close(alias)!=0;
            random_errors+=b_read(alias,b,1)!=-1 || *bionic_errno_ptr()!=9;
        }
    }
    random_fd=b_open("/dev/random",0);
    if(random_fd<0) ++random_errors;
    else { unsigned char sample[8]; random_errors+=b_read(random_fd,sample,sizeof sample)!=(ssize_t)sizeof sample; random_errors+=b_close(random_fd)!=0; }
    errors+=random_errors;
    fprintf(log,"RANDOM_DEVICE_TEST errors=%d source=csrng read_dup_close=CHECKED char_stat=CHECKED seek=ESPIPE quality_certification=NOT_CLAIMED\n",random_errors);
    const char *assets[]={"assets/bin/Data/globalgamemanagers","bin/Data/globalgamemanagers.assets","assets/bin/Data/level0","bin/Data/Managed/Metadata/global-metadata.dat"};
    for (unsigned i=0;i<sizeof assets/sizeof *assets;++i) {
        void *asset=b_fopen(assets[i],"rb"); unsigned char bytes[4]={0};
        int ok=asset && b_fread(bytes,1,4,asset)==4;
        if(i==3) ok=ok && bytes[0]==0xaf && bytes[1]==0x1b && bytes[2]==0xb1 && bytes[3]==0xfa;
        if(asset) b_fclose(asset);
        errors+=!ok; fprintf(log,"GAME_ASSET_CHECK path=%s result=%s\n",assets[i],ok ? "PASS" : "FAIL");
    }
    char path[128], renamed[256], text[128], real[1024];
    snprintf(path, sizeof path, "userdata/.compat-%llx.tmp", (unsigned long long)armGetSystemTick());
    snprintf(renamed, sizeof renamed, "%s.renamed", path);
    int fd = b_open(path, 2 | 0x40 | 0x80, 0600u);
    if (fd < 0) { fprintf(log, "IO_TEST_FAIL create errno=%d\n", *bionic_errno_ptr()); return -1; }
    errors += b_write(fd, "123 hello\n", 10) != 10;
    errors += b_chmod(path,0444)!=0;
    errors += b_write(fd,"",0)!=0; /* Existing writer remains valid after chmod. */
    errors += b_chmod(path,0644)!=0;
    AndroidStat status;
    errors += b_fstat(fd, &status) != 0 || status.size != 10;
    errors += b_lseek(fd, 0, SEEK_SET) != 0;
    memset(text, 0, sizeof text); errors += b_read(fd, text, 10) != 10 || strcmp(text, "123 hello\n");
    int alias=b_dup(fd);
    errors+=alias<0;
    if(alias>=0) {
        char link_path[64],link_target[1024],expected[1024];
        snprintf(link_path,sizeof link_path,"/proc/self/fd/%d",alias);
        snprintf(expected,sizeof expected,"/sdcard/%s",path+9);
        memset(link_target,0x5a,sizeof link_target);
        ssize_t link_size=b_readlink(link_path,link_target,sizeof link_target);
        errors+=link_size!=(ssize_t)strlen(expected) || memcmp(link_target,expected,strlen(expected)) || link_target[strlen(expected)]!=0x5a;
        memset(link_target,0x5a,sizeof link_target);
        errors+=b_readlink(link_path,link_target,3)!=3 || memcmp(link_target,expected,3) || link_target[3]!=0x5a;
        errors+=b_readlink(link_path,link_target,0)!=-1 || *bionic_errno_ptr()!=22;
        errors+=b_readlink("/proc/self/fd/not-a-number",link_target,sizeof link_target)!=-1 || *bionic_errno_ptr()!=2;
        ssize_t ordinary_result=b_readlink(path,link_target,sizeof link_target);
        int ordinary_errno=*bionic_errno_ptr();
        errors+=ordinary_result!=-1 || ordinary_errno!=22;
        fprintf(log,"IO_READLINK_REGULAR_OPEN result=%lld guest_errno=%d expected_errno=22\n",(long long)ordinary_result,ordinary_errno);
        errors+=b_lseek(alias,0,SEEK_SET)!=0;
        errors+=b_read(alias,text,1)!=1 || text[0]!='1';
        errors+=b_read(fd,text,1)!=1 || text[0]!='2';
    }
    errors += b_close(fd) != 0;
    if(alias>=0) { errors+=b_read(alias,text,1)!=1 || text[0]!='3'; errors+=b_close(alias)!=0; }
    fprintf(log,"IO_DUP_TEST cumulative_errors=%d shared_offset_and_close=CHECKED\n",errors);
    fprintf(log,"IO_READLINK_TEST cumulative_errors=%d descriptor_path=CHECKED truncation_no_nul=CHECKED regular_file=EINVAL exe_path=%s\n",errors,*executable_path ? executable_path : "UNAVAILABLE");
    {
        int link_errors=0; char target[1024];
        ssize_t ordinary_result=b_readlink(path,target,sizeof target);
        link_errors+=ordinary_result!=-1 || *bionic_errno_ptr()!=22;
        link_errors+=b_readlink("userdata/.missing-readlink-target",target,sizeof target)!=-1 || *bionic_errno_ptr()!=2;
        char closed_fd_path[64]; snprintf(closed_fd_path,sizeof closed_fd_path,"/proc/self/fd/%d",fd);
        link_errors+=b_readlink(closed_fd_path,target,sizeof target)!=-1 || *bionic_errno_ptr()!=2;
        if(*executable_path) {
            memset(target,0x5a,sizeof target);
            ssize_t n=b_readlink("/proc/self/exe",target,sizeof target);
            link_errors+=n!=(ssize_t)strlen(executable_path) || memcmp(target,executable_path,strlen(executable_path)) || target[strlen(executable_path)]!=0x5a;
        }
        fprintf(log,"IO_READLINK_EXTRA errors=%d regular_closed=CHECKED missing=CHECKED closed_fd=CHECKED executable=CHECKED\n",link_errors);
        errors+=link_errors;
    }
    void *file = b_fopen(path, "r+");
    if (!file) ++errors;
    else {
        int value = 0; char word[32];
        errors += b_fscanf(file, "%d %31s", &value, word) != 2 || value != 123 || strcmp(word, "hello");
        errors += b_fseek(file, 0, SEEK_END) != 0 || b_ftell(file) != 10;
        errors += b_fprintf(file, "%lld %.2f\n", 456LL, 1.25) != 9;
        errors += b_fflush(file) != 0;
        errors += b_fseek(file, 0, SEEK_SET) != 0;
        while (b_getc(file) != EOF) {}
        errors += !b_feof(file) || b_ferror(file);
        b_clearerr(file); errors += b_feof(file) != 0;
        errors += b_fclose(file) != 0;
    }
    errors += b_rename(path, renamed) != 0;
    errors += b_stat(renamed, &status) != 0 || !(status.mode & 0100000);
    errors += b_chmod(renamed,0444)!=0;
    errors += b_open(renamed,1)!=-1 || *bionic_errno_ptr()!=13;
    errors += b_chmod(renamed,0644)!=0;
    errors += b_truncate(renamed,4) != 0;
    errors += b_stat(renamed,&status) != 0 || status.size != 4;
    errors += b_truncate(renamed,32) != 0;
    errors += b_stat(renamed,&status) != 0 || status.size != 32;
    errors += b_truncate(renamed,-1) != -1 || *bionic_errno_ptr()!=22;
    errors += b_truncate("libunity.so",0) != -1;
    errors += b_truncate("userdata/../../outside",0) != -1;
    fprintf(log,"IO_TRUNCATE_DONE cumulative_errors=%d shrink_grow=CHECKED invalid_length_and_paths=CHECKED\n",errors);
    errors += b_unlink(renamed) != 0;
    errors += map_path("userdata/../../outside", 1, real) != -1;
    errors += map_path("/il2cpp/Metadata/global-metadata.dat",1,real)!=0 || strcmp(real,"sdmc:/switch/swag-userdata/il2cpp/Metadata/global-metadata.dat");
    errors += map_path("/il2cpp/../../outside",1,real)!=-1;
    errors += map_path("libunity.so", 1, real) != -1;
    file = b_fopen("libunity.so", "rb");
    if (!file) ++errors;
    else { errors += b_fread(text, 1, 4, file) != 4 || memcmp(text, ELFMAG, 4); errors += b_fclose(file) != 0; }
    void *directory = b_opendir(".");
    int found = 0;
    if (!directory) ++errors;
    else {
        AndroidDirent *entry;
        while ((entry = b_readdir(directory))) if (!strcmp(entry->name, "libunity.so")) found = 1;
        errors += !found;
        errors += b_closedir(directory) != 0;
    }
    char resolved[1024];
    errors += !b_realpath("libunity.so", resolved) || strcmp(resolved, "/roms/ports/swag/libunity.so");
    errors += !b_getcwd(text, sizeof text) || strcmp(text, "/roms/ports/swag");
    fprintf(log, "IO_METADATA_CHECK stat_bytes=%zu dirent_bytes=%zu directory_match=%d\n", sizeof status, sizeof(AndroidDirent), found);
    b_fprintf(&standards[2], "IO_STDERR_PROXY_PASS stride=%zu\n", sizeof(AndroidFile));
    fprintf(log, "IO_TEST_DONE errors=%d bindings=%zu\n", errors, sizeof bindings / sizeof *bindings);
    return errors ? -1 : 0;
}

int io_gc_lock_free(void) { return __atomic_load_n(&io_lock,__ATOMIC_ACQUIRE)==0; }
