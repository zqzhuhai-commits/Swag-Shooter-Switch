#include "diagnostics.h"
/* Compatibility code expects a non-null FILE*. In normal play, consume bytes
 * in memory without opening, creating or truncating either SD log file. */
static int enabled;
static int discard(void *cookie,const char *buffer,_READ_WRITE_BUFSIZE_TYPE bytes) {
 (void)cookie; (void)buffer; return (int)bytes;
}
int diagnostics_enabled(void) { return enabled; }
FILE *diagnostics_open(void) {
 FILE *flag=fopen("sdmc:/switch/swag-debug.flag","rb");
 enabled=flag!=NULL;
 if(flag) fclose(flag);
 if(enabled) {
  FILE *file=fopen("sdmc:/switch/swag-switch.log","w");
  if(file) return file;
  enabled=0;
 }
 return funopen(NULL,NULL,discard,NULL,NULL);
}
