/* Unity preload omitted the manager lock used by its sibling readers and sweeper. */
#include <stdint.h>

typedef uintptr_t (*ResourcePreload)(uintptr_t, uintptr_t, uintptr_t);
typedef void (*ResourceMutex)(uintptr_t);
static ResourcePreload resource_preload;
static ResourceMutex resource_lock, resource_unlock;

uintptr_t m3_resource_preload_locked(uintptr_t output, uintptr_t manager, uintptr_t name) {
  uintptr_t mutex = manager + 0x68;
  resource_lock(mutex);
  uintptr_t result = resource_preload(output, manager, name);
  resource_unlock(mutex);
  return result;
}

#ifndef RESOURCE_LOCK_TEST
#include "so_util.h"
#include "util.h"
extern unsigned char m3_tramp_resource_preload[], m3_tramp_resource_preload_resume[];

int m3_resource_load_lock_install(so_module *mod) {
  uintptr_t base = (uintptr_t)mod->load_virtbase;
  const struct { uint32_t offset, word; } guards[] = {
    {0x409f4c, 0xe92d4070}, {0x409f50, 0xe1a06001},
    {0x43f11c, 0xe2800004}, {0x43f120, 0xea0003e5},
    {0x43f124, 0xe2800004}, {0x43f128, 0xea0003e4}
  };
  for (unsigned j = 0; j < sizeof guards / sizeof guards[0]; ++j)
    if (*(const uint32_t *)(base + guards[j].offset) != guards[j].word) {
      debugPrintf("[resource-lock] unsupported Unity at %x\n", guards[j].offset);
      return 0;
    }
  uintptr_t resume = base + 0x409f54;
  so_patch_code(m3_tramp_resource_preload_resume, &resume, sizeof resume);
  resource_preload = (ResourcePreload)m3_tramp_resource_preload;
  resource_lock = (ResourceMutex)(base + 0x43f11c);
  resource_unlock = (ResourceMutex)(base + 0x43f124);
  hook_arm(base + 0x409f4c, (uintptr_t)m3_resource_preload_locked);
  debugPrintf("[resource-lock] preload protected by existing manager mutex +68\n");
  return 1;
}
#endif

