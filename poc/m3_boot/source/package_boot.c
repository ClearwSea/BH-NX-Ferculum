#if M3_STANDALONE
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "rt_settings.h"
#include "package_install.h"
#include "package_identity.h"
static uint64_t last_update;
static int progress(const char *name,uint64_t bytes,void *unused){
 (void)unused;
 if(!appletMainLoop())return 0;
 uint64_t now=armGetSystemTick();if(armTicksToNs(now-last_update)>250000000ULL){
  printf("\x1b[6;1H\x1b[J%.110s\n%llu MiB\n",name,(unsigned long long)(bytes/1048576));consoleUpdate(NULL);last_update=now;
 }
 return 1;
}
static int read_id(const char *path,char out[65]){
 FILE *f=fopen(path,"rb");if(!f)return 0;size_t n=fread(out,1,64,f);int tail=fgetc(f);fclose(f);out[64]=0;
 if(n!=64 || (tail!=EOF&&tail!='\n'))return 0;
 for(unsigned i=0;i<64;i++)if(!((out[i]>='0'&&out[i]<='9')||(out[i]>='a'&&out[i]<='f')))return 0;
 return 1;
}
int m3_package_prepare(void){
 char id[65],installed[65],error[512]="Cannot mount installed resource package.";
 const char *stamp="sdmc:" PORT_ROOT_PATH "/package-ready.txt";
 /* Both installed and gamecard sources use the same prepared SD tree. */
 if(read_id(stamp,installed)&&!strcmp(installed,M3_PACKAGE_ID))return 0;
 Result mounted=romfsMountFromCurrentProcess("pkg");
 int valid=R_SUCCEEDED(mounted)&&read_id("pkg:/package.id",id);
 if(valid&&read_id(stamp,installed)&&!strcmp(id,installed)){romfsUnmount("pkg");return 0;}
 consoleInit(NULL);printf("Houkai 3rd 2017\nFirst launch: preparing game files.\nKeep the console powered on.\nExisting saves will be preserved.\n");consoleUpdate(NULL);
 int rc=-1;
 if(valid){
  FILE *f=fopen("pkg:/manifest.tsv","rb");Sha256Context h;unsigned char b[4096],hash[32];sha256ContextCreate(&h);int ok=f!=NULL;
  if(f){size_t n;while((n=fread(b,1,sizeof b,f)))sha256ContextUpdate(&h,b,n);if(ferror(f))ok=0;fclose(f);}
  sha256ContextGetHash(&h,hash);char actual[65];for(unsigned i=0;i<32;i++)sprintf(actual+2*i,"%02x",hash[i]);
  if(ok&&!strcmp(actual,id))rc=m3_install_tree("pkg:","sdmc:" PORT_ROOT_PATH,progress,NULL,error);
  else snprintf(error,sizeof error,"Package manifest check failed.");
 }
 if(!rc){
  FILE *f=fopen("sdmc:" PORT_ROOT_PATH "/package-ready.installpart","wb");
  int ok=f!=NULL;if(f){if(fwrite(id,1,64,f)!=64)ok=0;if(fclose(f))ok=0;}
  if(!ok||rename("sdmc:" PORT_ROOT_PATH "/package-ready.installpart",stamp)){rc=-1;snprintf(error,sizeof error,"Cannot commit installation marker.");}
 }
 printf("\x1b[6;1H\x1b[J%s\n\nPress A to return to HOME.\n",rc?error:"Ready. Launch the game again from HOME.");consoleUpdate(NULL);
 PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
 while(appletMainLoop()){padUpdate(&pad);if(padGetButtonsDown(&pad)&HidNpadButton_A)break;consoleUpdate(NULL);svcSleepThread(16000000);}
 consoleExit(NULL);if(R_SUCCEEDED(mounted))romfsUnmount("pkg");return rc?-1:1;
}
#endif
