#ifndef M3_NETWORK_H
#define M3_NETWORK_H
int m3_net_init(void);
extern char m3_server_host[64];
extern unsigned m3_http_port, m3_game_port;
extern int m3_net_local;
#endif
