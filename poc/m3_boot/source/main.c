/* main.c -- the M3 boot probe: the M2 library loader plus the Java side of
 * Unity's start-up, played from C.
 *
 * Sequence (UnityPlayer's own, Unity 5.3): libmain JNI_OnLoad ->
 * NativeLoader.load(lib dir) -> libunity JNI_OnLoad registers the UnityPlayer
 * natives -> initJni(context) -> nativeFile(APK) -> nativeRecreateGfxState ->
 * nativeResume -> nativeRender.
 *
 * The APK is expected at sdmc:/switch/bh3_test/game.apk; dcr_path.c maps
 * /data/app/<pkg>-1/base.apk there. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <switch.h>
#include <sys/stat.h>

#include "dcr_path.h"
#include "dcr_time.h"
#include "gl_layer.h"
#include "imports.h"
#include "input_bridge.h"
#include "jni.h"
#include "rt_settings.h"
#include "rt_applet.h"
#include "watchdog.h"
#include "so_util.h"
#include "util.h"
#include "nx_init.h"
#include "network.h"
#include "boot_profile.h"
#include "load_profile.h"

#ifndef M3_HEAP_PROBE
#define M3_HEAP_PROBE 0
#endif

/* Opt-in address-layout experiment: touch and check every page beyond 1 GiB,
 * then release before loading game libraries. Never keep probe memory live. */
static int heap_layout_probe(void) {
  u64 alias = 0;
  Result rc = svcGetInfo(&alias, InfoType_AliasRegionSize, CUR_PROCESS_HANDLE, 0);
  debugPrintf("[memory-layout] heap_base=%p region=%llu heap=%llu alias=%llu rc=%x\n",
              (void *)g_nxinit.heap_base, (unsigned long long)g_nxinit.heap_region,
              (unsigned long long)g_nxinit.heap, (unsigned long long)alias, rc);
  if (!M3_HEAP_PROBE) return 1;
  const size_t bytes = (size_t)1152 * 1024 * 1024;
  volatile unsigned char *p = malloc(bytes);
  if (!p) { debugPrintf("[heap-probe] 1152 MiB allocation FAILED\n"); return 0; }
  for (size_t i = 0; i < bytes; i += 4096) p[i] = (unsigned char)((i >> 12) ^ 0xa5);
  int ok = 1;
  for (size_t i = 0; i < bytes; i += 4096)
    if (p[i] != (unsigned char)((i >> 12) ^ 0xa5)) { ok = 0; break; }
  debugPrintf("[heap-probe] 1152 MiB page write/read %s start=%p end=%p\n",
              ok ? "PASS" : "FAIL", (void *)p, (void *)(p + bytes - 1));
  free((void *)p);
  return ok;
}

static so_module g_main, g_ulua, g_unity, g_il2cpp, g_ak, g_avpro;
static unsigned completed_frames;
uint64_t dcr_boot_frames(void) {
  return __atomic_load_n(&completed_frames, __ATOMIC_RELAXED);
}

/* Experiment flags: 1 leaves the plugin out of the loader. */
#define M3_NO_AVPRO 0
#define M3_NO_AK 0

/* Diagnostic bypasses only; normal experiments preserve initialization. */
#ifndef M3_SKIP_INITDATA
#define M3_SKIP_INITDATA 0
#endif
#ifndef M3_SKIP_CONFIGACCOUNT
#define M3_SKIP_CONFIGACCOUNT 0
#endif
/* Raise diagnostics can fault before managed handlers run. Opt in only when
 * investigating the hook itself; leave the game's exception path intact. */
#ifndef M3_TRACE_RAISE
#define M3_TRACE_RAISE 0
#endif

void m3_hook_nre(so_module *mod);
void m3_hook_raise(so_module *mod);
void m3_hook_mdregister(so_module *mod);
void m3_hook_inits(so_module *mod);
void m3_hook_guard(so_module *mod);
void m3_patch_initdata(so_module *mod);
void m3_patch_configaccount(so_module *mod);
int m3_gc_install(so_module *mod);
int m3_zip_install(so_module *mod);
int m3_auto_login_install(so_module *mod);
int m3_camera_install(so_module *mod);
int m3_table_profile_install(so_module *mod);
int m3_resource_trace_install(so_module *mod);
int m3_uri_selftest(void);
int m3_string_selftest(void);
int m3_audio_jni_selftest(void);
void m3_www_pump(void);
int m3_keyboard_pump(void);

#define C_PLAYER "com/unity3d/player/UnityPlayer"
#define C_ACTIVITY "com/unity3d/player/UnityPlayerActivity"
#define LIB_DIR "/data/app/" PORT_PACKAGE "-1/lib/arm"

typedef jint (*fn_onload)(void *vm, void *reserved);
typedef jboolean (*fn_nl_load)(void *env, void *clazz, void *path);
typedef void (*fn_ctx)(void *env, void *thiz, void *obj);
typedef void (*fn_recreate)(void *env, void *thiz, jint flag, void *surface);
typedef void (*fn_v)(void *env, void *thiz);
typedef jboolean (*fn_z)(void *env, void *thiz);
typedef void (*fn_vz)(void *env, void *thiz, jboolean b);

static void boot_log_checkpoint(const char *name) {
  uint64_t start=dcr_monotonic_ns();
  log_flush_ring();
  m3_boot_emit("[boot-log] checkpoint=%s flush_us=%llu\n",name,
              (unsigned long long)((dcr_monotonic_ns()-start)/1000));
  m3_boot_mark(name);
}

static int load_module(so_module *mod, const char *name) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), name);
  m3_boot_mark(name);
  int rc = so_load(mod, path, NULL, PORT_SO_REGION_BYTES);
  m3_boot_mark("so-load-done");
  if (rc < 0) {
    debugPrintf("[m3] so_load(%s): rc=%d\n", name, rc);
    return -1;
  }
  debugPrintf("[m3] so_load(%s): %lu KB staged %p -> %p\n", name,
              (unsigned long)(mod->load_size >> 10), mod->load_base, mod->load_virtbase);
  if (so_relocate(mod) != 0) {
    debugPrintf("[m3] so_relocate(%s) failed\n", name);
    return -1;
  }
  m3_boot_mark("so-relocate-done");
  return 0;
}

static int resolve_module(so_module *mod, const char *name) {
  m3_boot_mark(name);
  int missing = so_resolve(mod, dcr_imports, dcr_imports_count, 1);
  debugPrintf("[m3] so_resolve(%s): %d unresolved\n", name, missing);
  m3_boot_mark("so-resolve-done");
  return missing;
}

static void finish_module(so_module *mod, const char *name) {
  m3_boot_mark(name);
  so_finalize(mod);
  so_flush_caches(mod);
  so_execute_init_array(mod);
  m3_boot_mark("constructors-done");
  debugPrintf("[m3] %s constructors done\n", name);
}

/* Relocations and constructors need the input ELF. After all modules are
 * sealed, exports point into their live mappings and dl_iterate_phdr uses
 * the loader's phdr copy. Retaining the input files wastes about 78 MiB. */
static int release_loader_images(void) {
  size_t bytes = 0;
  struct mallinfo before = mallinfo();
  for (so_module *m = so_first(); m; m = m->next)
    if (m->state != SO_SEALED || !m->inited || !m->syms_live) return 0;
  for (so_module *m = so_first(); m; m = m->next) {
    bytes += m->so_base ? m->so_size : 0;
    so_free_temp(m);
    m->elf_hdr = NULL; m->prog_hdr = NULL;
    m->sec_hdr = NULL; m->shstrtab = NULL;
  }
  struct mallinfo after = mallinfo();
  debugPrintf("[memory] released ELF input bytes=%lu used_before=%u used_after=%u free=%u\n",
              (unsigned long)bytes, (unsigned)before.uordblks,
              (unsigned)after.uordblks, (unsigned)after.fordblks);
  return 1;
}

static void *native(const char *cls, const char *name, int required) {
  void *p = jni_native(cls, name);
  if (p)
    debugPrintf("[m3]   %-28s %p\n", name, p);
  else if (required)
    debugPrintf("[m3] MISSING native %s.%s\n", cls, name);
  return p;
}

/* An Android .so loaded with System.loadLibrary gets JNI_OnLoad from the VM.
 * The plugins are preloaded by the port, so call theirs once JNI is ready. */
static void plugin_onload(so_module *mod, const char *name) {
  fn_onload f = (fn_onload)so_try_find_addr_rx(mod, "JNI_OnLoad");
  if (!f) {
    debugPrintf("[m3] %s has no JNI_OnLoad\n", name);
    return;
  }
  jint v = f(g_jni_vm, NULL);
  debugPrintf("[m3] %s JNI_OnLoad -> 0x%lx\n", name, (unsigned long)v);
}

static int boot_java(void) {
  fn_onload main_onload = (fn_onload)so_try_find_addr_rx(&g_main, "JNI_OnLoad");
  if (!main_onload) {
    debugPrintf("[m3] libmain has no JNI_OnLoad\n");
    return -1;
  }
  jint ver = main_onload(g_jni_vm, NULL);
  debugPrintf("[m3] libmain JNI_OnLoad -> 0x%lx\n", (unsigned long)ver);

  fn_nl_load nl_load = (fn_nl_load)jni_native("com/unity3d/player/NativeLoader", "load");
  if (!nl_load) {
    debugPrintf("[m3] NativeLoader.load was not registered by libmain\n");
    return -1;
  }
  JObj *dir = jni_str(LIB_DIR);
  jboolean ok = nl_load(g_jni_env, jni_class("com/unity3d/player/NativeLoader")->obj, dir);
  jni_release(dir);
  debugPrintf("[m3] NativeLoader.load(\"%s\") -> %d\n", LIB_DIR, ok);
  if (!ok)
    return -1;

  fn_ctx initJni = (fn_ctx)native(C_PLAYER, "initJni", 1);
  fn_ctx file = (fn_ctx)native(C_PLAYER, "nativeFile", 0);
  fn_ctx initWWW = (fn_ctx)native(C_PLAYER, "nativeInitWWW", 0);
  fn_ctx initWebRequest = (fn_ctx)native(C_PLAYER, "nativeInitWebRequest", 0);
  fn_recreate recreate = (fn_recreate)native(C_PLAYER, "nativeRecreateGfxState", 0);
  fn_z render = (fn_z)native(C_PLAYER, "nativeRender", 0);
  fn_v resume = (fn_v)native(C_PLAYER, "nativeResume", 0);
  fn_z pause = (fn_z)native(C_PLAYER, "nativePause", 0);
  fn_vz focus = (fn_vz)native(C_PLAYER, "nativeFocusChanged", 0);
  fn_v done = (fn_v)native(C_PLAYER, "nativeDone", 0);
  (void)pause, (void)done;
  if (!initJni)
    return -1;

  JObj *thiz = jni_singleton(C_PLAYER);
  JObj *ctx = jni_singleton(C_ACTIVITY);
  JObj *surface = jni_singleton("android/view/Surface");

  debugPrintf("[m3] initJni\n");
  m3_boot_mark("unity-initJni-begin");
  initJni(g_jni_env, thiz, ctx);
  m3_boot_mark("unity-initJni-end");
  debugPrintf("[m3] initJni returned\n");

  if (file) {
    JObj *apk = jni_str(DCR_ANDROID_APK);
    debugPrintf("[m3] nativeFile(%s)\n", DCR_ANDROID_APK);
    m3_boot_mark("unity-nativeFile-begin");
    file(g_jni_env, thiz, apk);
    m3_boot_mark("unity-nativeFile-end");
    jni_release(apk);
  }
  if (initWWW)
    initWWW(g_jni_env, thiz, jni_class("com/unity3d/player/WWW")->obj);
  if (initWebRequest)
    initWebRequest(g_jni_env, thiz, jni_class("com/unity3d/player/UnityWebRequest")->obj);

  if (recreate) {
    debugPrintf("[m3] nativeRecreateGfxState(0, surface)\n");
    m3_boot_mark("unity-recreate-begin");
    recreate(g_jni_env, thiz, 0, surface);
    m3_boot_mark("unity-recreate-end");
  }
  if (resume)
    resume(g_jni_env, thiz);
  if (focus)
    focus(g_jni_env, thiz, 1);

  /* From here the engine runs its own frame loop; keep the JNI call log quiet
   * (unhandled methods are still logged once) and present frames until the
   * engine says stop, HOME exits, or the probe's frame budget is spent. */
  g_jni_log = 0;
  PadState pad;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);
  m3_input_init();
  if (render) {
    boot_log_checkpoint("java-log-committed");
    rt_applet_start();
    dcr_watchdog_start();
    log_set_quiet(1);
    debugPrintf("[m3] buffered logging and hang snapshots enabled\n");
    m3_boot_mark("render-loop-begin");
    m3_asset_load_phase(1);
    unsigned long frames = 0;
    uint64_t perf_start=dcr_monotonic_ns(), perf_render=0, perf_max=0;
    unsigned perf_count=0, perf_slow=0;
    const char *exit_reason = "frame limit";
    debugPrintf("[m3] loop frame_limit=%lu auto_capture=%d (HOME to exit)\n",
                (unsigned long)M3_FRAME_LIMIT, M3_AUTO_CAPTURE);
    while (M3_FRAME_LIMIT == 0 || frames != M3_FRAME_LIMIT) {
      if (rt_exit_requested() || !appletMainLoop()) break;
      rt_applet_poll();
      if (!rt_focused()) { m3_input_cancel(); svcSleepThread(50000000ll); continue; }
      padUpdate(&pad);
      /* Plus now maps to the in-game pause button; HOME closes the software. */
      m3_input_set_active(1);
      if (M3_AUTO_CAPTURE && frames == 300)
        dcr_gl_request_capture_named("m3-www-stage");
      m3_input_frame(frames);
      uint64_t render_start=dcr_monotonic_ns();
      jboolean alive = render(g_jni_env, thiz);
      uint64_t render_end=dcr_monotonic_ns();
      uint64_t render_ns=render_end-render_start;
      m3_boot_frame((unsigned)frames,render_ns);
      if(frames==1) m3_asset_load_phase(2);
      perf_render+=render_ns; perf_count++;
      if (render_ns>perf_max) perf_max=render_ns;
      if (render_ns>50000000ull) perf_slow++;
      if (render_end-perf_start>=5000000000ull) {
        debugPrintf("[perf] frames=%u window_ms=%llu render_avg_us=%llu render_max_us=%llu over50ms=%u\n",
                    perf_count, (unsigned long long)((render_end-perf_start)/1000000ull),
                    (unsigned long long)(perf_render/perf_count/1000ull),
                    (unsigned long long)(perf_max/1000ull), perf_slow);
        perf_start=render_end; perf_render=perf_max=0; perf_count=perf_slow=0;
        struct mallinfo mi = mallinfo();
        debugPrintf("[memory] used=%u free=%u arena=%u top=%u\n",
                    (unsigned)mi.uordblks, (unsigned)mi.fordblks,
                    (unsigned)mi.arena, (unsigned)mi.keepcost);
      }
      m3_www_pump();
      /* Refresh the main thread's controller snapshot after the keyboard. */
      if (m3_keyboard_pump()) padUpdate(&pad);
      frames++;
      __atomic_store_n(&completed_frames, (unsigned)frames, __ATOMIC_RELAXED);
      if (frames % 60 == 0 || !alive)
        debugPrintf("[m3] frame %lu -> %d, swaps=%lu\n", frames, alive,
                    (unsigned long)dcr_gl_frames());
      if (!alive) {
        exit_reason = "engine stopped";
        break;
      }
    }
    debugPrintf("[m3] loop exit: %s, frames=%lu\n", exit_reason, frames);
#if M3_AVPRO_BRIDGE
    extern void m3_avpro_shutdown(void);
    m3_avpro_shutdown();
#endif
    rt_applet_stop();
    log_set_quiet(0);
  }
  return 0;
}

int main(void) {
#if M3_STANDALONE
  extern int m3_package_prepare(void);
  if(m3_package_prepare()!=0)return 0; /* Setup exits before any Unity/EGL work. */
#endif
  m3_boot_init();
  const char *root = dcr_game_root();
  mkdir(root, 0777);
  log_init(root);
  log_set_quiet(1); /* Batch pre-render diagnostics as well as in-game logs. */
  extern void m3_boot_ui_init(void),m3_boot_ui_console_stage(const char *);
  m3_boot_ui_init();
  extern void m3_startup_sched_init(void);
  m3_startup_sched_init();
  if (M3_SOFTWARE_ETC || dcr_is_emulator()) {
    setenv("NOUVEAU_SOFTWARE_ETC", "1", 0);
    debugPrintf("[m3] emulator ETC software fallback=%s\n", getenv("NOUVEAU_SOFTWARE_ETC"));
  }
  debugPrintf("[m3] %s: Java boot probe\n", PORT_TITLE);
  if (!heap_layout_probe()) goto out;
  /* Reserve the embedded host before Unity allocates its resources/threads. */
  char endpoint_path[512], endpoint_mode[64] = {0};
  snprintf(endpoint_path, sizeof endpoint_path, "%s/server.txt", root);
  FILE *endpoint = fopen(endpoint_path, "r");
  if (endpoint) { fscanf(endpoint, "%63s", endpoint_mode); fclose(endpoint); }
  m3_boot_mark("server-wait-begin");
  if (!strcmp(endpoint_mode, "local") && !m3_net_init()) goto out;
  m3_boot_mark("server-wait-end");
  m3_boot_ui_console_stage("正在读取资源");
  g_so_trace_ctors = 1;

  if (load_module(&g_main, "libmain.so") != 0)
    goto out;
  if (load_module(&g_ulua, "libulua.so") != 0)
    goto out;
  if (load_module(&g_unity, "libunity.so") != 0)
    goto out;
  if (load_module(&g_il2cpp, "libil2cpp.so") != 0)
    goto out;
  /* libunity dlopen()s the scripting runtime by its Java name, "il2cpp"
   * (System.loadLibrary strips lib/.so); the module was loaded as
   * libil2cpp.so, so expose the short name to bionic_dl. */
  g_il2cpp.base_name = "il2cpp";
#if !M3_NO_AK
  if (load_module(&g_ak, "libAkSoundEngine.so") != 0)
    goto out;
#endif
#if !M3_NO_AVPRO
  if (load_module(&g_avpro, "libAVProLocal.so") != 0)
    goto out;
#endif

  int unresolved = 0;
  unresolved += resolve_module(&g_main, "libmain.so");
  unresolved += resolve_module(&g_ulua, "libulua.so");
  unresolved += resolve_module(&g_unity, "libunity.so");
  unresolved += resolve_module(&g_il2cpp, "libil2cpp.so");
#if !M3_NO_AK
  unresolved += resolve_module(&g_ak, "libAkSoundEngine.so");
#endif
#if !M3_NO_AVPRO
  unresolved += resolve_module(&g_avpro, "libAVProLocal.so");
#endif

  finish_module(&g_main, "libmain.so");
  finish_module(&g_ulua, "libulua.so");
  /* MetadataCache::Register writes its registration pointers into a static
   * block; the NRE reporter reads that block directly. Do not hook the init
   * function here: patching it while IL2CPP is starting up hangs the run. */
  m3_boot_mark("il2cpp-finalize-begin");
  so_finalize(&g_il2cpp);
  so_flush_caches(&g_il2cpp);
  so_execute_init_array(&g_il2cpp);
  m3_boot_mark("il2cpp-constructors-end");
  debugPrintf("[m3] libil2cpp.so constructors done\n");
  if (!m3_gc_install(&g_il2cpp)) {
    debugPrintf("[m3] unsupported GC layout; stopping boot\n");
    goto out;
  }
#if !M3_NO_AK
  finish_module(&g_ak, "libAkSoundEngine.so");
#endif
#if !M3_NO_AVPRO
  finish_module(&g_avpro, "libAVProLocal.so");
#endif
  m3_hook_nre(&g_il2cpp);
  m3_camera_install(&g_il2cpp);
  m3_table_profile_install(&g_il2cpp);
  extern int m3_entry_flow_install(so_module *);
  if (!m3_entry_flow_install(&g_il2cpp)) {
    debugPrintf("[entry-flow] unsupported layout; stopping boot\n");
    goto out;
  }
  if (!m3_auto_login_install(&g_il2cpp)) {
    debugPrintf("[m3] unsupported auto-login layout; stopping boot\n");
    goto out;
  }
  if (!m3_zip_install(&g_il2cpp)) {
    debugPrintf("[m3] unsupported ZIP encoding layout; stopping boot\n");
    goto out;
  }
#if M3_TRACE_RAISE
  m3_hook_raise(&g_il2cpp);
#endif
  m3_hook_inits(&g_il2cpp);
  m3_hook_guard(&g_il2cpp);
  debugPrintf("[m3] bypass initdata=%d configaccount=%d\n",
              M3_SKIP_INITDATA, M3_SKIP_CONFIGACCOUNT);
#if M3_SKIP_INITDATA
  m3_patch_initdata(&g_il2cpp);
#endif
#if M3_SKIP_CONFIGACCOUNT
  m3_patch_configaccount(&g_il2cpp);
#endif
  finish_module(&g_unity, "libunity.so");
  m3_resource_trace_install(&g_unity);

  if (!release_loader_images()) {
    debugPrintf("[m3] loader images not ready for release; stopping boot\n");
    goto out;
  }

  debugPrintf("[m3] libraries ready (%d unresolved)\n", unresolved);

  boot_log_checkpoint("libraries-log-committed");
  dcr_set_apk_path("sdmc:" PORT_ROOT_PATH "/game.apk");
  m3_boot_mark("jni-begin");
  jni_init();
  if (!m3_uri_selftest()) goto out;
  if (!m3_string_selftest()) goto out;
  if (!m3_audio_jni_selftest()) goto out;
  g_jni_log = 1;
  debugPrintf("[m3] JNI ready\n");
  /* libulua caches JNIEnv in JNI_OnLoad. Its bridge helpers dereference
   * that cache even though System.load later sees an already mapped module. */
  plugin_onload(&g_ulua, "libulua.so");
#if !M3_NO_AK
  plugin_onload(&g_ak, "libAkSoundEngine.so");
#endif
#if !M3_NO_AVPRO
  plugin_onload(&g_avpro, "libAVProLocal.so");
#endif

#if M3_CG_SELFTEST && M3_AVPRO_BRIDGE
  extern int m3_avpro_selftest(void);
  if (!m3_avpro_selftest()) debugPrintf("[cg-test] FAIL\n");
  goto out;
#endif

  m3_boot_mark("boot-java-begin");
  m3_boot_ui_console_stage("正在初始化游戏");
  log_console_close(); /* Retire console before Unity enters any EGL code. */
  boot_java();

  debugPrintf("[m3] done\n");
out:
  m3_boot_finish("process-exit-before-login");
  log_flush_ring();
  return 0;
}
