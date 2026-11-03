/* Bounded startup-only diagnostics. No disk writes in measured intervals. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <switch.h>
#include "dcr_path.h"
#include "load_profile.h"
#include "boot_profile.h"
#include "dcr_sched.h"
#define CAP 262144
static char output[CAP];
static size_t used;
static unsigned dropped, running;
static uint64_t origin;
static Mutex lock;
int m3_boot_running(void){return __atomic_load_n(&running,__ATOMIC_RELAXED)!=0;}
void m3_boot_emit(const char *fmt, ...) {
  int saved=errno; char line[384]; va_list ap;
  va_start(ap,fmt); int n=vsnprintf(line,sizeof line,fmt,ap); va_end(ap);
  if(n>0){size_t size=(size_t)n<sizeof line ? (size_t)n:sizeof line-1;
    mutexLock(&lock);
    if(used+size<CAP){memcpy(output+used,line,size);used+=size;}else dropped++;
    mutexUnlock(&lock);
  }
  errno=saved;
}
void m3_boot_mark(const char *name){
  if(!__atomic_load_n(&running,__ATOMIC_RELAXED))return;
  uint64_t now=armGetSystemTick();
  uint64_t cpu=0; Handle thread=threadGetCurHandle();
  Result rc=svcGetInfo(&cpu,InfoType_ThreadTickCount,thread,TickCountInfo_Total);
  if(R_FAILED(rc))rc=svcGetInfo(&cpu,InfoType_ThreadTickCountDeprecated,thread,TickCountInfo_Total);
  s32 priority=-1, ideal=-1; u64 mask=0;
  Result pr=svcGetThreadPriority(&priority,thread);
  Result mr=dcr_thread_get_cores(thread,&ideal,&mask);
  m3_boot_emit("[boot] at_us=%llu thread=%u event=%s cpu_total_us=%llu cpu_rc=%u core=%u priority=%d ideal=%d mask=%llx priority_rc=%u mask_rc=%u\n",
    (unsigned long long)(armTicksToNs(now-origin)/1000),thread,name,(unsigned long long)(armTicksToNs(cpu)/1000),rc,
    (unsigned)svcGetCurrentProcessorNumber(),(int)priority,(int)ideal,(unsigned long long)mask,pr,mr);
}
void m3_boot_init(void){
  origin=armGetSystemTick();used=dropped=0;
  __atomic_store_n(&running,1,__ATOMIC_RELAXED);
  m3_asset_load_begin();m3_boot_mark("main-enter");
}
void m3_boot_frame(unsigned frame,unsigned long long ns){
  if(__atomic_load_n(&running,__ATOMIC_RELAXED) && (frame<8 || ns>=1000000000ULL))
    {m3_boot_emit("[boot-frame] index=%u duration_us=%llu\n",frame,ns/1000);m3_boot_mark("frame-return");}
}
void m3_boot_finish(const char *reason){
  if(!__atomic_exchange_n(&running,0,__ATOMIC_RELAXED))return;
  extern void m3_boot_ui_finish(void);
  m3_boot_ui_finish();
  m3_boot_emit("[boot] at_us=%llu event=%s\n",
    (unsigned long long)(armTicksToNs(armGetSystemTick()-origin)/1000),reason);
  m3_asset_load_end_to(m3_boot_emit);
  extern void m3_table_profile_summary(void);
  m3_table_profile_summary();
  char path[512];snprintf(path,sizeof path,"%s/startup-profile.log",dcr_game_root());
  /* Flush after timing ends. No shared debug ring; at most one bounded write. */
  mutexLock(&lock);FILE *f=fopen(path,"w");
  if(f){fwrite(output,1,used,f);fprintf(f,"[boot-summary] dropped=%u phase0=pre-render phase1=first-two-frames phase2=remaining-to-login\n",dropped);fclose(f);}
  mutexUnlock(&lock);
}
