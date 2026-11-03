/* Match the runtime's guest scheduling policy without its busy-loop selftest. */
#include <stdio.h>
#include "dcr_sched.h"
#include "dcr_path.h"
#include "boot_profile.h"

void m3_startup_sched_init(void) {
  char path[512];
  snprintf(path,sizeof path,"%s/startup-scheduling.txt",dcr_game_root());
  FILE *f=fopen(path,"r");
  int enabled=1;
  if(f){enabled=fgetc(f)!='0';fclose(f);}
  m3_boot_mark("sched-before");
  if(!enabled){m3_boot_emit("[sched-policy] enabled=0\n");return;}
  s32 priority=-1, ideal=-1; u64 mask=0;
  Result rp=svcGetThreadPriority(&priority,CUR_THREAD_HANDLE);
  Result rm=dcr_thread_get_cores(CUR_THREAD_HANDLE,&ideal,&mask);
  if(R_FAILED(rp)||R_FAILED(rm)){
    m3_boot_emit("[sched-policy] skipped query_priority_rc=%u query_mask_rc=%u\n",rp,rm);
    return;
  }
  rm=dcr_thread_set_cores(CUR_THREAD_HANDLE,-3,DCR_GUEST_CORES);
  rp=R_FAILED(rm)?rm:svcSetThreadPriority(CUR_THREAD_HANDLE,DCR_GUEST_PRIO);
  if(R_FAILED(rp)){
    Result restore=dcr_thread_set_cores(CUR_THREAD_HANDLE,ideal,mask);
    m3_boot_emit("[sched-policy] failed priority_rc=%u mask_rc=%u restore_mask_rc=%u\n",rp,rm,restore);
  }else{
    m3_boot_emit("[sched-policy] enabled=1 old_priority=%d old_mask=%llx\n",
      (int)priority,(unsigned long long)mask);
  }
  m3_boot_mark("sched-after");
}
