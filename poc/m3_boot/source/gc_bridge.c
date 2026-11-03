/* IL2CPP Boehm stop-the-world bridge for the pinned ARM32 library. MIT.
 * This version acknowledges suspend only. GC_start_world does NOT wait for
 * restart acknowledgements (unlike the Mono reference port).
 * No allocation or log-ring access is allowed while any target is paused.
 */
#include <string.h>
#include <switch.h>
#include "bionic_pthread.h"
#include "so_util.h"
#include "util.h"

static int (*gc_registered)(void);
static int (*gc_register)(const void *);
static int (*gc_unregister)(void);
static volatile int *gc_initialized;
static void *(*generic_malloc)(size_t, int);
static b_pthread_key_t foreign_key;
extern int b_pthread_key_create(b_pthread_key_t *, void (*)(void *));
extern int b_pthread_setspecific(b_pthread_key_t, const void *);
extern unsigned char m3_tramp_gc_alloc[];

static void unregister_foreign(void *owned) {
  if (owned && gc_registered()) {
    int rc = gc_unregister();
    debugPrintf("[gc-bridge] foreign thread unregister tid=%d rc=%d\n", b_gettid(), rc);
  }
}

/* Unity's native IO threads can allocate managed backing storage without an
 * IL2CPP Thread::Attach. Register before the allocator takes the GC lock;
 * never suppress GC_push_all_stacks' unknown-thread check. */
static void *registered_malloc(size_t bytes, int kind) {
  if (*gc_initialized && !gc_registered()) {
    BThread *t = b_thread_self();
    struct { void *mem_base; } stack = {
      (char *)t->stack_base + t->stack_size
    };
    if (!t->stack_base || !t->stack_size) return NULL;
    int rc = gc_register(&stack);
    if (rc != 0 && rc != 1) return NULL; /* GC_SUCCESS / GC_DUPLICATE */
    if (rc == 0 && b_pthread_setspecific(foreign_key, (void *)1)) {
      gc_unregister();
      return NULL;
    }
    debugPrintf("[gc-bridge] foreign allocator tid=%d name=%s register=%d\n", t->tid, t->name, rc);
  }
  return generic_malloc(bytes, kind);
}

typedef struct {
  uint64_t r[29], fp, lr, sp, pc;
  uint32_t psr, pad;
  uint8_t v[32][16];
  uint32_t fpcr, fpsr;
  uint64_t tpidr;
} KernelContext;
_Static_assert(sizeof(KernelContext) == 0x320, "kernel context ABI");

static uint8_t *(*lookup)(void *);
static volatile uint32_t *stop_count;
static void *ack_sem;
static int (*post)(void *);
static unsigned paused, suspends, restarts, cycles;
static int ready;

static Result context(KernelContext *ctx, Handle handle) {
  register uint32_t r0 __asm__("r0") = (uintptr_t)ctx;
  register uint32_t r1 __asm__("r1") = handle;
  __asm__ volatile("svc 0x33" : "+r"(r0), "+r"(r1)
                   : : "r2", "r3", "r12", "lr", "memory");
  return r0;
}

/* Do not acknowledge an unsafe snapshot and let GC reclaim live objects.
 * Use the kernel debug channel: another paused thread may hold libc locks. */
static void failed(void) {
  static const char msg[] = "[gc-bridge] STOP: pause/context/stack/ack failure\n";
  svcOutputDebugString(msg, sizeof msg - 1);
  for (;;) svcSleepThread(1000000000);
}

int m3_gc_install(so_module *mod) {
  uintptr_t base = (uintptr_t)mod->load_virtbase;
  /* Field offsets proven by GC_suspend_handler_inner and GC_push_all_stacks.
   * Refuse a different instruction layout before touching GC thread records. */
  const struct { uint32_t offset, word; } guards[] = {
    {0x2cbce14, 0xe594000c}, /* last_stop_count */
    {0x2cbce80, 0xe5842010}, /* stack_ptr */
    {0x2cbce94, 0xe584500c}, /* publish stop count */
    {0x2cbc52c, 0xe5998010}, /* stack scanner consumes stack_ptr */
    {0x2ca8d44, 0xe28dd004}, /* restart returns without sem_wait */
  };
  for (unsigned i = 0; i < sizeof guards / sizeof guards[0]; i++)
    if (*(const uint32_t *)(base + guards[i].offset) != guards[i].word)
      return 0;
  lookup = (void *)so_try_find_addr_rx(mod, "GC_lookup_thread");
  stop_count = (void *)so_try_find_addr_rx(mod, "GC_stop_count");
  ack_sem = (void *)so_try_find_addr_rx(mod, "GC_suspend_ack_sem");
  extern int b_sem_post(void *);
  post = b_sem_post;
  ready = lookup && stop_count && ack_sem;
  gc_registered = (void *)so_try_find_addr_rx(mod, "GC_thread_is_registered");
  gc_register = (void *)so_try_find_addr_rx(mod, "GC_register_my_thread");
  gc_unregister = (void *)so_try_find_addr_rx(mod, "GC_unregister_my_thread");
  gc_initialized = (void *)so_try_find_addr_rx(mod, "GC_is_initialized");
  void (*allow_register)(void) = (void *)so_try_find_addr_rx(mod, "GC_allow_register_threads");
  uintptr_t alloc = (uintptr_t)so_try_find_addr_rx(mod, "GC_generic_malloc");
  if (!ready || !gc_registered || !gc_register || !gc_unregister ||
      !gc_initialized || !allow_register || alloc != base + 0x2ca1dc4 ||
      *(uint32_t *)alloc != 0xe92d47f0 || *(uint32_t *)(alloc+4) != 0xe1a04000 ||
      b_pthread_key_create(&foreign_key, unregister_foreign)) return 0;
  allow_register();
  uintptr_t resume = alloc + 8;
  so_patch_code(m3_tramp_gc_alloc + 12, &resume, sizeof resume);
  generic_malloc = (void *)m3_tramp_gc_alloc;
  hook_arm(alloc, (uintptr_t)registered_malloc);
  debugPrintf("[gc-bridge] %s; suspend ack only\n", ready ? "armed" : "unavailable");
  return ready;
}

int port_gc_signal(BThread *t, int sig) {
  if (!ready || !t || (sig != 30 && sig != 24)) return 0;
  uint8_t *record = lookup(t);
  if (!record) failed();
  if (sig == 30) {
    uint32_t count = *stop_count;
    if (*(volatile uint32_t *)(record + 0xc) == count) return 1;
    if (!suspends)
      debugPrintf("[gc-bridge] first suspend tid=%d name=%s caller=%p\n",
                  t->tid, t->name, __builtin_return_address(0));
    KernelContext ctx;
    b_pause_lock();
    Result rc = svcSetThreadActivity(t->handle, ThreadActivity_Paused);
    if (R_FAILED(rc)) failed();
    t->gc_paused = 1;
    paused++;
    memset(&ctx, 0, sizeof ctx);
    rc = context(&ctx, t->handle);
    b_pause_unlock();
    if (R_FAILED(rc)) failed();
    uintptr_t sp = ctx.r[13], lo = (uintptr_t)t->stack_base;
    uintptr_t save = (sp - sizeof ctx) & ~(uintptr_t)7;
    if (!lo || sp < lo + sizeof ctx || sp > lo + t->stack_size || save < lo)
      failed();
    /* Include integer and SIMD registers as conservative roots. ARM32 has
     * no red zone; the paused thread cannot use this space until resumed. */
    memcpy((void *)save, &ctx, sizeof ctx);
    *(uintptr_t *)(record + 0x10) = save;
    *(volatile uint32_t *)(record + 0xc) = count;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    suspends++;
    if (post(ack_sem)) failed();
  } else if (t->gc_paused) {
    b_pause_lock();
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (R_FAILED(svcSetThreadActivity(t->handle, ThreadActivity_Runnable))) failed();
    t->gc_paused = 0;
    paused--;
    restarts++;
    b_pause_unlock();
    if (!paused) {
      cycles++;
      if (cycles <= 8 || !(cycles % 60))
        debugPrintf("[gc-bridge] cycle=%u stop=%lu suspend=%u resume=%u paused=0\n",
                    cycles, (unsigned long)*stop_count, suspends, restarts);
    }
  }
  return 1;
}
