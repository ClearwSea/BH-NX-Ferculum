/* Offline entry continuation and bounded open-world route diagnostics. */
#include <stdint.h>
#include <stdio.h>
#include <switch.h>
#include "dcr_path.h"
#include "so_util.h"
#include "util.h"
#include "network.h"
#include "load_profile.h"
#include "boot_profile.h"

typedef void (*PageFn)(void *, const void *);
typedef void (*GotoFn)(void *, int, void *, const void *);
typedef uintptr_t (*PacketFn)(void *, void *, const void *);
static PageFn login_ui, stub_setup, entry_floor, entry_timeout;
static uintptr_t (*entry_login_reply)(void *, void *, const void *);
static void (*entry_agreement)(void *, void *, int, const void *);
static int (*entry_login_retcode)(void *, const void *);
extern unsigned char m3_tramp_entry_floor[], m3_tramp_entry_timeout[], m3_tramp_entry_login_reply[], m3_tramp_entry_agreement[];
static GotoFn goto_world;
static PacketFn world_reply;
static void (*goto_reply)(void *, void *, const void *);
static void (*background_click)(void *, void *, const void *);
static int auto_continue;
/* 0: original/manual path; 1: loading; 2: ready for user; 3: entered. */
static unsigned entry_input_state;
static void *pending_floor, *entry_page, *entry_login_panel, *entry_loading_panel;
static void *(*entry_get_view)(void *,const void *);
static void *(*entry_get_transform)(void *,const void *);
static void *(*entry_find_transform)(void *,void *,const void *);
static void *(*entry_get_game_object)(void *,const void *);
static void (*entry_set_active)(void *,int,const void *);
static void **entry_login_panel_name, **entry_loading_panel_name;
static void set_login_panel(int visible) {
  if(!entry_login_panel && entry_page && *entry_login_panel_name) {
    void *v=entry_get_view(entry_page,NULL);
    if(v)v=entry_get_transform(v,NULL);
    if(v)v=entry_find_transform(v,*entry_login_panel_name,NULL);
    if(v)entry_login_panel=entry_get_game_object(v,NULL);
  }
  if(!entry_loading_panel && entry_page && *entry_loading_panel_name) {
    void *v=entry_get_view(entry_page,NULL);
    if(v)v=entry_get_transform(v,NULL);
    if(v)v=entry_find_transform(v,*entry_loading_panel_name,NULL);
    if(v)entry_loading_panel=entry_get_game_object(v,NULL);
  }
  if(entry_login_panel)entry_set_active(entry_login_panel,visible,NULL);
  if(entry_loading_panel)entry_set_active(entry_loading_panel,visible,NULL);
}
static const void *pending_floor_method;
extern unsigned char m3_tramp_entry_click[];
static PageFn wheel_setup;
static void (*wheel_wait)(void *, float, const void *);
extern unsigned char m3_tramp_entry_wheel[];
extern unsigned char m3_tramp_entry_login[], m3_tramp_entry_goto[];
extern unsigned char m3_tramp_entry_reply[], m3_tramp_entry_callback[], m3_tramp_entry_stub[];
void m3_asset_load_begin(void);
void m3_asset_load_end(void);

static void show_login(void *self, const void *method) {
  login_ui(self, method);
  unsigned char *p = self;
  if(p && p[0x49] && !p[0x40]) {
    m3_boot_mark("account-ui-ready");
    if(!auto_continue)m3_boot_finish("manual-login-interactive");
  }
  debugPrintf("[entry-flow] login UI ready=%u blocked=%u auto=%d\n",
              p ? p[0x49] : 0, p ? p[0x40] : 0, auto_continue);
  /* Same guards and native transition as OnBGClick, after UI setup returns.
   * No synthetic input, flag writes, saved identity or progress changes. */
  if (auto_continue && p && p[0x49] && !p[0x40]) {
    debugPrintf("[entry-flow] continue native game-server login\n");
    entry_page=self; entry_login_panel=NULL; entry_loading_panel=NULL;
    entry_input_state=1;
    pending_floor=NULL; pending_floor_method=NULL;
    background_click(self, NULL, NULL);
  }
}
/* Called by WaitAgreementConfirmed only after its native resource/data gates.
 * Do not reveal the inert login prompt while the automatic login is in flight. */
static void start_entry_floor(void *self, const void *method) {
  if(auto_continue && entry_input_state==1) {
    pending_floor=self; pending_floor_method=method;
    entry_input_state=2;
    set_login_panel(1);
    debugPrintf("[entry-flow] resources ready; waiting for user click\n");
    m3_boot_finish("entry-ready-for-user-click");
    return;
  }
  if(entry_input_state==2)return;
  entry_floor(self, method);
  m3_boot_finish("entry-resources-ready");
}
static void entry_user_click(void *self, void *event, const void *method) {
  if(entry_input_state==2 && pending_floor) {
    void *floor=pending_floor; const void *floor_method=pending_floor_method;
    pending_floor=NULL; pending_floor_method=NULL; entry_input_state=3;
    debugPrintf("[entry-flow] user click accepted; start native entry animation\n");
    set_login_panel(0);
    entry_floor(floor,floor_method);
    return;
  }
  if(entry_input_state==1 || entry_input_state==3)return;
  background_click(self,event,method);
}
static uintptr_t login_reply_ready(void *self, void *packet, const void *method) {
  if(packet && entry_login_retcode(packet,NULL)!=0){entry_input_state=0;m3_boot_finish("login-error-visible");}
  return entry_login_reply(self,packet,method);
}
static void show_entry_agreement(void *self, void *callback, int first, const void *method) {
  m3_boot_finish("login-agreement-interactive");
  entry_agreement(self,callback,first,method);
}
static void entry_login_timeout(void *self, const void *method) {
  entry_input_state=0;
  m3_boot_finish("login-timeout-visible");
  entry_timeout(self,method);
}
static void open_world(void *self, int overview, void *args, const void *method) {
  debugPrintf("[entry-flow] openworld request overview=%d args=%p\n", overview, args);
  goto_world(self, overview, args, method);
  debugPrintf("[entry-flow] openworld request queued\n");
}
static uintptr_t receive_world(void *self, void *packet, const void *method) {
  debugPrintf("[entry-flow] openworld data handler begin\n");
  uintptr_t result = world_reply(self, packet, method);
  debugPrintf("[entry-flow] openworld data handler end result=%u\n", (unsigned)result);
  return result;
}
static void receive_goto(void *self, void *packet, const void *method) {
  debugPrintf("[entry-flow] openworld route callback begin\n");
  m3_boot_finish("openworld-route-before-login-ready");
  m3_asset_load_begin();
  goto_reply(self, packet, method);
  m3_asset_load_end();
  debugPrintf("[entry-flow] openworld route callback end\n");
}
static void setup_stub(void *self, const void *method) {
  debugPrintf("[entry-flow] openworld page setup begin\n");
  m3_asset_load_phase(1);
  uint64_t started = armGetSystemTick();
  stub_setup(self, method);
  m3_asset_load_phase(2);
  debugPrintf("[load-cost] page_setup_us=%llu\n",
    (unsigned long long)(armTicksToNs(armGetSystemTick() - started) / 1000));
  debugPrintf("[entry-flow] openworld page setup end\n");
}
static void setup_wheel(void *self, const void *method) {
  unsigned char *p = self;
  /* The original five-second timer destroys this response callback while
   * startup queries are still queued. Keep the native bounded waiter alive.
   * 502 is GetOpenworldRsp; other widgets and external servers are unchanged. */
  if (m3_net_local && p && p[0x30] && *(uint16_t *)(p+0x32)==502) {
    wheel_wait(self, 90.0f, NULL);
    debugPrintf("[entry-flow] openworld native response wait=90s\n");
  }
  wheel_setup(self, method);
}

int m3_entry_flow_install(so_module *mod) {
  uintptr_t base = (uintptr_t)mod->load_virtbase;
  const struct { uint32_t off, push, frame; unsigned char *tramp; uintptr_t hook; } sites[] = {
    {0xb64e64,0xe92d4ff0,0xe28db01c,m3_tramp_entry_login,(uintptr_t)show_login},
    {0xb6d8f8,0xe92d4c70,0xe28db010,m3_tramp_entry_click,(uintptr_t)entry_user_click},
    {0xa1d5dc,0xe92d4ff0,0xe28db01c,m3_tramp_entry_floor,(uintptr_t)start_entry_floor},
    {0xb6430c,0xe92d4bf0,0xe28db018,m3_tramp_entry_login_reply,(uintptr_t)login_reply_ready},
    {0xb6e3d8,0xe92d4bf0,0xe28db018,m3_tramp_entry_agreement,(uintptr_t)show_entry_agreement},
    {0xb6a0e4,0xe92d4ff0,0xe28db01c,m3_tramp_entry_timeout,(uintptr_t)entry_login_timeout},
    {0x130ec0c,0xe92d4df0,0xe28db018,m3_tramp_entry_goto,(uintptr_t)open_world},
    {0x130a744,0xe92d4ff0,0xe28db01c,m3_tramp_entry_reply,(uintptr_t)receive_world},
    {0x13150c4,0xe92d4ff0,0xe28db01c,m3_tramp_entry_callback,(uintptr_t)receive_goto},
    {0x1302f58,0xe92d4ff0,0xe28db01c,m3_tramp_entry_stub,(uintptr_t)setup_stub},
    {0xd5c8f8,0xe92d48f0,0xe28db010,m3_tramp_entry_wheel,(uintptr_t)setup_wheel},
  };
  for (unsigned i=0;i<sizeof sites/sizeof sites[0];i++) {
    const uint32_t *p=(void *)(base+sites[i].off);
    if(p[0]!=sites[i].push || p[1]!=sites[i].frame) return 0;
  }
  if (*(uint32_t *)(base+0xb6505c)!=0xe5c67049 ||
      *(uint32_t *)(base+0xb6d944)!=0xe5d40040 ||
      *(uint32_t *)(base+0xb6d954)!=0xe5d40049) return 0;
  if (*(uint32_t *)(base+0x1eabdc8)!=0xe5900008 ||
      *(uint32_t *)(base+0x1eabdcc)!=0xe12fff1e) return 0;
  if (*(uint32_t *)(base+0xd5c804)!=0xee001a10 ||
      *(uint32_t *)(base+0xd5c810)!=0xcd800a0a) return 0;
  char path[512];
  snprintf(path,sizeof path,"%s/auto-login.txt",dcr_game_root());
  FILE *f=fopen(path,"r"); auto_continue=f && fgetc(f)=='1'; if(f) fclose(f);
  entry_get_view=(void *)(base+0x7ac6a4);
  entry_get_transform=(void *)(base+0x25838f8);
  entry_find_transform=(void *)(base+0x25f1568);
  entry_get_game_object=(void *)(base+0x2575b4c);
  entry_set_active=(void *)(base+0x2583c00);
  entry_loading_panel_name=(void *)(base+0x34ffa8c);
  entry_login_panel_name=(void *)(base+0x34ff9f0); /* original initialized LoginPanel literal */
  entry_floor=(PageFn)m3_tramp_entry_floor; entry_timeout=(PageFn)m3_tramp_entry_timeout;
  entry_login_reply=(void *)m3_tramp_entry_login_reply; entry_agreement=(void *)m3_tramp_entry_agreement;
  entry_login_retcode=(void *)(base+0x1eabdc8);
  login_ui=(PageFn)m3_tramp_entry_login; goto_world=(GotoFn)m3_tramp_entry_goto;
  world_reply=(PacketFn)m3_tramp_entry_reply; goto_reply=(void *)m3_tramp_entry_callback;
  stub_setup=(PageFn)m3_tramp_entry_stub; background_click=(void *)m3_tramp_entry_click;
  wheel_setup=(PageFn)m3_tramp_entry_wheel; wheel_wait=(void *)(base+0xd5c804);
  for(unsigned i=0;i<sizeof sites/sizeof sites[0];i++) {
    uint32_t code[]={sites[i].push,sites[i].frame,0xe51ff004,(uint32_t)(base+sites[i].off+8)};
    so_patch_code(sites[i].tramp,code,sizeof code);
    hook_arm(base+sites[i].off,sites[i].hook);
  }
  debugPrintf("[entry-flow] native login continuation and openworld route trace installed\n");
  return 1;
}
