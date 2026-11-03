/* exc_hook.c -- log a null-dereference's native frame chain.
 *
 * Generated IL2CPP code calls Exception::RaiseNullReferenceException on a
 * null dereference. Patch its entry to a wrapper that walks the current
 * thread's managed frames (when the build kept them), scans the native stack
 * for code addresses, and formats the cached NRE object. The managed frame
 * stack in this game is compiled out, so the native offsets are the useful
 * evidence. The wrapper then restores the original entry and lets it throw.
 * MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "so_util.h"
#include "util.h"

typedef const char *(*fn_method_name)(const void *method);
typedef const void *(*fn_method_class)(const void *method);
typedef const char *(*fn_class_name)(const void *cls);
typedef void (*fn_walk)(void (*cb)(const void *frame, void *user), void *user);
typedef const void *(*fn_method_info)(unsigned int index);
typedef void (*fn_md_register)(const void *code_reg, const void *metadata_reg,
                               const void *options);

typedef struct {
  const void *method; /* Il2CppStackFrameInfo: MethodInfo* first */
  uintptr_t raw_ip;
} FrameInfo;

static uintptr_t g_addr;
static unsigned char g_saved[8];
static int g_armed;
static fn_method_name g_method_name;
static fn_method_class g_method_class;
static fn_class_name g_class_name;
static fn_class_name g_class_ns;
static fn_method_info g_method_info;
static fn_method_info g_method_info_index;
static fn_walk g_walk;
static uintptr_t g_format_exception;
static uintptr_t g_format_trace;
static void *(*g_get_nre)(void);
static int g_frames;
static uintptr_t g_base;

/* MetadataCache::Register captures the two registration tables IL2CPP builds
 * from the generated metadata. CodeRegistration+4 is the method-pointer array
 * (GetMethodPointerFromIndex loads [reg+4]); the NRE caller is one of those
 * pointers plus an offset, so the array names the containing managed method. */
static const void *g_code_reg;
static const void *g_metadata_reg;
static uintptr_t g_mdreg_addr;
static unsigned char g_mdreg_saved[8];
static int g_mdreg_armed;

/* MetadataCache::Register stores (code registration, metadata registration,
 * options) in a three-word static block. Its prologue computes the block at
 * libil2cpp+0x3508230 (0x2c766b8..0x2c766c8: pc + 0x84a3e0 + 0x47788). Reading
 * that block at NRE time avoids patching the hot init function at all. */
#define MDCACHE_GLOBALS_OFF 0x3508230u

static const void *cached_code_reg(void) {
  if (g_code_reg)
    return g_code_reg;
  if (!g_base)
    return NULL;
  return *(const void *const *)(g_base + MDCACHE_GLOBALS_OFF);
}

static uintptr_t read_word(uintptr_t p) { return *(const uintptr_t *)p; }

static void log_method_brief(uint32_t index, const void *mi, uintptr_t caller, const char *tag);

static void frame_cb(const void *frame, void *user) {
  (void)user;
  const FrameInfo *f = frame;
  const char *cls = "?";
  const char *name = "?";
  g_frames++;
  if (f->method) {
    if (g_method_name)
      name = g_method_name(f->method);
    if (g_method_class) {
      const void *c = g_method_class(f->method);
      if (c && g_class_name)
        cls = g_class_name(c);
    }
  }
  debugPrintf("[il2cpp]   %s::%s ip=0x%lx\n", cls ? cls : "?", name ? name : "?",
              (unsigned long)f->raw_ip);
}

static uintptr_t g_stack_addrs[16];
static int g_stack_n;

/* Code addresses on the current stack identify the native callers even when
 * the managed frame stack is disabled. */
static void scan_stack(void) {
  uintptr_t sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  const uintptr_t *p = (const uintptr_t *)sp;
  MemoryInfo memory;
  uint32_t page_info;
  if (R_FAILED(svcQueryMemory(&memory, &page_info, sp)) ||
      !(memory.perm & Perm_R) || sp < memory.addr ||
      sp - memory.addr >= memory.size) return;
  size_t words = (memory.size - (sp - memory.addr)) / sizeof *p;
  if (words > 1024) words = 1024;
  int shown = 0;
  g_stack_n = 0;
  for (size_t i = 0; i < words && shown < 40; i++) {
    uintptr_t v = p[i];
    so_module *m = so_find_module_by_addr((void *)v);
    if (!m)
      continue;
    const Elf32_Phdr *seg = so_segment_of(m, v);
    if (!seg || !(seg->p_flags & PF_X))
      continue;
    if (g_stack_n < (int)(sizeof g_stack_addrs / sizeof g_stack_addrs[0]))
      g_stack_addrs[g_stack_n++] = v;
    debugPrintf("[il2cpp]   stack %s+0x%lx\n", m->base_name ? m->base_name : "?",
                (unsigned long)(v - (uintptr_t)m->load_virtbase));
    shown++;
  }
}

static void report_addr_method(uintptr_t addr) {
  if (!g_code_reg || !g_method_info)
    return;
  uint32_t count = *(const uint32_t *)g_code_reg;
  if (count == 0 || count > 1000000u)
    return;
  uintptr_t best = 0;
  uint32_t best_i = 0;
  uintptr_t best_d = ~(uintptr_t)0;
  for (uint32_t i = 0; i < count; i++) {
    const void *mi = g_method_info(i);
    if (!mi)
      continue;
    uintptr_t mp = *(const uintptr_t *)mi;
    if (mp <= addr && addr - mp < 0x4000 && addr - mp < best_d) {
      best = mp;
      best_i = i;
      best_d = addr - mp;
    }
  }
  if (best) {
    const void *mi = g_method_info(best_i);
    log_method_brief(best_i, mi, addr, "stack-method");
  }
}

static void format_exception(void *ex, const char *tag) {
  if (!ex)
    return;
  debugPrintf("[il2cpp] %s object %p\n", tag, ex);
  if (g_format_exception) {
    char buf[4096] = {0};
    ((void (*)(const void *, char *, int))g_format_exception)(ex, buf, sizeof buf);
    buf[sizeof buf - 1] = '\0';
    if (buf[0])
      debugPrintf("[il2cpp] %s text: %s\n", tag, buf);
  }
  if (g_format_trace) {
    /* Both IL2CPP format exports return void and fill a caller-owned buffer. */
    char buf[4096] = {0};
    ((void (*)(const void *, char *, int))g_format_trace)(ex, buf, sizeof buf);
    buf[sizeof buf - 1] = '\0';
    if (buf[0])
      debugPrintf("[il2cpp] %s trace:\n%.3000s\n", tag, buf);
  }
}

static void log_method_brief(uint32_t index, const void *mi, uintptr_t caller, const char *tag) {
  const uintptr_t *w = mi;
  const char *name = g_method_name ? g_method_name(mi) : NULL;
  const void *cls = g_method_class ? g_method_class(mi) : NULL;
  const char *cname = (cls && g_class_name) ? g_class_name(cls) : NULL;
  const char *ns = (cls && g_class_ns) ? g_class_ns(cls) : NULL;
  debugPrintf("[il2cpp]   %s [%u] MI=%p w0=%08lx w1=%08lx d0=0x%lx\n", tag, index, mi,
              (unsigned long)w[0], (unsigned long)w[1],
              (unsigned long)(caller - w[0]));
  debugPrintf("[il2cpp]   method %s%s%s::%s\n", ns && ns[0] ? ns : "",
              ns && ns[0] ? "." : "", cname ? cname : "?", name ? name : "?");
}

/* The code-registration array index and the MethodInfo index are not the same
 * space in this build. Walk the MethodInfo table and match each method's own
 * code pointer (w[0]) against the NRE caller. */
static void scan_all_methods(uint32_t count, uintptr_t caller) {
  uintptr_t best = 0;
  uint32_t best_i = 0;
  for (uint32_t i = 0; i < count; i++) {
    const void *mi = g_method_info ? g_method_info(i) : NULL;
    if (!mi)
      continue;
    uintptr_t mp = *(const uintptr_t *)mi;
    if (mp <= caller && mp > best) {
      best = mp;
      best_i = i;
    }
    if ((i & 0x3fffu) == 0)
      debugPrintf("[il2cpp] scan %u/%u best=%p\n", i, count, (void *)best);
  }
  if (best) {
    const void *mi = g_method_info(best_i);
    log_method_brief(best_i, mi, caller, "full");
  } else {
    debugPrintf("[il2cpp] no MethodInfo code pointer below caller\n");
  }
}

static void dump_method_info(uint32_t index, uintptr_t ptr, uintptr_t caller, const char *tag) {
  const void *mi = g_method_info ? g_method_info(index) : NULL;
  const void *mi2 = g_method_info_index ? g_method_info_index(index) : NULL;
  if (!mi)
    mi = mi2;
  if (!mi) {
    debugPrintf("[il2cpp]   %s [%u] %p: no MethodInfo\n", tag, index, (void *)ptr);
    return;
  }
  const uintptr_t *w = mi;
  const char *name = g_method_name ? g_method_name(mi) : NULL;
  const void *cls = g_method_class ? g_method_class(mi) : NULL;
  const char *cname = (cls && g_class_name) ? g_class_name(cls) : NULL;
  const char *ns = (cls && g_class_ns) ? g_class_ns(cls) : NULL;
  debugPrintf("[il2cpp]   %s [%u] ptr=%p caller_delta=0x%lx MI=%p MI2=%p name=%p cls=%p\n",
              tag, index, (void *)ptr, (unsigned long)(caller - ptr), mi, mi2,
              (const void *)name, cls);
  for (int i = 0; i < 24; i++) {
    unsigned long v = (unsigned long)w[i];
    const char *hit = "";
    if (w[i] == ptr)
      hit = " <ptr>";
    else if (name && w[i] == (uintptr_t)name)
      hit = " <name>";
    else if (cls && w[i] == (uintptr_t)cls)
      hit = " <class>";
    debugPrintf("[il2cpp]     w[%02d]=%08lx%s\n", i, v, hit);
  }
  debugPrintf("[il2cpp]   method %s%s%s::%s\n", ns && ns[0] ? ns : "",
              ns && ns[0] ? "." : "", cname ? cname : "?", name ? name : "?");
  if (mi2 && mi2 != mi) {
    const char *n2 = g_method_name ? g_method_name(mi2) : NULL;
    const void *c2 = g_method_class ? g_method_class(mi2) : NULL;
    const char *cn2 = (c2 && g_class_name) ? g_class_name(c2) : NULL;
    const char *ns2 = (c2 && g_class_ns) ? g_class_ns(c2) : NULL;
    debugPrintf("[il2cpp]   method2 %s%s%s::%s\n", ns2 && ns2[0] ? ns2 : "",
                ns2 && ns2[0] ? "." : "", cn2 ? cn2 : "?", n2 ? n2 : "?");
  }
}

/* The return address of RaiseNullReferenceException is the code just after
 * the call to the codegen NRE stub--inside the managed method that dereferenced
 * null. Find the method pointer at or below it and let IL2CPP name it. */
static void report_caller_method(uintptr_t caller_raw) {
  uintptr_t caller = caller_raw & ~(uintptr_t)1;
  debugPrintf("[il2cpp] NRE caller=%p (libil2cpp+0x%lx)\n", (void *)caller,
              (unsigned long)(caller - g_base));
  const void *code_reg = cached_code_reg();
  if (!code_reg) {
    debugPrintf("[il2cpp] code registration was not captured\n");
    return;
  }
  g_code_reg = code_reg;
  uint32_t count = *(const uint32_t *)code_reg;
  const uintptr_t *ptrs = *(const uintptr_t *const *)((const char *)code_reg + 4);
  debugPrintf("[il2cpp] codeReg=%p count=%u ptrs=%p words: %08lx %08lx %08lx %08lx\n",
              code_reg, count, (const void *)ptrs,
              (unsigned long)read_word((uintptr_t)code_reg),
              (unsigned long)read_word((uintptr_t)code_reg + 4),
              (unsigned long)read_word((uintptr_t)code_reg + 8),
              (unsigned long)read_word((uintptr_t)code_reg + 12));
  if (!ptrs || count == 0 || count > 1000000u) {
    debugPrintf("[il2cpp] implausible method pointer table\n");
    return;
  }
  uintptr_t best = 0, above = 0;
  uint32_t best_i = 0, above_i = 0;
  for (uint32_t i = 0; i < count; i++) {
    uintptr_t p = ptrs[i];
    if (!p)
      continue;
    if (p <= caller && p > best) {
      best = p;
      best_i = i;
    }
    if (p > caller && (above == 0 || p < above)) {
      above = p;
      above_i = i;
    }
  }
  if (best)
    dump_method_info(best_i, best, caller, "below");
  if (above)
    dump_method_info(above_i, above, caller, "above");
  scan_all_methods(count, caller);
  if (!best && !above)
    debugPrintf("[il2cpp] no candidate method pointer\n");
}

static void mdregister_wrapper(const void *code_reg, const void *metadata_reg,
                               const void *options) {
  if (g_mdreg_armed) {
    so_patch_code((void *)g_mdreg_addr, g_mdreg_saved, sizeof g_mdreg_saved);
    g_mdreg_armed = 0;
  }
  if (code_reg) {
    g_code_reg = code_reg;
    const uint32_t count = *(const uint32_t *)code_reg;
    const void *ptrs = *(const void *const *)((const char *)code_reg + 4);
    debugPrintf("[il2cpp] MetadataCache::Register codeReg=%p count=%u ptrs=%p meta=%p\n",
                code_reg, count, ptrs, metadata_reg);
  }
  if (metadata_reg)
    g_metadata_reg = metadata_reg;
  ((fn_md_register)g_mdreg_addr)(code_reg, metadata_reg, options);
  hook_arm(g_mdreg_addr, (uintptr_t)mdregister_wrapper);
  g_mdreg_armed = 1;
}

/* InitDataPersistent has two null checks after class-init helpers:
 *   0xb7aae0 bl 0x2a4f928 ; beq NRE at 0xb7ab18
 *   0xb7ab00 bl 0x2a3aa10 ; beq NRE at 0xb7ab18
 * Hook both and log only the calls whose return address is inside that
 * method, so the next run says which one returned NULL. */
typedef void *(*fn_init_helper)(void *a0, void *a1, void *a2);
static uintptr_t g_h1_addr, g_h2_addr;
extern unsigned char m3_tramp_h1[];
extern unsigned char m3_tramp_h2[];
extern unsigned char m3_tramp_nullchk[];
static fn_init_helper g_h1_call;
static fn_init_helper g_h2_call;
static fn_init_helper g_nullchk_call;
static uintptr_t g_nullchk_addr;

static int init_call_site(uintptr_t lr) {
  return g_base && lr >= g_base + 0xb7a9e4 && lr < g_base + 0xb7ab24;
}

static int looks_like_method(const void *p) {
  if (!p)
    return 0;
  const uintptr_t *w = p;
  uintptr_t lo = g_base + 0x433ae0;
  uintptr_t hi = g_base + 0x2d04890;
  return w[0] >= lo && w[0] < hi && w[1] >= lo && w[1] < hi;
}

static void log_init_arg(const char *tag, const void *p) {
  if (!p) {
    debugPrintf("[il2cpp]   %s NULL\n", tag);
    return;
  }
  const uintptr_t *w = p;
  debugPrintf("[il2cpp]   %s %p: %08lx %08lx %08lx %08lx\n", tag, p,
              (unsigned long)w[0], (unsigned long)w[1], (unsigned long)w[2],
              (unsigned long)w[3]);
  if (looks_like_method(p)) {
    const char *name = g_method_name ? g_method_name(p) : NULL;
    const void *cls = g_method_class ? g_method_class(p) : NULL;
    const char *cname = (cls && g_class_name) ? g_class_name(cls) : NULL;
    const char *ns = (cls && g_class_ns) ? g_class_ns(cls) : NULL;
    debugPrintf("[il2cpp]   %s method %s%s%s::%s\n", tag, ns && ns[0] ? ns : "",
                ns && ns[0] ? "." : "", cname ? cname : "?", name ? name : "?");
  }
}

static void log_il2cpp_string(const char *tag, const void *p) {
  if (!p) {
    debugPrintf("[il2cpp]   %s NULL\n", tag);
    return;
  }
  const uint32_t *w = p;
  uint32_t len = w[2];
  if (len == 0 || len > 512) {
    debugPrintf("[il2cpp]   %s %p not a plausible string (len %u)\n", tag, p, len);
    return;
  }
  const uint16_t *chars = (const uint16_t *)((const char *)p + 12);
  char buf[513];
  for (uint32_t i = 0; i < len; i++)
    buf[i] = (chars[i] >= 0x20 && chars[i] < 0x7f) ? (char)chars[i] : '?';
  buf[len] = 0;
  debugPrintf("[il2cpp]   %s \"%s\"\n", tag, buf);
}

static void *init_h1_wrapper(void *a0, void *a1, void *a2) {
  uintptr_t lr = (uintptr_t)__builtin_return_address(0);
  int from = init_call_site(lr);
  if (from) {
    debugPrintf("[il2cpp] InitDataPersistent helper1 args %p %p %p\n", a0, a1, a2);
    log_il2cpp_string("arg1", a1);
    log_init_arg("arg2", a2);
  }
  void *r = g_h1_call(a0, a1, a2);
  if (from)
    debugPrintf("[il2cpp] InitDataPersistent helper1 -> %p\n", r);
  return r;
}

static void *init_h2_wrapper(void *a0, void *a1, void *a2) {
  uintptr_t lr = (uintptr_t)__builtin_return_address(0);
  int from = init_call_site(lr);
  if (from)
    debugPrintf("[il2cpp] InitDataPersistent helper2 args %p %p %p\n", a0, a1, a2);
  void *r = g_h2_call(a0, a1, a2);
  if (from)
    debugPrintf("[il2cpp] InitDataPersistent helper2 -> %p\n", r);
  return r;
}

void m3_hook_inits(so_module *mod) {
  g_base = (uintptr_t)mod->load_virtbase;
  g_h1_addr = g_base + 0x2a4f928;
  g_h2_addr = g_base + 0x2a3aa10;
  uint32_t back1 = (uint32_t)(g_h1_addr + 8);
  uint32_t back2 = (uint32_t)(g_h2_addr + 8);
  so_patch_code(m3_tramp_h1 + 12, &back1, sizeof back1);
  so_patch_code(m3_tramp_h2 + 12, &back2, sizeof back2);
  g_h1_call = (fn_init_helper)m3_tramp_h1;
  g_h2_call = (fn_init_helper)m3_tramp_h2;
  hook_arm(g_h1_addr, (uintptr_t)init_h1_wrapper);
  hook_arm(g_h2_addr, (uintptr_t)init_h2_wrapper);
  debugPrintf("[il2cpp] InitDataPersistent helpers hooked at %p %p\n",
              (void *)g_h1_addr, (void *)g_h2_addr);
}

/* The null check just before Resources.Load is called with r1 = the guard
 * object. Log only the call from InitDataPersistent (+0xb7aab4) and print the
 * object's class/fields, so we can identify what enables the dev path. */
static void *guard_wrapper(void *a0, void *a1, void *a2) {
  uintptr_t lr = (uintptr_t)__builtin_return_address(0);
  if (g_base && lr == g_base + 0xb7aab4) {
    debugPrintf("[il2cpp] InitDataPersistent guard object=%p\n", a1);
    /* 0x3450340 is System.String used by IsNullOrEmpty. The owner loaded
     * before ldr r4,[r2,#0x24] is the separate GOT slot 0x3453044. */
    uintptr_t slot = read_word(g_base + 0x3453044);
    uintptr_t root = slot ? read_word(slot) : 0;
    uintptr_t p50 = root ? read_word(root + 0x50) : 0;
    uintptr_t guard = p50 ? read_word(p50 + 0x24) : 0;
    debugPrintf("[il2cpp]   chain slot=%p root=%p p50=%p guard=%p\n",
                (void *)slot, (void *)root, (void *)p50, (void *)guard);
    if (root)
      debugPrintf("[il2cpp]   root words: %08lx %08lx %08lx %08lx\n",
                  (unsigned long)read_word(root), (unsigned long)read_word(root + 4),
                  (unsigned long)read_word(root + 8), (unsigned long)read_word(root + 12));
    if (p50)
      debugPrintf("[il2cpp]   p50 words: %08lx %08lx %08lx %08lx\n",
                  (unsigned long)read_word(p50), (unsigned long)read_word(p50 + 4),
                  (unsigned long)read_word(p50 + 8), (unsigned long)read_word(p50 + 12));
    if (root && g_class_name) {
      const char *cn = g_class_name((const void *)root);
      const char *ns = g_class_ns((const void *)root);
      debugPrintf("[il2cpp]   guard owner %s.%s\n", ns ? ns : "?", cn ? cn : "?");
      typedef const void *(*fn_fields)(const void *, void **);
      typedef int (*fn_field_offset)(const void *);
      fn_fields fields = (fn_fields)(g_base + 0x2c3cc48);
      fn_class_name field_name = (fn_class_name)(g_base + 0x2c3d38c);
      fn_field_offset field_offset = (fn_field_offset)(g_base + 0x2c3d398);
      void *iter = NULL;
      const void *field;
      while ((field = fields((const void *)root, &iter)))
        debugPrintf("[il2cpp]   owner field %s offset=0x%x\n",
                    field_name(field), field_offset(field));
      log_il2cpp_string("guard value", a1);
    }
    if (a1) {
      const uintptr_t *w = a1;
      const char *cn = (g_class_name && w[0]) ? g_class_name((const void *)w[0]) : NULL;
      debugPrintf("[il2cpp]   class %s\n", cn ? cn : "?");
      for (int i = 0; i < 8; i++)
        debugPrintf("[il2cpp]   w[%d]=%08lx\n", i, (unsigned long)w[i]);
    }
  }
  return g_nullchk_call(a0, a1, a2);
}

extern unsigned char m3_tramp_persistent[];
static void persistent_wrapper(void *self, void *method) {
  uintptr_t slot = read_word(g_base + 0x3453044);
  uintptr_t cls = slot ? read_word(slot) : 0;
  uintptr_t fields = cls ? read_word(cls + 0x50) : 0;
  debugPrintf("[il2cpp] MonoDataPersistent.Init self=%p needInit=%u channel=%p textmap=%p\n",
              self, self ? *((unsigned char *)self + 0x18) : 0,
              self ? (void *)read_word((uintptr_t)self + 0x10) : NULL,
              self ? (void *)read_word((uintptr_t)self + 0x14) : NULL);
  ((void (*)(void *, void *))m3_tramp_persistent)(self, method);
  if (fields)
    log_il2cpp_string("channel after Init", (void *)read_word(fields + 0x24));
}

void m3_hook_guard(so_module *mod) {
  g_base = (uintptr_t)mod->load_virtbase;
  g_nullchk_addr = g_base + 0x27b8eb8;
  uint32_t back = (uint32_t)(g_nullchk_addr + 8);
  so_patch_code(m3_tramp_nullchk + 12, &back, sizeof back);
  g_nullchk_call = (fn_init_helper)m3_tramp_nullchk;
  hook_arm(g_nullchk_addr, (uintptr_t)guard_wrapper);
  uint32_t persistent_back = (uint32_t)(g_base + 0xf35248);
  so_patch_code(m3_tramp_persistent + 12, &persistent_back, sizeof persistent_back);
  hook_arm(g_base + 0xf35240, (uintptr_t)persistent_wrapper);
  debugPrintf("[il2cpp] InitDataPersistent guard hook at %p\n", (void *)g_nullchk_addr);
}

/* The 1.9 APK has no DataDev/DataPersistent resource (it is absent from the
 * globalgamemanagers resource map in 1.9/2.0/2.4/6.4). InitDataPersistent
 * calls Resources.Load and immediately dereferences the result, so the
 * missing asset becomes an NRE. Its first check (0xb7aab4) already decides
 * whether this optional data is needed; turn the conditional return at
 * 0xb7aab8 into an unconditional one, so the method exits there instead of
 * entering the missing-resource path. */
void m3_patch_initdata(so_module *mod) {
  g_base = (uintptr_t)mod->load_virtbase;
  uint32_t pop_return = 0xe8bd8830; /* pop {r4,r5,fp,pc} */
  if (so_patch_code((void *)(g_base + 0xb7aab8), &pop_return, sizeof pop_return) == 0)
    debugPrintf("[il2cpp] InitDataPersistent early return patched\n");
  else
    debugPrintf("[il2cpp] InitDataPersistent patch failed\n");
}

/* ConfigAccount::.ctor reads the (empty, because DataPersistent is absent)
 * config string and calls FullSerializer.fsJsonParser::Parse(""), which
 * throws "No input". Skip the parse and return with default fields. */
void m3_patch_configaccount(so_module *mod) {
  g_base = (uintptr_t)mod->load_virtbase;
  uint32_t skip_parse = 0xe3a00000; /* mov r0, #0 */
  uint32_t pop_return = 0xe8bd8c70; /* pop {r4,r5,r6,sl,fp,pc} */
  int a = so_patch_code((void *)(g_base + 0x9c8a90), &skip_parse, sizeof skip_parse);
  int b = so_patch_code((void *)(g_base + 0x9c8aac), &pop_return, sizeof pop_return);
  debugPrintf("[il2cpp] ConfigAccount parse skip: %d %d\n", a, b);
}

static void nre_wrapper(void) {
  debugPrintf("[il2cpp] NullReferenceException raised, caller=%p\n",
              __builtin_return_address(0));
  report_caller_method((uintptr_t)__builtin_return_address(0));
  {
    extern void jni_trace_dump(void);
    jni_trace_dump();
  }
  g_frames = 0;
  if (g_walk) {
    debugPrintf("[il2cpp] walking managed frames\n");
    g_walk(frame_cb, NULL);
  }
  debugPrintf("[il2cpp] managed frames: %d\n", g_frames);
  log_flush_ring();
  if (g_frames == 0)
    scan_stack();
  log_flush_ring();
  if (g_get_nre)
    format_exception(g_get_nre(), "NRE");
  if (g_armed) {
    so_patch_code((void *)g_addr, g_saved, sizeof g_saved);
    g_armed = 0;
  }
  ((void (*)(void))g_addr)();
  debugPrintf("[il2cpp] NRE raise returned unexpectedly\n");
}

/* Exception::Raise(Il2CppException*) catches the throws that go through the
 * explicit entry (DllNotFound and friends); the NRE path bypasses it. */
static uintptr_t g_raise_addr;
static unsigned char g_raise_saved[8];
static int g_raise_armed;

static void raise_wrapper(void *ex) {
  uintptr_t caller = (uintptr_t)__builtin_return_address(0);
  debugPrintf("[il2cpp] Exception::Raise caller=%p\n", (void *)caller);
  static int scans;
  if (scans < 4) {
    scans++;
    report_caller_method(caller);
    if (scans == 1) {
      scan_stack();
      for (int i = 0; i < g_stack_n && i < 6; i++)
        report_addr_method(g_stack_addrs[i]);
    }
  }
  format_exception(ex, "raise");
  if (g_raise_armed) {
    so_patch_code((void *)g_raise_addr, g_raise_saved, sizeof g_raise_saved);
    g_raise_armed = 0;
  }
  ((void (*)(void *))g_raise_addr)(ex);
  debugPrintf("[il2cpp] Exception::Raise returned unexpectedly\n");
}

void m3_hook_raise(so_module *mod) {
  g_raise_addr = so_try_find_addr_rx(
      mod, "_ZN6il2cpp2vm9Exception5RaiseEP15Il2CppException");
  if (!g_raise_addr) {
    debugPrintf("[il2cpp] Exception::Raise not found\n");
    return;
  }
  memcpy(g_raise_saved, (const void *)g_raise_addr, sizeof g_raise_saved);
  hook_arm(g_raise_addr, (uintptr_t)raise_wrapper);
  g_raise_armed = 1;
  debugPrintf("[il2cpp] Exception::Raise hooked at %p\n", (void *)g_raise_addr);
}

void m3_hook_nre(so_module *mod) {
  g_base = (uintptr_t)mod->load_virtbase;
  g_addr = so_try_find_addr_rx(
      mod, "_ZN6il2cpp2vm9Exception27RaiseNullReferenceExceptionEv");
  if (!g_addr) {
    debugPrintf("[il2cpp] RaiseNullReferenceException not found\n");
    return;
  }
  g_method_name = (fn_method_name)so_try_find_addr_rx(mod, "il2cpp_method_get_name");
  g_method_class = (fn_method_class)so_try_find_addr_rx(mod, "il2cpp_method_get_declaring_type");
  g_class_name = (fn_class_name)so_try_find_addr_rx(mod, "il2cpp_class_get_name");
  g_class_ns = (fn_class_name)so_try_find_addr_rx(mod, "il2cpp_class_get_namespace");
  g_method_info = (fn_method_info)so_try_find_addr_rx(
      mod, "_ZN6il2cpp2vm13MetadataCache38GetMethodInfoFromMethodDefinitionIndexEi");
  g_method_info_index = (fn_method_info)so_try_find_addr_rx(
      mod, "_ZN6il2cpp2vm13MetadataCache22GetMethodInfoFromIndexEj");
  g_walk = (fn_walk)so_try_find_addr_rx(mod, "il2cpp_current_thread_walk_frame_stack");
  g_format_exception = so_try_find_addr_rx(mod, "il2cpp_format_exception");
  g_format_trace = so_try_find_addr_rx(mod, "il2cpp_format_stack_trace");
  g_get_nre = (void *(*)(void))so_try_find_addr_rx(
      mod, "_ZN6il2cpp2vm9Exception25GetNullReferenceExceptionEv");
  {
    typedef void (*fn_init_trace)(void);
    fn_init_trace init_trace = (fn_init_trace)so_try_find_addr_rx(
        mod, "_ZN6il2cpp2vm10StackTrace37InitializeStackTracesForCurrentThreadEv");
    if (init_trace) {
      init_trace();
      debugPrintf("[il2cpp] stack traces initialized\n");
    }
  }
  memcpy(g_saved, (const void *)g_addr, sizeof g_saved);
  hook_arm(g_addr, (uintptr_t)nre_wrapper);
  g_armed = 1;
  debugPrintf("[il2cpp] NRE hooked at %p (walk %p nre %p)\n", (void *)g_addr,
              (void *)g_walk, (void *)g_get_nre);
}

/* Called after libil2cpp is sealed but before its init_array: IL2CPP builds
 * its metadata cache from these two registrations during start-up. */
void m3_hook_mdregister(so_module *mod) {
  g_base = (uintptr_t)mod->load_virtbase;
  g_mdreg_addr = so_try_find_addr_rx(
      mod,
      "_ZN6il2cpp2vm13MetadataCache8RegisterEPK22Il2CppCodeRegistration"
      "PK26Il2CppMetadataRegistrationPK20Il2CppCodeGenOptions");
  if (!g_mdreg_addr) {
    debugPrintf("[il2cpp] MetadataCache::Register not found\n");
    return;
  }
  memcpy(g_mdreg_saved, (const void *)g_mdreg_addr, sizeof g_mdreg_saved);
  hook_arm(g_mdreg_addr, (uintptr_t)mdregister_wrapper);
  g_mdreg_armed = 1;
  debugPrintf("[il2cpp] MetadataCache::Register hooked at %p\n", (void *)g_mdreg_addr);
}
