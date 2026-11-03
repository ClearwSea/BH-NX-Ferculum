/* Fixed touch mappings, independent of HID, JNI and GL. MIT. */
#ifndef M3_CONTROLLER_STATE_H
#define M3_CONTROLLER_STATE_H
#include <math.h>
#include "touch_state.h"
enum { CP_A=1, CP_B=2, CP_X=4, CP_Y=8, CP_L=16, CP_R=32,
       CP_BACK=64, CP_CURSOR=128, CP_MODE=256, CP_PAUSE=512,
       CP_UP=1024, CP_DOWN=2048 };
enum { CT_MOVE, CT_ATTACK, CT_EVADE, CT_ULT, CT_WEAPON, CT_TEAM1,
       CT_TEAM2, CT_BACK, CT_PAUSE, CT_CAMERA, CT_COUNT };
typedef struct { float x,y; } ControlPoint;
typedef struct {
  ControlPoint point[CT_COUNT];
  float move_radius, cursor_speed;
} ControlMap;
typedef struct {
  float x,y, camera_x,camera_y;
  unsigned previous, blocked;
  int battle, cursor, visible, moving, camera_active, native_camera;
  int64_t last_ms;
} ControlState;
static ControlMap control_defaults(void) {
  ControlMap m={{{180,570},{1120,590},{980,640},{1200,440},{1050,470},
                {1190,190},{1190,310},{60,45},{60,45},{720,360}},90,800};
  return m;
}
static void control_init(ControlState *s) {
  memset(s,0,sizeof *s); s->x=640; s->y=360; s->cursor=1;
}
static float control_axis(float x) {
  float a=fabsf(x);
  return a<=0.18f ? 0 : copysignf(fminf(1,(a-0.18f)/0.82f),x);
}
static void control_add(TouchPoint *p,int *n,int id,float x,float y,int w,int h) {
  if(*n<TOUCH_MAX) p[(*n)++]=(TouchPoint){0xf0000000u+(unsigned)id,0,x*w/1280,y*h/720};
}
static int control_update(ControlState *s,const ControlMap *m,unsigned held,
                          float lx,float ly,float rx,float ry,int connected,
                          int active,int64_t now,int w,int h,TouchPoint *p,int n) {
  float dt=s->last_ms ? (now-s->last_ms)/1000.0f : 0;
  s->last_ms=now; if(dt<0)dt=0; if(dt>0.05f)dt=0.05f;
  if(!active || !connected) {
    s->blocked=held; s->previous=held; s->moving=0; s->camera_active=0; s->visible=0;
    return n;
  }
  s->visible=1;
  unsigned down=held & ~s->previous; s->previous=held;
  s->blocked &= held;
  if(down & CP_MODE) {
    s->battle=!s->battle; s->blocked=held; s->moving=0; s->camera_active=0;
    s->cursor=!s->battle; return n; /* release all old virtual contacts */
  }
  int cursor=!s->battle || (held & CP_CURSOR);
  if(cursor!=s->cursor) {
    s->cursor=cursor; s->blocked=held & ~CP_CURSOR; s->moving=0; s->camera_active=0; return n;
  }
  held &= ~s->blocked;
  if(cursor) {
    float x=control_axis(fabsf(rx)>fabsf(lx)?rx:lx);
    float y=control_axis(fabsf(ry)>fabsf(ly)?ry:ly);
    s->x=fmaxf(0,fminf(1279,s->x+x*m->cursor_speed*dt));
    s->y=fmaxf(0,fminf(719,s->y-y*m->cursor_speed*dt));
    if(held & CP_A)control_add(p,&n,20,s->x,s->y,w,h); /* hold to drag */
  } else {
    float x=control_axis(lx),y=control_axis(ly),len=sqrtf(x*x+y*y);
    if(len>1){x/=len;y/=len;}
    if(len>0) {
      /* Establish the floating joystick's centre before dragging it. */
      ControlPoint a=m->point[CT_MOVE];
      control_add(p,&n,CT_MOVE,a.x+(s->moving?x*m->move_radius:0),
                  a.y-(s->moving?y*m->move_radius:0),w,h);
      s->moving=1;
    } else s->moving=0;
    const unsigned buttons[]={CP_A,CP_B,CP_Y,CP_X,CP_L|CP_UP,CP_R|CP_DOWN};
    for(int i=0;i<6;i++)if(held & buttons[i]) {
      ControlPoint a=m->point[CT_ATTACK+i];
      control_add(p,&n,CT_ATTACK+i,a.x,a.y,w,h);
    }
    /* Separate camera finger: bounded swipes, lifted before re-centring.
     * Never drag back to the anchor while down (that would reverse the view).
     * Movement/attack fingers remain down across camera re-anchoring. */
    float cx=control_axis(rx),cy=control_axis(ry);
    ControlPoint anchor=m->point[CT_CAMERA];
    if(!s->native_camera && (cx || cy)) {
      if(!s->camera_active) {
        s->camera_x=anchor.x; s->camera_y=anchor.y; s->camera_active=1;
      } else {
        s->camera_x+=cx*500*dt; s->camera_y-=cy*500*dt;
      }
      if(fabsf(s->camera_x-anchor.x)>160 || fabsf(s->camera_y-anchor.y)>120 ||
         s->camera_x<1 || s->camera_x>1278 || s->camera_y<1 || s->camera_y>718)
        s->camera_active=0;
      else control_add(p,&n,CT_CAMERA,s->camera_x,s->camera_y,w,h);
    } else s->camera_active=0;
  }
  if(cursor && (held & CP_B)) {
    ControlPoint a=m->point[CT_BACK]; control_add(p,&n,CT_BACK,a.x,a.y,w,h);
  }
  if(!cursor && (held & CP_PAUSE)) {
    ControlPoint a=m->point[CT_PAUSE]; control_add(p,&n,CT_PAUSE,a.x,a.y,w,h);
  }
  return n;
}
#endif

