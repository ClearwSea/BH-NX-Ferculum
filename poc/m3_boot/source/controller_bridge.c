/* Joy-Con pair/Pro -> the same multitouch stream as the touchscreen. MIT. */
#include <stdio.h>
#include <switch.h>
#include "controller_state.h"
#include "controller_bridge.h"
#include "camera_axis.h"
#include "dcr_path.h"
#include "rt_pad.h"
#include "util.h"
static PadState pad;
static ControlMap mapping;
static ControlState control;
static Mutex state_mutex;
static int ready;
static int native_camera;
static CameraDelta view_axes;

void m3_controller_native_camera_enable(void) {
  __atomic_store_n(&native_camera,1,__ATOMIC_RELEASE);
}

void m3_controller_init(void) {
  rt_pad_setup(1,1);
  padInitializeDefault(&pad);
  mutexInit(&state_mutex);
  mapping=control_defaults(); control_init(&control);
  __atomic_store_n(&ready,1,__ATOMIC_RELEASE);
  char path[512],line[160],key[40]; float x,y;
  snprintf(path,sizeof path,"%s/controller-map.txt",dcr_game_root());
  FILE *f=fopen(path,"r");
  const char *names[]={"move","attack","evade","ultimate","weapon","team1","team2","back","pause","camera"};
  if(f) {
    while(fgets(line,sizeof line,f)) {
      if(sscanf(line,"%39s %f %f",key,&x,&y)!=3 || !isfinite(x) || !isfinite(y))continue;
      for(int i=0;i<CT_COUNT;i++)
        if(!strcmp(key,names[i]) && x>=0 && x<1280 && y>=0 && y<720)
          mapping.point[i]=(ControlPoint){x,y};
    }
    fclose(f);
  }
  debugPrintf("[controller] v3 native-camera=%d; A attack B evade Y ultimate X weapon; ZL off; map=%s\n",native_camera,path);
  for(int i=0;i<CT_COUNT;i++)debugPrintf("[controller] %s %.0f %.0f\n",names[i],mapping.point[i].x,mapping.point[i].y);
}

int m3_controller_sample(TouchPoint *p,int n,int w,int h,int active,int64_t now) {
  padUpdate(&pad);
  u64 buttons=padGetButtons(&pad);
  HidAnalogStickState left=padGetStickPos(&pad,0),right=padGetStickPos(&pad,1);
  const u64 physical[]={HidNpadButton_A,HidNpadButton_B,HidNpadButton_X,HidNpadButton_Y,
    HidNpadButton_L,HidNpadButton_R,HidNpadButton_ZL,HidNpadButton_ZR,
    HidNpadButton_Minus,HidNpadButton_Plus,HidNpadButton_Up,HidNpadButton_Down};
  unsigned held=0;
  for(int i=0;i<12;i++)if(buttons & physical[i])held|=1u<<i;
  mutexLock(&state_mutex);
  int oldmode=control.battle;
  control.native_camera=__atomic_load_n(&native_camera,__ATOMIC_ACQUIRE);
  n=control_update(&control,&mapping,held,left.x/32767.0f,left.y/32767.0f,
       right.x/32767.0f,right.y/32767.0f,padIsConnected(&pad),active,now,w,h,p,n);
  view_axes=(CameraDelta){0,0};
  if(active && padIsConnected(&pad) && control.battle && !control.cursor && !(held & CP_PAUSE))
    view_axes=camera_axes(right.x/32767.0f,right.y/32767.0f);
  if(oldmode!=control.battle)debugPrintf("[controller] mode=%s\n",control.battle?"battle":"cursor");
  mutexUnlock(&state_mutex);
  return n;
}

void m3_controller_camera_axes(float *x,float *y) {
  *x=*y=0;
  if(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE))return;
  mutexLock(&state_mutex);
  *x=view_axes.x; *y=view_axes.y;
  mutexUnlock(&state_mutex);
}

void m3_controller_cursor(float *x,float *y,int *visible,int *cursor) {
  if(!__atomic_load_n(&ready,__ATOMIC_ACQUIRE)){*x=*y=0; *visible=*cursor=0; return;}
  mutexLock(&state_mutex);
  *x=control.x; *y=control.y; *visible=control.visible; *cursor=control.cursor;
  mutexUnlock(&state_mutex);
}

