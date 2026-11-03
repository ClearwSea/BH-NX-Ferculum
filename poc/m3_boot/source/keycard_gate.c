/* Optional startup-only card gate. No card data/certificate is read or copied. */
#if M3_KEYCARD_GATE
#include <switch.h>
#include <stdio.h>
int m3_keycard_gate(void){
  u64 ids[16]={0};s32 count=0;int match=0;
  Result rc=nsInitialize();
  if(R_SUCCEEDED(rc)){
    rc=nsListApplicationIdOnGameCard(ids,16,&count);
    if(R_SUCCEEDED(rc)&&count>0&&count<=16)
      for(s32 i=0;i<count;i++)if(ids[i]==0x01000C4015030000ULL)match=1;
    nsExit();
  }
  if(match)return 0;
  /* A rejected launch exits, so this console never hands its window to EGL. */
  consoleInit(NULL);
  printf("Houkai 3rd 2017 - keycard test\n\nInsert Power Pro Kun Pocket R (Japan).\nClose and restart this application after inserting it.\n\nRequired title: 01000C4015030000\nCard query result: %08X\n\nPress A to return to HOME.\n",rc);
  PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
  while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_A)break;consoleUpdate(NULL);svcSleepThread(16000000);}
  consoleExit(NULL);return 1;
}
#endif
