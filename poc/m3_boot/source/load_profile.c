/* Bounded per-thread exclusive API timing. No lock spans a real I/O call. */
#include <switch.h>
#include <string.h>
#include <errno.h>
#include "util.h"
#include "load_profile.h"
#define THREADS 32
#define DEPTH 16
typedef struct { uint64_t ticks, bytes; unsigned calls; } Cost;
typedef struct {
  Handle handle;
  unsigned depth, role;
  unsigned stack[DEPTH];
  uint64_t last;
} LoadThread;
static Mutex load_lock;
static LoadThread threads[THREADS];
static Cost costs[3][2][LOAD_KINDS];
static uint64_t walls[3], phase_started;
static unsigned active, generation, phase, overflow;
static Handle owner;

static void charge(LoadThread *t, uint64_t now) {
  if (t->depth)
    costs[phase][t->role][t->stack[t->depth - 1]].ticks += now - t->last;
  t->last = now;
}
uint64_t m3_load_enter(unsigned kind) {
  if (!__atomic_load_n(&active, __ATOMIC_RELAXED)) return 0;
  int saved = errno;
  Handle handle = threadGetCurHandle();
  uint64_t token = 0;
  mutexLock(&load_lock);
  if (active && kind < LOAD_KINDS) {
    unsigned i;
    for (i = 0; i < THREADS && threads[i].handle != handle; i++);
    if (i == THREADS) {
      for (i = 0; i < THREADS && threads[i].handle; i++);
      if (i < THREADS) {
        threads[i].handle = handle;
        threads[i].role = handle == owner ? 0 : 1;
      }
    }
    if (i < THREADS && threads[i].depth < DEPTH) {
      LoadThread *t = &threads[i];
      charge(t, armGetSystemTick());
      t->stack[t->depth++] = kind;
      costs[phase][t->role][kind].calls++;
      token = ((uint64_t)generation << 32) | (i + 1);
    } else overflow++;
  }
  mutexUnlock(&load_lock);
  errno = saved;
  return token;
}
void m3_load_leave(uint64_t token, uint64_t bytes) {
  if (!token) return;
  int saved = errno;
  mutexLock(&load_lock);
  unsigned i = (uint32_t)token - 1;
  if (active && (unsigned)(token >> 32) == generation && i < THREADS) {
    LoadThread *t = &threads[i];
    if (t->depth) {
      charge(t, armGetSystemTick());
      costs[phase][t->role][t->stack[--t->depth]].bytes += bytes;
    }
  }
  mutexUnlock(&load_lock);
  errno = saved;
}
void m3_asset_load_begin(void) {
  int saved = errno;
  mutexLock(&load_lock);
  memset(threads, 0, sizeof threads);
  memset(costs, 0, sizeof costs);
  memset(walls, 0, sizeof walls);
  generation++;
  phase = overflow = 0;
  owner = threadGetCurHandle();
  phase_started = armGetSystemTick();
  __atomic_store_n(&active, 1, __ATOMIC_RELAXED);
  mutexUnlock(&load_lock);
  errno = saved;
}
void m3_asset_load_phase(unsigned next) {
  int saved = errno;
  mutexLock(&load_lock);
  if (active && next < 3 && next > phase && threadGetCurHandle() == owner) {
    uint64_t now = armGetSystemTick();
    for (unsigned i = 0; i < THREADS; i++) charge(&threads[i], now);
    walls[phase] += now - phase_started;
    phase_started = now;
    phase = next;
  }
  mutexUnlock(&load_lock);
  errno = saved;
}
void m3_asset_load_end_to(void (*emit)(const char *, ...)) {
  int saved = errno;
  Cost snapshot[3][2][LOAD_KINDS];
  uint64_t wall_snapshot[3];
  unsigned unfinished = 0, dropped;
  mutexLock(&load_lock);
  uint64_t now = armGetSystemTick();
  for (unsigned i = 0; i < THREADS; i++) {
    charge(&threads[i], now);
    unfinished += threads[i].depth;
  }
  walls[phase] += now - phase_started;
  __atomic_store_n(&active, 0, __ATOMIC_RELAXED);
  memcpy(snapshot, costs, sizeof snapshot);
  memcpy(wall_snapshot, walls, sizeof walls);
  dropped = overflow;
  mutexUnlock(&load_lock);
  static const char *names[] = {"open", "fread", "seek", "stat", "close",
    "fdopen", "fdread", "pread", "mmap", "fsread", "inflate"};
  emit("[load-v2] exclusive=1 overflow=%u unfinished=%u bytes_at_return=1\n", dropped, unfinished);
  for (unsigned p = 0; p < 3; p++) {
    emit("[load-v2] phase=%u wall_us=%llu\n", p,
      (unsigned long long)(armTicksToNs(wall_snapshot[p]) / 1000));
    for (unsigned role = 0; role < 2; role++)
      for (unsigned k = 0; k < LOAD_KINDS; k++) {
        Cost *c = &snapshot[p][role][k];
        if (c->calls || c->ticks || c->bytes)
          emit("[load-v2] phase=%u thread=%s api=%s calls=%u exclusive_us=%llu bytes=%llu\n",
            p, role ? "background" : "route", names[k], c->calls,
            (unsigned long long)(armTicksToNs(c->ticks) / 1000), (unsigned long long)c->bytes);
      }
  }
  errno = saved;
}

void m3_asset_load_end(void) { m3_asset_load_end_to(debugPrintf); }
