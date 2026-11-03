#include <switch.h>
#include <stdlib.h>
#include "game_performance.h"
#include "performance_policy.h"
/* Request only enumerated frequencies. External clock managers and Horizon
 * retain control; do not continuously fight their decisions. */
static struct {ClkrstSession session;u32 original,applied;int opened,changed;} clocks[3];
static FILE *output;
static int ready,psm_ready,enabled=1,last_profile=-1,registered;
static uint64_t next_poll;
static void performance_exit(void) {
 if(ready) {
  for(unsigned i=0;i<3;++i)if(clocks[i].opened) {
   u32 actual=0;
   if(clocks[i].changed && clocks[i].original &&
      R_SUCCEEDED(clkrstGetClockRate(&clocks[i].session,&actual)) && actual==clocks[i].applied)
    clkrstSetClockRate(&clocks[i].session,clocks[i].original);
   clkrstCloseSession(&clocks[i].session);clocks[i].opened=0;
  }
  clkrstExit();ready=0;
 }
 if(psm_ready) {psmExit();psm_ready=0;}
}
static int power_profile(void) {
 if(appletGetOperationMode()==AppletOperationMode_Console)return 2;
 PsmChargerType type=PsmChargerType_Unconnected;
 if(psm_ready && R_SUCCEEDED(psmGetChargerType(&type)) && type==PsmChargerType_EnoughPower)return 2;
 if(type==PsmChargerType_LowPower)return 1;
 return 0;
}
void game_performance_poll(void) {
 if(!ready || !enabled)return;
 uint64_t now=armGetSystemTick();if(now<next_poll)return;
 next_poll=now+armGetSystemTickFreq()*2;
 int profile=power_profile();if(profile==last_profile)return;
 last_profile=profile;
 /* CPU/EMC remain within the stock boost/docked frequency tables. GPU uses
  * battery/USB/adequate-power caps rather than forcing dock clocks on battery. */
 const u32 ceilings[]={1785000000u,performance_gpu_ceiling(profile),1600000000u};
 for(unsigned i=0;i<3;++i)if(clocks[i].opened) {
  u32 rates[64]={0},chosen=0,actual=0;s32 count=0;
  PcvClockRatesListType type=PcvClockRatesListType_Invalid;
  Result rc=clkrstGetPossibleClockRates(&clocks[i].session,rates,64,&type,&count);
  if(R_SUCCEEDED(rc))chosen=performance_select_rate(rates,count,type==PcvClockRatesListType_Discrete,ceilings[i]);
  if(chosen) {
   rc=clkrstSetClockRate(&clocks[i].session,chosen);
   if(R_SUCCEEDED(rc)) {clocks[i].changed=1;clocks[i].applied=chosen;}
  }
  Result read=clkrstGetClockRate(&clocks[i].session,&actual);
  if(output)fprintf(output,"PERFORMANCE_CLOCK module=%u profile=%d ceiling=%u selected=%u result=%x actual=%u read_result=%x\n",i,profile,ceilings[i],chosen,rc,actual,read);
 }
}
void game_performance_init(FILE *log) {
 output=log;
 FILE *config=fopen("sdmc:/switch/swag-performance.cfg","r");
 if(config) {int value=1;if(fscanf(config,"enabled %d",&value)==1)enabled=value!=0;fclose(config);}
 if(!enabled)return;
 Result rc=clkrstInitialize();
 if(R_FAILED(rc)) {if(output)fprintf(output,"PERFORMANCE_CLOCK unavailable=%x default_clocks=1\n",rc);return;}
 ready=1;psm_ready=R_SUCCEEDED(psmInitialize());
 const PcvModuleId modules[]={PcvModuleId_CpuBus,PcvModuleId_GPU,PcvModuleId_EMC};
 for(unsigned i=0;i<3;++i) {
  rc=clkrstOpenSession(&clocks[i].session,modules[i],3);
  if(R_FAILED(rc))continue;
  clocks[i].opened=1;
  if(R_FAILED(clkrstGetClockRate(&clocks[i].session,&clocks[i].original))) {
   clkrstCloseSession(&clocks[i].session);clocks[i].opened=0;
  }
 }
 if(!registered) {registered=atexit(performance_exit)==0;if(!registered) {performance_exit();return;}}
 game_performance_poll();
}
