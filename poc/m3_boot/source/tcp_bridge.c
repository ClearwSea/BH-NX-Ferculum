/* Linux/Bionic IPv4 TCP ABI -> libnx sockets, configured test server only. MIT. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdint.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "bionic.h"
#include "dcr_net.h"
#include "network.h"
#include "util.h"
#include "local_server.h"

/* Raw descriptors remain unique in newlib's shared file table and fit the
 * engine's 1024-bit fd_set; no synthetic high fd range is necessary. */
static unsigned char owned[1024];
int port_net_owns(int fd) { return fd >= 0 && fd < 1024 && owned[fd]; }
static int fix(int rc) { if (rc < 0) b_fix_errno(); return rc; }
int port_net_socket(int domain, int type, int proto) {
  if (domain != 2 || (type & 15) != 1 || !m3_net_init()) return -1;
  int fd = m3_net_local ? m3_local_socket(type & 0x800) : socket(AF_INET, SOCK_STREAM, proto);
  if (fd < 0) return -1;
  if (fd >= 1024) { close(fd); return -1; }
  owned[fd] = 1;
  if (!m3_net_local && (type & 0x800)) fcntl(fd, F_SETFL, O_NONBLOCK);
  debugPrintf("[tcp] socket fd=%d\n", fd);
  return fd;
}
int port_net_close(int fd) { owned[fd] = 0; return fix(m3_net_local ? m3_local_close(fd) : close(fd)); }
int port_net_fcntl(int fd, int cmd, long arg) {
  if(m3_net_local)return fix(m3_local_fcntl(fd,cmd,arg));
  if (cmd == 3) { int f = fcntl(fd, F_GETFL); return f < 0 ? fix(f) : (f & O_NONBLOCK ? 0x800 : 0); }
  if (cmd == 4) return fix(fcntl(fd, F_SETFL, arg & 0x800 ? O_NONBLOCK : 0));
  if (cmd == 1) return 0;
  if (cmd == 2) return 0; /* process-private descriptor; no exec */
  b_set_errno(L_EINVAL); return -1;
}
int port_net_ioctl(int fd, unsigned long req, void *arg) {
  if(m3_net_local)return fix(m3_local_ioctl(fd,req,arg));
  if (req == 0x5421) return fix(ioctl(fd, FIONBIO, arg));
  if (req == 0x541b) return fix(ioctl(fd, FIONREAD, arg));
  b_set_errno(L_EINVAL); return -1;
}
short port_net_ready(int fd, short events) {
  if(m3_net_local)return m3_local_ready(fd,events);
  struct pollfd p = {fd, 0, 0};
  if (events & 1) p.events |= POLLIN;
  if (events & 4) p.events |= POLLOUT;
  if (poll(&p, 1, 0) < 0) return 8;
  return (p.revents & POLLIN ? 1 : 0) | (p.revents & POLLOUT ? 4 : 0) |
         (p.revents & POLLERR ? 8 : 0) | (p.revents & POLLHUP ? 16 : 0) |
         (p.revents & POLLNVAL ? 32 : 0);
}
int __real_b_connect(int, const void *, unsigned);
int __wrap_b_connect(int fd, const void *raw, unsigned len) {
  const unsigned char *trace = raw;
  if(trace && len>=8)
    debugPrintf("[tcp] connect-attempt fd=%d owned=%d family=%u length=%u target=%u.%u.%u.%u:%u expected=%s:%u\n",
                fd,port_net_owns(fd),(unsigned)trace[0]|((unsigned)trace[1]<<8),len,
                trace[4],trace[5],trace[6],trace[7],((unsigned)trace[2]<<8)|trace[3],
                m3_server_host,m3_game_port);
  if (!port_net_owns(fd)) return __real_b_connect(fd, raw, len);
  const unsigned char *p = raw;
  if (!p || len < 16 || p[0] != 2 || p[1]) { b_set_errno(L_EAFNOSUPPORT); return -1; }
  struct sockaddr_in a = {0};
  a.sin_len = sizeof a; a.sin_family = AF_INET;
  memcpy(&a.sin_port, p + 2, 2); memcpy(&a.sin_addr, p + 4, 4);
  struct in_addr expected; inet_pton(AF_INET, m3_server_host, &expected);
  if (a.sin_addr.s_addr != expected.s_addr || ntohs(a.sin_port) != m3_game_port) {
    b_set_errno(L_ENETUNREACH); return -1;
  }
  int rc = m3_net_local ? m3_local_connect(fd) : connect(fd, (struct sockaddr *)&a, sizeof a);
  int e = rc < 0 ? b_errno_to_linux(errno) : 0;
  debugPrintf("[tcp] connect fd=%d rc=%d linux_errno=%d\n", fd, rc, e);
  if (rc < 0) b_set_errno(e);
  return rc;
}
static int flags(int f) { return (f & 2 ? MSG_PEEK : 0) | (f & 0x40 ? MSG_DONTWAIT : 0); }
ssize_t __real_b_send(int, const void *, size_t, int);
ssize_t __real_b_recv(int, void *, size_t, int);
ssize_t __wrap_b_send(int fd, const void *buf, size_t n, int f) {
  if (!port_net_owns(fd)) return __real_b_send(fd, buf, n, f);
  ssize_t rc = m3_net_local ? m3_local_send(fd,buf,n,f) : send(fd, buf, n, flags(f)); if (rc < 0) b_fix_errno(); return rc;
}
ssize_t __wrap_b_recv(int fd, void *buf, size_t n, int f) {
  if (!port_net_owns(fd)) return __real_b_recv(fd, buf, n, f);
  ssize_t rc = m3_net_local ? m3_local_recv(fd,buf,n,f) : recv(fd, buf, n, flags(f)); if (rc < 0) b_fix_errno(); return rc;
}
ssize_t port_net_sendto(int fd, const void *b, size_t n, int f, const void *a, unsigned len) {
  if (a || len) { b_set_errno(L_EOPNOTSUPP); return -1; }
  return __wrap_b_send(fd, b, n, f);
}
ssize_t port_net_recvfrom(int fd, void *b, size_t n, int f, void *a, unsigned *len) {
  if (a) { b_set_errno(L_EOPNOTSUPP); return -1; }
  if (len) *len = 0;
  return __wrap_b_recv(fd, b, n, f);
}
int __real_b_getsockopt(int, int, int, void *, unsigned *);
int __wrap_b_getsockopt(int fd, int level, int option, void *value, unsigned *len) {
  if (!port_net_owns(fd)) return __real_b_getsockopt(fd, level, option, value, len);
  if (level != 1 || (option != 4 && option != 3)) { b_set_errno(L_EOPNOTSUPP); return -1; }
  if(m3_net_local)return fix(m3_local_getsockopt(fd,option,value,len));
  socklen_t size = len ? *len : 0;
  int rc = getsockopt(fd, SOL_SOCKET, option == 4 ? SO_ERROR : SO_TYPE, value, &size);
  if (len) *len = size;
  if (!rc && option == 4 && value && size == 4) *(int *)value = b_errno_to_linux(*(int *)value);
  return fix(rc);
}
int __real_b_setsockopt(int, int, int, const void *, unsigned);
static int traced_setsockopt(int fd,int level,int option,const void *value,unsigned len) {
  int rc=setsockopt(fd,level,option,value,len);
  debugPrintf("[tcp] native setsockopt fd=%d level=%d option=%d len=%u rc=%d errno=%d\n",
              fd,level,option,len,rc,rc<0 ? errno:0);
  return rc;
}
int __wrap_b_setsockopt(int fd, int level, int option, const void *value, unsigned len) {
  debugPrintf("[tcp] Bionic setsockopt fd=%d owned=%d level=%d option=%d len=%u\n",
              fd,port_net_owns(fd),level,option,len);
  if (!port_net_owns(fd)) return __real_b_setsockopt(fd, level, option, value, len);
  if(m3_net_local)return fix(m3_local_setsockopt(fd,level,option,value,len));
  if (level == 6 && option == 1) return fix(traced_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, value, len));
  if (level == 1 && (option == 20 || option == 21)) {
    /* Bionic ARM timeval is two signed 32-bit words; convert to host ABI. */
    int32_t parts[2];
    if (!value || len != sizeof parts) { b_set_errno(L_EINVAL); return -1; }
    memcpy(parts, value, sizeof parts);
    struct timeval tv = {parts[0], parts[1]};
    return fix(traced_setsockopt(fd, SOL_SOCKET, option == 20 ? SO_RCVTIMEO : SO_SNDTIMEO, &tv, sizeof tv));
  }
  int opt = -1;
  if (level == 1) {
    switch (option) {
      case 2: opt = SO_REUSEADDR; break;
      case 7: opt = SO_SNDBUF; break;
      case 8: opt = SO_RCVBUF; break;
      case 9: opt = SO_KEEPALIVE; break;
    }
  }
  if (opt < 0) { debugPrintf("[tcp] unsupported setsockopt %d/%d\n", level, option); b_set_errno(L_EOPNOTSUPP); return -1; }
  return fix(traced_setsockopt(fd, SOL_SOCKET, opt, value, len));
}
int __real_b_shutdown(int, int);
int __wrap_b_shutdown(int fd, int how) {
  if(port_net_owns(fd) && m3_net_local)return fix(m3_local_shutdown(fd,how));
  return port_net_owns(fd) ? fix(shutdown(fd, how)) : __real_b_shutdown(fd, how);
}
