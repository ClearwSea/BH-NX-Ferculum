/* Android pointer transitions, independent of the platform/JNI. MIT. */
#ifndef M3_TOUCH_STATE_H
#define M3_TOUCH_STATE_H
#include <stdint.h>
#include <string.h>
#define TOUCH_MAX 16
typedef struct { uint32_t finger; int id; float x, y; } TouchPoint;
typedef struct {
  int action, count;
  TouchPoint points[TOUCH_MAX];
  int64_t down_ms, event_ms;
} Motion;
typedef void (*TouchEmit)(const Motion *);
static int touch_find(const TouchPoint *p, int n, uint32_t finger) {
  for (int i=0; i<n; i++) if (p[i].finger==finger) return i;
  return -1;
}
static void touch_cancel(Motion *s, int64_t now, TouchEmit emit) {
  if (s->count) { s->action=3; s->event_ms=now; emit(s); s->count=0; }
}
static void touch_update(Motion *s, const TouchPoint *p, int n,
                         int64_t now, TouchEmit emit) {
  if (n>TOUCH_MAX) n=TOUCH_MAX;
  s->event_ms=now;
  int changed=0;
  /* Keep surviving pointers in stable order even if HID reorders its list. */
  for (int i=0; i<s->count; i++) {
    int j=touch_find(p,n,s->points[i].finger);
    if (j>=0) {
      changed |= s->points[i].x!=p[j].x || s->points[i].y!=p[j].y;
      s->points[i].x=p[j].x; s->points[i].y=p[j].y;
    }
  }
  for (int i=s->count-1; i>=0; i--) {
    if (touch_find(p,n,s->points[i].finger)>=0) continue;
    /* The released pointer is still included in POINTER_UP's snapshot. */
    s->action=s->count==1 ? 1 : 6|(i<<8); emit(s);
    memmove(s->points+i,s->points+i+1,(s->count-i-1)*sizeof(TouchPoint));
    s->count--; changed=1;
  }
  for (int j=0; j<n; j++) {
    if (touch_find(s->points,s->count,p[j].finger)>=0) continue;
    int id=0;
    for (;;id++) {
      int used=0;
      for (int i=0;i<s->count;i++) used |= s->points[i].id==id;
      if (!used) break;
    }
    int i=s->count++; s->points[i]=p[j]; s->points[i].id=id;
    if (i==0) s->down_ms=now;
    s->action=i==0 ? 0 : 5|(i<<8); emit(s); changed=1;
  }
  if (changed && s->count) { s->action=2; emit(s); }
}
#endif
