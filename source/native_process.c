#include "native_process.h"
/* Same-process HIPC copy-handle handshake used by switchbrew nx-hbloader.
 * See third_party/nx-hbloader-LICENSE.txt. Native NSO startup does not carry
 * the ProcessHandle homebrew ABI entry supplied to NROs. */
static Handle own_handle;
typedef struct { Handle server,received; Result result; } HandleTransfer;
static void receive_handle(void *argument) {
 HandleTransfer *transfer=argument;
 void *tls=armGetTls(); hipcMakeRequestInline(tls);
 s32 index=0;
 transfer->result=svcReplyAndReceive(&index,&transfer->server,1,INVALID_HANDLE,10000000000ULL);
 if(R_SUCCEEDED(transfer->result)) {
  HipcParsedRequest request=hipcParseRequest(tls);
  if(request.meta.num_copy_handles==1) transfer->received=request.data.copy_handles[0];
  else transfer->result=MAKERESULT(Module_Libnx,LibnxError_BadInput);
 }
 svcCloseHandle(transfer->server);
}
Handle runtime_process_handle(void) {
 Handle provided=envGetOwnProcessHandle(); return provided ? provided : own_handle;
}
Result runtime_process_initialize(FILE *log) {
 if(envGetOwnProcessHandle()) return 0;
 HandleTransfer transfer={0}; Handle client=0;
 Result rc=svcCreateSession(&transfer.server,&client,false,0);
 if(R_FAILED(rc)) return rc;
 Thread receiver;
 rc=threadCreate(&receiver,receive_handle,&transfer,NULL,0x4000,0x2c,-2);
 if(R_FAILED(rc)) { svcCloseHandle(transfer.server); svcCloseHandle(client); return rc; }
 rc=threadStart(&receiver);
 if(R_FAILED(rc)) { threadClose(&receiver); svcCloseHandle(transfer.server); svcCloseHandle(client); return rc; }
 hipcMakeRequestInline(armGetTls(),.num_copy_handles=1).copy_handles[0]=CUR_PROCESS_HANDLE;
 /* The receiver closes its session after copying the handle. SessionClosed
  * from SendSyncRequest is expected: no CMIF service response is required. */
 (void)svcSendSyncRequest(client); svcCloseHandle(client);
 threadWaitForExit(&receiver); threadClose(&receiver);
 if(R_FAILED(transfer.result) || !transfer.received) return R_FAILED(transfer.result) ? transfer.result : MAKERESULT(Module_Libnx,LibnxError_BadInput);
 own_handle=transfer.received;
 u64 expected=0,actual=0;
 Result a=svcGetProcessId(&expected,CUR_PROCESS_HANDLE),b=svcGetProcessId(&actual,own_handle);
 if(R_FAILED(a) || R_FAILED(b) || expected!=actual) { svcCloseHandle(own_handle); own_handle=0; return MAKERESULT(Module_Libnx,LibnxError_BadInput); }
 fprintf(log,"NATIVE_PROCESS_HANDLE ready=1 self_pid_verified=1 native_nso=%d\n",envIsNso());
 return 0;
}
