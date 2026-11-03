/* CG decoder backend, derived from spacehulk-nx sh_video.c/sh_resample.h
 * (MIT). Keeps FFmpeg demux/audio and picture decoding on separate threads.
 * AVPro owns the UI and GL context; only decoded pixels enter its texture.
 */
#include "rt_settings.h"
#if M3_CG_DECODE
#include <arm_neon.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_path.h"
#include "cg_resample.h"
#include "cg_decoder.h"
#include "rt_audout.h"
#include <GLES2/gl2.h>
#include "util.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

/* FFmpeg 7.1's h2645_sei.c resets the AOM film grain sets unconditionally,
 * but aom_film_grain.o is only built with HEVC, which ffmpeg32 leaves out
 * (the Sonic and Angry Birds ports do the same). */
void ff_aom_uninit_film_grain_params(void *s);
void ff_aom_uninit_film_grain_params(void *s) { (void)s; }

#define NSLOT 4 /* pictures decoded ahead */

/* ------------------------------------------------------------ the movie */
static struct {
  Mutex lock;
  volatile int stop, eof;
  uint8_t *bytes;
  int64_t base, len, pos;
  AVFormatContext *fmt;
  AVIOContext *io;
  AVCodecContext *vdec, *adec;
  int vs, as;
  double vtb;
  Thread thread;
  int thread_on;
  int w, h;
  uint8_t *rgba[NSLOT];
  double pts[NSLOT];
  volatile int ready[NSLOT];
  int16_t *pcm; /* all of the sound, s16 stereo at arate */
  size_t pcm_cap, pcm_frames;
  int arate;
  double apos; /* next frame to play */
  volatile int audio_go;
  volatile uint64_t played; /* output frames given to the pump */
  uint64_t starved;          /* of those, played as silence: the sound there not decoded yet */
} V;

static int io_read(void *opaque, uint8_t *buf, int n) {
  (void)opaque;
  const int64_t left = V.len - V.pos;
  if (left <= 0)
    return AVERROR_EOF;
  if (n > left)
    n = (int)left;
  memcpy(buf, V.bytes + V.pos, (size_t)n);
  size_t got = (size_t)n;
  V.pos += (int64_t)got;
  return (int)got;
}

static int64_t io_seek(void *opaque, int64_t off, int whence) {
  (void)opaque;
  if (whence == AVSEEK_SIZE)
    return V.len;
  whence &= ~AVSEEK_FORCE;
  int64_t p = whence == SEEK_SET ? off : whence == SEEK_CUR ? V.pos + off : V.len + off;
  if (p < 0 || p > V.len)
    return -1;
  V.pos = p;
  return p;
}

/* BT.601 (video range) YUV 4:2:0 -> RGBA, 16 pixels a step (ssr_video.c) */
static void yuv_to_rgba(const AVFrame *f, uint8_t *out, int w, int h) {
  const int16x8_t k74 = vdupq_n_s16(74), k102 = vdupq_n_s16(102), k25 = vdupq_n_s16(25), k52 = vdupq_n_s16(52),
                  k129 = vdupq_n_s16(129), k16 = vdupq_n_s16(16), k128 = vdupq_n_s16(128);
  for (int y = 0; y < h; y++) {
    const uint8_t *py = f->data[0] + (size_t)y * f->linesize[0];
    const uint8_t *pu = f->data[1] + (size_t)(y / 2) * f->linesize[1];
    const uint8_t *pv = f->data[2] + (size_t)(y / 2) * f->linesize[2];
    uint8_t *d = out + (size_t)y * w * 4;
    int x = 0;
    for (; x + 16 <= w; x += 16) {
      const uint8x16_t yy = vld1q_u8(py + x);
      const uint8x8_t uu = vld1_u8(pu + x / 2), vv = vld1_u8(pv + x / 2);
      const uint8x8x2_t u2 = vzip_u8(uu, uu), v2 = vzip_u8(vv, vv);
      uint8x16x4_t px;
      for (int half = 0; half < 2; half++) {
        const uint8x8_t y8 = half ? vget_high_u8(yy) : vget_low_u8(yy);
        const int16x8_t c = vmulq_s16(vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(y8)), k16), k74);
        const int16x8_t d_ = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(u2.val[half])), k128);
        const int16x8_t e = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(v2.val[half])), k128);
        const int16x8_t r = vqaddq_s16(c, vmulq_s16(e, k102));
        const int16x8_t g = vqsubq_s16(vqsubq_s16(c, vmulq_s16(d_, k25)), vmulq_s16(e, k52));
        const int16x8_t b = vqaddq_s16(c, vmulq_s16(d_, k129));
        const uint8x8_t r8 = vqrshrun_n_s16(r, 6), g8 = vqrshrun_n_s16(g, 6), b8 = vqrshrun_n_s16(b, 6);
        if (half) {
          px.val[0] = vcombine_u8(vget_low_u8(px.val[0]), r8);
          px.val[1] = vcombine_u8(vget_low_u8(px.val[1]), g8);
          px.val[2] = vcombine_u8(vget_low_u8(px.val[2]), b8);
        } else {
          px.val[0] = vcombine_u8(r8, r8);
          px.val[1] = vcombine_u8(g8, g8);
          px.val[2] = vcombine_u8(b8, b8);
        }
      }
      px.val[3] = vdupq_n_u8(255);
      vst4q_u8(d + x * 4, px);
    }
    for (; x < w; x++) {
      const int c = (py[x] - 16) * 74, du = pu[x / 2] - 128, ev = pv[x / 2] - 128;
      int r = (c + 102 * ev + 32) >> 6, g = (c - 25 * du - 52 * ev + 32) >> 6, b = (c + 129 * du + 32) >> 6;
      d[x * 4 + 0] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
      d[x * 4 + 1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
      d[x * 4 + 2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
      d[x * 4 + 3] = 255;
    }
  }
}

static void put_picture(const AVFrame *f) {
  if (f->format != AV_PIX_FMT_YUV420P || f->width != V.w || f->height != V.h)
    return;
  int slot = -1;
  while (!V.stop) {
    mutexLock(&V.lock);
    for (int i = 0; i < NSLOT && slot < 0; i++)
      if (!V.ready[i])
        slot = i;
    mutexUnlock(&V.lock);
    if (slot >= 0)
      break;
    svcSleepThread(2000000ll);
  }
  if (slot < 0)
    return;
  yuv_to_rgba(f, V.rgba[slot], V.w, V.h);
  const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
  mutexLock(&V.lock);
  V.pts[slot] = ts == AV_NOPTS_VALUE ? 0 : (double)ts * V.vtb;
  V.ready[slot] = 1;
  mutexUnlock(&V.lock);
}

static void put_sound(const AVFrame *f) {
  if (!V.pcm || f->nb_samples <= 0)
    return;
  const int ch = f->ch_layout.nb_channels;
  size_t n = (size_t)f->nb_samples;
  mutexLock(&V.lock);
  if (V.pcm_frames + n > V.pcm_cap)
    n = V.pcm_cap - V.pcm_frames;
  int16_t *d = V.pcm + V.pcm_frames * 2;
  mutexUnlock(&V.lock);
  for (size_t i = 0; i < n; i++)
    for (int c = 0; c < 2; c++) {
      const int k = c < ch ? c : 0;
      float s;
      switch (f->format) {
      case AV_SAMPLE_FMT_FLTP: s = ((const float *)f->extended_data[k])[i]; break;
      case AV_SAMPLE_FMT_FLT: s = ((const float *)f->extended_data[0])[i * ch + k]; break;
      case AV_SAMPLE_FMT_S16P: s = ((const int16_t *)f->extended_data[k])[i] / 32768.0f; break;
      case AV_SAMPLE_FMT_S16: s = ((const int16_t *)f->extended_data[0])[i * ch + k] / 32768.0f; break;
      default: s = 0; break;
      }
      int v = (int)(s * 32767.0f);
      d[i * 2 + c] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
  mutexLock(&V.lock);
  V.pcm_frames += n;
  mutexUnlock(&V.lock);
}

static ShResampler g_rs; /* the movie's rate -> the output's */

/* The audio pump's source while a movie plays (dcr_audio.c): the movie's
 * sound at the output rate, silence until the first picture. */
static int audio_source(int16_t *out, int frames, int rate) {
  memset(out, 0, (size_t)frames * 4);
  if (!V.audio_go || !V.pcm || V.arate <= 0) {
    V.played += (uint64_t)frames;
    return 1;
  }
  mutexLock(&V.lock);
  /* the position moves on whether or not the sound there is decoded yet
   * (silence then): it is the clock the pictures follow, and must not stall
   * behind a decoder that is busy with pictures */
  const double step = (double)V.arate / (double)rate;
  const double have = (double)V.pcm_frames;
  if (g_rs.in_rate != V.arate || g_rs.out_rate != rate)
    sh_resample_init(&g_rs, V.arate, rate);
  for (int i = 0; i < frames; i++) {
    if (V.apos + 1 < have)
      sh_resample_at(&g_rs, V.pcm, (int64_t)have, V.apos, &out[i * 2]);
    else if (!V.eof)
      V.starved++;
    V.apos += step;
  }
  mutexUnlock(&V.lock);
  V.played += (uint64_t)frames;
  return 1;
}

/* Two threads. The file keeps a moment's sound up to 0.35-0.40 s after its
 * pictures (both movies), and one thread reading the file and decoding the
 * pictures left that sound undecoded while it waited for a picture slot or
 * spent its time on 1080p pictures: the clock played silence in its place
 * (the intro's music came out holed -- hiss and echo on the console's
 * speaker; the logo 5.9 s silent of 6.2, 2026-10-09). So the reader decodes
 * the sound at once and queues the pictures' packets; the picture thread
 * decodes them into the slots. */
#define VQ_MAX 256 /* ~8 s of pictures read ahead */
static AVPacket *g_vq[VQ_MAX];
static int g_vq_head, g_vq_n;
static volatile int g_read_done;
static Thread g_vthread;
static int g_vthread_on;

static void picture_thread(void *arg) {
  (void)arg;
  AVFrame *fr = av_frame_alloc();
  int pictures = 0;
  while (fr && !V.stop) {
    AVPacket *p = NULL;
    mutexLock(&V.lock);
    if (g_vq_n) {
      p = g_vq[g_vq_head];
      g_vq_head = (g_vq_head + 1) % VQ_MAX;
      g_vq_n--;
    }
    mutexUnlock(&V.lock);
    if (!p) {
      if (!g_read_done) {
        svcSleepThread(2000000ll);
        continue;
      }
      avcodec_send_packet(V.vdec, NULL); /* the end: what the decoder holds */
      while (!V.stop && avcodec_receive_frame(V.vdec, fr) == 0)
        put_picture(fr), pictures++;
      break;
    }
    if (avcodec_send_packet(V.vdec, p) >= 0)
      while (!V.stop && avcodec_receive_frame(V.vdec, fr) == 0)
        put_picture(fr), pictures++;
    av_packet_free(&p);
  }
  av_frame_free(&fr);
  debugPrintf("[video] decoded %d pictures, %u sound frames%s\n", pictures, (unsigned)V.pcm_frames,
              V.stop ? " (stopped)" : "");
  V.eof = 1;
}

static void decode_thread(void *arg) {
  (void)arg;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *fr = av_frame_alloc();
  while (pkt && fr && !V.stop) {
    mutexLock(&V.lock);
    const int full = g_vq_n == VQ_MAX;
    mutexUnlock(&V.lock);
    if (full) { /* far enough ahead: wait for the pictures */
      svcSleepThread(2000000ll);
      continue;
    }
    const int r = av_read_frame(V.fmt, pkt);
    if (r < 0) {
      if (V.adec)
        avcodec_send_packet(V.adec, NULL);
      while (V.adec && avcodec_receive_frame(V.adec, fr) == 0)
        put_sound(fr);
      break;
    }
    if (pkt->stream_index == V.vs && V.vdec) {
      AVPacket *q = av_packet_alloc();
      if (q) {
        av_packet_move_ref(q, pkt);
        mutexLock(&V.lock);
        g_vq[(g_vq_head + g_vq_n) % VQ_MAX] = q;
        g_vq_n++;
        mutexUnlock(&V.lock);
      }
    } else if (pkt->stream_index == V.as && V.adec && avcodec_send_packet(V.adec, pkt) >= 0) {
      while (!V.stop && avcodec_receive_frame(V.adec, fr) == 0)
        put_sound(fr);
    }
    av_packet_unref(pkt);
  }
  av_frame_free(&fr);
  av_packet_free(&pkt);
  g_read_done = 1;
  if (!V.vdec)
    V.eof = 1;
}

static void audio_disable(void);

static void close_all(void) {
  V.stop = 1;
  if (V.thread_on) {
    threadWaitForExit(&V.thread);
    threadClose(&V.thread);
    V.thread_on = 0;
  }
  if (g_vthread_on) {
    threadWaitForExit(&g_vthread);
    threadClose(&g_vthread);
    g_vthread_on = 0;
  }
  while (g_vq_n) { /* stopped: the packets left */
    av_packet_free(&g_vq[g_vq_head]);
    g_vq_head = (g_vq_head + 1) % VQ_MAX;
    g_vq_n--;
  }
  g_vq_head = 0;
  g_read_done = 0;
  audio_disable();
  V.audio_go = 0;
  avcodec_free_context(&V.vdec);
  avcodec_free_context(&V.adec);
  if (V.fmt)
    avformat_close_input(&V.fmt);
  if (V.io) {
    av_freep(&V.io->buffer);
    avio_context_free(&V.io);
  }
  for (int i = 0; i < NSLOT; i++) {
    free(V.rgba[i]);
    V.rgba[i] = NULL;
    V.ready[i] = 0;
  }
  free(V.pcm);
  V.pcm = NULL;
  free(V.bytes);
  V.bytes = NULL;
}

static AVCodecContext *open_decoder(AVStream *st, int fast) {
  const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
  AVCodecContext *ctx = c ? avcodec_alloc_context3(c) : NULL;
  if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
    avcodec_free_context(&ctx);
    return NULL;
  }
  if (fast) { /* 1080p H.264 on one core: no deblocking, the fast paths */
    ctx->skip_loop_filter = AVDISCARD_ALL;
    ctx->flags2 |= AV_CODEC_FLAG2_FAST;
  }
  if (avcodec_open2(ctx, c, NULL) < 0)
    avcodec_free_context(&ctx);
  return ctx;
}

static int open_movie(uint8_t *bytes, size_t size) {
  memset(&V, 0, sizeof V);
  V.vs = V.as = -1;
  mutexInit(&V.lock);
  V.bytes = bytes;
  V.len = (int64_t)size;
  av_log_set_level(AV_LOG_ERROR);
  unsigned char *buf = av_malloc(65536);
  V.io = buf ? avio_alloc_context(buf, 65536, 0, NULL, io_read, NULL, io_seek) : NULL;
  V.fmt = avformat_alloc_context();
  if (!V.io || !V.fmt)
    return 0;
  V.fmt->pb = V.io;
  if (avformat_open_input(&V.fmt, NULL, NULL, NULL) < 0 || avformat_find_stream_info(V.fmt, NULL) < 0) {
    debugPrintf("[video] not a movie FFmpeg reads here\n");
    return 0;
  }
  V.vs = av_find_best_stream(V.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  V.as = av_find_best_stream(V.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  if (V.vs >= 0) {
    const AVCodecParameters *cp = V.fmt->streams[V.vs]->codecpar;
    V.vdec = open_decoder(V.fmt->streams[V.vs], cp->width * cp->height > 1280 * 720);
  }
  if (V.as >= 0)
    V.adec = open_decoder(V.fmt->streams[V.as], 0);
  if (!V.vdec) {
    debugPrintf("[video] no picture decoder for it\n");
    return 0;
  }
  V.w = V.vdec->width;
  V.h = V.vdec->height;
  if (V.w < 1 || V.h < 1 || V.w > 1920 || V.h > 1080) return 0;
  V.vtb = av_q2d(V.fmt->streams[V.vs]->time_base);
  for (int i = 0; i < NSLOT; i++)
    if (!(V.rgba[i] = memalign(64, (size_t)V.w * V.h * 4)))
      return 0;
  if (V.adec) {
    V.arate = V.adec->sample_rate;
    const double secs = V.fmt->duration > 0 ? (double)V.fmt->duration / AV_TIME_BASE : 120.0;
    if (secs > 600 || V.arate <= 0 || V.arate > 96000) return 0;
    V.pcm_cap = (size_t)((secs + 2.0) * V.arate);
    V.pcm = malloc(V.pcm_cap * 4);
  }
  debugPrintf("[video] %dx%d %s, %s %d Hz, %.1f s\n", V.w, V.h, avcodec_get_name(V.vdec->codec_id),
              V.adec ? avcodec_get_name(V.adec->codec_id) : "no sound", V.arate,
              V.fmt->duration > 0 ? (double)V.fmt->duration / AV_TIME_BASE : 0.0);
  return 1;
}


/* The existing OpenSL mixer remains the only audout submitter. CG is mixed
 * into its buffers, so no second audio device or competing pump is created. */
static Mutex audio_lock;
static int active, playing;
static float gain = 1.0f, speed = 1.0f;
static void audio_disable(void) {
  mutexLock(&audio_lock);
  active = playing = 0;
  mutexUnlock(&audio_lock);
}
int __real_rt_audout_submit(const int16_t *frames);
static int mix_frames(const int16_t *frames, int16_t *mixed) {
  int16_t cg[RT_AUDOUT_FRAMES * 2];
  mutexLock(&audio_lock);
  if (!active || !playing) {
    mutexUnlock(&audio_lock);
    return 0;
  }
  audio_source(cg, RT_AUDOUT_FRAMES, (int)(rt_audout_rate() / speed));
  for (int i = 0; i < RT_AUDOUT_FRAMES * 2; i++) {
    int sample = frames[i] + (int)(cg[i] * gain);
    mixed[i] = sample < -32768 ? -32768 : sample > 32767 ? 32767 : sample;
  }
  mutexUnlock(&audio_lock);
  return 1;
}
int __wrap_rt_audout_submit(const int16_t *frames) {
  int16_t mixed[RT_AUDOUT_FRAMES * 2];
  return __real_rt_audout_submit(mix_frames(frames, mixed) ? mixed : frames);
}
unsigned m3_cg_decoder_mix_probe(void) {
  const int16_t silence[RT_AUDOUT_FRAMES * 2] = {0};
  int16_t mixed[RT_AUDOUT_FRAMES * 2];
  unsigned peak = 0;
  for (int block = 0; block < 128; block++) {
    if (!mix_frames(silence, mixed)) return 0;
    for (unsigned i = 0; i < RT_AUDOUT_FRAMES * 2; i++) {
      unsigned value = mixed[i] < 0 ? -mixed[i] : mixed[i];
      if (value > peak) peak = value;
    }
  }
  return peak;
}

void m3_cg_decoder_close(void) { close_all(); }

int m3_cg_decoder_open(uint8_t *owned, size_t size) {
  close_all();
  if (!open_movie(owned, size)) { close_all(); return 0; }
  Result rc = threadCreate(&V.thread, decode_thread, NULL, NULL, 0x40000, 0x2c, -2);
  if (R_FAILED(rc)) { close_all(); return 0; }
  rc = threadStart(&V.thread);
  if (R_FAILED(rc)) { threadClose(&V.thread); close_all(); return 0; }
  V.thread_on = 1;
  rc = threadCreate(&g_vthread, picture_thread, NULL, NULL, 0x40000, 0x2c, -2);
  if (R_FAILED(rc)) { close_all(); return 0; }
  rc = threadStart(&g_vthread);
  if (R_FAILED(rc)) { threadClose(&g_vthread); close_all(); return 0; }
  g_vthread_on = 1;
  mutexLock(&audio_lock);
  active = 1; playing = 0; gain = speed = 1.0f;
  mutexUnlock(&audio_lock);
  return 1;
}
int m3_cg_decoder_width(void) { return V.w; }
int m3_cg_decoder_height(void) { return V.h; }
int64_t m3_cg_decoder_duration(void) { return V.fmt ? V.fmt->duration / 1000 : 0; }
int m3_cg_decoder_has_audio(void) { return V.adec && V.pcm; }
void m3_cg_decoder_audio(int on, int64_t ms, float volume, float rate) {
  mutexLock(&audio_lock);
  playing = on;
  V.audio_go = on;
  if (ms >= 0) V.apos = (double)ms * V.arate / 1000.0;
  gain = volume < 0 ? 0 : volume > 1 ? 1 : volume;
  speed = rate > 0.1f && rate < 8 ? rate : 1;
  mutexUnlock(&audio_lock);
}

/* Only a render callback with a current EGL context calls upload. Keep
 * the slot reserved until GL has copied its pixels; decoder reuses it then. */
int m3_cg_decoder_upload(unsigned texture, int64_t ms) {
  int due = -1;
  mutexLock(&V.lock);
  for (int i = 0; i < NSLOT; i++)
    if (V.ready[i] && V.pts[i] * 1000 <= ms + 1 &&
        (due < 0 || V.pts[i] > V.pts[due])) due = i;
  if (due >= 0) {
    for (int i = 0; i < NSLOT; i++)
      if (i != due && V.ready[i] && V.pts[i] < V.pts[due]) V.ready[i] = 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, V.w, V.h, GL_RGBA, GL_UNSIGNED_BYTE, V.rgba[due]);
    V.ready[due] = 0;
  }
  mutexUnlock(&V.lock);
  return due >= 0;
}

static unsigned probe_texture;
void m3_cg_decoder_probe_texture(unsigned texture) { probe_texture = texture; }

/* Consume frames and, when requested, verify their real GPU texture pixels. */
int m3_cg_decoder_probe(unsigned *checksum, unsigned *nonblack) {
  int slot = -1;
  mutexLock(&V.lock);
  for (int i = 0; i < NSLOT; i++)
    if (V.ready[i] && (slot < 0 || V.pts[i] < V.pts[slot])) slot = i;
  if (slot >= 0) {
    const uint8_t *p = V.rgba[slot];
    unsigned hash = 2166136261u, lit = 0;
    for (size_t i = 0; i < (size_t)V.w * V.h * 4; i += 4) {
      hash = (hash ^ p[i]) * 16777619u;
      hash = (hash ^ p[i+1]) * 16777619u;
      hash = (hash ^ p[i+2]) * 16777619u;
      if (p[i] || p[i+1] || p[i+2]) lit++;
    }
    *checksum ^= hash; *nonblack += lit;
    if (probe_texture) {
      GLint previous_tex, previous_fbo;
      GLuint fbo;
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_tex);
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
      glBindTexture(GL_TEXTURE_2D, probe_texture);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, V.w, V.h, GL_RGBA, GL_UNSIGNED_BYTE, p);
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probe_texture, 0);
      uint8_t pixel[4];
      glReadPixels(V.w/2, V.h/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
      int ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
               !memcmp(pixel, p + ((size_t)(V.h/2) * V.w + V.w/2) * 4, 4) && glGetError() == GL_NO_ERROR;
      glBindFramebuffer(GL_FRAMEBUFFER, previous_fbo);
      glDeleteFramebuffers(1, &fbo);
      glBindTexture(GL_TEXTURE_2D, previous_tex);
      if (!ok) { mutexUnlock(&V.lock); return -2; }
    }
    V.ready[slot] = 0;
  }
  int result = slot >= 0 ? 1 : V.eof ? -1 : 0;
  mutexUnlock(&V.lock);
  return result;
}
unsigned m3_cg_decoder_audio_probe(void) {
  unsigned peak = 0;
  mutexLock(&V.lock);
  for (size_t i = 0; i < V.pcm_frames * 2; i++) {
    int sample = V.pcm[i];
    unsigned value = sample < 0 ? -sample : sample;
    if (value > peak) peak = value;
  }
  mutexUnlock(&V.lock);
  return peak;
}
#endif
