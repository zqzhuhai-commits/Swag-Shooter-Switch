#include "gyro_aim.h"
#include "gyro_policy.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static FILE *output;
static HidSixAxisSensorHandle handles[2];static unsigned started[2],handle_count;
static u32 sensor_style;static HidNpadIdType sensor_id;static uint64_t retry_ms;
static int enabled=1,invert_x,invert_y,registered;
static unsigned sensitivity_x=1000,sensitivity_y=1000;
static GyroPolicy policy;
void gyro_aim_reset(void) {gyro_policy_reset(&policy);}
void gyro_aim_exit(void) {
 for(unsigned i=0;i<handle_count;++i)if(started[i])hidStopSixAxisSensor(handles[i]);
 memset(started,0,sizeof started);handle_count=0;sensor_style=0;gyro_aim_reset();
}
void gyro_aim_init(FILE *log) {
 output=log;gyro_aim_exit();enabled=1;invert_x=invert_y=0;sensitivity_x=sensitivity_y=1000;
 FILE *file=fopen("sdmc:/switch/swag-gyro.cfg","r");
 if(file) {
  char line[160],key[64];float value;
  while(fgets(line,sizeof line,file)) {
   if(line[0]=='#' || sscanf(line,"%63s %f",key,&value)!=2 || !isfinite(value))continue;
   if(!strcmp(key,"enabled"))enabled=value!=0;
   else if(!strcmp(key,"invert_x"))invert_x=value!=0;
   else if(!strcmp(key,"invert_y"))invert_y=value!=0;
   else if(value>=0.1f && value<=5.0f) {
    if(!strcmp(key,"sensitivity_x"))sensitivity_x=(unsigned)(value*1000.0f);
    if(!strcmp(key,"sensitivity_y"))sensitivity_y=(unsigned)(value*1000.0f);
   }
  }
  fclose(file);
 }
 if(!registered) {registered=atexit(gyro_aim_exit)==0;}
 if(output)fprintf(output,"GYRO_CONFIG enabled=%d aim_only=1 sensitivity=%u,%u invert=%d,%d physical_sensors_only=1\n",enabled,sensitivity_x,sensitivity_y,invert_x,invert_y);
}
static void bind_sensor(HidNpadIdType id,u32 style,uint64_t now) {
 gyro_aim_exit();sensor_id=id;sensor_style=style;retry_ms=now+1000;
 if(!style)return;
 handle_count=style==HidNpadStyleTag_NpadJoyDual ? 2 : 1;
 Result result=hidGetSixAxisSensorHandles(handles,(s32)handle_count,id,(HidNpadStyleTag)style);
 if(R_FAILED(result)) {handle_count=0;return;}
 for(unsigned i=0;i<handle_count;++i) {
  result=hidStartSixAxisSensor(handles[i]);started[i]=R_SUCCEEDED(result);
  if(started[i])hidSetGyroscopeZeroDriftMode(handles[i],HidGyroscopeZeroDriftMode_Standard);
 }
 if(output)fprintf(output,"GYRO_SENSOR style=%x started=%u,%u\n",style,started[0],started[1]);
}
int gyro_aim_update(const PadState *pad,int active,float *dx,float *dy) {
 *dx=*dy=0;
 if(!enabled) {gyro_aim_reset();return 0;}
 uint64_t now=armTicksToNs(armGetSystemTick())/1000000;
 HidNpadIdType id=HidNpadIdType_No1;u32 style=0,set=padGetStyleSet(pad);
 if(padIsHandheld(pad) && (set&HidNpadStyleTag_NpadHandheld)) {id=HidNpadIdType_Handheld;style=HidNpadStyleTag_NpadHandheld;}
 else if(padIsNpadActive(pad,HidNpadIdType_No1)) {
  if(set&HidNpadStyleTag_NpadFullKey)style=HidNpadStyleTag_NpadFullKey;
  else if(set&HidNpadStyleTag_NpadJoyDual)style=HidNpadStyleTag_NpadJoyDual;
 }
 if(style!=sensor_style || id!=sensor_id || (style && !started[0] && !started[1] && now>=retry_ms))bind_sensor(id,style,now);
 if(!active || !style) {gyro_aim_reset();return 0;}
 unsigned first=0;
 if(style==HidNpadStyleTag_NpadJoyDual && (padGetAttributes(pad)&HidNpadAttribute_IsRightConnected))first=1;
 HidSixAxisSensorState state={0};int valid=0;
 for(unsigned k=0;k<handle_count;++k) {
  unsigned i=(first+k)%handle_count;
  if(started[i] && hidGetSixAxisSensorStates(handles[i],&state,1) && (state.attributes&HidSixAxisSensorAttribute_IsConnected)) {valid=1;break;}
 }
 if(!valid) {gyro_aim_reset();return 0;}
 float wx=state.angular_velocity.x,wy=state.angular_velocity.y,wz=state.angular_velocity.z;
 float ax=state.acceleration.x,ay=state.acceleration.y,az=state.acceleration.z;
 if(!isfinite(wx) || !isfinite(wy) || !isfinite(wz) || fabsf(wx)>8 || fabsf(wy)>8 || fabsf(wz)>8) {gyro_aim_reset();return 0;}
 float gravity=sqrtf(ax*ax+ay*ay+az*az),yaw=wy;
 if(isfinite(gravity) && gravity>0.2f && gravity<3.0f)yaw=(wx*ax+wy*ay+wz*az)/gravity;
 float x=yaw,y=-wx;if(invert_x)x=-x;if(invert_y)y=-y;
 if(!isfinite(x) || !isfinite(y) || fabsf(x)>8 || fabsf(y)>8) {gyro_aim_reset();return 0;}
 int32_t mx=0,my=0;
 gyro_policy_step(&policy,1,state.sampling_number,now,(int32_t)(x*1000000.0f),(int32_t)(y*1000000.0f),sensitivity_x,sensitivity_y,&mx,&my);
 *dx=(float)mx/1000.0f;*dy=(float)my/1000.0f;
 return 1;
}
