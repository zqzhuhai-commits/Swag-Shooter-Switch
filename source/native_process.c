#include "native_process.h"
/* Native NSPs do not receive the ProcessHandle homebrew ABI entry.
 * Atmosphere/Mesosphere GetInfo(65001, INVALID_HANDLE, 0) allocates a real
 * handle for the current process without creating a local HIPC session.
 * https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/source/svc/kern_svc_info.cpp
 */
static Handle own_handle;

Handle runtime_process_handle(void) {
 Handle provided=envGetOwnProcessHandle(); return provided ? provided : own_handle;
}
Result runtime_process_initialize(FILE *log) {
 if(envGetOwnProcessHandle() || own_handle) return 0;
 u64 handle=0;
 Result rc=svcGetInfo(&handle,65001,INVALID_HANDLE,0);
 if(R_FAILED(rc)) return rc;
 if(!handle || handle>UINT32_MAX) return MAKERESULT(Module_Libnx,LibnxError_BadInput);
 own_handle=(Handle)handle;
 u64 expected=0,actual=0;
 Result a=svcGetProcessId(&expected,CUR_PROCESS_HANDLE),b=svcGetProcessId(&actual,own_handle);
 if(R_FAILED(a) || R_FAILED(b) || expected!=actual) { svcCloseHandle(own_handle); own_handle=0; return MAKERESULT(Module_Libnx,LibnxError_BadInput); }
 if(log) fprintf(log,"NATIVE_PROCESS_HANDLE ready=1 self_pid_verified=1 native_nso=%d provider=mesosphere_getinfo\n",envIsNso());
 return 0;
}
