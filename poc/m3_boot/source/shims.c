/* shims.c -- the imports the runtime does not serve yet, as port shims.
 *
 * These are the 49 symbols gen_imports.py reported for the M2 module set that
 * have no b_ implementation in android32. The input and window calls are not
 * reached before the engine's Java/native window setup; they return neutral
 * values until that path is implemented. compress2 goes to miniz, like the
 * runtime's b_compress. MIT.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>

#include "dcr_path.h"
#include "util.h"

/* ------------------------------------------------------------- AInputQueue */
int b_AInputEvent_getSource(void *event) {
  (void)event;
  return 0;
}
void b_AInputQueue_attachLooper(void *queue, void *looper, int ident, void *callback, void *data) {
  (void)queue, (void)looper, (void)ident, (void)callback, (void)data;
}
void b_AInputQueue_detachLooper(void *queue) { (void)queue; }
void b_AInputQueue_finishEvent(void *queue, void *event, int handled) {
  (void)queue, (void)event, (void)handled;
}
int b_AInputQueue_getEvent(void *queue, void **event) {
  (void)queue;
  if (event)
    *event = NULL;
  return -1;
}
int b_AInputQueue_preDispatchEvent(void *queue, void *event) {
  (void)queue, (void)event;
  return 0;
}

/* ------------------------------------------------------------ AMotionEvent */
float b_AMotionEvent_getX(void *event, size_t pointer_index) {
  (void)event, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getY(void *event, size_t pointer_index) {
  (void)event, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getPressure(void *event, size_t pointer_index) {
  (void)event, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getSize(void *event, size_t pointer_index) {
  (void)event, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getTouchMajor(void *event, size_t pointer_index) {
  (void)event, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getTouchMinor(void *event, size_t pointer_index) {
  (void)event, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getToolMajor(void *event, size_t pointer_index) {
  (void)event, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getToolMinor(void *event, size_t pointer_index) {
  (void)event, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getOrientation(void *event, size_t pointer_index) {
  (void)event, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getXPrecision(void *event) {
  (void)event;
  return 0.0f;
}
float b_AMotionEvent_getYPrecision(void *event) {
  (void)event;
  return 0.0f;
}
float b_AMotionEvent_getHistoricalX(void *event, size_t history_index, size_t pointer_index) {
  (void)event, (void)history_index, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getHistoricalY(void *event, size_t history_index, size_t pointer_index) {
  (void)event, (void)history_index, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getHistoricalPressure(void *event, size_t history_index, size_t pointer_index) {
  (void)event, (void)history_index, (void)pointer_index;
  return 0.0f;
}
float b_AMotionEvent_getHistoricalSize(void *event, size_t history_index, size_t pointer_index) {
  (void)event, (void)history_index, (void)pointer_index;
  return 0.0f;
}
int64_t b_AMotionEvent_getDownTime(void *event) {
  (void)event;
  return 0;
}
int64_t b_AMotionEvent_getEventTime(void *event) {
  (void)event;
  return 0;
}
int64_t b_AMotionEvent_getHistoricalEventTime(void *event, size_t history_index) {
  (void)event, (void)history_index;
  return 0;
}
int b_AMotionEvent_getAction(void *event) {
  (void)event;
  return 0;
}
int b_AMotionEvent_getEdgeFlags(void *event) {
  (void)event;
  return 0;
}
int b_AMotionEvent_getFlags(void *event) {
  (void)event;
  return 0;
}
int b_AMotionEvent_getHistorySize(void *event) {
  (void)event;
  return 0;
}
int b_AMotionEvent_getMetaState(void *event) {
  (void)event;
  return 0;
}
size_t b_AMotionEvent_getPointerCount(void *event) {
  (void)event;
  return 0;
}
int b_AMotionEvent_getPointerId(void *event, size_t pointer_index) {
  (void)event, (void)pointer_index;
  return 0;
}

/* ----------------------------------------------------------- ANativeWindow */
int b_ANativeWindow_lock(void *window, void *buffer, void *dirty_rect) {
  (void)window, (void)buffer, (void)dirty_rect;
  return -1;
}
int b_ANativeWindow_unlockAndPost(void *window) {
  (void)window;
  return -1;
}

/* ---------------------------------------------------------------- C library */
int b_isdigit(int c) { return c >= '0' && c <= '9'; }
int b_isnan(double x) { return __builtin_isnan(x) ? 1 : 0; }
float b_ldexpf(float x, int exponent) { return (float)ldexp((double)x, exponent); }

int b_pthread_attr_setschedpolicy(void *attr, int policy) {
  (void)attr, (void)policy;
  return 0;
}
int b_umask(int mask) {
  (void)mask;
  return 0;
}

/* inotify: -1 makes libulua/the engine fall back to polling, as the runtime's
 * b_syscall does for the same syscalls. */
int b_inotify_init(void) { return -1; }
int b_inotify_add_watch(int fd, const char *path, unsigned int mask) {
  (void)fd, (void)path, (void)mask;
  return -1;
}

/* -------------------------------------------------------------- AAsset */
/* Wwise reads its banks through the NDK asset API. Each open locates the
 * entry in game.apk with miniz and keeps the uncompressed bytes; banks are a
 * few MB, and this avoids a second zip reader in the runtime. */

typedef struct {
  unsigned char *data;
  size_t size;
  size_t pos;
} AAssetStub;

static const char *apk_path(void) {
  const char *p = dcr_apk_path();
  static char fallback[512];
  if (p && p[0])
    return p;
  snprintf(fallback, sizeof fallback, "%s/game.apk", dcr_game_root());
  return fallback;
}

void *b_AAssetManager_fromJava(void *env, void *manager) {
  (void)env, (void)manager;
  return (void *)1;
}

void *b_AAssetManager_open(void *manager, const char *filename, int mode) {
  (void)manager, (void)mode;
  if (!filename)
    return NULL;
  const char *apk = apk_path();
  mz_zip_archive zip;
  memset(&zip, 0, sizeof zip);
  if (!mz_zip_reader_init_file(&zip, apk, 0)) {
    debugPrintf("[asset] cannot open APK %s\n", apk);
    return NULL;
  }
  char name[512];
  int idx = -1;
  const char *cands[5];
  int ncand = 0;
  if (filename[0] == '/')
    filename++;
  cands[ncand++] = filename;
  snprintf(name, sizeof name, "assets/%s", filename);
  cands[ncand++] = name;
  /* This APK has one Init.bnk at the bank root, not per-language copies;
   * Wwise asks for <language>/Init.bnk first. */
  if (strstr(filename, "Init.bnk")) {
    cands[ncand++] = "assets/Audio/GeneratedSoundBanks/Android/Init.bnk";
    cands[ncand++] = "Audio/GeneratedSoundBanks/Android/Init.bnk";
  }
  for (int i = 0; i < ncand && idx < 0; i++)
    idx = mz_zip_reader_locate_file(&zip, cands[i], NULL, 0);
  if (idx < 0) {
    debugPrintf("[asset] %s not in APK\n", filename);
    mz_zip_reader_end(&zip);
    return NULL;
  }
  mz_zip_archive_file_stat st;
  if (!mz_zip_reader_file_stat(&zip, idx, &st) || st.m_uncomp_size == 0) {
    mz_zip_reader_end(&zip);
    return NULL;
  }
  AAssetStub *a = calloc(1, sizeof *a);
  if (!a) {
    mz_zip_reader_end(&zip);
    return NULL;
  }
  a->size = (size_t)st.m_uncomp_size;
  a->data = malloc(a->size);
  if (!a->data || !mz_zip_reader_extract_to_mem(&zip, idx, a->data, a->size, 0)) {
    free(a->data);
    free(a);
    mz_zip_reader_end(&zip);
    return NULL;
  }
  mz_zip_reader_end(&zip);
  debugPrintf("[asset] %s: %lu bytes\n", filename, (unsigned long)a->size);
  return a;
}

void *b_AAssetManager_openDir(void *manager, const char *dirname) {
  (void)manager;
  debugPrintf("[asset] openDir(%s)\n", dirname ? dirname : "(null)");
  return (void *)1; /* the directory exists; Wwise opens known banks by name */
}
void b_AAssetDir_close(void *dir) { (void)dir; }
void b_AAsset_close(void *asset) {
  AAssetStub *a = asset;
  if (!a)
    return;
  free(a->data);
  free(a);
}
int b_AAsset_read(void *asset, void *buf, int count) {
  AAssetStub *a = asset;
  if (!a || !buf || count <= 0 || a->pos >= a->size)
    return a && a->pos >= a->size ? 0 : -1;
  size_t n = (size_t)count;
  if (n > a->size - a->pos)
    n = a->size - a->pos;
  memcpy(buf, a->data + a->pos, n);
  a->pos += n;
  return (int)n;
}
long b_AAsset_getLength(void *asset) {
  AAssetStub *a = asset;
  return a ? (long)a->size : 0;
}
long b_AAsset_seek(void *asset, long offset, int whence) {
  AAssetStub *a = asset;
  if (!a)
    return -1;
  long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)a->pos : (long)a->size;
  long pos = base + offset;
  if (pos < 0)
    pos = 0;
  if ((size_t)pos > a->size)
    pos = (long)a->size;
  a->pos = (size_t)pos;
  return pos;
}

/* -------------------------------------------------------------- zlib/libulua */
int b_compress2(unsigned char *dst, unsigned long *dst_len, const unsigned char *src,
                unsigned long src_len, int level) {
  return mz_compress2(dst, (mz_ulong *)dst_len, src, src_len, level);
}
