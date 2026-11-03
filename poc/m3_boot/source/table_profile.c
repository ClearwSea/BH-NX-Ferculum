/* Observe existing managed runtime_invoke calls without changing table semantics.
 * Only startup metadata ReadAllText -> Parse -> TryDeserialize is measured.
 * Original API catches managed exceptions; this wrapper preserves its result,
 * four arguments, out parameters and errno. No object cache, UI or file I/O. */
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <switch.h>
#include "so_util.h"
#include "boot_profile.h"
#include "util.h"
typedef void *(*Invoke)(void *,void *,void **,void **);
static Invoke original_invoke;
static uintptr_t module_base;
static Mutex table_lock;
static struct {Handle thread;unsigned state;char name[64];} slots[16];
static unsigned measured,overflow;
extern int m3_serializer_begin(void);
extern void m3_serializer_end(const char *name);
extern int m3_serializer_install(so_module *mod);
extern unsigned char m3_tramp_table_invoke[];
static uint64_t cpu_now(unsigned *rc) {
  uint64_t value=0;*rc=svcGetInfo(&value,InfoType_ThreadTickCount,threadGetCurHandle(),TickCountInfo_Total);
  if(R_FAILED(*rc))*rc=svcGetInfo(&value,InfoType_ThreadTickCountDeprecated,threadGetCurHandle(),TickCountInfo_Total);
  return armTicksToNs(value)/1000;
}
static int table_name(void *obj,char out[64]) {
  if(!obj)return 0;
  const uint32_t *w=obj;unsigned n=w[2];if(!n||n>240)return 0;
  const uint16_t *s=(const uint16_t *)((char *)obj+12);char path[241];
  for(unsigned i=0;i<n;i++)path[i]=(s[i]>=32&&s[i]<127)?s[i]:'?';
  path[n]=0;
  const char *p=strstr(path,"/_exceloutputasset/");if(!p)return 0;p+=sizeof("/_exceloutputasset/")-1;
  unsigned len=strlen(p);if(!len||len>=64)return 0;
  memcpy(out,p,len+1);return 1;
}
static void *table_invoke(void *method,void *instance,void **args,void **exception) {
  if(!m3_boot_running() || !method)return original_invoke(method,instance,args,exception);
  uintptr_t target=*(uintptr_t *)method-module_base;
  unsigned kind=target==0x26b4448?1:target==0x1784220?2:target==0x178d6b0?3:0;
  if(!kind)return original_invoke(method,instance,args,exception);
  int incoming_errno=errno;char name[64]={0};
  if(kind==1 && (!args||!table_name(args[0],name))) {
    /* An intervening unrelated ReadAllText breaks the association. */
    Handle thread=threadGetCurHandle();mutexLock(&table_lock);
    for(unsigned i=0;i<16;i++)if(slots[i].thread==thread)slots[i].state=0;
    mutexUnlock(&table_lock);errno=incoming_errno;
    return original_invoke(method,instance,args,exception);
  }
  Handle thread=threadGetCurHandle();int slot=-1;unsigned match=0;
  mutexLock(&table_lock);
  for(unsigned i=0;i<16;i++)if(slots[i].thread==thread){slot=(int)i;break;}
  if(slot<0 && kind==1)for(unsigned i=0;i<16;i++)if(!slots[i].thread){slot=(int)i;slots[i].thread=thread;break;}
  if(slot>=0){
    if(kind==1){memcpy(slots[slot].name,name,64);slots[slot].state=1;match=1;}
    else if(slots[slot].state==kind-1){memcpy(name,slots[slot].name,64);match=1;}
  }else if(kind==1)overflow++;
  /* Reserve a phase before calling original code: recursive invokes cannot
   * accidentally inherit the same expected phase. No lock crosses native code. */
  if(match)slots[slot].state=0;
  unsigned sequence=match?++measured:0;
  mutexUnlock(&table_lock);
  if(!match || sequence>1024){errno=incoming_errno;return original_invoke(method,instance,args,exception);}
  unsigned rc0,rc1;uint64_t c0=cpu_now(&rc0),t0=armGetSystemTick();
  int serializer_scope=0;
#if !M3_TABLE_WORKER_PROBE
  serializer_scope=kind==3?m3_serializer_begin():0;
#endif
  errno=incoming_errno;
  if(kind==1){extern void m3_boot_ui_stage(const char *);m3_boot_ui_stage("正在读取配置");}
  void *result;
#if M3_TABLE_WORKER_PROBE
  extern void *m3_table_worker_invoke(Invoke,void *,void *,void **,void **,const char *);
  result=kind==3?m3_table_worker_invoke(original_invoke,method,instance,args,exception,name):original_invoke(method,instance,args,exception);
#else
  result=original_invoke(method,instance,args,exception);
#endif
  int saved=errno;uint64_t t1=armGetSystemTick(),c1=cpu_now(&rc1);
  unsigned failed=exception && *exception;
  if(serializer_scope)m3_serializer_end(name);
  mutexLock(&table_lock);slots[slot].state=(!failed && result && kind<3)?kind:0;mutexUnlock(&table_lock);
  m3_boot_emit("[table-phase] seq=%u thread=%u name=%s kind=%u wall_us=%llu cpu_us=%llu cpu_rc=%u exception=%u result=%u\n",
    sequence,thread,name,kind,(unsigned long long)(armTicksToNs(t1-t0)/1000),
    (unsigned long long)((rc0|rc1)||c1<c0?0:c1-c0),rc0|rc1,failed,result!=0);
  errno=saved;return result;
}
void m3_table_profile_summary(void){m3_boot_emit("[table-summary] measured=%u limit=1024 slot_overflow=%u kinds=1:read,2:parse,3:deserialize\n",measured,overflow);}
int m3_table_profile_install(so_module *mod) {
  uintptr_t b=(uintptr_t)mod->load_virtbase;
  const struct {unsigned offset,a,c;} guards[]={
    {0x2c3d4e4,0xe590c00c,0xe1dccbb0},
    {0x26b4448,0xe92d4c70,0xe28db010},
    {0x1784220,0xe92d4df0,0xe28db018},
    {0x178d6b0,0xe92d4c10,0xe28db008}};
  for(unsigned i=0;i<sizeof guards/sizeof *guards;i++)if(*(uint32_t *)(b+guards[i].offset)!=guards[i].a || *(uint32_t *)(b+guards[i].offset+4)!=guards[i].c){
    debugPrintf("[table-profile] unsupported preimage %x; no hook installed\n",guards[i].offset);return 0;}
  module_base=b;uintptr_t resume=b+0x2c3d4ec;
  so_patch_code(m3_tramp_table_invoke+12,&resume,4);
  original_invoke=(Invoke)m3_tramp_table_invoke;
  hook_arm(b+0x2c3d4e4,(uintptr_t)table_invoke);
  m3_serializer_install(mod);
#if M3_TABLE_WORKER_PROBE
  extern void m3_table_worker_install(so_module *);
  m3_table_worker_install(mod);
#endif
  debugPrintf("[table-profile] startup read/parse/deserialize timing installed; no UI\n");return 1;
}
