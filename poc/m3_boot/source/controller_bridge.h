#ifndef M3_CONTROLLER_BRIDGE_H
#define M3_CONTROLLER_BRIDGE_H
#include "touch_state.h"
void m3_controller_init(void);
int m3_controller_sample(TouchPoint *p,int count,int w,int h,int active,int64_t now);
void m3_controller_cursor(float *x,float *y,int *visible,int *cursor);
void m3_controller_native_camera_enable(void);
void m3_controller_camera_axes(float *x,float *y);
#endif

