/* Diagnose failed stack mappings without changing kernel behavior. MIT. */
#include <switch.h>
#include "util.h"
Result __real_nvAddressSpaceMap(NvAddressSpace *, u32, bool, NvKind, iova_t *);
Result __wrap_nvAddressSpaceMap(NvAddressSpace *a, u32 handle, bool cache, NvKind kind, iova_t *out) {
  Result rc = __real_nvAddressSpaceMap(a, handle, cache, kind, out);
  debugPrintf("[gpu-map] fd=%u handle=%u iova=%llx rc=%x\n", a->fd, handle,
              (unsigned long long)(R_SUCCEEDED(rc) && out ? *out : 0), rc);
  return rc;
}
Result __real_nvAddressSpaceUnmap(NvAddressSpace *, iova_t);
Result __wrap_nvAddressSpaceUnmap(NvAddressSpace *a, iova_t addr) {
  Result rc = __real_nvAddressSpaceUnmap(a, addr);
  debugPrintf("[gpu-unmap] fd=%u iova=%llx rc=%x\n", a->fd, (unsigned long long)addr, rc);
  return rc;
}
Result __real_nvIoctl(u32, u32, void *);
Result __wrap_nvIoctl(u32 fd, u32 request, void *arg) {
  Result rc = __real_nvIoctl(fd, request, arg);
  /* libnx's actual 24-byte Nvmap Free request; observe its output in place. */
  if (request == 0xc0180105 && arg) {
    struct FreeReply { u32 handle, pad; u64 refcount; u32 size, flags; };
    const struct FreeReply *r = arg;
    debugPrintf("[gpu-free] fd=%u handle=%u rc=%x refs=%llu size=%u flags=%x\n",
                fd, r->handle, rc, (unsigned long long)r->refcount, r->size, r->flags);
  }
  return rc;
}
void __real_nvMapClose(NvMap *map);
void __wrap_nvMapClose(NvMap *map) {
  void *addr = map->cpu_addr;
  u32 size = map->size;
  u32 handle = map->handle;
  debugPrintf("[gpu-close] begin handle=%u addr=%p size=%u\n", handle, addr, size);
  __real_nvMapClose(map);
  if (addr) {
    MemoryInfo mi; u32 pi;
    Result rc = svcQueryMemory(&mi, &pi, (u64)(uintptr_t)addr);
    if (R_SUCCEEDED(rc))
      debugPrintf("[gpu-release] handle=%u addr=%p size=%u attr=%x device_refs=%u\n",
                  handle, addr, size, mi.attr, mi.device_refcount);
    else debugPrintf("[gpu-release] query rc=%x\n", rc);
  }
}
Result __real_svcMapMemory(void *dst, void *src, u64 size);
Result __wrap_svcMapMemory(void *dst, void *src, u64 size) {
  Result rc = __real_svcMapMemory(dst, src, size);
  if (R_SUCCEEDED(rc)) debugPrintf("[map-ok] src=%p dst=%p size=%lu\n", src, dst, (unsigned long)size);
  if (R_FAILED(rc)) {
    debugPrintf("[map-fail] rc=%x src=%p dst=%p size=%lu\n", rc, src, dst, (unsigned long)size);
    uintptr_t start = (uintptr_t)src, end = start + (uintptr_t)size;
    for (uintptr_t p = start; p < end;) {
      MemoryInfo mi; u32 pi;
      if (R_FAILED(svcQueryMemory(&mi, &pi, p))) break;
      debugPrintf("[map-fail] source region=%lx+%lx type=%x attr=%x perm=%x\n",
                  (unsigned long)mi.addr, (unsigned long)mi.size, mi.type, mi.attr, mi.perm);
      uintptr_t next = (uintptr_t)(mi.addr + mi.size);
      if (next <= p) break;
      p = next;
    }
  }
  return rc;
}
