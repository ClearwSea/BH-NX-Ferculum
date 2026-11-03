/* Unity's deferred Android text dialog backed by the Switch keyboard. MIT. */
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include "jni.h"
#include "input_bridge.h"
#include "rt_applet.h"
#include "util.h"
#define PLAYER "com/unity3d/player/UnityPlayer"
static int pending, secure, close_pending;
static char initial[1025], hint[256];
extern unsigned m3_string_encode_calls, m3_string_decode_calls;
extern size_t m3_string_encode_bytes, m3_string_decode_bytes;
static unsigned observe_frames, observed_enc, observed_dec;

int m3_keyboard_jni(JObj *self, JMethod *m, const jvalue *a, jvalue *out) {
  (void)self;
  if (!m || !m->cls || strcmp(m->cls->name, PLAYER)) return 0;
  if (!strcmp(m->name, "showSoftInput") &&
      !strcmp(m->sig, "(Ljava/lang/String;IZZZZLjava/lang/String;)V")) {
    snprintf(initial, sizeof initial, "%s", jni_utf(a[0].l));
    snprintf(hint, sizeof hint, "%s", jni_utf(a[6].l));
    secure = a[4].z != 0;
    pending = 1;
    debugPrintf("[keyboard] requested type=%d secure=%d\n", a[1].i, secure);
  } else if (!strcmp(m->name, "hideSoftInput")) {
    pending = 0;
  } else if (!strcmp(m->name, "setSoftInputStr")) {
    snprintf(initial, sizeof initial, "%s", jni_utf(a[0].l));
  } else return 0;
  *out = jv_none();
  return 1;
}

int m3_keyboard_pump(void) {
  if (observe_frames) {
    observe_frames--;
    if (!observe_frames)
      debugPrintf("[keyboard-trace] render-window enc_calls=%u enc_last=%lu dec_calls=%u dec_last=%lu\n",
                  m3_string_encode_calls-observed_enc, (unsigned long)m3_string_encode_bytes,
                  m3_string_decode_calls-observed_dec, (unsigned long)m3_string_decode_bytes);
  }
  /* Android delivers text-change events while the keyboard is still active.
   * A blocking system keyboard has no intermediate events: allow one Unity
   * render/update after committing text before delivering the final close. */
  if (close_pending) {
    typedef void (*Closed)(void *, void *);
    Closed closed = (Closed)jni_native(PLAYER, "nativeSoftInputClosed");
    close_pending = 0;
    if (closed) closed(g_jni_env, jni_singleton(PLAYER));
    debugPrintf("[keyboard-trace] deferred close after render\n");
  }
  if (!pending) return 0;
  pending = 0;
  typedef void (*SetText)(void *, void *, void *);
  typedef void (*SetCanceled)(void *, void *, jboolean);
  typedef void (*Closed)(void *, void *);
  SetText set_text = (SetText)jni_native(PLAYER, "nativeSetInputString");
  SetCanceled set_canceled = (SetCanceled)jni_native(PLAYER, "nativeSetInputCanceled");
  Closed closed = (Closed)jni_native(PLAYER, "nativeSoftInputClosed");
  if (!set_text || !set_canceled || !closed) {
    debugPrintf("[keyboard] missing Unity callbacks\n");
    return 0;
  }
  SwkbdConfig config;
  char text[1025] = {0};
  m3_input_cancel();
  dcr_applet_busy(1);
  Result rc = swkbdCreate(&config, 0);
  if (R_SUCCEEDED(rc)) {
    if (secure) swkbdConfigMakePresetPassword(&config);
    else swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetStringLenMax(&config, 256);
    swkbdConfigSetInitialText(&config, initial);
    swkbdConfigSetGuideText(&config, hint);
    rc = swkbdShow(&config, text, sizeof text);
    swkbdClose(&config);
  }
  dcr_applet_busy(0);
  m3_input_set_active(1);
  JObj *player = jni_singleton(PLAYER);
  debugPrintf("[keyboard-trace] system-result rc=%x bytes=%lu\n", rc, (unsigned long)strlen(text));
  unsigned before_enc = m3_string_encode_calls, before_dec = m3_string_decode_calls;
  /* Match UnityPlayer$10: cancellation callback is only sent on cancel. */
  if (R_FAILED(rc)) set_canceled(g_jni_env, player, 1);
  if (R_SUCCEEDED(rc)) {
    JObj *str = jni_str(text);
    set_text(g_jni_env, player, str);
    debugPrintf("[keyboard-trace] native-text enc_calls=%u enc_last=%lu dec_calls=%u dec_last=%lu\n",
                m3_string_encode_calls-before_enc, (unsigned long)m3_string_encode_bytes,
                m3_string_decode_calls-before_dec, (unsigned long)m3_string_decode_bytes);
    jni_release(str);
  }
  if (R_SUCCEEDED(rc)) close_pending = 1;
  else closed(g_jni_env, player);
  observed_enc = m3_string_encode_calls; observed_dec = m3_string_decode_calls;
  observe_frames = 3;
  /* Never log account/password text. */
  debugPrintf("[keyboard] closed rc=%x canceled=%d\n", rc, R_FAILED(rc));
  memset(text, 0, sizeof text);
  memset(initial, 0, sizeof initial);
  return 1;
}
