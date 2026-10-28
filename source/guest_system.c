#define _GNU_SOURCE
#include "guest_system.h"
#include "bionic_threads.h"
#include <switch.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <wchar.h>
#include <pthread.h>
#include <poll.h>
extern char **environ;
static FILE *system_log;
/* The guest system profile advertises UTC. Keep libc's calendar conversion
 * and formatted timezone consistent with that profile. */
void guest_system_set_log(FILE *log) { system_log = log; setenv("TZ", "UTC0", 1); tzset(); }
typedef struct {
    int sec, min, hour, day, month, year, weekday, yearday, dst;
    int padding; int64_t offset; const char *zone;
} GuestTm;
_Static_assert(sizeof(GuestTm) == 56 && offsetof(GuestTm, offset) == 40, "Android arm64 tm ABI");
static _Thread_local GuestTm calendar_result;
static void export_calendar(GuestTm *out, const struct tm *in) {
    *out = (GuestTm){in->tm_sec, in->tm_min, in->tm_hour, in->tm_mday, in->tm_mon,
        in->tm_year, in->tm_wday, in->tm_yday, in->tm_isdst, 0, 0, "UTC"};
}
static struct tm import_calendar(const GuestTm *in) {
    return (struct tm){.tm_sec=in->sec, .tm_min=in->min, .tm_hour=in->hour,
        .tm_mday=in->day, .tm_mon=in->month, .tm_year=in->year,
        .tm_wday=in->weekday, .tm_yday=in->yearday, .tm_isdst=in->dst};
}
static GuestTm *b_gmtime_r(const time_t *clock, GuestTm *out) {
    if (!clock || !out) { *bionic_errno_ptr() = 22; return NULL; }
    struct tm native;
    if (!gmtime_r(clock, &native)) { *bionic_errno_ptr() = 75; return NULL; }
    export_calendar(out, &native); return out;
}
static GuestTm *b_gmtime(const time_t *clock) { return b_gmtime_r(clock, &calendar_result); }
static time_t b_mktime(GuestTm *calendar) {
    if (!calendar) { *bionic_errno_ptr() = 22; return (time_t)-1; }
    struct tm native = import_calendar(calendar); native.tm_isdst = 0;
    errno = 0; time_t result = mktime(&native);
    if (result == (time_t)-1 && errno) { *bionic_errno_ptr() = 75; return result; }
    export_calendar(calendar, &native); return result;
}
static size_t b_strftime(char *out, size_t size, const char *format, const GuestTm *calendar) {
    if (!out || !format || !calendar) { *bionic_errno_ptr() = 22; return 0; }
    struct tm native = import_calendar(calendar); return strftime(out, size, format, &native);
}
static int android_vprint(int priority, const char *tag, const char *format, va_list args) {
    FILE *out = system_log ? system_log : stderr;
    fprintf(out, "[ANDROID %d/%s] ", priority, tag ? tag : "app");
    int n = vfprintf(out, format, args); fputc('\n', out); fflush(out); return n;
}
static int android_print(int priority, const char *tag, const char *format, ...) {
    va_list args; va_start(args, format); int n = android_vprint(priority, tag, format, args); va_end(args); return n;
}
static int android_write(int priority, const char *tag, const char *text) { return android_print(priority, tag, "%s", text ? text : ""); }
static void abort_message(const char *text) { android_write(6, "abort", text); }
typedef struct { const char *name, *value; } Property;
static const Property properties[] = {
    {"ro.build.version.sdk", "30"}, {"ro.build.version.release", "11"},
    {"ro.product.model", "Nintendo Switch"}, {"ro.product.manufacturer", "Nintendo"},
    {"ro.product.cpu.abi", "arm64-v8a"}, {"ro.product.cpu.abilist", "arm64-v8a"},
    {"ro.hardware", "switch"}, {"ro.opengles.version", "196610"},
    {"persist.sys.timezone", "Etc/UTC"}
};
static const Property *property_find(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof properties / sizeof *properties; ++i)
        if (!strcmp(name, properties[i].name)) return &properties[i];
    return NULL;
}
static int property_get(const char *name, char *out) {
    if (!out) return 0;
    const Property *p = property_find(name);
    if (!p) { *out = 0; return 0; }
    strcpy(out, p->value); return strlen(out);
}
static int property_read(const Property *p, char *name, char *out) {
    int found = 0;
    for (size_t i = 0; i < sizeof properties / sizeof *properties; ++i) if (p == &properties[i]) found = 1;
    if (!found) { if (name) *name = 0; if (out) *out = 0; return 0; }
    if (name) strcpy(name, p->name);
    if (out) strcpy(out, p->value);
    return strlen(p->value);
}
static long b_sysconf(int name) {
    if (name == 39 || name == 40) return 4096;
    if (name == 6) return 100;
    u64 value;
    if (name == 96 || name == 97) {
        if (R_FAILED(svcGetInfo(&value, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) value = 1;
        return __builtin_popcountll(value);
    }
    if (name == 98 || name == 99) {
        if (R_FAILED(svcGetInfo(&value, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0))) { *bionic_errno_ptr() = 22; return -1; }
        if (name == 99) {
            u64 used;
            if (R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0))) value = value > used ? value - used : 0;
        }
        return value / 4096;
    }
    *bionic_errno_ptr() = 22; return -1;
}
static int b_getpagesize(void) { return 4096; }
static int b_gettid(void) {
    u64 tid = 0;
    if (R_FAILED(svcGetThreadId(&tid, CUR_THREAD_HANDLE))) return -1;
    return tid & 0x7fffffff;
}
static int b_strerror_r(int error, char *out, size_t size) {
    if (!out || !size) return 34;
    const char *message = error == 110 ? "Connection timed out" : error == 38 ? "Function not implemented" :
                          error == 95 ? "Operation not supported" : error == 35 ? "Resource deadlock avoided" :
                          error == 84 ? "Invalid UTF-8 sequence" : error < 35 ? strerror(error) : "Android runtime error";
    size_t n = strlen(message);
    if (n >= size) { memcpy(out, message, size - 1); out[size - 1] = 0; return 34; }
    memcpy(out, message, n + 1); return 0;
}
static _Thread_local char error_text[128];
static char *b_strerror(int error) { b_strerror_r(error, error_text, sizeof error_text); return error_text; }
static int b_setenv(const char *name, const char *value, int overwrite) {
    int rc = setenv(name, value, overwrite); if (rc) *bionic_errno_ptr() = bionic_error(errno); return rc;
}
static int b_unsetenv(const char *name) { int rc = unsetenv(name); if (rc) *bionic_errno_ptr() = bionic_error(errno); return rc; }
/* Android AF_INET6=10 differs from libnx's AF_INET6=28. Address
 * conversion functions operate on raw address bytes, not sockaddr objects. */
static int network_family(int family) { return family==2 ? AF_INET : family==10 ? AF_INET6 : -1; }
static int guest_inet_pton(int family,const char *text,void *out) {
    int native=network_family(family);
    if(native<0) { *bionic_errno_ptr()=97; return -1; }
    int rc=inet_pton(native,text,out);
    if(rc<0) *bionic_errno_ptr()=bionic_error(errno);
    return rc;
}
static const char *guest_inet_ntop(int family,const void *address,char *text,unsigned size) {
    int native=network_family(family);
    if(native<0) { *bionic_errno_ptr()=97; return NULL; }
    /* Format in native storage first: SDK short-buffer errno may differ
     * from Android inet_ntop's ENOSPC contract. */
    char formatted[64];
    const unsigned char *bytes=address;
    if(native==AF_INET) {
        snprintf(formatted,sizeof formatted,"%u.%u.%u.%u",bytes[0],bytes[1],bytes[2],bytes[3]);
    } else {
        /* An uncompressed IPv6 representation is valid and avoids the SDK
         * formatter's observed round-trip incompatibility. */
        unsigned words[8];
        for(unsigned i=0;i<8;++i) words[i]=((unsigned)bytes[2*i]<<8)|bytes[2*i+1];
        snprintf(formatted,sizeof formatted,"%x:%x:%x:%x:%x:%x:%x:%x",
            words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7]);
    }
    size_t required=strlen(formatted)+1;
    if(size<required) { *bionic_errno_ptr()=28; return NULL; }
    memcpy(text,formatted,required); return text;
}
#define NETWORK_CHECK(expression) do { int bad=!!(expression); errors+=bad; fprintf(log,"NETWORK_CHECK line=%d status=%s expression=%s guest_errno=%d\n",__LINE__,bad ? "FAIL" : "PASS",#expression,*bionic_errno_ptr()); } while(0)
static int network_text_selftest(FILE *log) {
    int errors=0; unsigned char ipv4[4],ipv6[16],again[16]; char text[64];
    NETWORK_CHECK(guest_inet_pton(2,"127.0.0.1",ipv4)!=1);
    NETWORK_CHECK(ipv4[0]!=127 || ipv4[1]!=0 || ipv4[2]!=0 || ipv4[3]!=1);
    NETWORK_CHECK(!guest_inet_ntop(2,ipv4,text,sizeof text) || strcmp(text,"127.0.0.1"));
    NETWORK_CHECK(guest_inet_pton(10,"2001:db8::1234",ipv6)!=1);
    const unsigned char expected_ipv6[16]={0x20,0x01,0x0d,0xb8,0,0,0,0,0,0,0,0,0,0,0x12,0x34};
    NETWORK_CHECK(memcmp(ipv6,expected_ipv6,16)!=0);
    NETWORK_CHECK(!guest_inet_ntop(10,ipv6,text,sizeof text));
    NETWORK_CHECK(guest_inet_pton(10,text,again)!=1 || memcmp(ipv6,again,16));
    NETWORK_CHECK(guest_inet_pton(2,"999.1.2.3",ipv4)!=0);
    NETWORK_CHECK(guest_inet_pton(123,"127.0.0.1",ipv4)!=-1 || *bionic_errno_ptr()!=97);
    NETWORK_CHECK(guest_inet_ntop(2,ipv4,text,1)!=NULL || *bionic_errno_ptr()!=28);
    fprintf(log,"NETWORK_TEXT_DONE errors=%d ipv4_ipv6=CHECKED family_translation=CHECKED sockets=NOT_IMPLEMENTED\n",errors);
    return errors;
}

#undef NETWORK_CHECK
/* Android arm64's regular sigset_t is one 64-bit word. These routines
 * only manipulate guest data; signal delivery requires a separate backend. */
static int guest_sigempty(uint64_t *set) {
    if(!set) { *bionic_errno_ptr()=14; return -1; }
    *set=0; return 0;
}
static int guest_sigfill(uint64_t *set) {
    if(!set) { *bionic_errno_ptr()=14; return -1; }
    *set=UINT64_MAX; return 0;
}
static int guest_sigchange(uint64_t *set,int signal,int add) {
    if(!set || signal<1 || signal>64) { *bionic_errno_ptr()=22; return -1; }
    uint64_t bit=UINT64_C(1)<<(signal-1);
    if(add) *set|=bit; else *set&=~bit;
    return 0;
}
static int guest_sigadd(uint64_t *set,int signal) { return guest_sigchange(set,signal,1); }
static int guest_sigdel(uint64_t *set,int signal) { return guest_sigchange(set,signal,0); }
static int guest_sigmember(const uint64_t *set,int signal) {
    if(!set || signal<1 || signal>64) { *bionic_errno_ptr()=22; return -1; }
    return !!(*set & (UINT64_C(1)<<(signal-1)));
}
static int signal_set_selftest(FILE *log) {
    struct { uint64_t before,set,after; } guarded={0x12345678,0,0x87654321};
    int errors=guest_sigempty(&guarded.set)!=0;
    for(int i=1;i<=64;++i) {
        errors+=guest_sigadd(&guarded.set,i)!=0 || guest_sigmember(&guarded.set,i)!=1;
        errors+=guest_sigdel(&guarded.set,i)!=0 || guarded.set!=0;
    }
    errors+=guest_sigfill(&guarded.set)!=0 || guarded.set!=UINT64_MAX;
    errors+=guest_sigadd(&guarded.set,0)!=-1 || *bionic_errno_ptr()!=22;
    errors+=guest_sigdel(&guarded.set,65)!=-1 || *bionic_errno_ptr()!=22;
    errors+=guarded.set!=UINT64_MAX || guarded.before!=0x12345678 || guarded.after!=0x87654321;
    fprintf(log,"SIGNAL_SET_DONE errors=%d guest_bytes=8 signal_range=1..64 delivery=NOT_IMPLEMENTED\n",errors);
    return errors;
}
/* This runtime has one application identity and no host POSIX users. */
static unsigned guest_uid(void) { return 10000; }
static int guest_hostname(char *out, size_t size) {
    const char name[]="switch";
    if (!out) { *bionic_errno_ptr()=14; return -1; }
    if (size<sizeof name) { if(size) memcpy(out,name,size); *bionic_errno_ptr()=36; return -1; }
    memcpy(out,name,sizeof name); return 0;
}
typedef struct { char sysname[65], nodename[65], release[65], version[65], machine[65], domain[65]; } GuestUts;
static int guest_uname(GuestUts *out) {
    if (!out) { *bionic_errno_ptr()=14; return -1; }
    memset(out,0,sizeof *out);
    strcpy(out->sysname,"Linux"); strcpy(out->nodename,"switch");
    strcpy(out->release,"4.9.0-swag-compat"); strcpy(out->version,"SWAG Android compatibility profile");
    strcpy(out->machine,"aarch64"); strcpy(out->domain,"localdomain"); return 0;
}
static void checked_fd_set(int fd, unsigned long *set, size_t bytes) {
    if (!set || fd<0 || fd>=1024 || bytes<128) abort();
    set[(unsigned)fd/64] |= 1UL<<((unsigned)fd%64);
}
static int checked_fd_isset(int fd, const unsigned long *set, size_t bytes) {
    if (!set || fd<0 || fd>=1024 || bytes<128) abort();
    return !!(set[(unsigned)fd/64] & (1UL<<((unsigned)fd%64)));
}
static char *guest_basename(char *path) {
    static char dot[]=".";
    if (!path || !*path) return dot;
    size_t n=strlen(path);
    while(n>1 && path[n-1]=='/') path[--n]=0;
    if(n==1 && path[0]=='/') return path;
    char *last=strrchr(path,'/'); return last ? last+1 : path;
}
static int profile_selftest(FILE *log) {
    int errors=0; char host[16], path[]="/roms/ports/swag///", slash[]="///";
    struct { uint64_t before; unsigned long bits[16]; uint64_t after; } fd={0x12345678,{0},0x87654321};
    const int cases[]={0,63,64,511,1023};
    for(unsigned i=0;i<sizeof cases/sizeof *cases;++i) checked_fd_set(cases[i],fd.bits,sizeof fd.bits);
    for(int i=0;i<1024;++i) {
        int expected=i==0 || i==63 || i==64 || i==511 || i==1023;
        errors+=checked_fd_isset(i,fd.bits,sizeof fd.bits)!=expected;
    }
    errors+=fd.before!=0x12345678 || fd.after!=0x87654321;
    GuestUts uts; errors+=guest_uname(&uts)!=0 || strcmp(uts.machine,"aarch64");
    errors+=guest_hostname(host,sizeof host)!=0 || strcmp(host,"switch");
    errors+=strcmp(guest_basename(path),"swag") || strcmp(guest_basename(slash),"/") || strcmp(guest_basename(NULL),".");
    fprintf(log,"SYSTEM_PROFILE_DONE errors=%d fd_bits=1024 uid=%u kernel_identity=VIRTUAL\n",errors,guest_uid());
    return errors;
}
/* The single application user is the same identity returned by getuid. */
typedef struct { char *name,*password; uint32_t uid,gid; char *gecos,*directory,*shell; } GuestPasswd;
_Static_assert(sizeof(GuestPasswd)==48, "Android arm64 passwd ABI");
static const char *const user_fields[]={"u0_a0","","","/sdcard","/system/bin/sh"};
static int guest_getpwuid_r(unsigned uid,GuestPasswd *out,char *buffer,size_t size,GuestPasswd **result) {
    if(!result) return 22;
    *result=NULL;
    if(uid!=guest_uid()) return 0;
    if(!out || !buffer) return 22;
    size_t needed=0;
    for(unsigned i=0;i<5;++i) needed+=strlen(user_fields[i])+1;
    if(size<needed) return 34;
    char *fields[5];
    for(unsigned i=0;i<5;++i) { size_t n=strlen(user_fields[i])+1; fields[i]=buffer; memcpy(buffer,user_fields[i],n); buffer+=n; }
    *out=(GuestPasswd){fields[0],fields[1],uid,uid,fields[2],fields[3],fields[4]};
    *result=out; return 0;
}
static GuestPasswd *guest_getpwuid(unsigned uid) {
    static _Thread_local GuestPasswd value;
    static _Thread_local char buffer[128]; GuestPasswd *result;
    int rc=guest_getpwuid_r(uid,&value,buffer,sizeof buffer,&result);
    if(rc) *bionic_errno_ptr()=rc;
    return result;
}
static int user_profile_selftest(FILE *log) {
    char buffer[128]; GuestPasswd value,*result=NULL; int errors=0;
    errors+=guest_getpwuid_r(guest_uid(),&value,buffer,sizeof buffer,&result)!=0 || result!=&value;
    errors+=value.uid!=guest_uid() || strcmp(value.name,"u0_a0") || strcmp(value.directory,"/sdcard");
    errors+=guest_getpwuid_r(guest_uid(),&value,buffer,1,&result)!=34 || result!=NULL;
    errors+=guest_getpwuid_r(123,&value,buffer,sizeof buffer,&result)!=0 || result!=NULL;
    errors+=guest_getpwuid(guest_uid())==NULL || guest_getpwuid(123)!=NULL;
    wchar_t formatted[16];
    errors+=swprintf(formatted,16,L"%d-%ls",42,L"ok")!=5 || wcscmp(formatted,L"42-ok");
    fprintf(log,"USER_PROFILE_DONE errors=%d passwd_bytes=%zu reentrant_lookup=CHECKED unknown_user=CHECKED wide_format=CHECKED\n",errors,sizeof value);
    return errors;
}
static Mutex syslog_lock;
static char syslog_identity[64]="swag";
static int syslog_options;
static void guest_openlog(const char *identity,int options,int facility) {
    (void)facility;
    mutexLock(&syslog_lock);
    snprintf(syslog_identity,sizeof syslog_identity,"%s",identity ? identity : "swag");
    syslog_options=options; mutexUnlock(&syslog_lock);
}
static void guest_syslog(int priority,const char *format,...) {
    int saved=*bionic_errno_ptr();
    mutexLock(&syslog_lock);
    FILE *out=system_log ? system_log : stderr;
    fprintf(out,"[SYSLOG %d/%s",priority,syslog_identity);
    if(syslog_options & 1) fprintf(out," pid=%d",getpid());
    fputs("] ",out);
    va_list args; va_start(args,format); vfprintf(out,format,args); va_end(args);
    fputc('\n',out); fflush(out);
    mutexUnlock(&syslog_lock); *bionic_errno_ptr()=saved;
}
static void guest_closelog(void) {
    mutexLock(&syslog_lock); strcpy(syslog_identity,"swag"); syslog_options=0; mutexUnlock(&syslog_lock);
}
static int syslog_selftest(FILE *log) {
    const char *path="sdmc:/switch/swag-userdata/.syslog-probe.tmp";
    FILE *capture=fopen(path,"w+");
    if(!capture) { fprintf(log,"SYSLOG_TEST_DONE errors=1 capture_open_failed=1\n"); return 1; }
    FILE *saved_log=system_log; int saved_errno=*bionic_errno_ptr();
    system_log=capture; guest_openlog("startup-probe",1,0);
    *bionic_errno_ptr()=22; guest_syslog(6,"integer=%d string=%s",42,"ok");
    int errors=*bionic_errno_ptr()!=22;
    guest_closelog(); system_log=saved_log;
    rewind(capture); char text[256]={0}; fread(text,1,sizeof text-1,capture);
    errors+=!strstr(text,"startup-probe pid=") || !strstr(text,"integer=42 string=ok");
    fclose(capture); remove(path); *bionic_errno_ptr()=saved_errno;
    fprintf(log,"SYSLOG_TEST_DONE errors=%d formatted_output=CHECKED errno_preserved=CHECKED\n",errors);
    return errors;
}
static pthread_once_t socket_once=PTHREAD_ONCE_INIT;
static int socket_init_error;
static void initialize_sockets(void) { socket_init_error=R_FAILED(socketInitializeDefault()); }
static int network_result(int rc) {
    if(rc<0) *bionic_errno_ptr()=bionic_error(errno);
    return rc;
}
static int guest_socket(int family,int type,int protocol) {
    int native=network_family(family);
    if(native<0) { *bionic_errno_ptr()=97; return -1; }
    if((type & ~(0x800|0x80000))!=SOCK_STREAM && (type & ~(0x800|0x80000))!=SOCK_DGRAM) {
        *bionic_errno_ptr()=22; return -1;
    }
    pthread_once(&socket_once,initialize_sockets);
    if(socket_init_error) { *bionic_errno_ptr()=100; return -1; }
    int translated=type & ~(0x800|0x80000);
    if(type & 0x800) translated|=SOCK_NONBLOCK;
    if(type & 0x80000) translated|=SOCK_CLOEXEC;
    return network_result(socket(native,translated,protocol));
}
static int guest_listen(int fd,int backlog) { return network_result(listen(fd,backlog)); }
static int guest_shutdown(int fd,int how) {
    if(how<0 || how>2) { *bionic_errno_ptr()=22; return -1; }
    return network_result(shutdown(fd,how));
}
static int network_flags(int flags) {
    /* Other flags require explicit Android-to-BSD translation. */
    if(flags & ~(0x4000|0x40|2|1)) return -1;
    return ((flags & 0x40) ? MSG_DONTWAIT : 0) | ((flags & 2) ? MSG_PEEK : 0) | ((flags & 1) ? MSG_OOB : 0);
}
static int64_t guest_send(int fd,const void *buffer,size_t size,int flags) {
    int native=network_flags(flags);
    if(native<0) { *bionic_errno_ptr()=95; return -1; }
    ssize_t rc=send(fd,buffer,size,native); if(rc<0) *bionic_errno_ptr()=bionic_error(errno); return rc;
}
static int64_t guest_recv(int fd,void *buffer,size_t size,int flags) {
    int native=network_flags(flags);
    if(native<0) { *bionic_errno_ptr()=95; return -1; }
    ssize_t rc=recv(fd,buffer,size,native); if(rc<0) *bionic_errno_ptr()=bionic_error(errno); return rc;
}
static int guest_sockaddr(const void *guest,unsigned size,struct sockaddr_storage *native,socklen_t *length) {
    if(!guest || size<2) { *bionic_errno_ptr()=22; return -1; }
    uint16_t family; memcpy(&family,guest,2);
    int translated=network_family(family);
    unsigned required=family==2 ? 16 : family==10 ? 28 : 0;
    if(translated<0) { *bionic_errno_ptr()=97; return -1; }
    if(size<required) { *bionic_errno_ptr()=22; return -1; }
    memset(native,0,sizeof *native); memcpy(native,guest,required);
    struct sockaddr *address=(struct sockaddr *)native;
    address->sa_len=required; address->sa_family=translated; *length=required; return 0;
}
static int export_sockaddr(const struct sockaddr_storage *native,void *guest,unsigned *size) {
    if(!guest || !size) { *bionic_errno_ptr()=14; return -1; }
    const struct sockaddr *address=(const struct sockaddr *)native;
    uint16_t family=address->sa_family==AF_INET ? 2 : address->sa_family==AF_INET6 ? 10 : 0;
    if(!family) { *bionic_errno_ptr()=97; return -1; }
    unsigned required=family==2 ? 16 : 28; unsigned char bytes[28];
    memcpy(bytes,native,required); memcpy(bytes,&family,2);
    memcpy(guest,bytes,*size<required ? *size : required); *size=required; return 0;
}
static int guest_bind(int fd,const void *address,unsigned size) {
    struct sockaddr_storage native; socklen_t length;
    if(guest_sockaddr(address,size,&native,&length)) return -1;
    return network_result(bind(fd,(struct sockaddr *)&native,length));
}
static int guest_connect(int fd,const void *address,unsigned size) {
    struct sockaddr_storage native; socklen_t length;
    if(guest_sockaddr(address,size,&native,&length)) return -1;
    return network_result(connect(fd,(struct sockaddr *)&native,length));
}
static int guest_socket_name(int fd,void *out,unsigned *size,int peer) {
    struct sockaddr_storage native; socklen_t length=sizeof native;
    if(!out || !size) { *bionic_errno_ptr()=14; return -1; }
    int rc=peer ? getpeername(fd,(struct sockaddr *)&native,&length) : getsockname(fd,(struct sockaddr *)&native,&length);
    if(rc<0) return network_result(rc);
    return export_sockaddr(&native,out,size);
}
static int guest_getsockname(int fd,void *out,unsigned *size) { return guest_socket_name(fd,out,size,0); }
static int guest_getpeername(int fd,void *out,unsigned *size) { return guest_socket_name(fd,out,size,1); }
static int guest_accept(int fd,void *address,unsigned *size) {
    if(address && !size) { *bionic_errno_ptr()=14; return -1; }
    struct sockaddr_storage native; socklen_t length=sizeof native;
    int accepted=accept(fd,address ? (struct sockaddr *)&native : NULL,address ? &length : NULL);
    if(accepted<0) return network_result(accepted);
    if(address && export_sockaddr(&native,address,size)) { close(accepted); return -1; }
    return accepted;
}
static int64_t guest_sendto(int fd,const void *buffer,size_t bytes,int flags,const void *address,unsigned size) {
    if(!address) return guest_send(fd,buffer,bytes,flags);
    int translated=network_flags(flags);
    if(translated<0) { *bionic_errno_ptr()=95; return -1; }
    struct sockaddr_storage native; socklen_t length;
    if(guest_sockaddr(address,size,&native,&length)) return -1;
    ssize_t rc=sendto(fd,buffer,bytes,translated,(struct sockaddr *)&native,length);
    if(rc<0) *bionic_errno_ptr()=bionic_error(errno);
    return rc;
}
static int64_t guest_recvfrom(int fd,void *buffer,size_t bytes,int flags,void *address,unsigned *size) {
    if(address && !size) { *bionic_errno_ptr()=14; return -1; }
    int translated=network_flags(flags);
    if(translated<0) { *bionic_errno_ptr()=95; return -1; }
    struct sockaddr_storage native; socklen_t length=sizeof native;
    ssize_t rc=recvfrom(fd,buffer,bytes,translated,address ? (struct sockaddr *)&native : NULL,address ? &length : NULL);
    if(rc<0) { *bionic_errno_ptr()=bionic_error(errno); return -1; }
    if(address && export_sockaddr(&native,address,size)) return -1;
    return rc;
}
static int sockaddr_selftest(FILE *log) {
    unsigned char ipv4[16]={2,0,0x1f,0x90,127,0,0,1},ipv6[28]={10,0,0x01,0xbb};
    ipv6[8]=0x20; ipv6[9]=0x01; ipv6[10]=0x0d; ipv6[11]=0xb8; ipv6[23]=1;
    int errors=0;
    for(unsigned i=0;i<2;++i) {
        void *input=i ? ipv6 : ipv4; unsigned length=i ? 28 : 16;
        struct sockaddr_storage native; socklen_t native_size=0; unsigned char output[28]={0}; unsigned out_size=sizeof output;
        int rc=guest_sockaddr(input,length,&native,&native_size);
        errors+=rc!=0 || native_size!=length;
        if(!rc) errors+=export_sockaddr(&native,output,&out_size)!=0 || out_size!=length || memcmp(input,output,length);
    }
    fprintf(log,"SOCKADDR_TEST_DONE errors=%d ipv4_ipv6_layout=CHECKED network_io=NOT_TESTED\n",errors); return errors;
}
typedef struct { int32_t fd; int16_t events,revents; } GuestPollFd;
_Static_assert(sizeof(GuestPollFd)==8 && sizeof(struct pollfd)==8,"pollfd layout");
static int guest_poll(GuestPollFd *fds,unsigned count,int timeout) {
    if(count>4096 || (count && !fds)) { *bionic_errno_ptr()=22; return -1; }
    if(!count) {
        if(timeout) svcSleepThread(timeout<0 ? INT64_MAX : (int64_t)timeout*1000000);
        return 0;
    }
    struct pollfd *native=calloc(count,sizeof *native);
    if(!native) { *bionic_errno_ptr()=12; return -1; }
    for(unsigned i=0;i<count;++i) {
        unsigned short events=(unsigned short)fds[i].events;
        if(events & ~0x3ffu) { free(native); *bionic_errno_ptr()=95; return -1; }
        native[i].fd=fds[i].fd;
        native[i].events=events & 0xff;
        if(events & 0x100) native[i].events|=POLLOUT;
        if(events & 0x200) native[i].events|=POLLWRBAND;
    }
    pthread_once(&socket_once,initialize_sockets);
    if(socket_init_error) { free(native); *bionic_errno_ptr()=100; return -1; }
    int rc=poll(native,count,timeout);
    if(rc<0) { int error=errno; free(native); *bionic_errno_ptr()=bionic_error(error); return -1; }
    for(unsigned i=0;i<count;++i) {
        unsigned short native_events=native[i].revents;
        unsigned short result=native_events & 0xff;
        if((native_events & POLLOUT) && (fds[i].events & 0x100)) result|=0x100;
        if(native_events & POLLWRBAND) result|=0x200;
        fds[i].revents=result;
    }
    free(native); return rc;
}
static int poll_selftest(FILE *log) {
    uint64_t start=armGetSystemTick(); int errors=guest_poll(NULL,0,3)!=0;
    errors+=armTicksToNs(armGetSystemTick()-start)<2000000;
    errors+=guest_poll(NULL,1,0)!=-1 || *bionic_errno_ptr()!=22;
    GuestPollFd bad={-1,0x4000,0};
    errors+=guest_poll(&bad,1,0)!=-1 || *bionic_errno_ptr()!=95;
    fprintf(log,"POLL_TEST_DONE errors=%d timeout_and_rejection=CHECKED socket_readiness=NOT_TESTED\n",errors); return errors;
}
typedef struct { int32_t flags,pad; uintptr_t handler; uint64_t mask; uintptr_t restorer; } GuestSignalAction;
_Static_assert(sizeof(GuestSignalAction)==32 && offsetof(GuestSignalAction,handler)==8,"Bionic LP64 sigaction");
static int guest_sigaction(int signal,const GuestSignalAction *action,GuestSignalAction *old) {
    if(signal<1 || signal>64 || (action && (signal==9 || signal==19))) { *bionic_errno_ptr()=22; return -1; }
    /* There is no POSIX asynchronous signal-delivery backend on Switch.
     * Report the default disposition, but reject installing callbacks rather
     * than pretending that crash handlers can be delivered. */
    if(action && (action->handler || action->flags || action->mask || action->restorer)) {
        fprintf(system_log ? system_log : stderr,"SIGNAL_ACTION_UNSUPPORTED signal=%d flags=%x handler=%p delivery=UNAVAILABLE\n",signal,(unsigned)action->flags,(void *)action->handler);
        *bionic_errno_ptr()=95; return -1;
    }
    if(old) memset(old,0,sizeof *old);
    fprintf(system_log ? system_log : stderr,"SIGNAL_ACTION_DEFAULT signal=%d query=%d delivery=UNAVAILABLE\n",signal,action==NULL);
    return 0;
}
static int sigaction_selftest(FILE *log) {
    struct { uint64_t before; GuestSignalAction action; uint64_t after; } probe={0x12345678,{0},0x87654321};
    int errors=guest_sigaction(11,NULL,&probe.action)!=0 || probe.action.handler!=0;
    GuestSignalAction request={.handler=1};
    errors+=guest_sigaction(11,&request,NULL)!=-1 || *bionic_errno_ptr()!=95;
    errors+=guest_sigaction(9,&request,NULL)!=-1 || *bionic_errno_ptr()!=22;
    errors+=probe.before!=0x12345678 || probe.after!=0x87654321;
    fprintf(log,"SIGACTION_PROFILE_DONE errors=%d query_default=CHECKED unsupported_install=REJECTED\n",errors); return errors;
}
typedef struct { const char *name; uintptr_t address; } SystemBinding;
#define SYS_BIND(name, fn) {name, (uintptr_t)(fn)}
static const SystemBinding bindings[] = {
    SYS_BIND("sigaction", guest_sigaction),
    SYS_BIND("poll", guest_poll),
    SYS_BIND("accept", guest_accept), SYS_BIND("sendto", guest_sendto), SYS_BIND("recvfrom", guest_recvfrom),
    SYS_BIND("bind", guest_bind), SYS_BIND("connect", guest_connect), SYS_BIND("getsockname", guest_getsockname), SYS_BIND("getpeername", guest_getpeername),
    SYS_BIND("socket", guest_socket), SYS_BIND("listen", guest_listen), SYS_BIND("shutdown", guest_shutdown),
    SYS_BIND("send", guest_send), SYS_BIND("recv", guest_recv),
    SYS_BIND("openlog", guest_openlog), SYS_BIND("syslog", guest_syslog), SYS_BIND("closelog", guest_closelog),
    SYS_BIND("getpwuid", guest_getpwuid), SYS_BIND("getpwuid_r", guest_getpwuid_r), SYS_BIND("swprintf", swprintf),
    SYS_BIND("sigemptyset", guest_sigempty), SYS_BIND("sigfillset", guest_sigfill), SYS_BIND("sigaddset", guest_sigadd),
    SYS_BIND("sigdelset", guest_sigdel), SYS_BIND("sigismember", guest_sigmember),
    SYS_BIND("inet_pton", guest_inet_pton), SYS_BIND("inet_ntop", guest_inet_ntop), SYS_BIND("inet_addr", inet_addr),
    SYS_BIND("getuid", guest_uid), SYS_BIND("geteuid", guest_uid), SYS_BIND("gethostname", guest_hostname), SYS_BIND("uname", guest_uname),
    SYS_BIND("__FD_SET_chk", checked_fd_set), SYS_BIND("__FD_ISSET_chk", checked_fd_isset), SYS_BIND("basename", guest_basename),
    SYS_BIND("__android_log_print", android_print), SYS_BIND("__android_log_vprint", android_vprint),
    SYS_BIND("__android_log_write", android_write), SYS_BIND("android_set_abort_message", abort_message),
    SYS_BIND("__system_property_get", property_get), SYS_BIND("__system_property_find", property_find),
    SYS_BIND("__system_property_read", property_read), SYS_BIND("sysconf", b_sysconf), SYS_BIND("getpagesize", b_getpagesize),
    SYS_BIND("gettid", b_gettid), SYS_BIND("getpid", getpid),
    SYS_BIND("getenv", getenv), SYS_BIND("setenv", b_setenv), SYS_BIND("unsetenv", b_unsetenv), SYS_BIND("environ", &environ),
    SYS_BIND("strerror", b_strerror), SYS_BIND("strerror_r", b_strerror_r), SYS_BIND("strcasecmp", strcasecmp),
    SYS_BIND("strtold", strtold), SYS_BIND("div", div), SYS_BIND("lldiv", lldiv), SYS_BIND("difftime", difftime),
    SYS_BIND("clock", clock), SYS_BIND("time", time), SYS_BIND("gettimeofday", gettimeofday),
    SYS_BIND("gmtime", b_gmtime), SYS_BIND("gmtime_r", b_gmtime_r),
    SYS_BIND("localtime", b_gmtime), SYS_BIND("localtime_r", b_gmtime_r),
    SYS_BIND("mktime", b_mktime), SYS_BIND("strftime", b_strftime),
    SYS_BIND("abort", abort), SYS_BIND("exit", exit), SYS_BIND("_exit", _exit), SYS_BIND("usleep", usleep)
};
uintptr_t guest_system_resolve(const char *name) {
    for (size_t i = 0; i < sizeof bindings / sizeof *bindings; ++i)
        if (!strcmp(name, bindings[i].name)) return bindings[i].address;
    return 0;
}
int guest_system_selftest(FILE *log) {
    int errors = profile_selftest(log) + network_text_selftest(log) + signal_set_selftest(log) + user_profile_selftest(log) + syslog_selftest(log) + sockaddr_selftest(log) + poll_selftest(log) + sigaction_selftest(log); char value[92], name[32];
    errors += property_get("ro.build.version.sdk", value) != 2 || strcmp(value, "30");
    const Property *p = property_find("ro.product.model");
    errors += !p || property_read(p, name, value) <= 0 || strcmp(name, "ro.product.model");
    errors += b_sysconf(39) != 4096 || b_sysconf(97) < 1 || b_gettid() <= 0;
    struct { uint64_t before; GuestTm value; uint64_t after; } guarded = {0x12345678, {0}, 0x87654321};
    time_t leap = 1709164800; char stamp[64];
    errors += !b_gmtime_r(&leap, &guarded.value);
    errors += guarded.value.year != 124 || guarded.value.month != 1 || guarded.value.day != 29;
    errors += b_mktime(&guarded.value) != leap;
    errors += !b_strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &guarded.value) || strcmp(stamp, "2024-02-29 00:00:00");
    guarded.value.day = 30;
    errors += b_mktime(&guarded.value) != leap + 86400 || guarded.value.month != 2 || guarded.value.day != 1;
    time_t before_epoch = -1;
    errors += !b_gmtime_r(&before_epoch, &guarded.value) || b_mktime(&guarded.value) != -1;
    errors += guarded.before != 0x12345678 || guarded.after != 0x87654321;
    fprintf(log, "CALENDAR_ABI_DONE cumulative_errors=%d tm_bytes=%zu leap_normalization=CHECKED negative_epoch=CHECKED timezone=UTC\n", errors, sizeof(GuestTm));
    fprintf(log, "SYSTEM_ABI_DONE errors=%d cores=%ld memory_pages=%ld\n", errors, b_sysconf(97), b_sysconf(98));
    return errors ? -1 : 0;
}
