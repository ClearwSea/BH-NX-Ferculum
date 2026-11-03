/* Startup-only native FullSerializer subcall timing. No managed object cache.
 * Inclusive timings can overlap: never sum them as a critical-path total.
 * All selected APIs use at most four integer/pointer A32 arguments. */
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <switch.h>
#include "so_util.h"
#include "boot_profile.h"
#include "util.h"

typedef uintptr_t (*Native4)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static Native4 serializer_original[6];
static unsigned serializer_ready;
static Handle serializer_owner;
static struct {uint64_t ticks;unsigned calls,completed,depth;} serializer_stats[6];
static const char *const labels[]={"processors","converter","metadata","create","write","read"};
extern unsigned char m3_serializer_tramp_0[],m3_serializer_tramp_1[],m3_serializer_tramp_2[];
extern unsigned char m3_serializer_tramp_3[],m3_serializer_tramp_4[],m3_serializer_tramp_5[];

int m3_serializer_begin(void) {
  if(!serializer_ready)return 0;
  /* Scope is only opened by the matched outer runtime_invoke. A second
   * thread never resets the first thread's statistics. */
  Handle expected=0;
  if(!__atomic_compare_exchange_n(&serializer_owner,&expected,threadGetCurHandle(),0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
  memset(serializer_stats,0,sizeof serializer_stats);
  return 1;
}
void m3_serializer_end(const char *name) {
  if(__atomic_load_n(&serializer_owner,__ATOMIC_ACQUIRE)!=threadGetCurHandle())return;
  for(unsigned i=0;i<6;i++)m3_boot_emit("[serializer-phase] name=%s part=%s calls=%u completed=%u open=%u inclusive_us=%llu\n",
    name,labels[i],serializer_stats[i].calls,serializer_stats[i].completed,serializer_stats[i].depth,
    (unsigned long long)(armTicksToNs(serializer_stats[i].ticks)/1000));
  __atomic_store_n(&serializer_owner,0,__ATOMIC_RELEASE);
}
static uintptr_t serializer_native(unsigned k,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d){
#if M3_TABLE_WORKER_PROBE >= 2 && M3_TABLE_WORKER_PROBE < 5
  extern uintptr_t m3_serializer_metadata_guard(Native4,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
  if(k==2)return m3_serializer_metadata_guard(serializer_original[k],a,b,c,d);
#endif
  if(k==4){((void (*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t))serializer_original[k])(a,b,c,d);return 0;}
  return serializer_original[k](a,b,c,d);
}
static uintptr_t serializer_call(unsigned k,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d) {
  int incoming=errno;
  Handle owner=__atomic_load_n(&serializer_owner,__ATOMIC_ACQUIRE);
  if(!owner || owner!=threadGetCurHandle()) {errno=incoming;return serializer_native(k,a,b,c,d);}
  serializer_stats[k].calls++;
  unsigned outer=serializer_stats[k].depth++==0;
  uint64_t start=outer?armGetSystemTick():0;
  errno=incoming;
  uintptr_t result=serializer_native(k,a,b,c,d);
  int saved=errno;
  if(outer)serializer_stats[k].ticks+=armGetSystemTick()-start;
  serializer_stats[k].depth--;serializer_stats[k].completed++;
  errno=saved;return result;
}
#define WRAP(N) static uintptr_t serializer_wrap_##N(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d){return serializer_call(N,a,b,c,d);}
WRAP(0) WRAP(1) WRAP(2) WRAP(3) WRAP(4) WRAP(5)

int m3_serializer_install(so_module *mod) {
  static const unsigned sites[][3]={
    {0x178b7ac,0xe92d4ff0,0xe28db01c},{0x178bb00,0xe92d4ff0,0xe28db01c},
    {0x1785fe8,0xe92d4bf0,0xe28db018},{0x1787d74,0xe92d4ff0,0xe28db01c},
    {0x1799da8,0xe92d4df0,0xe28db018},{0x17999f0,0xe92d4df0,0xe28db018}};
  unsigned char *tramps[]={m3_serializer_tramp_0,m3_serializer_tramp_1,m3_serializer_tramp_2,m3_serializer_tramp_3,m3_serializer_tramp_4,m3_serializer_tramp_5};
  Native4 wraps[]={serializer_wrap_0,serializer_wrap_1,serializer_wrap_2,serializer_wrap_3,serializer_wrap_4,serializer_wrap_5};
  uintptr_t b=(uintptr_t)mod->load_virtbase;
  for(unsigned i=0;i<6;i++)if(memcmp((void *)(b+sites[i][0]),sites[i]+1,8)){
    debugPrintf("[serializer-profile] unsupported preimage %x; no hooks\n",sites[i][0]);return 0;}
  for(unsigned i=0;i<6;i++){
    uintptr_t resume=b+sites[i][0]+8;
    so_patch_code(tramps[i]+12,&resume,4);
    serializer_original[i]=(Native4)tramps[i];
    hook_arm(b+sites[i][0],(uintptr_t)wraps[i]);
  }
  serializer_ready=1;
#if M3_TABLE_WORKER_PROBE >= 2
  extern void m3_serializer_cache_install(so_module *);
  m3_serializer_cache_install(mod);
#endif
  debugPrintf("[serializer-profile] six native subcall timers installed; inclusive wall time, no UI\n");return 1;
}
