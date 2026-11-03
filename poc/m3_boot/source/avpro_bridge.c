/* avpro_bridge.c -- the Java side of AVPro's AndroidMediaPlayer, for the
 * Android video path the game uses for CG playback.
 *
 * The APK's classes.dex carries com.RenderHeads.AVProVideo.AVProMobileVideo,
 * which wraps android.media.MediaPlayer and a SurfaceTexture. This runtime
 * executes no Java, so every call the managed AVPro plugin makes on that
 * class landed in the "unhandled method" default and OpenVideoFromFile()
 * answered false; AVPro then logged "Failed to open jar:...!/assets/Video/..."
 * and the CG dialog force-ended. This file answers that class.
 *
 * M3_CG_DECODE=1 adds FFmpeg H.264/AAC decoding, render-thread texture
 * uploads and PCM mixed through the existing OpenSL audout stream. The
 * decode-disabled path retains the earlier state-only diagnostic bridge.
 *
 * MIT.
 */
#include "rt_settings.h"

#ifndef M3_AVPRO_BRIDGE
#define M3_AVPRO_BRIDGE 0
#endif

#if M3_AVPRO_BRIDGE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "dcr_path.h"
#include "jni.h"
#include "so_util.h"
#include "util.h"
#if M3_CG_DECODE
#include "cg_decoder.h"
#endif

#define CG_CLASS "com/RenderHeads/AVProVideo/AVProMobileVideo"
#define CG_MAX_PLAYERS 4
#define CG_DEFAULT_DURATION_MS 30000
#define CG_DEFAULT_FPS 30.0f

/* The port's APK asset shim (shims.c). */
void *b_AAssetManager_open(void *manager, const char *filename, int mode);
long b_AAsset_getLength(void *asset);
int b_AAsset_read(void *asset, void *buf, int count);
void b_AAsset_close(void *asset);

enum { CG_IDLE, CG_PREPARED, CG_PLAYING, CG_PAUSED, CG_STOPPED, CG_FINISHED, CG_ERROR };

typedef struct {
  int used;
  JObj *obj;
  char path[512];
  char source[560];
  int opened;
  int state;
  int64_t duration_ms;
  int64_t pos_ms;
  int width, height;
  int frame_count;
  int last_log_sec;
  float fps;
  int looping;
  int muted;
  float volume;
  float rate;
  int last_error;
  u64 play_base_ns;
  GLuint tex;
  int tex_w, tex_h;
  int tex_reset;
  int get_tex_calls;
  int frame_queries;
  int finished_logged;
} CgPlayer;

static CgPlayer g_players[CG_MAX_PLAYERS];
static int g_native_init_done;
static int g_render_calls;
#if M3_CG_DECODE
static CgPlayer *g_decode_owner;
static void cg_audio(CgPlayer *p, int64_t position) {
  if (g_decode_owner == p)
    m3_cg_decoder_audio(p->state == CG_PLAYING, position,
                        p->muted ? 0 : p->volume, p->rate);
}
#endif

static u64 cg_now_ns(void) { return armTicksToNs(armGetSystemTick()); }

static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

/* The movie header's duration (moov/mvhd), in milliseconds. 0 if absent or a
 * fragmented file that only carries mvex/trex. */
static int64_t mp4_duration_ms(const uint8_t *d, size_t n) {
  size_t off = 0;
  while (off + 8 <= n) {
    uint64_t size = be32(d + off);
    const uint8_t *type = d + off + 4;
    size_t hdr = 8;
    if (size == 1) {
      if (off + 16 > n) break;
      size = be64(d + off + 8);
      hdr = 16;
    } else if (size == 0) {
      size = n - off;
    }
    if (size < hdr || off + size > n) break;
    if (!memcmp(type, "moov", 4)) {
      const uint8_t *b = d + off + hdr;
      size_t end = off + (size_t)size;
      size_t p = 0;
      while (b + p + 8 <= d + end) {
        uint64_t bs = be32(b + p);
        const uint8_t *bt = b + p + 4;
        size_t bh = 8;
        if (bs == 1) {
          if (b + p + 16 > d + end) break;
          bs = be64(b + p + 8);
          bh = 16;
        } else if (bs == 0) {
          bs = (uint64_t)(d + end - (b + p));
        }
        if (bs < bh || b + p + bs > d + end) break;
        if (!memcmp(bt, "mvhd", 4) && bs >= bh + 20) {
          const uint8_t *m = b + p + bh;
          uint32_t timescale, duration;
          if (m[0] == 1 && bs >= bh + 32) {
            timescale = be32(m + 20);
            duration = (uint32_t)be64(m + 24);
          } else {
            timescale = be32(m + 12);
            duration = be32(m + 16);
          }
          if (timescale)
            return (int64_t)((uint64_t)duration * 1000u / timescale);
          return 0;
        }
        p += (size_t)bs;
      }
    }
    off += (size_t)size;
  }
  return 0;
}

/* Read the APK asset named by a jar: URL, or the file behind an Android path.
 * The bytes are only needed to read the duration at this stage. */
static uint8_t *cg_load(const char *path, size_t *out_size, char *source, size_t source_cap) {
  *out_size = 0;
  source[0] = 0;
  if (!path || !*path)
    return NULL;

  const char *asset = NULL;
  if (!strncmp(path, "jar:", 4)) {
    const char *sep = strstr(path, "!/");
    if (sep)
      asset = sep + 2;
    else
      asset = strstr(path, "!/");
  } else if (!strncmp(path, "file://", 7)) {
    path += 7;
  }

  if (asset) {
    while (*asset == '/')
      asset++;
    snprintf(source, source_cap, "apk:%s", asset);
    void *a = b_AAssetManager_open(NULL, asset, 0);
    if (!a)
      return NULL;
    long len = b_AAsset_getLength(a);
    if (len <= 0) {
      b_AAsset_close(a);
      return NULL;
    }
    uint8_t *buf = malloc((size_t)len);
    if (!buf) {
      b_AAsset_close(a);
      return NULL;
    }
    int got = 0;
    while (got < len) {
      int n = b_AAsset_read(a, buf + got, (int)(len - got));
      if (n <= 0)
        break;
      got += n;
    }
    b_AAsset_close(a);
    if (got != len) {
      free(buf);
      return NULL;
    }
    *out_size = (size_t)len;
    return buf;
  }

  char real[DCR_PATH_MAX];
  const char *mapped = dcr_translate_path(path, real, sizeof real);
  if (!mapped)
    mapped = path;
  snprintf(source, source_cap, "file:%s", mapped);
  FILE *f = fopen(mapped, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long len = ftell(f);
  if (len <= 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  uint8_t *buf = malloc((size_t)len);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t got = fread(buf, 1, (size_t)len, f);
  fclose(f);
  if (got != (size_t)len) {
    free(buf);
    return NULL;
  }
  *out_size = got;
  return buf;
}

static int cg_slot_index(const CgPlayer *p) {
  if (p >= &g_players[0] && p < &g_players[CG_MAX_PLAYERS])
    return (int)(p - g_players);
  return -1;
}

static int cg_any_opened(void) {
  for (int i = 0; i < CG_MAX_PLAYERS; i++)
    if (g_players[i].used && g_players[i].opened)
      return 1;
  return 0;
}

#if !M3_CG_DECODE
static void cg_start(CgPlayer *p) {
  if (!p || !p->opened || p->state == CG_PLAYING)
    return;
  p->state = CG_PLAYING;
  p->play_base_ns = cg_now_ns() - (u64)(p->pos_ms * 1000000);
#if M3_CG_DECODE
  cg_audio(p, p->pos_ms);
#endif
  debugPrintf("[cg] start idx=%d at %lldms\n", cg_slot_index(p), (long long)p->pos_ms);
}

#endif

/* A real 2D texture for AVPro's DisplayUGUI. Called from the plugin's render
 * event or GetTextureHandle; only touches GL when a context is current. */
static GLuint cg_make_texture(CgPlayer *p) {
  if (!p)
    return 0;
  EGLContext ctx = eglGetCurrentContext();
  if (ctx == EGL_NO_CONTEXT) {
    debugPrintf("[cg] texture idx=%d: no current EGL context\n", cg_slot_index(p));
    return 0;
  }
  GLint previous;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
  if (p->tex_reset) {
    if (p->tex) {
      if ((GLuint)previous == p->tex) previous = 0;
      glDeleteTextures(1, &p->tex);
    }
    p->tex = 0;
    p->tex_reset = 0;
  }
  if (p->tex) {
#if M3_CG_DECODE
    if (g_decode_owner == p && m3_cg_decoder_upload(p->tex, p->pos_ms)) {
      p->frame_count++;
      if (p->frame_count <= 3)
        debugPrintf("[cg] decoded texture frame=%d at %lldms\n", p->frame_count, (long long)p->pos_ms);
    }
#endif
    glBindTexture(GL_TEXTURE_2D, previous);
    return p->tex;
  }
  GLuint tex = 0;
  glGenTextures(1, &tex);
  if (!tex) {
    debugPrintf("[cg] texture idx=%d: glGenTextures failed err=0x%x\n", cg_slot_index(p),
                glGetError());
    return 0;
  }
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  void *zero = calloc(1, (size_t)p->width * p->height * 4u);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p->width, p->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
  free(zero);
  GLenum err = glGetError();
  p->tex = tex;
  p->tex_w = p->width;
  p->tex_h = p->height;
  glBindTexture(GL_TEXTURE_2D, previous);
  debugPrintf("[cg] texture idx=%d id=%u err=0x%x ctx=%p\n", cg_slot_index(p), tex, err,
              (void *)ctx);
  return tex;
}

static CgPlayer *cg_slot_of(JObj *self, int create) {
  if (self && self->p) {
    CgPlayer *p = self->p;
    if (p >= &g_players[0] && p < &g_players[CG_MAX_PLAYERS])
      return p;
  }
  if (!create)
    return NULL;
  for (int i = 0; i < CG_MAX_PLAYERS; i++) {
    if (!g_players[i].used) {
      memset(&g_players[i], 0, sizeof g_players[i]);
      g_players[i].used = 1;
      g_players[i].state = CG_IDLE;
      g_players[i].volume = 1.0f;
      g_players[i].rate = 1.0f;
      g_players[i].fps = CG_DEFAULT_FPS;
      if (self)
        self->p = &g_players[i];
      return &g_players[i];
    }
  }
  return NULL;
}

static CgPlayer *cg_slot_indexed(int index, int create) {
  if (index < 0 || index >= CG_MAX_PLAYERS)
    return NULL;
  if (create && !g_players[index].used) {
    memset(&g_players[index], 0, sizeof g_players[index]);
    g_players[index].used = 1;
    g_players[index].state = CG_IDLE;
    g_players[index].volume = 1.0f;
    g_players[index].rate = 1.0f;
    g_players[index].fps = CG_DEFAULT_FPS;
  }
  return g_players[index].used ? &g_players[index] : NULL;
}

static void cg_update(CgPlayer *p) {
  if (!p || p->state != CG_PLAYING)
    return;
  int64_t pos = (int64_t)((cg_now_ns() - p->play_base_ns) / 1000000ull);
  if (pos < 0)
    pos = 0;
  if (pos >= p->duration_ms) {
    if (p->looping) {
      p->play_base_ns = cg_now_ns();
      p->pos_ms = 0;
      p->frame_count = 0;
    } else {
      p->pos_ms = p->duration_ms;
      p->state = CG_FINISHED;
#if M3_CG_DECODE
      cg_audio(p, -1);
#endif
      if (!p->finished_logged) {
        p->finished_logged = 1;
        debugPrintf("[cg] finished idx=%d at %lldms frames=%d\n", cg_slot_index(p),
                    (long long)p->pos_ms, p->frame_count);
      }
    }
    return;
  }
  p->pos_ms = pos;
#if !M3_CG_DECODE
  p->frame_count = (int)((int64_t)(pos * p->fps) / 1000);
#endif
  int sec = (int)(pos / 1000);
  if (sec != p->last_log_sec) {
    p->last_log_sec = sec;
    debugPrintf("[cg] play idx=%d %lld/%lldms frames=%d src=%s\n", cg_slot_index(p),
                (long long)p->pos_ms, (long long)p->duration_ms, p->frame_count,
                p->source[0] ? p->source : "?");
  }
}

static void cg_reset(CgPlayer *p) {
  if (!p)
    return;
#if M3_CG_DECODE
  if (g_decode_owner == p) {
    m3_cg_decoder_close();
    g_decode_owner = NULL;
  }
#endif
  if (p->tex && eglGetCurrentContext() != EGL_NO_CONTEXT) {
    glDeleteTextures(1, &p->tex);
    p->tex = 0;
  }
  p->tex_reset = p->tex != 0;
  p->opened = 0;
  p->state = CG_IDLE;
  p->duration_ms = 0;
  p->pos_ms = 0;
  p->frame_count = 0;
  p->last_log_sec = -1;
  p->get_tex_calls = 0;
  p->frame_queries = 0;
  p->finished_logged = 0;
  p->path[0] = 0;
  p->source[0] = 0;
}

static void cg_native_init(void) {
  if (g_native_init_done)
    return;
  g_native_init_done = 1;
  typedef void (*init_fn)(void *, void *);
  init_fn fn = (init_fn)so_resolve_external(
      "Java_com_RenderHeads_AVProVideo_AVProMobileVideo_nativeInit");
  if (fn) {
    debugPrintf("[cg] AVProMobileVideo.nativeInit -> %p\n", (void *)fn);
    fn(g_jni_env, jni_class(CG_CLASS)->obj);
  } else {
    debugPrintf("[cg] AVProMobileVideo.nativeInit symbol not found\n");
  }
}

static jvalue cg_open(CgPlayer *p, const char *path) {
  if (!p)
    return (jvalue){.z = 0};
  cg_reset(p);
  snprintf(p->path, sizeof p->path, "%s", path ? path : "");
  size_t size = 0;
  uint8_t *data = cg_load(p->path, &size, p->source, sizeof p->source);
  if (!data) {
    p->state = CG_ERROR;
    p->last_error = 100;
    debugPrintf("[cg] open failed path=%s\n", p->path);
    return (jvalue){.z = 0};
  }
  int64_t duration = mp4_duration_ms(data, size);
#if M3_CG_DECODE
  if (g_decode_owner && g_decode_owner != p) cg_reset(g_decode_owner);
  if (!m3_cg_decoder_open(data, size)) {
    p->state = CG_ERROR;
    p->last_error = 101;
    debugPrintf("[cg] decoder open failed\n");
    return (jvalue){.z = 0};
  }
  g_decode_owner = p;
  if (m3_cg_decoder_duration() > 0) duration = m3_cg_decoder_duration();
#else
  free(data);
#endif
  if (duration <= 0)
    duration = CG_DEFAULT_DURATION_MS;
  p->opened = 1;
  p->state = CG_PREPARED;
  p->duration_ms = duration;
  p->width = 1280;
  p->height = 720;
#if M3_CG_DECODE
  p->width = m3_cg_decoder_width();
  p->height = m3_cg_decoder_height();
#endif
  p->fps = CG_DEFAULT_FPS;
  p->pos_ms = 0;
  p->frame_count = 0;
  p->last_log_sec = -1;
  p->last_error = 0;
  p->get_tex_calls = 0;
  p->frame_queries = 0;
  p->finished_logged = 0;
  debugPrintf("[cg] open ok idx=%d bytes=%lu duration=%lldms src=%s\n", cg_slot_index(p),
              (unsigned long)size, (long long)duration, p->source);
  /* The game passes autoPlay=false and expects AVPro's Started event to call
   * back into MonoVideoPlayer.OnVideoStarted, which calls Play(). With no
   * platform player behind AVPro, start the timeline here so IsPlaying and
   * Finished can drive that event path. */
#if !M3_CG_DECODE
  cg_start(p);
#endif
  return (jvalue){.z = 1};
}

static jvalue cg_get_texture(CgPlayer *p) {
  if (p && !p->tex)
    cg_make_texture(p);
  if (p && p->get_tex_calls++ < 3)
    debugPrintf("[cg] texture query idx=%d id=%u state=%d pos=%lld\n", cg_slot_index(p),
                p->tex ? p->tex : 0, p->state, (long long)p->pos_ms);
  return (jvalue){.i = p ? (jint)p->tex : 0};
}

void m3_avpro_shutdown(void) {
  for (int i = 0; i < CG_MAX_PLAYERS; i++)
    if (g_players[i].used) cg_reset(&g_players[i]);
}

int m3_avpro_jni(JObj *self, JMethod *m, const jvalue *a, jvalue *out) {
  if (!m || !m->cls || strcmp(m->cls->name, CG_CLASS))
    return 0;
  const char *n = m->name;

#define RET(v) do { *out = (v); return 1; } while (0)

  if (m->is_static) {
    int idx = (a && m->sig[0] == '(' && m->sig[1] == 'I') ? a[0].i : 0;
    CgPlayer *sp = cg_slot_indexed(idx, 0);
    if (!strcmp(n, "GetPluginVersion"))
      RET((jvalue){.l = jni_str("1.7.0-nx")});
    if (!strcmp(n, "RendererSetupPlayer")) {
      debugPrintf("[cg] RendererSetupPlayer idx=%d gl=%d\n", idx, a ? a[1].i : 0);
      CgPlayer *p = cg_slot_indexed(idx, 1);
      cg_make_texture(p);
      RET((jvalue){.z = 0});
    }
    if (!strcmp(n, "RenderPlayer")) {
      cg_update(sp);
      cg_make_texture(sp);
      if (g_render_calls++ < 3)
        debugPrintf("[cg] RenderPlayer idx=%d state=%d pos=%lld tex=%u\n", idx,
                    sp ? sp->state : -1, sp ? (long long)sp->pos_ms : -1,
                    sp ? (unsigned)sp->tex : 0);
      RET((jvalue){.z = 0});
    }
    if (!strcmp(n, "RendererDestroyPlayer") || !strcmp(n, "RendererReset") ||
        !strcmp(n, "WaitForNewFramePlayer"))
      RET((jvalue){.z = 0});
    if (!strcmp(n, "_GetWidth"))
      RET((jvalue){.i = sp ? sp->width : 0});
    if (!strcmp(n, "_GetHeight"))
      RET((jvalue){.i = sp ? sp->height : 0});
    if (!strcmp(n, "_GetTextureHandle"))
      RET(cg_get_texture(sp));
    if (!strcmp(n, "_GetDuration"))
      RET((jvalue){.j = sp ? sp->duration_ms : 0});
    if (!strcmp(n, "_GetFrameCount")) {
      if (sp && sp->frame_queries++ < 3)
        debugPrintf("[cg] _GetFrameCount idx=%d frames=%d pos=%lld state=%d\n", idx,
                    sp->frame_count, (long long)sp->pos_ms, sp->state);
      RET((jvalue){.i = sp ? sp->frame_count : 0});
    }
    if (!strcmp(n, "_GetVideoDisplayRate"))
      RET((jvalue){.f = sp ? sp->fps : 0});
    if (!strcmp(n, "_CanPlay")) {
      static int logged;
      int ok = (sp && sp->opened) || cg_any_opened();
      if (logged++ < 3)
        debugPrintf("[cg] _CanPlay idx=%d sp=%p opened=%d any=%d\n", idx, (void *)sp,
                    sp ? sp->opened : -1, cg_any_opened());
      RET((jvalue){.z = ok});
    }
    if (!strcmp(n, "_GetLastErrorCode"))
      RET((jvalue){.i = sp ? sp->last_error : 0});
    return 0;
  }

  CgPlayer *p = cg_slot_of(self, 0);

  if (!strcmp(n, "<init>") || !strcmp(n, "Initialise")) {
    p = cg_slot_of(self, 1);
    cg_native_init();
    debugPrintf("[cg] player init idx=%d\n", cg_slot_index(p));
    RET((jvalue){.i = 0});
  }
  if (!p)
    p = cg_slot_of(self, 1);
  if (!p)
    return 0;

  if (!strcmp(n, "GetPlayerIndex"))
    RET((jvalue){.i = cg_slot_index(p)});
  if (!strcmp(n, "SetPlayerOptions") || !strcmp(n, "SetDeinitialiseFlagged") ||
      !strcmp(n, "RendererSetup"))
    RET((jvalue){.i = 0});
  if (!strcmp(n, "OpenVideoFromFile")) {
    const char *path = a && a[0].l ? jni_utf(a[0].l) : "";
    RET(cg_open(p, path));
  }
  if (!strcmp(n, "CloseVideo") || !strcmp(n, "Deinitialise")) {
    debugPrintf("[cg] close idx=%d\n", cg_slot_index(p));
    cg_reset(p);
    RET((jvalue){.i = 0});
  }
  if (!strcmp(n, "Play")) {
    cg_update(p);
    if (p->opened) {
      if (p->state == CG_FINISHED || p->state == CG_STOPPED)
        p->pos_ms = 0;
      p->state = CG_PLAYING;
      p->play_base_ns = cg_now_ns() - (u64)(p->pos_ms * 1000000);
#if M3_CG_DECODE
      cg_audio(p, p->pos_ms);
#endif
      debugPrintf("[cg] play idx=%d at %lldms\n", cg_slot_index(p), (long long)p->pos_ms);
    } else {
      static int logged;
      if (logged++ < 3)
        debugPrintf("[cg] Play ignored idx=%d not opened\n", cg_slot_index(p));
    }
    RET((jvalue){.i = 0});
  }
  if (!strcmp(n, "Pause")) {
    cg_update(p);
    if (p->state == CG_PLAYING)
      p->state = CG_PAUSED;
#if M3_CG_DECODE
    cg_audio(p, -1);
#endif
    RET((jvalue){.i = 0});
  }
  if (!strcmp(n, "Stop")) {
    p->state = CG_STOPPED;
    p->pos_ms = 0;
    p->frame_count = 0;
#if M3_CG_DECODE
    cg_audio(p, 0);
#endif
    RET((jvalue){.i = 0});
  }
  if (!strcmp(n, "Seek")) {
    int ms = a ? a[0].i : 0;
    if (ms < 0)
      ms = 0;
    if (ms > p->duration_ms)
      ms = (int)p->duration_ms;
    p->pos_ms = ms;
    if (p->state == CG_PLAYING)
      p->play_base_ns = cg_now_ns() - (u64)(p->pos_ms * 1000000);
    RET((jvalue){.i = 0});
  }
  if (!strcmp(n, "SetLooping")) {
    p->looping = a && a[0].z;
    RET((jvalue){.i = 0});
  }
  if (!strcmp(n, "IsLooping"))
    RET((jvalue){.z = p->looping});
  if (!strcmp(n, "SetVolume")) {
    p->volume = a ? a[0].f : 1.0f;
#if M3_CG_DECODE
    cg_audio(p, -1);
#endif
    RET((jvalue){.i = 0});
  }
  if (!strcmp(n, "GetVolume"))
    RET((jvalue){.f = p->volume});
  if (!strcmp(n, "MuteAudio")) {
    p->muted = a && a[0].z;
#if M3_CG_DECODE
    cg_audio(p, -1);
#endif
    RET((jvalue){.i = 0});
  }
  if (!strcmp(n, "IsMuted"))
    RET((jvalue){.z = p->muted});
  if (!strcmp(n, "SetPlaybackRate")) {
    p->rate = a ? a[0].f : 1.0f;
    RET((jvalue){.i = 0});
  }
  if (!strcmp(n, "GetPlaybackRate"))
    RET((jvalue){.f = p->rate});
  if (!strcmp(n, "CanPlay")) {
    static int logged;
    int ok = (p->opened && p->state != CG_ERROR) || cg_any_opened();
    if (logged++ < 3)
      debugPrintf("[cg] CanPlay idx=%d opened=%d any=%d\n", cg_slot_index(p), p->opened,
                  cg_any_opened());
    RET((jvalue){.z = ok});
  }
  if (!strcmp(n, "HasMetaData")) {
    static int logged;
    int ok = p->opened || cg_any_opened();
    if (logged++ < 3)
      debugPrintf("[cg] HasMetaData idx=%d ok=%d\n", cg_slot_index(p), ok);
    RET((jvalue){.z = ok});
  }
  if (!strcmp(n, "HasVideo"))
    RET((jvalue){.z = p->opened});
  if (!strcmp(n, "HasAudio"))
#if M3_CG_DECODE
    RET((jvalue){.z = p->opened && g_decode_owner == p && m3_cg_decoder_has_audio()});
#else
    RET((jvalue){.z = p->opened});
#endif
  if (!strcmp(n, "HasSubtitles") || !strcmp(n, "HasTimedText"))
    RET((jvalue){.z = 0});
  if (!strcmp(n, "GetDurationMs"))
    RET((jvalue){.j = p->duration_ms});
  if (!strcmp(n, "GetCurrentTimeMs")) {
    cg_update(p);
    RET((jvalue){.j = p->pos_ms});
  }
  if (!strcmp(n, "GetWidth"))
    RET((jvalue){.i = p->width});
  if (!strcmp(n, "GetHeight"))
    RET((jvalue){.i = p->height});
  if (!strcmp(n, "GetFrameCount")) {
    cg_update(p);
    RET((jvalue){.i = p->frame_count});
  }
  if (!strcmp(n, "GetDisplayRate") || !strcmp(n, "GetSourceVideoFrameRate"))
    RET((jvalue){.f = p->fps});
  if (!strcmp(n, "GetBufferingProgressPercent"))
    RET((jvalue){.f = 100.0f});
  if (!strcmp(n, "GetNumberAudioTracks"))
    RET((jvalue){.i = 1});
  if (!strcmp(n, "GetCurrentAudioTrackIndex"))
    RET((jvalue){.i = 0});
  if (!strcmp(n, "GetLastErrorCode"))
    RET((jvalue){.i = p->last_error});
  if (!strcmp(n, "IsPlaying")) {
    cg_update(p);
    RET((jvalue){.z = p->state == CG_PLAYING});
  }
  if (!strcmp(n, "IsPaused")) {
    cg_update(p);
    RET((jvalue){.z = p->state == CG_PAUSED});
  }
  if (!strcmp(n, "IsFinished")) {
    cg_update(p);
    RET((jvalue){.z = p->state == CG_FINISHED});
  }
  if (!strcmp(n, "IsBuffering") || !strcmp(n, "IsSeeking") || !strcmp(n, "GetDeinitialised"))
    RET((jvalue){.z = 0});
  if (!strcmp(n, "GetTextureHandle"))
    RET(cg_get_texture(p));
  if (!strcmp(n, "GetTextureTimeStamp")) {
    cg_update(p);
    RET((jvalue){.j = p->pos_ms});
  }
  if (!strcmp(n, "Render")) {
    cg_update(p);
    cg_make_texture(p);
    RET((jvalue){.z = 1});
  }
  if (!strcmp(n, "WaitForNewFrame") || !strcmp(n, "WaitForExtract") ||
      !strcmp(n, "StartExtractFrame"))
    RET((jvalue){.z = 1});
  return 0;
#undef RET
}

#if M3_CG_SELFTEST
/* Actual port JNI + original native plugin + actual ARM FFmpeg, without
 * entering Unity's scene/GC loop. No GL context or device deployment needed. */
int m3_avpro_selftest(void) {
  extern void dcr_window_prepare(void);
  dcr_window_prepare();
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major, minor, count;
  EGLConfig config;
  const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
  const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  if (!eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_ES_API) ||
      !eglChooseConfig(display, attrs, &config, 1, &count) || count < 1) return 0;
  EGLSurface surface = eglCreateWindowSurface(display, config,
                     (EGLNativeWindowType)nwindowGetDefault(), NULL);
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attrs);
  if (!surface || !context || !eglMakeCurrent(display, surface, surface, context)) return 0;
  JObj *self = jni_new(CG_CLASS);
  CgPlayer *p = cg_slot_of(self, 1);
  cg_native_init();
  if (!p) return 0;
  const char *names[] = {"Story_01", "start"};
  for (unsigned movie = 0; movie < 2; movie++) {
    char path[256];
    snprintf(path, sizeof path, "jar:file:///data/app/port/base.apk!/assets/Video/%s.mp4", names[movie]);
    if (!cg_open(p, path).z) return 0;
    typedef int (*query_fn)(int);
    typedef int64_t (*duration_fn)(int);
    query_fn can = (query_fn)so_resolve_external("_CanPlay");
    query_fn width = (query_fn)so_resolve_external("_GetWidth");
    query_fn height = (query_fn)so_resolve_external("_GetHeight");
    duration_fn duration = (duration_fn)so_resolve_external("_GetDuration");
    int idx = cg_slot_index(p);
    if (!can || !width || !height || !duration || !can(idx) ||
        width(idx) != p->width || height(idx) != p->height || duration(idx) != p->duration_ms) return 0;
    debugPrintf("[cg-test] native getters PASS movie=%s idx=%d %dx%d duration=%lld\n",
                names[movie], idx, p->width, p->height, (long long)p->duration_ms);
#if M3_CG_DECODE
    m3_cg_decoder_probe_texture(cg_make_texture(p));
    unsigned checksum = 0, nonblack = 0, peak;
    int frames = 0;
    u64 deadline = cg_now_ns() + 90000000000ull;
    while (frames < 120 && cg_now_ns() < deadline) {
      int got = m3_cg_decoder_probe(&checksum, &nonblack);
      if (got < 0) break;
      frames += got;
      if (!got) svcSleepThread(2000000ll);
    }
    peak = m3_cg_decoder_audio_probe();
    p->state = CG_PLAYING;
    cg_audio(p, 0);
    unsigned mixed_peak = m3_cg_decoder_mix_probe();
    p->state = CG_PAUSED;
    cg_audio(p, -1);
    unsigned paused_peak = m3_cg_decoder_mix_probe();
    debugPrintf("[cg-test] decode movie=%s frames=%d checksum=%08x nonblack=%u audio_peak=%u\n",
                names[movie], frames, checksum, nonblack, peak);
    debugPrintf("[cg-test] GPU pixels/mixer movie=%s mixed_peak=%u paused_peak=%u\n",
                names[movie], mixed_peak, paused_peak);
    if (frames < 120 || !nonblack || !peak || !mixed_peak || paused_peak) { cg_reset(p); return 0; }
#endif
    cg_reset(p);
    if (can(idx)) return 0;
  }
  jni_release(self);
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglDestroySurface(display, surface);
  eglTerminate(display);
  debugPrintf("[cg-test] PASS native JNI/FFmpeg/reopen/close\n");
  return 1;
}
#endif
#endif /* M3_AVPRO_BRIDGE */
