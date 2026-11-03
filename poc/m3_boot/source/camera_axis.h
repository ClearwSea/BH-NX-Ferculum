/* Radial dead zone and frame-rate-independent native drag increments. */
#ifndef M3_CAMERA_AXIS_H
#define M3_CAMERA_AXIS_H
#include <math.h>
typedef struct { float x,y; } CameraDelta;
static CameraDelta camera_axes(float x,float y) {
  float len=sqrtf(x*x+y*y);
  if(!isfinite(len) || len<=0.18f)return (CameraDelta){0,0};
  float scale=(fminf(len,1)-0.18f)/(0.82f*len);
  return (CameraDelta){x*scale,y*scale};
}
static CameraDelta camera_delta(CameraDelta axes,float seconds) {
  if(!isfinite(seconds) || seconds<=0)return (CameraDelta){0,0};
  if(seconds>0.05f)seconds=0.05f; /* no large camera jump after a stalled frame */
  /* Same units as the original touch handler: inches of drag, per frame. */
  return (CameraDelta){axes.x*3.125f*seconds,axes.y*3.125f*seconds};
}
#endif

