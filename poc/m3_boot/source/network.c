/* Local-server endpoint configuration. MIT. */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <switch.h>
#include <arpa/inet.h>
#include "dcr_path.h"
#include "network.h"
#include "util.h"
#include "local_server.h"
char m3_server_host[64];
unsigned m3_http_port, m3_game_port;
int m3_net_local;
int m3_net_init(void) {
  static int state;
  static Mutex lock;
  mutexLock(&lock);
  if (state) { int ready=state>0; mutexUnlock(&lock); return ready; }
  state=-1;
  char path[512];
  snprintf(path,sizeof path,"%s/server.txt",dcr_game_root());
  FILE *f=fopen(path,"r");
  char mode[64]={0};
  if(f){fscanf(f,"%63s",mode);fclose(f);}
  if(!strcmp(mode,"local")){
    m3_net_local=1;strcpy(m3_server_host,"127.0.0.1");m3_http_port=21118;m3_game_port=21119;
    state=m3_local_start() ? 1 : -1;
    debugPrintf("[net] in-process server ready=%d (no BSD sockets)\n",state>0);
    mutexUnlock(&lock);return state>0;
  }
  /* Existing IPv4 PC mode remains explicitly selected by server.txt. */
  f = fopen(path, "r");
  if (!f) { debugPrintf("[net] server.txt absent; transport disabled\n"); mutexUnlock(&lock); return 0; }
  int n = fscanf(f, "%63s %u %u", m3_server_host, &m3_http_port, &m3_game_port);
  fclose(f);
  struct in_addr addr;
  if (n != 3 || inet_pton(AF_INET, m3_server_host, &addr) != 1 ||
      !m3_http_port || m3_http_port > 65535 || !m3_game_port || m3_game_port > 65535) {
    debugPrintf("[net] invalid server.txt (local, or IPv4 HTTP-port TCP-port)\n"); mutexUnlock(&lock); return 0;
  }
  Result rc = socketInitializeDefault();
  debugPrintf("[net] sockets rc=0x%x endpoint=%s:%u/%u\n", rc, m3_server_host, m3_http_port, m3_game_port);
  if (R_FAILED(rc)) { mutexUnlock(&lock); return 0; }
  state = 1;
  mutexUnlock(&lock);
  return 1;
}
