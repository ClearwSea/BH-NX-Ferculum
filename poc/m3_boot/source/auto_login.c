/* Opt-in offline test identity; preserve the native token-login state machine. MIT. */
#include <stdint.h>
#include <stdio.h>
#include "dcr_path.h"
#include "network.h"
#include "so_util.h"
#include "util.h"

typedef void *(*GetAccount)(void *, const void *);
static GetAccount original;
static void *(*new_string)(const char *);
static uintptr_t login_caller;
extern unsigned char m3_tramp_account[];

static void *get_account(void *self, const void *method) {
  uintptr_t caller = (uintptr_t)__builtin_return_address(0);
  void *account = original(self, method);
  /* Only seed at LoginUI, never in save/reset/get-user-info consumers. */
  if (caller == login_caller && account && m3_net_init()) {
    void **uid = (void **)((char *)account + 8);
    void **token = (void **)((char *)account + 12);
    int old_uid_len = *uid ? *(int32_t *)((char *)*uid + 8) : 0;
    int old_token_len = *token ? *(int32_t *)((char *)*token + 8) : 0;
    *uid = new_string("1024");
    *token = new_string("server19-local-only");
    debugPrintf("[auto-login] test identity seeded at LoginUI; previous lengths=%d/%d; native token login follows\n",
                old_uid_len, old_token_len);
  }
  return account;
}

int m3_auto_login_install(so_module *mod) {
  char path[512];
  snprintf(path, sizeof path, "%s/auto-login.txt", dcr_game_root());
  FILE *f = fopen(path, "r");
  int enabled = f ? fgetc(f) == '1' : 0;
  if (f) fclose(f);
  if (!enabled) { debugPrintf("[auto-login] disabled\n"); return 1; }
  uintptr_t base = (uintptr_t)mod->load_virtbase;
  const struct { uint32_t off, word; } guards[] = {
    {0xb6dc4c, 0xe92d4c70}, {0xb6dc50, 0xe28db010},
    {0xb6dc98, 0xe5945008}, /* GeneralLocalDataItem._account */
    {0x1597bec, 0xebd75816}, /* LoginUI -> get_Account */
    {0x1597c08, 0xe5915008}, /* AccountData.uid */
    {0x1598020, 0xe590200c}, /* TokenLoginTest reads AccountData.token */
  };
  for (unsigned i = 0; i < sizeof guards / sizeof guards[0]; ++i)
    if (*(const uint32_t *)(base + guards[i].off) != guards[i].word) return 0;
  new_string = (void *)so_try_find_addr_rx(mod, "il2cpp_string_new");
  if (!new_string) return 0;
  uintptr_t resume = base + 0xb6dc54;
  so_patch_code(m3_tramp_account + 12, &resume, sizeof resume);
  original = (GetAccount)m3_tramp_account;
  login_caller = base + 0x1597bf0;
  hook_arm(base + 0xb6dc4c, (uintptr_t)get_account);
  debugPrintf("[auto-login] enabled for isolated server19 test account\n");
  return 1;
}
