/* Bounded evidence for Unity's settlement ZIP read failure. No stream edits. */
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz/miniz.h>
#include "util.h"
#include "bionic_pthread.h"
#include "load_profile.h"

extern int __real_b_inflate(mz_streamp, int);
int __wrap_b_inflate(mz_streamp s, int flush) {
  unsigned in = s ? s->avail_in : 0, out = s ? s->avail_out : 0;
  unsigned long total_in = s ? s->total_in : 0, total_out = s ? s->total_out : 0;
  uint64_t token = m3_load_enter(LOAD_INFLATE);
  int rc = __real_b_inflate(s, flush);
  m3_load_leave(token, s && s->avail_out <= out ? out - s->avail_out : 0);
  if (rc < 0 && rc != MZ_BUF_ERROR) {
    static unsigned errors;
    unsigned n = __atomic_fetch_add(&errors, 1, __ATOMIC_RELAXED);
    if (n < 12)
      debugPrintf("[inflate-error] tid=%d caller=%p stream=%p state=%p flush=%d rc=%d in=%u->%u out=%u->%u total=%lu/%lu->%lu/%lu\n",
        b_gettid(), __builtin_return_address(0), s, s ? s->state : 0, flush, rc,
        in, s ? s->avail_in : 0, out, s ? s->avail_out : 0,
        total_in, total_out, s ? s->total_in : 0, s ? s->total_out : 0);
  }
  return rc;
}

