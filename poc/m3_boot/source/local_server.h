#ifndef M3_LOCAL_SERVER_H
#define M3_LOCAL_SERVER_H
#include <stddef.h>
#include <sys/types.h>
int m3_local_start(void);
int m3_local_socket(int nonblock);
int m3_local_connect(int fd);
int m3_local_close(int fd);
int m3_local_fcntl(int fd, int cmd, long arg);
int m3_local_ioctl(int fd, unsigned long req, void *arg);
short m3_local_ready(int fd, short events);
ssize_t m3_local_send(int fd, const void *buf, size_t n, int flags);
ssize_t m3_local_recv(int fd, void *buf, size_t n, int flags);
int m3_local_getsockopt(int fd, int option, void *value, unsigned *len);
int m3_local_setsockopt(int fd, int level, int option, const void *value, unsigned len);
int m3_local_shutdown(int fd, int how);
int m3_local_http(const char *path, const void *post, size_t post_len, char **raw, size_t *size);
#endif
