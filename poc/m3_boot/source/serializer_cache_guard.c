/* Emulator experiment only. A shared recursive gate covers the two mutable
 * reflection caches, not table parsing or per-instance object construction.
 * Compile with -fexceptions so the cleanup releases the gate on ARM unwind. */
#if M3_TABLE_WORKER_PROBE >= 2
#include <stdint.h>
#include <string.h>
#include <switch.h>
#include "so_util.h"
#include "util.h"
static Mutex gate;
static Handle owner;
static unsigned depth,installed;
typedef uintptr_t (*Fn4)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
typedef uintptr_t (*Fn5)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static Fn5 attribute_original;
#if M3_TABLE_WORKER_PROBE >= 3
/* Valid only during a single table half. Original caches retain the objects;
 * the task's rooted serializer/config owns their lifetime. Never reuse across
 * table calls or cache-reset boundaries. Misses still use the shared gate. */
static __thread unsigned local_active;
static __thread struct {uintptr_t key[5],result;unsigned valid;} hot[512];
void m3_serializer_cache_begin(void){memset(hot,0,sizeof hot);local_active=1;}
void m3_serializer_cache_end(void){local_active=0;memset(hot,0,sizeof hot);}
static unsigned bucket(uintptr_t b,uintptr_t c,uintptr_t d){return ((b>>3)^(c>>5)^(d>>1))&511;}
#endif
extern unsigned char m3_attribute_cache_tramp[];
static void enter(void){
  Handle self=threadGetCurHandle();
  if(__atomic_load_n(&owner,__ATOMIC_ACQUIRE)!=self){
    mutexLock(&gate);__atomic_store_n(&owner,self,__ATOMIC_RELEASE);
  }
  depth++;
}
static void leave(int *unused){
  (void)unused;
  if(--depth==0){__atomic_store_n(&owner,0,__ATOMIC_RELEASE);mutexUnlock(&gate);}
}
static uintptr_t attribute(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e){
#if M3_TABLE_WORKER_PROBE >= 3
  uintptr_t key[]={a,b,c,d,e};unsigned i=bucket(b,c,d);
  if(local_active&&hot[i].valid==1&&!memcmp(hot[i].key,key,sizeof key))return hot[i].result;
#endif
  enter();int cleanup __attribute__((cleanup(leave)))=0;
  uintptr_t result=attribute_original(a,b,c,d,e);
#if M3_TABLE_WORKER_PROBE >= 3
  if(local_active){memcpy(hot[i].key,key,sizeof key);hot[i].result=result;hot[i].valid=1;}
#endif
  return result;
}
uintptr_t m3_serializer_metadata_guard(Fn4 fn,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d){
#if M3_TABLE_WORKER_PROBE >= 3
  uintptr_t key[]={a,b,c,d,0};unsigned i=bucket(b,c,d);
  if(local_active&&hot[i].valid==2&&!memcmp(hot[i].key,key,sizeof key))return hot[i].result;
#endif
  enter();int cleanup __attribute__((cleanup(leave)))=0;
  uintptr_t result=fn(a,b,c,d);
#if M3_TABLE_WORKER_PROBE >= 3
  if(local_active){memcpy(hot[i].key,key,sizeof key);hot[i].result=result;hot[i].valid=2;}
#endif
  return result;
}
int m3_serializer_cache_ready(void){return installed;}
void m3_serializer_cache_install(so_module *mod){
#if M3_TABLE_WORKER_PROBE >= 5
  (void)mod;installed=1;return; /* Serial typed mapper needs no shared gate. */
#endif
  uintptr_t b=(uintptr_t)mod->load_virtbase,site=b+0x179cdec;
  if(*(uint32_t *)site!=0xe92d4ff0 || *(uint32_t *)(site+4)!=0xe28db01c){
    debugPrintf("[serializer-cache] unsupported attribute query preimage\n");return;
  }
  uintptr_t resume=site+8;so_patch_code(m3_attribute_cache_tramp+12,&resume,4);
  attribute_original=(Fn5)m3_attribute_cache_tramp;
  hook_arm(site,(uintptr_t)attribute);installed=1;
  debugPrintf("[serializer-cache] attribute and metadata startup cache gate enabled\n");
}
#endif
