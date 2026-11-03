/* Diagnose the original Unity resource-slot failure without changing its flow. */
#include <stdint.h>
#include <string.h>
#include <switch.h>
#include "so_util.h"
#include "util.h"
#include "exc_handler.h"

static uintptr_t g_resource_unity_base;
static uint32_t g_resource_watched_slot;
static uint64_t g_resource_loader_tid;

/* Exception context: mapped reads only, no allocation, locks or game calls. */
static uint32_t crash_word(uint32_t address) {
  return !(address & 3) && dcr_readable(address, 4) == 4
      ? *(const volatile uint32_t *)(uintptr_t)address : 0;
}

static void crash_path(uint32_t address, char dst[193]) {
  size_t n = dcr_readable(address, 192), j = 0;
  for (; j < n; ++j) {
    char c = *(const volatile char *)(uintptr_t)(address + j);
    if (!c) break;
    dst[j] = c;
  }
  dst[j] = 0;
}

void port_crash_detail(ThreadExceptionInfo32 *i, ThreadExceptionFrame32 *f,
                       void (*emit)(const char *, ...)) {
  if (!g_resource_unity_base || i->pc != g_resource_unity_base + 0x40ea4c)
    return;
  uint32_t cached = i->r[4], index = f->r8_12[0], manager = f->r8_12[1];
  uint32_t base = crash_word(manager + 4), live = base + index * 24;
  emit("[resource-crash] manager=%08x index=%u cached=%08x base=%08x live=%08x moved=%u\n",
       manager, index, cached, base, live, live != cached);
  emit("[resource-crash] cached_readable=%u live_readable=%u cached_object=%08x live_object=%08x r7=%08x\n",
       (unsigned)dcr_readable(cached, 24), (unsigned)dcr_readable(live, 24),
       crash_word(cached), crash_word(live), i->r[7]);
  char path[193];
  uint32_t requested = crash_word(i->sp + 0x34), resolved = crash_word(i->sp + 0x30);
  crash_path(requested, path);
  emit("[resource-crash] requested@%08x=%s\n", requested, path);
  crash_path(resolved, path);
  emit("[resource-crash] resolved@%08x=%s\n", resolved, path);
}

extern unsigned char m3_trace_resource_slot[];
extern unsigned char m3_trace_resource_resume[];
extern unsigned char m3_resource_created[], m3_resource_created_resume[];
extern unsigned char m3_resource_middle[], m3_resource_middle_resume[];
extern unsigned char m3_resource_last[], m3_resource_last_resume[];
extern unsigned char m3_resource_joined[], m3_resource_joined_resume[];
extern unsigned char m3_resource_checked[], m3_resource_checked_resume[];
extern unsigned char m3_resource_sweep[], m3_resource_sweep_resume[];
extern unsigned char m3_resource_unload[], m3_resource_unload_resume[];

void m3_resource_lifetime(const uint32_t *sp, const uint32_t *slot,
                           uint32_t created, uint32_t phase) {
  if (phase >= 5) {
    if ((uintptr_t)slot != __atomic_load_n(&g_resource_watched_slot, __ATOMIC_ACQUIRE)) return;
    uint64_t tid = 0;
    svcGetThreadId(&tid, CUR_THREAD_HANDLE);
    debugPrintf("[resource-clear] site=%s slot=%p object=%08x loader_tid=%llu clear_tid=%llu sp=%p\n",
                phase == 5 ? "40f4c4-sweep" : "40f648-unload", slot,
                (unsigned)*slot, (unsigned long long)g_resource_loader_tid,
                (unsigned long long)tid, sp);
    /* No game calls or writes; only mapped words from the clearing stack. */
    unsigned words = dcr_readable((uint32_t)(uintptr_t)sp, 128) / 4;
    for (unsigned j = 0; j < words; ++j) {
      uint32_t v = sp[j];
      if (v >= g_resource_unity_base && v < g_resource_unity_base + 0xd00000)
        debugPrintf("[resource-clear-stack] word=%u unity+%08x\n", j,
                    (unsigned)(v - g_resource_unity_base));
    }
    return;
  }
  const char *name = (const char *)(uintptr_t)sp[0x34 / 4];
  if (!name || strcmp(name, "level2")) return;
  if (phase == 0) {
    svcGetThreadId(&g_resource_loader_tid, CUR_THREAD_HANDLE);
    __atomic_store_n(&g_resource_watched_slot, (uint32_t)(uintptr_t)slot, __ATOMIC_RELEASE);
  }
  debugPrintf("[resource-life] phase=%u sp=%p slot=%p object=%08x created=%08x\n",
              (unsigned)phase, sp, slot, (unsigned)*slot, (unsigned)created);
}

void m3_resource_slot_check(const uint32_t *original_sp, const uint32_t *slot,
                            const uint32_t *manager, uint32_t index) {
  /* The original instruction immediately below reads this same slot. */
  if (*slot) return;
  uintptr_t live_base = manager[1];
  uintptr_t live_slot = live_base + (uintptr_t)index * 24;
  /* These strings have just been consumed by the original path routines. */
  const char *requested = (const char *)(uintptr_t)original_sp[0x34 / 4];
  const char *resolved = (const char *)(uintptr_t)original_sp[0x30 / 4];
  debugPrintf("[resource-slot] NULL before Unity+40ea24 manager=%p index=%u "
              "cached_slot=%p live_base=%p live_slot=%p moved=%u "
              "requested=%.192s resolved=%.192s\n",
              manager, (unsigned)index, slot, (void *)live_base, (void *)live_slot,
              (unsigned)(live_slot != (uintptr_t)slot),
              requested ? requested : "(null)", resolved ? resolved : "(null)");
}

int m3_resource_trace_install(so_module *mod) {
  extern int m3_resource_load_lock_install(so_module *);
  if (!m3_resource_load_lock_install(mod)) return 0;
  uintptr_t base = (uintptr_t)mod->load_virtbase;
  const struct { uint32_t offset, word; } guards[] = {
    {0x40ea24, 0xe28db010}, /* add fp, sp, #16 */
    {0x40ea28, 0xe28d2034}, /* add r2, sp, #52 */
    {0x40ea2c, 0xe1a01009}, /* mov r1, r9 */
    {0x40ea30, 0xe5947000}, /* ldr r7, [r4] */
    {0x40ea4c, 0xe5876018}, /* original failing write */
    {0x40e6e0, 0xe0885088}, /* index * 3, then * 8 */
    {0x40e870, 0xe5840000}, {0x40e874, 0xe1a00006},
    {0x40e938, 0xe28d001c}, {0x40e93c, 0xe28d1030},
    {0x40e9dc, 0xe28d001c}, {0x40e9e0, 0xe28d1030},
    {0x40e9ec, 0xe59d1018}, {0x40e9f0, 0xe7962007},
    {0x40ea08, 0xe59f03b4}, {0x40ea0c, 0xe59d101c},
    {0x40edc4, 0x00acf758}, /* literal replayed in checked trampoline */
    {0x40f4c4, 0xe3a00000}, {0x40f4c8, 0xe5840000},
    {0x40f648, 0xe3a00000}, {0x40f64c, 0xe5840000}
  };
  for (unsigned i = 0; i < sizeof guards / sizeof guards[0]; ++i) {
    if (*(const uint32_t *)(base + guards[i].offset) != guards[i].word) {
      debugPrintf("[resource-slot] unsupported Unity instruction at %x\n", guards[i].offset);
      return 0;
    }
  }
  uintptr_t resume = base + 0x40ea2c;
  g_resource_unity_base = base;
  so_patch_code(m3_trace_resource_resume, &resume, sizeof resume);
  hook_arm(base + 0x40ea24, (uintptr_t)m3_trace_resource_slot);
  resume = base + 0x40e878;
  so_patch_code(m3_resource_created_resume, &resume, sizeof resume);
  hook_arm(base + 0x40e870, (uintptr_t)m3_resource_created);
  resume = base + 0x40e940;
  so_patch_code(m3_resource_middle_resume, &resume, sizeof resume);
  hook_arm(base + 0x40e938, (uintptr_t)m3_resource_middle);
  resume = base + 0x40e9e4;
  so_patch_code(m3_resource_last_resume, &resume, sizeof resume);
  hook_arm(base + 0x40e9dc, (uintptr_t)m3_resource_last);
  resume = base + 0x40e9f4;
  so_patch_code(m3_resource_joined_resume, &resume, sizeof resume);
  hook_arm(base + 0x40e9ec, (uintptr_t)m3_resource_joined);
  resume = base + 0x40ea10;
  so_patch_code(m3_resource_checked_resume, &resume, sizeof resume);
  hook_arm(base + 0x40ea08, (uintptr_t)m3_resource_checked);
  resume = base + 0x40f4cc;
  so_patch_code(m3_resource_sweep_resume, &resume, sizeof resume);
  hook_arm(base + 0x40f4c4, (uintptr_t)m3_resource_sweep);
  resume = base + 0x40f650;
  so_patch_code(m3_resource_unload_resume, &resume, sizeof resume);
  hook_arm(base + 0x40f648, (uintptr_t)m3_resource_unload);
  debugPrintf("[resource-clear] sweep/unload watched-slot writers installed\n");
  debugPrintf("[resource-life] join/check return checkpoints installed (phases 3/4)\n");
  debugPrintf("[resource-life] level2 creation/middle/last checkpoints installed\n");
  debugPrintf("[resource-slot] null-slot diagnostic installed; original flow unchanged\n");
  return 1;
}

