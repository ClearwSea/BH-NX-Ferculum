/* Pinned SharpZipLib CP437 decoder. UTF-8 and other code pages stay native. MIT. */
#include <stdint.h>
#include <stdlib.h>
#include "so_util.h"
#include "util.h"

static const uint16_t cp437_high[128] = {
  0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
  0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
  0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
  0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
  0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
  0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
  0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
  0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
  0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
  0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
  0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
  0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
  0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
  0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
  0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
  0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
};

typedef void *(*Convert)(void *, void *, int32_t, const void *);
static Convert original;
static int32_t (*codepage)(void *, const void *);
static void *(*new_utf16)(const uint16_t *, int32_t);
extern unsigned char m3_tramp_zip[];

static void *convert(void *unused, void *bytes, int32_t count, const void *method) {
  if (!bytes || count < 0 || (uint32_t)count > *(uint32_t *)((char *)bytes + 12)
      || codepage(NULL, NULL) != 437)
    return original(unused, bytes, count, method);
  uint16_t small[256];
  uint16_t *chars = count <= 256 ? small : malloc((size_t)count * sizeof *chars);
  if (!chars) return original(unused, bytes, count, method);
  const uint8_t *data = (const uint8_t *)bytes + 16;
  for (int32_t i = 0; i < count; i++)
    chars[i] = data[i] < 128 ? data[i] : cp437_high[data[i] - 128];
  static unsigned calls;
  if (calls++ < 4)
    debugPrintf("[zip-cp437] count=%ld caller=%p\n", (long)count, __builtin_return_address(0));
  void *result = new_utf16(chars, count);
  if (chars != small) free(chars);
  return result;
}

int m3_zip_install(so_module *mod) {
  uintptr_t base = (uintptr_t)mod->load_virtbase;
  uintptr_t target = base + 0x1d59ecc;
  /* ConvertToString(byte[], int), DefaultCodePage getter, and array length ABI. */
  const struct { uint32_t off, word; } guards[] = {
    {0x1d59ecc, 0xe92d4df0}, {0x1d59ed0, 0xe28db018},
    {0x1d59e1c, 0xe92d4830}, {0x1d59ea4, 0xe5900050},
    {0x1d59ea8, 0xe5900000}, {0x1d5a0f8, 0xe594200c},
    {0x1d5a1d8, 0xe3160b02}, /* UTF-8 flag test remains untouched. */
  };
  for (unsigned i = 0; i < sizeof guards / sizeof guards[0]; i++)
    if (*(const uint32_t *)(base + guards[i].off) != guards[i].word) return 0;
  new_utf16 = (void *)so_try_find_addr_rx(mod, "il2cpp_string_new_utf16");
  if (!new_utf16) return 0;
  codepage = (void *)(base + 0x1d59e1c);
  uintptr_t resume = target + 8;
  so_patch_code(m3_tramp_zip + 12, &resume, sizeof resume);
  original = (Convert)m3_tramp_zip;
  hook_arm(target, (uintptr_t)convert);
  debugPrintf("[zip-cp437] installed; other encodings remain native\n");
  return 1;
}
