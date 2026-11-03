/* Observe the real Wwise Seek boundary without changing its arguments. */
#include <stdint.h>
#include <string.h>

#include "so_util.h"
#include "util.h"

#if M3_VOICE_SEEK_PROBE
typedef int (*seek_fn)(uint32_t, uint32_t, int32_t, int32_t, uint32_t);
static seek_fn real_seek;

static int observed_seek(uint32_t event_id, uint32_t game_object_id,
                         int32_t position_ms, int32_t nearest_marker,
                         uint32_t playing_id) {
  debugPrintf("[voice-seek] args event=%08lx object=%08lx ms=%ld marker=%ld playing=%lu\n",
              (unsigned long)event_id, (unsigned long)game_object_id,
              (long)position_ms, (long)nearest_marker, (unsigned long)playing_id);
  int result = real_seek(event_id, game_object_id, position_ms, nearest_marker, playing_id);
  debugPrintf("[voice-seek] queued result=%d\n", result);
  return result;
}

void *port_import_interpose(const char *sym, void *real) {
  if (real && strcmp(sym, "CSharp_SeekOnEvent__SWIG_0") == 0) {
    debugPrintf("[voice-seek] interpose bound\n");
    real_seek = (seek_fn)real;
    return (void *)observed_seek;
  }
  return real;
}
#endif
