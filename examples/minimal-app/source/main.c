/* main.c -- the M1 probe: boot an AArch32 program, report the heap, and prove
 * EGL/GLES through mesa32 with a solid colour on screen.
 *
 * It replaces the runtime's main.c (runtime.mk lets a port file of the same
 * name take its place), so it needs no APK and loads no game library. M2/M3
 * replace this with the real port's boot.
 *
 * Evidence goes to sdmc:/switch/bh3_test/debug.log. The solid colour stays
 * until + is pressed or about 30 seconds pass.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <switch.h>
#include <sys/stat.h>

#include "dcr_path.h"
#include "gl_layer.h"
#include "rt_settings.h"
#include "rt_window.h"
#include "util.h"

/* newlib's heap, set up by libnx32's __libnx_initheap (clamped to the 1 GiB
 * 32-bit heap region). */
extern char *fake_heap_start;
extern char *fake_heap_end;

static void report_heap(void) {
  unsigned long bytes = (unsigned long)(fake_heap_end - fake_heap_start);
  u64 region = 0;
  Result rc = svcGetInfo(&region, InfoType_HeapRegionSize, CUR_PROCESS_HANDLE, 0);
  debugPrintf("[m1] heap %p..%p: %lu MiB\n", (void *)fake_heap_start, (void *)fake_heap_end,
              bytes >> 20);
  debugPrintf("[m1] heap region: rc=0x%x, %llu MiB\n", (unsigned)rc,
              (unsigned long long)(region >> 20));
}

/* The boot log's evidence (GL vendor, renderer, version, a triangle and a
 * texture), shared with every android32 port. */
static int stack_selftest(void) {
  int ok = dcr_gl_selftest();
  debugPrintf("[m1] dcr_gl_selftest: %s\n", ok ? "OK" : "FAILED");
  return ok;
}

/* A frame loop of our own: clear to one colour and present, so the screen
 * shows that the GL stack came up on hardware. */
static int solid_hold(void) {
  dcr_window_prepare();
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint maj = 0, min = 0;
  if (!eglInitialize(d, &maj, &min)) {
    debugPrintf("[m1] solid: eglInitialize failed 0x%x\n", eglGetError());
    return 0;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  static const EGLint cfg_attrs[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE,   8,
      EGL_GREEN_SIZE,      8,                  EGL_BLUE_SIZE,  8,
      EGL_NONE,
  };
  EGLConfig cfg;
  EGLint n = 0;
  if (!eglChooseConfig(d, cfg_attrs, &cfg, 1, &n) || n < 1) {
    debugPrintf("[m1] solid: no config 0x%x\n", eglGetError());
    eglTerminate(d);
    return 0;
  }
  EGLSurface surf = eglCreateWindowSurface(d, cfg, (EGLNativeWindowType)nwindowGetDefault(), NULL);
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLContext ctx = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctx_attrs);
  if (!surf || !ctx || !eglMakeCurrent(d, surf, surf, ctx)) {
    debugPrintf("[m1] solid: surface %p / context %p / make-current failed 0x%x\n", surf, ctx,
                eglGetError());
    eglTerminate(d);
    return 0;
  }
  debugPrintf("[m1] solid: %s | %s | %s\n", (const char *)glGetString(GL_VENDOR),
              (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));

  PadState pad;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);
  int frames = 0;
  while (appletMainLoop() && frames < 1800) {
    padUpdate(&pad);
    if (padGetButtons(&pad) & HidNpadButton_Plus)
      break;
    glViewport(0, 0, RT_SCREEN_W, RT_SCREEN_H);
    glClearColor(0.06f, 0.36f, 0.74f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!eglSwapBuffers(d, surf)) {
      debugPrintf("[m1] solid: eglSwapBuffers failed 0x%x at frame %d\n", eglGetError(), frames);
      break;
    }
    frames++;
  }
  debugPrintf("[m1] solid: %d frames presented\n", frames);
  eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(d, ctx);
  eglDestroySurface(d, surf);
  eglTerminate(d);
  return frames > 0;
}

int main(void) {
  const char *root = dcr_game_root();
  mkdir(root, 0777);
  log_init(root);
  debugPrintf("[m1] %s: AArch32 boot\n", PORT_TITLE);
  report_heap();
  int gl = stack_selftest();
  int solid = solid_hold();
  debugPrintf("[m1] result: gl %s, solid %s\n", gl ? "OK" : "FAILED", solid ? "OK" : "FAILED");
  log_flush_ring();
  return gl && solid ? 0 : 1;
}
