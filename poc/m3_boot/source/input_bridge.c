/* Switch multitouch -> UnityPlayer.nativeInjectEvent.
 * Adapted from the MIT dcr_sea_nx input/JNI reference. Touch objects must be
 * copied by MotionEvent.obtain and survive until Unity drains its queue.
 * Coordinates use Android's top-left origin, scaled to the render window. */
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include "dcr_time.h"
#include "input_bridge.h"
#include "rt_window.h"
#include "util.h"
#include "controller_bridge.h"
#include "rt_applet.h"

#ifndef M3_TOUCH_TEST
#define M3_TOUCH_TEST 0
#endif
#ifndef M3_INPUT_SAMPLER
#define M3_INPUT_SAMPLER 1
#endif
#define MOTION "android/view/MotionEvent"
#include "touch_state.h"
typedef jboolean (*Inject)(void *, void *, void *);
static Inject inject_event;
static JObj *player;
static JObj *obtained[16];
static unsigned nobtained, live_events, copies;
static Motion current;

/* The frame loop can stall for seconds while the APK is read (startup logs
 * show 12 s frames). HID used to be polled only from m3_input_frame, so a tap
 * that went down and up inside one stalled frame never existed for the game.
 * A sampler thread keeps the transitions in a queue; the frame loop injects
 * them on the main thread, where Unity's nativeInjectEvent belongs. */
#define M3_INPUT_QUEUE 512
static Motion g_queue[M3_INPUT_QUEUE];
static unsigned g_qhead, g_qtail, g_qcount;
static Mutex g_qmutex;
static Thread g_sampler_thread;
static volatile int g_sampler_stop;
static int g_sampler_running;
static int g_input_active=1;
static Motion g_delivered;

void m3_input_set_active(int active) {
  __atomic_store_n(&g_input_active,active,__ATOMIC_RELEASE);
}

static void queue_motion(const Motion *m);
static int pop_motion(Motion *m);

static void free_event(JObj *o) {
  free(o->p);
  o->p = NULL;
  live_events--;
}
static JObj *new_event(const Motion *src) {
  Motion *p = malloc(sizeof *p);
  if (!p) return NULL;
  *p = *src;
  JObj *o = jni_new(MOTION);
  o->p = p;
  o->finalize = free_event;
  live_events++;
  return o;
}

/* Only serve the event methods we implement; unknown methods retain the
 * runtime's unhandled-call diagnostics. JNI times are milliseconds. */
int m3_input_jni(JObj *self, JMethod *m, const jvalue *a, jvalue *out) {
  if (!m || !m->cls) return 0;
  int motion_class = !strcmp(m->cls->name, MOTION);
  if (!motion_class && strcmp(m->cls->name, "android/view/InputEvent")) return 0;
  const char *n = m->name;
  const Motion *e = self ? self->p : NULL;
  int index = a && !strncmp(m->sig, "(I)", 3) ? a[0].i : 0;
  const TouchPoint *pt = e && index >= 0 && index < e->count ? &e->points[index] : NULL;
  if (motion_class && !strcmp(n, "obtain") &&
      !strcmp(m->sig, "(Landroid/view/MotionEvent;)Landroid/view/MotionEvent;")) {
    JObj *src = a[0].l;
    if (!src || !src->p || nobtained == 16) { *out = jv_l(NULL); return 1; }
    JObj *o = new_event(src->p);
    if (o) { obtained[nobtained++] = o; copies++; }
    *out = jv_l(o);
  } else if (!strcmp(n, "getSource")) *out = jv_i(0x1002);
  else if (!strcmp(n, "getDeviceId")) *out = jv_i(0);
  else if (!motion_class) return 0;
  else if (!strcmp(n, "getAction")) *out = jv_i(e ? e->action : 0);
  else if (!strcmp(n, "getActionMasked")) *out = jv_i(e ? e->action & 255 : 0);
  else if (!strcmp(n, "getActionIndex")) *out = jv_i(e ? e->action >> 8 : 0);
  else if (!strcmp(n, "getPointerCount")) *out = jv_i(e ? e->count : 0);
  else if (!strcmp(n, "getPointerId")) *out = jv_i(pt ? pt->id : -1);
  else if (!strcmp(n, "findPointerIndex")) {
    int found = -1;
    if (e) for (int i=0; i<e->count; i++) if (e->points[i].id==a[0].i) found=i;
    *out = jv_i(found);
  }
  else if (!strcmp(n, "getX") || !strcmp(n, "getRawX")) *out = jv_f(pt ? pt->x : 0);
  else if (!strcmp(n, "getY") || !strcmp(n, "getRawY")) *out = jv_f(pt ? pt->y : 0);
  else if (!strcmp(n, "getEventTime")) *out = jv_j(e ? e->event_ms : 0);
  else if (!strcmp(n, "getDownTime")) *out = jv_j(e ? e->down_ms : 0);
  else if (!strcmp(n, "getPressure")) *out = jv_f(pt && (e->action & 255) != 1 && (e->action & 255) != 3 ? 1.0f : 0.0f);
  else if (!strcmp(n, "getSize")) *out = jv_f(0.1f);
  else if (!strcmp(n, "getToolType")) *out = jv_i(1);
  else if (!strcmp(n, "getAxisValue")) {
    int pi = !strcmp(m->sig, "(II)F") ? a[1].i : 0;
    const TouchPoint *axis = e && pi >= 0 && pi < e->count ? &e->points[pi] : NULL;
    *out = jv_f(!axis ? 0 : a[0].i == 0 ? axis->x : a[0].i == 1 ? axis->y : 0);
  }
  else if (!strcmp(n, "getHistorySize") || !strcmp(n, "getMetaState") ||
           !strcmp(n, "getButtonState") || !strcmp(n, "getFlags") ||
           !strcmp(n, "getEdgeFlags") || !strcmp(n, "recycle")) *out = jv_i(0);
  else return 0;
  return 1;
}

static void send_motion(const Motion *event) {
  g_delivered=*event;
  if((event->action & 255)==1 || (event->action & 255)==3)g_delivered.count=0;
  JObj *o = new_event(event);
  if (!o) return;
  jni_retain(o); /* protect the argument if the native drops its local ref */
  jboolean accepted = inject_event(g_jni_env, player, o);
  for (unsigned i = 0; i < nobtained; i++) jni_release(obtained[i]);
  nobtained = 0; /* Unity owns global refs to queued copies */
  if (o->refs > 1) jni_release(o);
  jni_release(o);
  if ((event->action & 255) != 2)
    debugPrintf("[input] action=%d index=%d count=%d accepted=%d copies=%u live=%u\n",
                event->action & 255, event->action >> 8, event->count, accepted, copies, live_events);
}

static void queue_motion(const Motion *m) {
  mutexLock(&g_qmutex);
  if(!__atomic_load_n(&g_input_active,__ATOMIC_ACQUIRE)) {
    mutexUnlock(&g_qmutex); return;
  }
  if (g_qcount == M3_INPUT_QUEUE) {
    g_qhead = (g_qhead + 1) % M3_INPUT_QUEUE;
    g_qcount--;
  }
  g_queue[g_qtail] = *m;
  g_qtail = (g_qtail + 1) % M3_INPUT_QUEUE;
  g_qcount++;
  mutexUnlock(&g_qmutex);
}

static int pop_motion(Motion *m) {
  int got = 0;
  mutexLock(&g_qmutex);
  if (g_qcount) {
    *m = g_queue[g_qhead];
    g_qhead = (g_qhead + 1) % M3_INPUT_QUEUE;
    g_qcount--;
    got = 1;
  }
  mutexUnlock(&g_qmutex);
  return got;
}

static void touch_sampler(void *unused) {
  (void)unused;
  Motion local;
  memset(&local, 0, sizeof local);
  while (!g_sampler_stop) {
    HidTouchScreenState ts = {0};
    {
      int active=__atomic_load_n(&g_input_active,__ATOMIC_ACQUIRE) && rt_focused();
      if(active)hidGetTouchScreenStates(&ts, 1);
      TouchPoint points[TOUCH_MAX] = {0};
      int count = 0;
      int w, h;
      dcr_window_size(&w, &h);
      for (int i = 0; i < ts.count && i < TOUCH_MAX; i++)
        points[count++] = (TouchPoint){ts.touches[i].finger_id, 0,
          (float)ts.touches[i].x * w / 1280.0f,
          (float)ts.touches[i].y * h / 720.0f};
      int64_t now=dcr_monotonic_ns()/1000000ull;
      count=m3_controller_sample(points,count,w,h,active,now);
      touch_update(&local, points, count, now, queue_motion);
      if(!active)local.count=0;
    }
    svcSleepThread(8000000ll); /* 125 Hz sampling, independent of frame time */
  }
}

void m3_input_init(void) {
  hidInitializeTouchScreen();
  inject_event = (Inject)jni_native("com/unity3d/player/UnityPlayer", "nativeInjectEvent");
  player = jni_singleton("com/unity3d/player/UnityPlayer");
  mutexInit(&g_qmutex);
  m3_controller_init();
  g_qhead = g_qtail = g_qcount = 0;
  g_sampler_stop = 0;
#if M3_INPUT_SAMPLER
  Result rc = threadCreate(&g_sampler_thread, touch_sampler, NULL, NULL, 64 * 1024, 44, -2);
  if (R_SUCCEEDED(rc))
    rc = threadStart(&g_sampler_thread);
  g_sampler_running = R_SUCCEEDED(rc);
#else
  g_sampler_running = 0;
#endif
  int w, h;
  dcr_window_size(&w, &h);
  debugPrintf("[input] multitouch (16 pointers) %s; panel 1280x720 -> view %dx%d, test=%d, sampler=%s\n",
              inject_event ? "ready" : "UNAVAILABLE", w, h, M3_TOUCH_TEST,
              g_sampler_running ? "on" : "off");
}

void m3_input_cancel(void) {
  m3_input_set_active(0);
  if (!inject_event)
    return;
  if (g_sampler_running) {
    mutexLock(&g_qmutex);
    g_qhead=g_qtail=g_qcount=0;
    mutexUnlock(&g_qmutex);
    Motion delivered=g_delivered;
    touch_cancel(&delivered,dcr_monotonic_ns()/1000000ull,send_motion);
    return;
  }
  touch_cancel(&current, dcr_monotonic_ns()/1000000ull, send_motion);
}

void m3_input_frame(unsigned long frame) {
  if (!inject_event) return;
  if (g_sampler_running) {
    Motion e;
    int sent = 0;
    while (sent < 64 && pop_motion(&e)) { send_motion(&e); sent++; }
    if ((frame + 1) % 600 == 0)
      debugPrintf("[input] frames=%lu copies=%u live=%u queued=%u sampled=1\n",
                  frame + 1, copies, live_events, g_qcount);
    return;
  }
  TouchPoint points[TOUCH_MAX] = {0};
  int count = 0;
  int w, h; dcr_window_size(&w, &h);
#if M3_TOUCH_TEST
  /* Opt-in synthetic two-finger JNI smoke; never enabled on hardware. */
  if (frame >= 300 && frame < 310) points[count++] = (TouchPoint){7,0,10,10};
  if (frame >= 302 && frame < 312) points[count++] = (TouchPoint){12,0,20,20};
#else
  HidTouchScreenState ts = {0};
  if (!hidGetTouchScreenStates(&ts, 1)) return;
  for (int i=0; i<ts.count && i<TOUCH_MAX; i++) {
    points[count++] = (TouchPoint){ts.touches[i].finger_id, 0,
      (float)ts.touches[i].x * w / 1280.0f,
      (float)ts.touches[i].y * h / 720.0f};
  }
#endif
  count=m3_controller_sample(points,count,w,h,1,dcr_monotonic_ns()/1000000ull);
  touch_update(&current, points, count, dcr_monotonic_ns()/1000000ull, send_motion);
  if ((frame + 1) % 600 == 0)
    debugPrintf("[input] frames=%lu copies=%u live=%u\n", frame + 1, copies, live_events);
}
