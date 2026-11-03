/* jni_tables.c -- the port's Java world. The runtime's jni_core.c is the VM;
 * these tables are the classes and methods it answers. The File and Context
 * pieces follow the reference ports; every unhandled call is logged once. MIT.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "dcr_path.h"
#include "jni.h"
#include "input_bridge.h"
#include "port_config.h"
#include "rt_audout.h"
#include "util.h"

#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)
#define S(i) jni_utf(a[i].l)

#define C_PLAYER "com/unity3d/player/UnityPlayer"
#define C_ACTIVITY "com/unity3d/player/UnityPlayerActivity"
#define DATA_DIR "/data/data/" PORT_PACKAGE
#define EXT_APP "/storage/emulated/0/Android/data/" PORT_PACKAGE

/* Wwise uses this Android query to calculate its resampling step. Returning
 * the generic JNI default (zero) causes division by zero and an audio spin. */
H(audio_native_rate) {
  (void)self; (void)a; (void)m;
  unsigned rate = rt_audout_rate();
  debugPrintf("[audio-jni] native output sample rate=%u Hz\n", rate);
  return (jvalue){.i = (jint)rate};
}

int m3_audio_jni_selftest(void) {
  JClass *cls = jni_class("android/media/AudioTrack");
  JMethod *method = jni_method(cls, "getNativeOutputSampleRate", "(I)I", 1);
  jvalue args[] = {{.i = 3}}; /* STREAM_MUSIC */
  jint rate = jni_call(NULL, method, args).i;
  int ok = rate > 0 && (unsigned)rate == rt_audout_rate();
  debugPrintf("[audio-jni] sample-rate dispatch %s\n", ok ? "PASS" : "FAIL");
  return ok;
}

/* Unity encodes PlayerPrefs keys and values through android.net.Uri. A
 * generic empty-string response collapses all keys into a single entry.
 * Android Uri is percent encoding, not form encoding: '+' stays '+' on decode.
 * Reference: https://developer.android.com/reference/android/net/Uri */
static int uri_hex(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
H(uri_encode) {
  (void)self, (void)m;
  if (!a[0].l) return (jvalue){0};
  const unsigned char *s = (const unsigned char *)S(0);
  char *buf = malloc(strlen((const char *)s) * 3 + 1), *p = buf;
  if (!buf) return (jvalue){0};
  static const char hex[] = "0123456789ABCDEF";
  for (; *s; s++) {
    unsigned char c = *s;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || strchr("_-!.~'()*", c)) *p++ = c;
    else { *p++ = '%'; *p++ = hex[c >> 4]; *p++ = hex[c & 15]; }
  }
  *p = 0;
  JObj *o = jni_str(buf);
  free(buf);
  return (jvalue){.l = o};
}
H(uri_decode) {
  (void)self, (void)m;
  if (!a[0].l) return (jvalue){0};
  const char *s = S(0);
  char *buf = malloc(strlen(s) * 3 + 1), *p = buf;
  if (!buf) return (jvalue){0};
  while (*s) {
    if (*s == '%') {
      if (s[1] && s[2] && uri_hex(s[1]) >= 0 && uri_hex(s[2]) >= 0) {
        unsigned char c = (uri_hex(s[1]) << 4) | uri_hex(s[2]);
        /* JNI modified UTF-8 represents embedded NUL as C0 80. */
        if (!c) { *p++ = (char)0xc0; *p++ = (char)0x80; }
        else *p++ = c;
        s += 3;
      } else {
        *p++ = (char)0xef; *p++ = (char)0xbf; *p++ = (char)0xbd;
        s++;
      }
    } else *p++ = *s++;
  }
  *p = 0;
  JObj *o = jni_str(buf);
  free(buf);
  return (jvalue){.l = o};
}

int m3_uri_selftest(void) {
  const struct { const char *plain, *encoded; } cases[] = {
    {"GeneralLocalData", "GeneralLocalData"}, {"Screenmanager Resolution Width", "Screenmanager%20Resolution%20Width"},
    {"a+b/%", "a%2Bb%2F%25"}, {"\xe4\xb8\xad", "%E4%B8%AD"}, {"", ""},
  };
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    jvalue a = {.l = jni_str(cases[i].plain)};
    JObj *encoded = uri_encode(NULL, &a, NULL).l;
    jvalue b = {.l = encoded};
    JObj *decoded = uri_decode(NULL, &b, NULL).l;
    int ok = encoded && decoded && !strcmp(jni_utf(encoded), cases[i].encoded) &&
             !strcmp(jni_utf(decoded), cases[i].plain);
    jni_release(a.l); jni_release(encoded); jni_release(decoded);
    if (!ok) return 0;
  }
  jvalue nil = {0};
  if (uri_encode(NULL, &nil, NULL).l || uri_decode(NULL, &nil, NULL).l) return 0;
  debugPrintf("[uri] key/UTF-8/percent roundtrip selftest passed\n");
  return 1;
}

static void free_p(JObj *o) {
  free(o->p);
  o->p = NULL;
}

static JObj *file_new(const char *path) {
  JObj *o = jni_new("java/io/File");
  char *p = strdup(path ? path : "");
  size_t n = strlen(p);
  while (n > 1 && p[n - 1] == '/')
    p[--n] = 0;
  o->p = p;
  o->finalize = free_p;
  return o;
}

static const char *file_path(const JObj *o) {
  return (o && jni_is(o, "java/io/File") && o->p) ? (const char *)o->p : "";
}
static const char *path_arg(const void *v) {
  return jni_is(v, "java/io/File") ? file_path(v) : jni_utf(v);
}

static int sd_stat(const char *android_path, struct stat *st) {
  char buf[DCR_PATH_MAX];
  return stat(dcr_translate_path(android_path, buf, sizeof buf), st);
}

static void sd_mkdirs(const char *android_path) {
  char buf[DCR_PATH_MAX];
  char p[DCR_PATH_MAX];
  snprintf(p, sizeof p, "%s", dcr_translate_path(android_path, buf, sizeof buf));
  for (char *c = strchr(p + 1, '/'); c; c = strchr(c + 1, '/')) {
    *c = 0;
    if (strchr(p, '/') && p[strlen(p) - 1] != ':')
      mkdir(p, 0777);
    *c = '/';
  }
  mkdir(p, 0777);
}

/* ---------------------------------------------------------------- Object */
static jvalue object_init(JObj *self, const jvalue *a, const JMethod *m) {
  (void)self, (void)a, (void)m;
  return (jvalue){0};
}

static jvalue object_get_class(JObj *self, const jvalue *a, const JMethod *m) {
  (void)a, (void)m;
  return (jvalue){.l = self->cls->obj};
}

static jvalue class_get_class_loader(JObj *self, const jvalue *a, const JMethod *m) {
  (void)self, (void)a, (void)m;
  return (jvalue){.l = jni_singleton("java/lang/ClassLoader")};
}

/* The engine's scripting-runtime loader asks Java where a library is, then
 * System.load()s it. Return Android's lib path; System.load is a no-op because
 * the loader already mapped every game library. */
static jvalue classloader_find_library(JObj *self, const jvalue *a, const JMethod *m) {
  (void)self, (void)m;
  const char *name = a && a[0].l ? S(0) : "";
  return (jvalue){.l = jni_str_fmt("/data/app/%s-1/lib/arm/lib%s.so", PORT_PACKAGE, name)};
}

static jvalue system_load(JObj *self, const jvalue *a, const JMethod *m) {
  (void)self, (void)m;
  debugPrintf("[m3] System.load(%s): already mapped\n", a && a[0].l ? S(0) : "(null)");
  return (jvalue){0};
}

/* -------------------------------------------------------------- UnityPlayer */
static jvalue player_load_library(JObj *self, const jvalue *a, const JMethod *m) {
  (void)self, (void)m;
  const char *name = a && a[0].l ? ((JObj *)a[0].l)->s.utf : "(null)";
  debugPrintf("[m3] UnityPlayer.loadLibrary(%s) -> true\n", name);
  return (jvalue){.z = 1};
}

/* ------------------------------------------------------------------ Context */
H(ctx_get_package_name) { (void)self, (void)m; return (jvalue){.l = jni_str(PORT_PACKAGE)}; }
H(ctx_get_code_path) { (void)self, (void)a, (void)m; return (jvalue){.l = jni_str(DCR_ANDROID_APK)}; }
H(class_get_name) {
  (void)a, (void)m;
  JClass *c = jni_class_of(self);
  if (!c) return (jvalue){0};
  char name[sizeof c->name];
  snprintf(name, sizeof name, "%s", c->name);
  for (char *p = name; *p; p++) if (*p == '/') *p = '.';
  return (jvalue){.l = jni_str(name)};
}
H(ctx_get_files_dir) { (void)self, (void)a, (void)m; sd_mkdirs(DCR_ANDROID_FILES); return (jvalue){.l = file_new(DCR_ANDROID_FILES)}; }
H(ctx_get_cache_dir) { (void)self, (void)a, (void)m; sd_mkdirs(DCR_ANDROID_CACHE); return (jvalue){.l = file_new(DCR_ANDROID_CACHE)}; }
H(ctx_get_external_files_dir) {
  (void)self, (void)m;
  char p[DCR_PATH_MAX];
  const char *sub = a[0].l ? S(0) : "";
  snprintf(p, sizeof p, "%s/files%s%s", EXT_APP, *sub ? "/" : "", sub);
  sd_mkdirs(p);
  return (jvalue){.l = file_new(p)};
}
H(ctx_get_external_cache_dir) { (void)self, (void)a, (void)m; sd_mkdirs(EXT_APP "/cache"); return (jvalue){.l = file_new(EXT_APP "/cache")}; }
H(ctx_get_external_files_dirs) {
  (void)self, (void)m;
  JObj *arr = jni_array('L', 1);
  ((JObj **)arr->a.data)[0] = ctx_get_external_files_dir(NULL, a, NULL).l;
  return (jvalue){.l = arr};
}
H(ctx_get_dir) {
  (void)self, (void)m;
  char p[DCR_PATH_MAX];
  snprintf(p, sizeof p, "%s/app_%s", DATA_DIR, S(0));
  sd_mkdirs(p);
  return (jvalue){.l = file_new(p)};
}
/* ---------------------------------------------------- SharedPreferences */
/* Application PlayerPrefs backing, persisted on Editor.apply/commit. */
#define PREFS_MAGIC 0x50524653u
#define MAP_MAGIC   0x4d415044u
#define SET_MAGIC   0x53455444u
#define ITER_MAGIC  0x49544552u

typedef struct PrefEntry {
  char *key;
  char *val; /* NULL means remove */
  struct PrefEntry *next;
} PrefEntry;

typedef struct {
  uint32_t magic;
  PrefEntry *head;
  int disk_writable;
} Prefs;

typedef struct {
  uint32_t magic;
  JObj *parent;
  PrefEntry *pending;
  int clear;
} Editor;

typedef struct MapEntry {
  char *key;
  JObj *value;
  struct MapEntry *next;
} MapEntry;

typedef struct {
  uint32_t magic;
  MapEntry *head;
} MapData;

typedef struct {
  uint32_t magic;
  JObj *map;
  int entries;
} SetData;

typedef struct {
  uint32_t magic;
  JObj *set;
  MapEntry *cur;
  int entries;
} IterData;

typedef struct {
  uint32_t magic;
  char *key;
  JObj *value;
} EntryData;

static void pref_entries_free(PrefEntry *e) {
  while (e) {
    PrefEntry *n = e->next;
    free(e->key);
    free(e->val);
    free(e);
    e = n;
  }
}

static void prefs_finalize(JObj *o) {
  Prefs *p = o->p;
  if (p) {
    pref_entries_free(p->head);
    free(p);
    o->p = NULL;
  }
}

static const char *prefs_find(Prefs *p, const char *key) {
  if (!p || !key)
    return NULL;
  for (PrefEntry *e = p->head; e; e = e->next)
    if (!strcmp(e->key, key))
      return e->val;
  return NULL;
}

static void prefs_set(Prefs *p, const char *key, const char *val) {
  if (!p || !key)
    return;
  PrefEntry **pp = &p->head;
  while (*pp && strcmp((*pp)->key, key))
    pp = &(*pp)->next;
  if (!*pp) {
    *pp = calloc(1, sizeof **pp);
    (*pp)->key = strdup(key);
  }
  free((*pp)->val);
  (*pp)->val = val ? strdup(val) : NULL;
}

static void prefs_remove(Prefs *p, const char *key) {
  if (!p || !key)
    return;
  PrefEntry **pp = &p->head;
  while (*pp && strcmp((*pp)->key, key))
    pp = &(*pp)->next;
  if (*pp) {
    PrefEntry *e = *pp;
    *pp = e->next;
    free(e->key);
    free(e->val);
    free(e);
  }
}

/* BEGIN PREFS DISK: length-prefixed UTF-8, including empty values. */
static int prefs_load(Prefs *p, const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f && errno == ENOENT) {
    char backup[600];
    snprintf(backup, sizeof backup, "%s.bak", path);
    f = fopen(backup, "rb");
  }
  if (!f) return errno == ENOENT ? 0 : -1;
  uint32_t header[2];
  PrefEntry *head = NULL;
  size_t total = 0;
  int ok = fread(header, sizeof header, 1, f) == 1 &&
           header[0] == 0x31504642u && header[1] <= 4096;
  for (uint32_t i = 0; ok && i < header[1]; i++) {
    uint32_t lengths[2];
    ok = fread(lengths, sizeof lengths, 1, f) == 1;
    if (!ok) break;
    total += (size_t)lengths[0] + lengths[1];
    if (lengths[0] > 65536 || lengths[1] > 1048576 || total > 16777216) { ok = 0; break; }
    PrefEntry *e = calloc(1, sizeof *e);
    if (!e) { ok = 0; break; }
    e->key = calloc((size_t)lengths[0] + 1, 1);
    e->val = calloc((size_t)lengths[1] + 1, 1);
    e->next = head; head = e;
    ok = e->key && e->val && fread(e->key, 1, lengths[0], f) == lengths[0] &&
         fread(e->val, 1, lengths[1], f) == lengths[1];
    if (ok) ok = strlen(e->key) == lengths[0] && strlen(e->val) == lengths[1];
  }
  if (ok) ok = fgetc(f) == EOF && !ferror(f);
  fclose(f);
  if (!ok) { pref_entries_free(head); return -1; }
  pref_entries_free(p->head); p->head = head;
  return 1;
}

static int prefs_save(Prefs *p, const char *path) {
  if (!p->disk_writable) return 0;
  char temporary[600];
  snprintf(temporary, sizeof temporary, "%s.tmp", path);
  uint32_t header[2] = {0x31504642u, 0};
  size_t total = 0;
  for (PrefEntry *e = p->head; e; e = e->next) if (e->val) {
    size_t k = strlen(e->key), v = strlen(e->val);
    total += k + v;
    if (k > 65536 || v > 1048576 || total > 16777216 || ++header[1] > 4096) return 0;
  }
  FILE *f = fopen(temporary, "wb");
  if (!f) return 0;
  int ok = fwrite(header, sizeof header, 1, f) == 1;
  for (PrefEntry *e = p->head; ok && e; e = e->next) if (e->val) {
    uint32_t lengths[2] = {(uint32_t)strlen(e->key), (uint32_t)strlen(e->val)};
    ok = fwrite(lengths, sizeof lengths, 1, f) == 1 &&
         fwrite(e->key, 1, lengths[0], f) == lengths[0] &&
         fwrite(e->val, 1, lengths[1], f) == lengths[1];
  }
  if (fflush(f) != 0) ok = 0;
  if (ok && fsync(fileno(f)) != 0) ok = 0;
  if (fclose(f) != 0) ok = 0;
  /* Horizon RenameFile does not replace an existing destination. Keep the
   * previous complete file; loading .bak recovers interruption at this gap. */
  if (ok) {
    char backup[600];
    struct stat st;
    snprintf(backup, sizeof backup, "%s.bak", path);
    int exists = stat(path, &st) == 0;
    if (exists) {
      if (remove(backup) != 0 && errno != ENOENT) ok = 0;
      if (ok && rename(path, backup) != 0) ok = 0;
    }
    if (ok && rename(temporary, path) != 0) {
      ok = 0;
      if (exists) rename(backup, path);
    }
  }
  if (!ok) remove(temporary);
  return ok;
}
/* END PREFS DISK */

static JObj *g_prefs_obj;

static JObj *prefs_obj(void) {
  if (!g_prefs_obj) {
    g_prefs_obj = jni_new("android/content/SharedPreferences");
    Prefs *p = calloc(1, sizeof *p);
    p->magic = PREFS_MAGIC;
    g_prefs_obj->p = p;
    g_prefs_obj->finalize = prefs_finalize;
    g_prefs_obj->immortal = 1;
    char saved_path[512];
    snprintf(saved_path, sizeof saved_path, "%s/player-prefs.bin", dcr_game_root());
    int loaded = prefs_load(p, saved_path);
    p->disk_writable = loaded >= 0;
    if (loaded == 1) {
      debugPrintf("[prefs] loaded persistent PlayerPrefs\n");
      return g_prefs_obj;
    }
    if (loaded < 0) debugPrintf("[prefs] persistent file unreadable; preserved, writes disabled\n");
    /* Lowest preset before GeneralLocalData/scene initialization. The actual
     * PlayerPrefs key is GENERAL_DATA; GeneralLocalData is its managed name. */
    static const char low[] = "{\"_personalGraphicsSetting\":{\"RecommendGrade\":\"Off\",\"IsUserDefinedGrade\":true,\"IsUserDefinedVolatile\":true,\"IsEcoMode\":true,\"RecommendResolutionX\":1280,\"RecommendResolutionY\":720,\"ResolutionQuality\":\"Low\",\"TargetFrameRate\":30,\"ContrastDelta\":0.0,\"VolatileSetting\":{\"$type\":\"ConfigGraphicsVolatileSetting\",\"PostFXGrade\":\"Low\",\"UsePostFX\":false,\"UseHDR\":false,\"UseDistortion\":false,\"UseReflection\":false,\"UseFXAA\":false,\"UseDynamicBone\":false,\"UseStaticCloud\":true}}}";
    char path[512], profile[16384];
    snprintf(path, sizeof path, "%s/general-data.json", dcr_game_root());
    FILE *f = fopen(path, "r");
    size_t n = f ? fread(profile, 1, sizeof profile - 1, f) : 0;
    if (f) fclose(f);
    profile[n] = 0;
    jvalue arg = {.l = jni_str(n ? profile : low)};
    JObj *encoded = uri_encode(NULL, &arg, NULL).l;
    prefs_set(p, "GENERAL_DATA", jni_utf(encoded));
    jni_release(encoded); jni_release(arg.l);
    debugPrintf("[prefs] GENERAL_DATA seeded (%s); lowest graphics profile before boot\n", n ? "SD test profile" : "built-in Off/Low/30fps");
  }
  return g_prefs_obj;
}

static void editor_finalize(JObj *o) {
  Editor *e = o->p;
  if (e) {
    pref_entries_free(e->pending);
    jni_release(e->parent);
    free(e);
    o->p = NULL;
  }
}

static JObj *editor_new(JObj *parent) {
  JObj *o = jni_new("android/content/SharedPreferences$Editor");
  Editor *e = calloc(1, sizeof *e);
  e->magic = PREFS_MAGIC;
  e->parent = jni_retain(parent);
  o->p = e;
  o->finalize = editor_finalize;
  return o;
}

static void editor_set(Editor *e, const char *key, const char *val) {
  if (!e || !key)
    return;
  PrefEntry **pp = &e->pending;
  while (*pp && strcmp((*pp)->key, key))
    pp = &(*pp)->next;
  if (!*pp) {
    *pp = calloc(1, sizeof **pp);
    (*pp)->key = strdup(key);
  }
  free((*pp)->val);
  (*pp)->val = val ? strdup(val) : NULL;
}

static void map_finalize(JObj *o) {
  MapData *m = o->p;
  if (m) {
    MapEntry *e = m->head;
    while (e) {
      MapEntry *n = e->next;
      free(e->key);
      jni_release(e->value);
      free(e);
      e = n;
    }
    free(m);
    o->p = NULL;
  }
}

static JObj *map_new(void) {
  JObj *o = jni_new("java/util/HashMap");
  MapData *m = calloc(1, sizeof *m);
  m->magic = MAP_MAGIC;
  o->p = m;
  o->finalize = map_finalize;
  return o;
}

static void map_put(MapData *m, const char *key, JObj *value) {
  if (!m || !key)
    return;
  MapEntry **pp = &m->head;
  while (*pp && strcmp((*pp)->key, key))
    pp = &(*pp)->next;
  if (!*pp) {
    *pp = calloc(1, sizeof **pp);
    (*pp)->key = strdup(key);
  }
  jni_release((*pp)->value);
  (*pp)->value = jni_retain(value);
}

static JObj *map_get(MapData *m, const char *key) {
  if (!m || !key)
    return NULL;
  for (MapEntry *e = m->head; e; e = e->next)
    if (!strcmp(e->key, key))
      return e->value;
  return NULL;
}

static void entry_finalize(JObj *o) {
  EntryData *e = o->p;
  if (e) {
    free(e->key);
    jni_release(e->value);
    free(e);
    o->p = NULL;
  }
}

static JObj *entry_new(const MapEntry *e) {
  JObj *o = jni_new("java/util/Map$Entry");
  EntryData *d = calloc(1, sizeof *d);
  d->magic = MAP_MAGIC;
  d->key = e ? strdup(e->key) : strdup("");
  d->value = e ? jni_retain(e->value) : NULL;
  o->p = d;
  o->finalize = entry_finalize;
  return o;
}

static void set_finalize(JObj *o) {
  SetData *s = o->p;
  if (s) {
    jni_release(s->map);
    free(s);
    o->p = NULL;
  }
}

static JObj *set_new(JObj *map, int entries) {
  JObj *o = jni_new("java/util/Set");
  SetData *s = calloc(1, sizeof *s);
  s->magic = SET_MAGIC;
  s->map = jni_retain(map);
  s->entries = entries;
  o->p = s;
  o->finalize = set_finalize;
  return o;
}

static void iter_finalize(JObj *o) {
  IterData *it = o->p;
  if (it) {
    jni_release(it->set);
    free(it);
    o->p = NULL;
  }
}

static JObj *iter_new(JObj *set) {
  JObj *o = jni_new("java/util/Iterator");
  IterData *it = calloc(1, sizeof *it);
  it->magic = ITER_MAGIC;
  it->set = jni_retain(set);
  SetData *s = set->p;
  if (s) {
    it->entries = s->entries;
    if (s->map && s->map->p)
      it->cur = ((MapData *)s->map->p)->head;
  }
  o->p = it;
  o->finalize = iter_finalize;
  return o;
}

H(ctx_get_shared_prefs) {
  (void)a, (void)m;
  return (jvalue){.l = jni_retain(prefs_obj())};
}

H(prefs_get_string) {
  (void)m;
  const char *key = S(0);
  Prefs *p = self->p;
  const char *v = prefs_find(p, key);
  static unsigned traced;
  if (traced++ < 24)
    debugPrintf("[prefs] getString key=%s hit=%d default-null=%d\n",
                key ? key : "(null)", v != NULL, a[1].l == NULL);
  if (v)
    return (jvalue){.l = jni_str(v)};
  return (jvalue){.l = a[1].l ? jni_retain(a[1].l) : NULL};
}
H(prefs_get_int) {
  (void)m;
  Prefs *p = self->p;
  const char *v = prefs_find(p, S(0));
  return (jvalue){.i = v ? atoi(v) : a[1].i};
}
H(prefs_get_boolean) {
  (void)m;
  Prefs *p = self->p;
  const char *v = prefs_find(p, S(0));
  return (jvalue){.z = v ? (!strcmp(v, "true") || atoi(v) != 0) : a[1].z};
}
H(prefs_get_long) {
  (void)m;
  Prefs *p = self->p;
  const char *v = prefs_find(p, S(0));
  return (jvalue){.j = v ? strtoll(v, NULL, 10) : a[1].j};
}
H(prefs_get_float) {
  (void)m;
  Prefs *p = self->p;
  const char *v = prefs_find(p, S(0));
  return (jvalue){.f = v ? (jfloat)atof(v) : a[1].f};
}
H(prefs_contains) {
  (void)a, (void)m;
  return (jvalue){.z = prefs_find(self->p, S(0)) != NULL};
}
H(prefs_get_all) {
  (void)a, (void)m;
  JObj *map = map_new();
  Prefs *p = self->p;
  for (PrefEntry *e = p ? p->head : NULL; e; e = e->next) {
    if (e->val) {
      JObj *v = jni_str(e->val);
      map_put(map->p, e->key, v);
      jni_release(v);
    }
  }
  return (jvalue){.l = map};
}
H(prefs_edit) {
  (void)a, (void)m;
  return (jvalue){.l = editor_new(self)};
}
H(prefs_noop) {
  (void)self, (void)a, (void)m;
  return (jvalue){0};
}

H(editor_put_string) {
  (void)m;
  editor_set(self->p, S(0), S(1));
  return (jvalue){.l = jni_retain(self)};
}
H(editor_put_int) {
  (void)m;
  char buf[32];
  snprintf(buf, sizeof buf, "%d", a[1].i);
  editor_set(self->p, S(0), buf);
  return (jvalue){.l = jni_retain(self)};
}
H(editor_put_boolean) {
  (void)m;
  editor_set(self->p, S(0), a[1].z ? "true" : "false");
  return (jvalue){.l = jni_retain(self)};
}
H(editor_put_long) {
  (void)m;
  char buf[32];
  snprintf(buf, sizeof buf, "%lld", (long long)a[1].j);
  editor_set(self->p, S(0), buf);
  return (jvalue){.l = jni_retain(self)};
}
H(editor_put_float) {
  (void)m;
  char buf[32];
  snprintf(buf, sizeof buf, "%g", (double)a[1].f);
  editor_set(self->p, S(0), buf);
  return (jvalue){.l = jni_retain(self)};
}
H(editor_remove) {
  (void)m;
  editor_set(self->p, S(0), NULL);
  return (jvalue){.l = jni_retain(self)};
}
H(editor_clear) {
  (void)a, (void)m;
  Editor *e = self->p;
  if (e)
    e->clear = 1;
  return (jvalue){.l = jni_retain(self)};
}
static int editor_apply(Editor *e) {
  if (!e || !e->parent || !e->parent->p)
    return 0;
  Prefs *p = e->parent->p;
  if (e->clear) {
    pref_entries_free(p->head);
    p->head = NULL;
  }
  for (PrefEntry *x = e->pending; x; x = x->next) {
    if (x->val)
      prefs_set(p, x->key, x->val);
    else
      prefs_remove(p, x->key);
  }
  pref_entries_free(e->pending);
  e->pending = NULL;
  e->clear = 0;
  char path[512];
  snprintf(path, sizeof path, "%s/player-prefs.bin", dcr_game_root());
  int ok = prefs_save(p, path);
  debugPrintf("[prefs] persist %s\n", ok ? "OK" : "FAILED");
  return ok;
}
H(editor_apply_h) {
  (void)a, (void)m;
  editor_apply(self->p);
  return (jvalue){0};
}
H(editor_commit) {
  (void)a, (void)m;
  return (jvalue){.z = editor_apply(self->p)};
}

H(map_init) {
  (void)a, (void)m;
  if (!self->p) {
    MapData *map = calloc(1, sizeof *map);
    map->magic = MAP_MAGIC;
    self->p = map;
    self->finalize = map_finalize;
  }
  return (jvalue){.l = NULL};
}
H(map_put_h) {
  (void)m;
  const char *key = S(0);
  JObj *val = a[1].l;
  MapData *map = self->p;
  JObj *old = map_get(map, key);
  JObj *ret = old ? jni_retain(old) : NULL;
  map_put(map, key, val);
  return (jvalue){.l = ret};
}
H(map_get_h) {
  (void)m;
  JObj *v = map_get(self->p, S(0));
  return (jvalue){.l = v ? jni_retain(v) : NULL};
}
H(map_contains) {
  (void)m;
  return (jvalue){.z = map_get(self->p, S(0)) != NULL};
}
H(map_size) {
  (void)a, (void)m;
  MapData *map = self->p;
  int n = 0;
  for (MapEntry *e = map ? map->head : NULL; e; e = e->next)
    n++;
  return (jvalue){.i = n};
}
H(map_is_empty) {
  (void)a, (void)m;
  MapData *map = self->p;
  return (jvalue){.z = !map || !map->head};
}
H(map_key_set) {
  (void)a, (void)m;
  return (jvalue){.l = set_new(self, 0)};
}
H(map_entry_set) {
  (void)a, (void)m;
  return (jvalue){.l = set_new(self, 1)};
}
H(set_iterator) {
  (void)a, (void)m;
  return (jvalue){.l = iter_new(self)};
}
H(set_size) {
  (void)a, (void)m;
  SetData *s = self->p;
  MapData *map = (s && s->map && s->map->p) ? s->map->p : NULL;
  int n = 0;
  for (MapEntry *e = map ? map->head : NULL; e; e = e->next)
    n++;
  return (jvalue){.i = n};
}
H(iter_has_next) {
  (void)a, (void)m;
  IterData *it = self->p;
  return (jvalue){.z = it && it->cur != NULL};
}
H(iter_next) {
  (void)a, (void)m;
  IterData *it = self->p;
  if (!it || !it->cur)
    return (jvalue){.l = NULL};
  MapEntry *e = it->cur;
  it->cur = it->cur->next;
  if (it->entries)
    return (jvalue){.l = entry_new(e)};
  return (jvalue){.l = jni_str(e->key)};
}
H(entry_get_key) {
  (void)a, (void)m;
  EntryData *e = self->p;
  return (jvalue){.l = jni_str(e ? e->key : "")};
}
H(entry_get_value) {
  (void)a, (void)m;
  EntryData *e = self->p;
  return (jvalue){.l = (e && e->value) ? jni_retain(e->value) : NULL};
}

H(ctx_get_intent) { (void)self, (void)a, (void)m; return (jvalue){.l = NULL}; }
H(ctx_run_on_ui_thread) { (void)self, (void)a, (void)m; return (jvalue){0}; }
H(ctx_check_permission) { (void)self, (void)a, (void)m; return (jvalue){.i = 0}; }
H(ctx_get_package_manager) {
  (void)self, (void)a, (void)m;
  return (jvalue){.l = jni_singleton("android/content/pm/PackageManager")};
}
H(ctx_get_system_service) {
  (void)self, (void)m;
  static const struct {
    const char *name, *cls;
  } svc[] = {
      {"window", "android/view/WindowManager"},
      {"audio", "android/media/AudioManager"},
      {"connectivity", "android/net/ConnectivityManager"},
      {"input", "android/hardware/input/InputManager"},
      {"display", "android/hardware/display/DisplayManager"},
      {"activity", "android/app/ActivityManager"},
      {"power", "android/os/PowerManager"},
      {"uimode", "android/app/UiModeManager"},
  };
  const char *want = S(0);
  for (unsigned i = 0; i < sizeof svc / sizeof svc[0]; i++)
    if (!strcmp(svc[i].name, want))
      return (jvalue){.l = jni_singleton(svc[i].cls)};
  debugPrintf("[m3] getSystemService(%s) -> null\n", want);
  return (jvalue){.l = NULL};
}

/* ------------------------------------------------------------ InputDevice */
H(input_get_device_ids) {
  (void)self, (void)a, (void)m;
  return (jvalue){.l = jni_array('I', 0)};
}

/* --------------------------------------------------------------------- File */
H(file_init_s) { return (jvalue){.l = file_new(S(0))}; }
H(file_init_fs) {
  char p[DCR_PATH_MAX];
  snprintf(p, sizeof p, "%s/%s", path_arg(a[0].l), S(1));
  return (jvalue){.l = file_new(p)};
}
H(file_get_path) { return (jvalue){.l = jni_str(file_path(self))}; }
H(file_get_name) {
  const char *p = file_path(self), *s = strrchr(p, '/');
  return (jvalue){.l = jni_str(s ? s + 1 : p)};
}
H(file_get_parent) {
  char p[DCR_PATH_MAX];
  snprintf(p, sizeof p, "%s", file_path(self));
  char *s = strrchr(p, '/');
  if (!s || s == p)
    return (jvalue){.l = NULL};
  *s = 0;
  return (jvalue){.l = !strcmp(m->name, "getParentFile") ? file_new(p) : jni_str(p)};
}
H(file_exists) { struct stat st; return (jvalue){.z = sd_stat(file_path(self), &st) == 0}; }
H(file_is_dir) { struct stat st; return (jvalue){.z = sd_stat(file_path(self), &st) == 0 && S_ISDIR(st.st_mode)}; }
H(file_is_file) { struct stat st; return (jvalue){.z = sd_stat(file_path(self), &st) == 0 && S_ISREG(st.st_mode)}; }
H(file_length) { struct stat st; return (jvalue){.j = sd_stat(file_path(self), &st) == 0 ? (jlong)st.st_size : 0}; }
H(file_mkdirs) { sd_mkdirs(file_path(self)); return (jvalue){.z = 1}; }
H(file_true) { (void)self, (void)a, (void)m; return (jvalue){.z = 1}; }
H(file_delete) {
  char buf[DCR_PATH_MAX];
  return (jvalue){.z = remove(dcr_translate_path(file_path(self), buf, sizeof buf)) == 0};
}
H(file_free_space) { (void)self, (void)a, (void)m; return (jvalue){.j = 2ll << 30}; }

/* --------------------------------------------------------- DisplayMetrics */
static jvalue fld_width(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.i = 1280};
}
static jvalue fld_height(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.i = 720};
}
static jvalue fld_dpi(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.i = 320};
}
static jvalue fld_density(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.f = 2.0f};
}

/* Preserve Unity's APK fallback by default: libulua's original decoder
 * needs the APK identity and ZIP offsets. Loose assets bypass that decoder.
 * Retain the old mapping only as an explicit diagnostic comparison. */
const char *port_path_fixup(const char *real, char *out, size_t cap) {
  /* FileStream checks the APK's parent before calling open(). The runtime
   * maps base.apk and libraries under lib/arm, but not their parent directory. */
  if (real && !strcmp(real, "/data/app/" PORT_PACKAGE "-1")) {
    snprintf(out, cap, "%s", dcr_game_root());
    debugPrintf("[apk-dir] package directory -> %s\n", out);
    return out;
  }
#ifndef M3_LOOSE_ASSETS
#define M3_LOOSE_ASSETS 0
#endif
#if M3_LOOSE_ASSETS
  if (!real)
    return real;
  const char *p = strstr(real, "/base.apk/assets");
  if (p) {
    snprintf(out, cap, "%s/assets%s", dcr_game_root(), p + strlen("/base.apk/assets"));
    return out;
  }
  return real;
#else
  (void)out, (void)cap;
  return real;
#endif
}

/* ---------------------------------------------------- ApplicationInfo etc. */
static JObj *app_info(void) { return jni_singleton("android/content/pm/ApplicationInfo"); }

static jvalue fld_apk_path(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.l = jni_str(DCR_ANDROID_APK)};
}
static jvalue fld_lib_dir(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.l = jni_str("/data/app/" PORT_PACKAGE "-1/lib/arm")};
}
static jvalue fld_data_dir(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.l = jni_str("/data/data/" PORT_PACKAGE)};
}
static jvalue fld_package(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.l = jni_str(PORT_PACKAGE)};
}
static jvalue fld_meta(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.l = jni_singleton("android/os/Bundle")};
}
static jvalue fld_i0(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.i = 0};
}
static jvalue fld_i28(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.i = 28};
}
static jvalue fld_i10000(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.i = 10000};
}
static jvalue fld_version_name(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.l = jni_str("1.9.0")};
}
static jvalue fld_version_code(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.i = 1};
}
static jvalue fld_app_info(JObj *self, const JField *f) {
  (void)self, (void)f;
  return (jvalue){.l = app_info()};
}

H(ctx_get_application_info) { (void)self, (void)a, (void)m; return (jvalue){.l = app_info()}; }
H(pm_get_application_info) { (void)self, (void)a, (void)m; return (jvalue){.l = app_info()}; }
H(pm_get_package_info) {
  (void)self, (void)a, (void)m;
  return (jvalue){.l = jni_singleton("android/content/pm/PackageInfo")};
}
H(pm_get_activity_info) {
  (void)self, (void)a, (void)m;
  return (jvalue){.l = jni_singleton("android/content/pm/ActivityInfo")};
}
H(pm_has_feature) { (void)self, (void)a, (void)m; return (jvalue){.z = 0}; }
H(act_get_preferences) {
  (void)self, (void)a, (void)m;
  return (jvalue){.l = jni_retain(prefs_obj())};
}

/* A non-null proxy object: the native callback path is not implemented yet,
 * but the Java side of Unity's AndroidJavaProxy no longer gets null. */
H(jnibridge_new_proxy) {
  (void)self, (void)m;
  const char *cls = "java/lang/Object";
  JObj *arr = a[1].l;
  if (arr && arr->kind == JK_ARRAY && arr->a.len > 0) {
    JObj *c = ((JObj **)arr->a.data)[0];
    if (c && c->kind == JK_CLASS)
      cls = c->c.of->name;
  }
  debugPrintf("[proxy] newInterfaceProxy(%s) (dummy)\n", cls);
  return (jvalue){.l = jni_new(cls)};
}
H(jnibridge_disable_proxy) { (void)self, (void)a, (void)m; return (jvalue){0}; }

/* Unity's AndroidJavaObject/Class/Proxy resolve members through
 * com.unity3d.player.ReflectionHelper and java.lang.reflect.Method. The
 * Method object's p is the interned JMethod, which is exactly what
 * FromReflectedMethod returns to libunity. */
static JMethod *meth_of(const JObj *self) {
  JMethod *jm = self ? self->p : NULL;
  return jm && jm->magic == JMETH_MAGIC ? jm : NULL;
}

static JObj *type_class(const char *s, const char **end) {
  const char *e = s;
  while (*e == '[')
    e++;
  e = *e == 'L' ? strchr(e, ';') + 1 : e + 1;
  if (end)
    *end = e;
  char name[112];
  size_t len = (size_t)(e - s) < sizeof name ? (size_t)(e - s) : sizeof name - 1;
  if (s[0] == 'L') {
    snprintf(name, sizeof name, "%.*s", (int)(len - 2), s + 1);
  } else if (s[0] == '[') {
    snprintf(name, sizeof name, "%.*s", (int)len, s);
  } else {
    static const char *const prim[] = {"Zboolean", "Bbyte", "Cchar", "Sshort", "Iint",
                                       "Jlong", "Ffloat", "Ddouble", "Vvoid", NULL};
    snprintf(name, sizeof name, "void");
    for (int i = 0; prim[i]; i++)
      if (prim[i][0] == s[0])
        snprintf(name, sizeof name, "%s", prim[i] + 1);
  }
  return jni_class(name)->obj;
}

H(rh_get_method_id) {
  (void)m;
  JClass *c = jni_class_of(a[0].l);
  if (!c) {
    jni_throw("java/lang/NullPointerException", "ReflectionHelper.getMethodID: no class");
    return (jvalue){.l = NULL};
  }
  JObj *o = jni_new("java/lang/reflect/Method");
  o->p = jni_method_best(c, jni_utf(a[1].l), jni_utf(a[2].l), a[3].z);
  return (jvalue){.l = o};
}
H(rh_get_field_id) {
  (void)m;
  JClass *c = jni_class_of(a[0].l);
  if (!c)
    return (jvalue){.l = NULL};
  JObj *o = jni_new("java/lang/reflect/Field");
  o->p = jni_field(c, jni_utf(a[1].l), jni_utf(a[2].l), a[3].z);
  return (jvalue){.l = o};
}
H(rh_get_constructor_id) {
  (void)m;
  JClass *c = jni_class_of(a[0].l);
  if (!c)
    return (jvalue){.l = NULL};
  JObj *o = jni_new("java/lang/reflect/Constructor");
  o->p = jni_method_best(c, "<init>", jni_utf(a[1].l), 0);
  return (jvalue){.l = o};
}
H(rh_new_proxy_instance) {
  (void)m;
  debugPrintf("[proxy] ReflectionHelper.newProxyInstance (dummy)\n");
  return (jvalue){.l = jni_new("java/lang/Object")};
}
H(method_get_name) {
  (void)a, (void)m;
  JMethod *jm = meth_of(self);
  return (jvalue){.l = jni_str(jm ? jm->name : "")};
}
H(method_get_declaring_class) {
  (void)a, (void)m;
  JMethod *jm = meth_of(self);
  return (jvalue){.l = (jm && jm->cls) ? jni_retain(jm->cls->obj) : NULL};
}
H(method_get_return_type) {
  (void)a, (void)m;
  JMethod *jm = meth_of(self);
  const char *r = jm ? strchr(jm->sig, ')') : NULL;
  return (jvalue){.l = r ? jni_retain(type_class(r + 1, NULL)) : NULL};
}
H(method_get_parameter_types) {
  (void)a, (void)m;
  JMethod *jm = meth_of(self);
  const char *s = jm && jm->sig[0] == '(' ? jm->sig + 1 : "";
  JObj *tmp[24];
  int n = 0;
  while (*s && *s != ')' && n < 24)
    tmp[n++] = type_class(s, &s);
  JObj *arr = jni_array('L', n);
  for (int i = 0; i < n; i++)
    ((JObj **)arr->a.data)[i] = jni_retain(tmp[i]);
  return (jvalue){.l = arr};
}
H(method_get_modifiers) {
  (void)a, (void)m;
  JMethod *jm = meth_of(self);
  return (jvalue){.i = jm && jm->is_static ? 0x9 : 0x1};
}
H(method_equals) {
  (void)m;
  const JObj *o = a[0].l;
  return (jvalue){.z = o && o->kind == JK_OBJECT && jni_is(o, "java/lang/reflect/Method") &&
                        o->p == self->p};
}
H(method_hash_code) {
  (void)a, (void)m;
  return (jvalue){.i = (jint)(uintptr_t)self->p};
}
H(method_to_string) {
  (void)a, (void)m;
  JMethod *jm = meth_of(self);
  return (jvalue){.l = jm ? jni_str_fmt("%s.%s%s", jm->cls->name, jm->name, jm->sig)
                          : jni_str("")};
}

static jvalue class_for_name(JObj *self, const jvalue *a, const JMethod *m) {
  (void)self, (void)m;
  const char *name = a && a[0].l ? jni_utf(a[0].l) : "";
  char normalized[112];
  snprintf(normalized, sizeof normalized, "%s", name);
  for (char *p = normalized; *p; p++)
    if (*p == '.') *p = '/';
  if (!*normalized || strlen(name) >= sizeof normalized || !jni_class_exists(normalized)) {
    debugPrintf("[jni-class] forName(%s) unavailable\n", name);
    jni_throw("java/lang/ClassNotFoundException", name);
    return (jvalue){0};
  }
  JObj *result = jni_class(normalized)->obj;
  debugPrintf("[jni-class] forName(%s) -> %p\n", name, result);
  return (jvalue){.l = jni_retain(result)};
}

unsigned m3_string_encode_calls, m3_string_decode_calls;
size_t m3_string_encode_bytes, m3_string_decode_bytes;
static int string_utf8_charset(JObj *charset) {
  const char *name = jni_utf(charset);
  if (!strcmp(name, "UTF-8") || !strcmp(name, "utf-8") || !strcmp(name, "UTF8")) return 1;
  jni_throw("java/io/UnsupportedEncodingException", "Only UTF-8 is implemented");
  return 0;
}
static jvalue string_from_bytes(JObj *self, const jvalue *a, const JMethod *m) {
  (void)m;
  if (!string_utf8_charset(a[1].l)) return jv_none();
  JObj *bytes = a[0].l;
  if (!bytes || bytes->kind != JK_ARRAY || bytes->a.elem != 'B') {
    jni_throw("java/lang/IllegalArgumentException", "Expected byte array");
    return jv_none();
  }
  char *text = malloc((size_t)bytes->a.len + 1);
  if (!text) { jni_throw("java/lang/OutOfMemoryError", "String allocation"); return jv_none(); }
  memcpy(text, bytes->a.data, bytes->a.len);
  text[bytes->a.len] = 0;
  if (self->kind == JK_STRING) { free(self->s.utf); free(self->s.u16); }
  self->kind = JK_STRING;
  self->s.utf = text; self->s.u16 = NULL; self->s.u16len = 0;
  m3_string_decode_calls++;
  m3_string_decode_bytes = bytes->a.len;
  return jv_none();
}
static jvalue string_get_bytes(JObj *self, const jvalue *a, const JMethod *m) {
  (void)m;
  if (!string_utf8_charset(a[0].l)) return jv_l(NULL);
  const char *text = jni_utf(self);
  size_t n = strlen(text);
  m3_string_encode_calls++;
  m3_string_encode_bytes = n;
  JObj *bytes = jni_array('B', n);
  if (bytes && n) memcpy(bytes->a.data, text, n);
  return jv_l(bytes);
}
int m3_string_selftest(void) {
  const char *cases[] = {"test123", "\xe6\xb5\x8b\xe8\xaf\x95", ""};
  JClass *cls = jni_class("java/lang/String");
  JMethod *get = jni_method(cls, "getBytes", "(Ljava/lang/String;)[B", 0);
  JMethod *init = jni_method(cls, "<init>", "([BLjava/lang/String;)V", 0);
  JObj *charset = jni_str("UTF-8");
  int ok = 1;
  for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
    JObj *src = jni_str(cases[i]), *dst = jni_new("java/lang/String");
    jvalue a[] = {{.l = charset}};
    JObj *bytes = jni_call(src, get, a).l;
    if (!bytes || bytes->kind != JK_ARRAY || bytes->a.len != (jsize)strlen(cases[i])) ok = 0;
    else {
      jvalue b[] = {{.l = bytes}, {.l = charset}};
      jni_call(dst, init, b);
      if (dst->kind != JK_STRING || strcmp(jni_utf(dst), cases[i])) ok = 0;
    }
    jni_release(bytes); jni_release(src); jni_release(dst);
  }
  jni_release(charset);
  debugPrintf("[string-utf8] JNI roundtrip ASCII/CJK/empty %s\n", ok ? "PASS" : "FAIL");
  return ok;
}
jvalue m3_www_init(JObj *, const jvalue *, const JMethod *);
jvalue m3_www_start(JObj *, const jvalue *, const JMethod *);

const JMethodDef jni_method_defs[] = {
    {"android/media/AudioTrack", "getNativeOutputSampleRate", "(I)I", audio_native_rate},
    {"java/lang/String", "<init>", "([BLjava/lang/String;)V", string_from_bytes},
    {"java/lang/String", "getBytes", "(Ljava/lang/String;)[B", string_get_bytes},
    {"com/unity3d/player/WWW", "<init>", "(ILjava/lang/String;[BLjava/util/Map;)V", m3_www_init},
    {"com/unity3d/player/WWW", "start", "()V", m3_www_start},
    {"java/lang/Class", "getName", "()Ljava/lang/String;", class_get_name},
    {"android/net/Uri", "encode", "(Ljava/lang/String;)Ljava/lang/String;", uri_encode},
    {"android/net/Uri", "decode", "(Ljava/lang/String;)Ljava/lang/String;", uri_decode},
    {"java/lang/Class", "forName", NULL, class_for_name},
    {"java/lang/Object", "<init>", "()V", object_init},
    {"java/lang/Object", "getClass", "()Ljava/lang/Class;", object_get_class},
    {"java/lang/Class", "getClassLoader", "()Ljava/lang/ClassLoader;", class_get_class_loader},
    /* AVPro's nativeInit resolves its callback class through this loader,
     * rather than JNI FindClass. Returning null leaves every native query
     * and render callback unbound even when OpenVideoFromFile succeeds. */
    {"java/lang/ClassLoader", "findClass", "(Ljava/lang/String;)Ljava/lang/Class;",
     class_for_name},
    {"java/lang/ClassLoader", "findLibrary", "(Ljava/lang/String;)Ljava/lang/String;",
     classloader_find_library},
    {"java/lang/System", "load", "(Ljava/lang/String;)V", system_load},
    {"java/lang/System", "loadLibrary", "(Ljava/lang/String;)V", system_load},
    {C_PLAYER, "loadLibrary", "(Ljava/lang/String;)Z", player_load_library},

    {C_ACTIVITY, "getPackageName", "()Ljava/lang/String;", ctx_get_package_name},
    {"android/content/Context", "getPackageName", "()Ljava/lang/String;", ctx_get_package_name},
    {"android/content/Context", "getPackageCodePath", "()Ljava/lang/String;", ctx_get_code_path},
    {"android/content/Context", "getPackageResourcePath", "()Ljava/lang/String;", ctx_get_code_path},
    {"android/content/Context", "getFilesDir", "()Ljava/io/File;", ctx_get_files_dir},
    {"android/content/Context", "getCacheDir", "()Ljava/io/File;", ctx_get_cache_dir},
    {"android/content/Context", "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;", ctx_get_external_files_dir},
    {"android/content/Context", "getExternalFilesDirs", "(Ljava/lang/String;)[Ljava/io/File;", ctx_get_external_files_dirs},
    {"android/content/Context", "getExternalCacheDir", "()Ljava/io/File;", ctx_get_external_cache_dir},
    {"android/content/Context", "getDir", "(Ljava/lang/String;)Ljava/io/File;", ctx_get_dir},
    {"android/content/Context", "getSharedPreferences",
     "(Ljava/lang/String;I)Landroid/content/SharedPreferences;", ctx_get_shared_prefs},
    {"android/content/Context", "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
     ctx_get_system_service},
    {"android/content/Context", "checkCallingOrSelfPermission", "(Ljava/lang/String;)I",
     ctx_check_permission},
    {"android/content/Context", "getPackageManager", "()Landroid/content/pm/PackageManager;",
     ctx_get_package_manager},
    {"android/content/Context", "getApplicationInfo", "()Landroid/content/pm/ApplicationInfo;",
     ctx_get_application_info},
    {"android/content/pm/PackageManager", "getApplicationInfo",
     "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;", pm_get_application_info},
    {"android/content/pm/PackageManager", "getPackageInfo",
     "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;", pm_get_package_info},
    {"android/content/pm/PackageManager", "getActivityInfo",
     "(Landroid/content/ComponentName;I)Landroid/content/pm/ActivityInfo;", pm_get_activity_info},
    {"android/content/pm/PackageManager", "hasSystemFeature", "(Ljava/lang/String;)Z", pm_has_feature},
    {"android/app/Activity", "getPreferences", "(I)Landroid/content/SharedPreferences;",
     act_get_preferences},
    {"bitter/jnibridge/JNIBridge", "newInterfaceProxy",
     "(J[Ljava/lang/Class;)Ljava/lang/Object;", jnibridge_new_proxy},
    {"bitter/jnibridge/JNIBridge", "disableInterfaceProxy", "(Ljava/lang/Object;)V",
     jnibridge_disable_proxy},
    {"com/unity3d/player/ReflectionHelper", "getMethodID",
     "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;Z)Ljava/lang/reflect/Method;",
     rh_get_method_id},
    {"com/unity3d/player/ReflectionHelper", "getFieldID",
     "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;Z)Ljava/lang/reflect/Field;",
     rh_get_field_id},
    {"com/unity3d/player/ReflectionHelper", "getConstructorID",
     "(Ljava/lang/Class;Ljava/lang/String;)Ljava/lang/reflect/Constructor;",
     rh_get_constructor_id},
    {"com/unity3d/player/ReflectionHelper", "newProxyInstance",
     "(ILjava/lang/Class;)Ljava/lang/Object;", rh_new_proxy_instance},
    {"com/unity3d/player/ReflectionHelper", "newProxyInstance",
     "(I[Ljava/lang/Class;)Ljava/lang/Object;", rh_new_proxy_instance},
    {"java/lang/reflect/Method", "getName", "()Ljava/lang/String;", method_get_name},
    {"java/lang/reflect/Method", "getDeclaringClass", "()Ljava/lang/Class;",
     method_get_declaring_class},
    {"java/lang/reflect/Method", "getReturnType", "()Ljava/lang/Class;",
     method_get_return_type},
    {"java/lang/reflect/Method", "getParameterTypes", "()[Ljava/lang/Class;",
     method_get_parameter_types},
    {"java/lang/reflect/Method", "getModifiers", "()I", method_get_modifiers},
    {"java/lang/reflect/Method", "equals", "(Ljava/lang/Object;)Z", method_equals},
    {"java/lang/reflect/Method", "hashCode", "()I", method_hash_code},
    {"java/lang/reflect/Method", "toString", "()Ljava/lang/String;", method_to_string},
    {"android/view/InputDevice", "getDeviceIds", "()[I", input_get_device_ids},
    {"android/app/Activity", "getIntent", "()Landroid/content/Intent;", ctx_get_intent},
    {"android/app/Activity", "runOnUiThread", "(Ljava/lang/Runnable;)V", ctx_run_on_ui_thread},
    {"android/app/ApplicationErrorReport", "getErrorReportReceiver",
     "(Landroid/content/Context;Ljava/lang/String;I)Landroid/content/ComponentName;", ctx_get_intent},

    {"java/io/File", "<init>", "(Ljava/lang/String;)V", file_init_s},
    {"java/io/File", "<init>", "(Ljava/io/File;Ljava/lang/String;)V", file_init_fs},
    {"java/io/File", "<init>", "(Ljava/lang/String;Ljava/lang/String;)V", file_init_fs},
    {"java/io/File", "getPath", "()Ljava/lang/String;", file_get_path},
    {"java/io/File", "getAbsolutePath", "()Ljava/lang/String;", file_get_path},
    {"java/io/File", "getCanonicalPath", "()Ljava/lang/String;", file_get_path},
    {"java/io/File", "toString", "()Ljava/lang/String;", file_get_path},
    {"java/io/File", "getName", "()Ljava/lang/String;", file_get_name},
    {"java/io/File", "getParent", "()Ljava/lang/String;", file_get_parent},
    {"java/io/File", "getParentFile", "()Ljava/io/File;", file_get_parent},
    {"java/io/File", "exists", "()Z", file_exists},
    {"java/io/File", "isDirectory", "()Z", file_is_dir},
    {"java/io/File", "isFile", "()Z", file_is_file},
    {"java/io/File", "length", "()J", file_length},
    {"java/io/File", "mkdir", "()Z", file_mkdirs},
    {"java/io/File", "mkdirs", "()Z", file_mkdirs},
    {"java/io/File", "canRead", "()Z", file_true},
    {"java/io/File", "canWrite", "()Z", file_true},
    {"java/io/File", "delete", "()Z", file_delete},
    {"java/io/File", "getFreeSpace", "()J", file_free_space},
    {"java/io/File", "getUsableSpace", "()J", file_free_space},

    {"android/content/SharedPreferences", "getString",
     "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", prefs_get_string},
    {"android/content/SharedPreferences", "getInt", "(Ljava/lang/String;I)I", prefs_get_int},
    {"android/content/SharedPreferences", "getBoolean", "(Ljava/lang/String;Z)Z",
     prefs_get_boolean},
    {"android/content/SharedPreferences", "getLong", "(Ljava/lang/String;J)J", prefs_get_long},
    {"android/content/SharedPreferences", "getFloat", "(Ljava/lang/String;F)F", prefs_get_float},
    {"android/content/SharedPreferences", "contains", "(Ljava/lang/String;)Z", prefs_contains},
    {"android/content/SharedPreferences", "getAll", "()Ljava/util/Map;", prefs_get_all},
    {"android/content/SharedPreferences", "edit",
     "()Landroid/content/SharedPreferences$Editor;", prefs_edit},
    {"android/content/SharedPreferences", "registerOnSharedPreferenceChangeListener",
     "(Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;)V", prefs_noop},
    {"android/content/SharedPreferences", "unregisterOnSharedPreferenceChangeListener",
     "(Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;)V", prefs_noop},

    {"android/content/SharedPreferences$Editor", "putString",
     "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;",
     editor_put_string},
    {"android/content/SharedPreferences$Editor", "putInt",
     "(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;", editor_put_int},
    {"android/content/SharedPreferences$Editor", "putBoolean",
     "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;", editor_put_boolean},
    {"android/content/SharedPreferences$Editor", "putLong",
     "(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;", editor_put_long},
    {"android/content/SharedPreferences$Editor", "putFloat",
     "(Ljava/lang/String;F)Landroid/content/SharedPreferences$Editor;", editor_put_float},
    {"android/content/SharedPreferences$Editor", "remove",
     "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", editor_remove},
    {"android/content/SharedPreferences$Editor", "clear",
     "()Landroid/content/SharedPreferences$Editor;", editor_clear},
    {"android/content/SharedPreferences$Editor", "apply", "()V", editor_apply_h},
    {"android/content/SharedPreferences$Editor", "commit", "()Z", editor_commit},

    {"java/util/HashMap", "<init>", "()V", map_init},
    {"java/util/HashMap", "<init>", "(I)V", map_init},
    {"java/util/HashMap", "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
     map_put_h},
    {"java/util/HashMap", "get", "(Ljava/lang/Object;)Ljava/lang/Object;", map_get_h},
    {"java/util/HashMap", "containsKey", "(Ljava/lang/Object;)Z", map_contains},
    {"java/util/HashMap", "size", "()I", map_size},
    {"java/util/HashMap", "isEmpty", "()Z", map_is_empty},
    {"java/util/HashMap", "keySet", "()Ljava/util/Set;", map_key_set},
    {"java/util/HashMap", "entrySet", "()Ljava/util/Set;", map_entry_set},
    {"java/util/Set", "iterator", "()Ljava/util/Iterator;", set_iterator},
    {"java/util/Set", "size", "()I", set_size},
    {"java/util/Iterator", "hasNext", "()Z", iter_has_next},
    {"java/util/Iterator", "next", "()Ljava/lang/Object;", iter_next},
    {"java/util/Map$Entry", "getKey", "()Ljava/lang/Object;", entry_get_key},
    {"java/util/Map$Entry", "getValue", "()Ljava/lang/Object;", entry_get_value},
    {0},
};

static jvalue fld_current_activity(JObj *self, const JField *field) {
  (void)self, (void)field;
  return (jvalue){.l = jni_retain(jni_singleton(C_ACTIVITY))};
}

const JFieldDef jni_field_defs[] = {
    {C_PLAYER, "currentActivity", fld_current_activity, 0, NULL},
    {"android/content/Context", "MODE_PRIVATE", NULL, 0, NULL},
    {"android/content/Context", "WINDOW_SERVICE", NULL, 0, "window"},
    {"android/content/Context", "TELEPHONY_SERVICE", NULL, 0, "phone"},
    {"android/content/Context", "BIND_AUTO_CREATE", NULL, 1, NULL},
    {"android/content/pm/PackageManager", "PERMISSION_GRANTED", NULL, 0, NULL},
    {"android/content/pm/PackageManager", "GET_META_DATA", NULL, 128, NULL},
    {"android/Manifest$permission", "READ_PHONE_STATE", NULL, 0,
     "android.permission.READ_PHONE_STATE"},
    {"android/util/DisplayMetrics", "widthPixels", fld_width, 0, NULL},
    {"android/util/DisplayMetrics", "heightPixels", fld_height, 0, NULL},
    {"android/util/DisplayMetrics", "densityDpi", fld_dpi, 0, NULL},
    {"android/util/DisplayMetrics", "density", fld_density, 0, NULL},
    {"android/content/pm/ActivityInfo", "SCREEN_ORIENTATION_PORTRAIT", NULL, 1, NULL},
    {"android/content/pm/ActivityInfo", "SCREEN_ORIENTATION_REVERSE_PORTRAIT", NULL, 9, NULL},
    {"android/content/pm/ActivityInfo", "SCREEN_ORIENTATION_LANDSCAPE", NULL, 0, NULL},
    {"android/content/pm/ActivityInfo", "SCREEN_ORIENTATION_REVERSE_LANDSCAPE", NULL, 8, NULL},
    {"android/content/pm/ActivityInfo", "SCREEN_ORIENTATION_SENSOR_LANDSCAPE", NULL, 6, NULL},
    {"android/content/pm/ActivityInfo", "SCREEN_ORIENTATION_SENSOR_PORTRAIT", NULL, 7, NULL},
    {"android/content/pm/ActivityInfo", "SCREEN_ORIENTATION_FULL_SENSOR", NULL, 10, NULL},
    {"android/content/pm/ActivityInfo", "SCREEN_ORIENTATION_SENSOR", NULL, 4, NULL},
    {"android/content/pm/ActivityInfo", "SCREEN_ORIENTATION_UNSPECIFIED", NULL, -1, NULL},
    {"android/os/Build$VERSION", "SDK_INT", NULL, 28, NULL},
    {"android/os/Build$VERSION", "RELEASE", NULL, 0, "9"},
    {"android/os/Build$VERSION", "INCREMENTAL", NULL, 0, "1"},
    {"android/os/Build", "MODEL", NULL, 0, "Nintendo Switch"},
    {"android/os/Build", "MANUFACTURER", NULL, 0, "Nintendo"},
    {"android/os/Build", "DEVICE", NULL, 0, "nx"},
    {"android/os/Build", "ID", NULL, 0, "PKQ1"},
    {"android/content/pm/ApplicationInfo", "sourceDir", fld_apk_path, 0, NULL},
    {"android/content/pm/ApplicationInfo", "publicSourceDir", fld_apk_path, 0, NULL},
    {"android/content/pm/ApplicationInfo", "nativeLibraryDir", fld_lib_dir, 0, NULL},
    {"android/content/pm/ApplicationInfo", "dataDir", fld_data_dir, 0, NULL},
    {"android/content/pm/ApplicationInfo", "flags", fld_i0, 0, NULL},
    {"android/content/pm/ApplicationInfo", "targetSdkVersion", fld_i28, 0, NULL},
    {"android/content/pm/ApplicationInfo", "minSdkVersion", fld_i28, 0, NULL},
    {"android/content/pm/ApplicationInfo", "uid", fld_i10000, 0, NULL},
    {"android/content/pm/ApplicationInfo", "packageName", fld_package, 0, NULL},
    {"android/content/pm/PackageItemInfo", "packageName", fld_package, 0, NULL},
    {"android/content/pm/PackageItemInfo", "metaData", fld_meta, 0, NULL},
    {"android/content/pm/PackageInfo", "versionName", fld_version_name, 0, NULL},
    {"android/content/pm/PackageInfo", "versionCode", fld_version_code, 0, NULL},
    {"android/content/pm/PackageInfo", "packageName", fld_package, 0, NULL},
    {"android/content/pm/PackageInfo", "applicationInfo", fld_app_info, 0, NULL},
    {"android/content/pm/PackageInfo", "firstInstallTime", fld_i0, 0, NULL},
    {"android/content/pm/PackageInfo", "lastUpdateTime", fld_i0, 0, NULL},
    {0},
};

const char *const jni_class_supers[][2] = {
    {"android/view/MotionEvent", "android/view/InputEvent"},
    {"android/view/KeyEvent", "android/view/InputEvent"},
    {"android/view/InputEvent", "java/lang/Object"},
    {C_ACTIVITY, "android/app/Activity"},
    {"android/app/Activity", "android/content/Context"},
    {"android/app/Application", "android/content/Context"},
    {"android/content/ContextWrapper", "android/content/Context"},
    {"android/view/SurfaceView", "android/view/View"},
    {"android/view/View", "java/lang/Object"},
    {"java/util/HashMap", "java/util/Map"},
    {"java/util/Set", "java/util/Collection"},
    {0},
};

const char *const jni_missing_classes[] = {
    0,
};

/* Called first by jni_core for every call through a method ID. Record the
 * last calls so the NRE reporter can show what InitDataPersistent touched;
 * return 0 to let the normal handler table answer. (The dynamic-proxy
 * dispatcher, when it exists, belongs here too.) */
#define JNI_TRACE_MAX 128
typedef struct {
  const char *cls;
  const char *name;
} JniTraceRec;
static JniTraceRec g_jni_trace[JNI_TRACE_MAX];
static int g_jni_trace_pos;

int m3_keyboard_jni(JObj *, JMethod *, const jvalue *, jvalue *);
#if M3_AVPRO_BRIDGE
int m3_avpro_jni(JObj *, JMethod *, const jvalue *, jvalue *);
#endif
int port_jni_invoke(JObj *self, JMethod *m, const jvalue *args, jvalue *out) {
#if M3_AVPRO_BRIDGE
  if (m3_avpro_jni(self, m, args, out)) return 1;
#endif
  if (m3_keyboard_jni(self, m, args, out)) return 1;
  if (m3_input_jni(self, m, args, out)) return 1;
  (void)self, (void)args, (void)out;
  if (m) {
    /* Unity polls the input device every frame; it would otherwise fill the
     * whole ring and hide the calls that matter. */
    int noise = m->cls && !strcmp(m->cls->name, "android/view/InputDevice") &&
                !strcmp(m->name, "getDeviceIds");
    if (!noise) {
      g_jni_trace[g_jni_trace_pos].cls = m->cls ? m->cls->name : "?";
      g_jni_trace[g_jni_trace_pos].name = m->name;
      g_jni_trace_pos = (g_jni_trace_pos + 1) % JNI_TRACE_MAX;
    }
  }
  return 0;
}

void jni_trace_dump(void) {
  debugPrintf("[jni] last %d calls (oldest first):\n", JNI_TRACE_MAX);
  for (int i = 0; i < JNI_TRACE_MAX; i++) {
    int k = (g_jni_trace_pos + i) % JNI_TRACE_MAX;
    if (g_jni_trace[k].name)
      debugPrintf("[jni]   %s.%s\n", g_jni_trace[k].cls, g_jni_trace[k].name);
  }
}
