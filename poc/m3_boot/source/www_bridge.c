/* Local HTTP on a worker; Unity callbacks stay on the render thread. MIT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <switch.h>
#include "jni.h"
#include "network.h"
#include "util.h"
#include "local_server.h"
#define WWW "com/unity3d/player/WWW"
#define RESPONSE_LIMIT (2 * 1024 * 1024)
typedef struct Request {
  struct Request *next;
  JObj *owner;
  jint id;
  int started;
  char *url, *post, *response;
  size_t post_len, response_len;
  char error[160];
} Request;
static Request *pending, *pending_tail, *complete, *complete_tail;
static Mutex lock;
static CondVar wake;
static Thread worker;
static int worker_ready;

static int send_all(int fd, const char *data, size_t size) {
  while (size) {
    ssize_t n = send(fd, data, size, 0);
    if (n <= 0) return 0;
    data += n; size -= n;
  }
  return 1;
}
static void finish_response(Request *r, char *raw, size_t used) {
  if(used>RESPONSE_LIMIT){strcpy(r->error,"HTTP response too large");free(raw);return;}
  int status = 0;
  char *body = strstr(raw, "\r\n\r\n");
  if (!r->error[0] && (!body || sscanf(raw, "HTTP/%*s %d", &status) != 1 || status != 200))
    snprintf(r->error, sizeof r->error, "HTTP invalid response/status=%d", status);
  if (!r->error[0]) {
    char *cl = NULL;
    for (char *p = raw; p < body; p++)
      if (!strncasecmp(p, "\r\nContent-Length:", 17)) { cl = p + 17; break; }
    body += 4;
    size_t length = used - (body - raw);
    if (!cl || strtoul(cl, NULL, 10) != length) strcpy(r->error, "HTTP content length mismatch");
    else { memmove(raw, body, length); r->response = raw; r->response_len = length; raw = NULL; }
  }
  free(raw);
}
static void fetch(Request *r) {
  const char *authority = strstr(r->url, "://");
  const char *path = authority ? strchr(authority + 3, '/') : NULL;
  char expected[100];
  snprintf(expected, sizeof expected, "http://%s:%u/", m3_server_host, m3_http_port);
  int dispatch = (!strncmp(r->url, "https://global1.bh3.com/query_dispatch", 36) ||
                  !strncmp(r->url, "https://global2.bh3.com/query_dispatch", 36));
  if (!path || (!dispatch && strncmp(r->url, expected, strlen(expected)))) {
    snprintf(r->error, sizeof r->error, "NX: URL outside configured local server"); return;
  }
  if(m3_net_local){
    char *raw=NULL;size_t size=0;
    if(!m3_local_http(path,r->post,r->post_len,&raw,&size)){strcpy(r->error,"NX internal HTTP failed");return;}
    finish_response(r,raw,size);return;
  }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) { snprintf(r->error, sizeof r->error, "socket errno=%d", errno); return; }
  struct timeval timeout = {5, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET; addr.sin_port = htons(m3_http_port);
  inet_pton(AF_INET, m3_server_host, &addr.sin_addr);
  char header[2048];
  int len = snprintf(header, sizeof header,
      "%s %s HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %lu\r\n\r\n",
      r->post ? "POST" : "GET", path, m3_server_host, m3_http_port, (unsigned long)r->post_len);
  if (len < 0 || len >= (int)sizeof header || connect(fd, (struct sockaddr *)&addr, sizeof addr) ||
      !send_all(fd, header, len) || (r->post_len && !send_all(fd, r->post, r->post_len))) {
    snprintf(r->error, sizeof r->error, "HTTP connect/send errno=%d", errno); close(fd); return;
  }
  char *raw = malloc(RESPONSE_LIMIT + 1);
  size_t used = 0;
  if (!raw) { strcpy(r->error, "HTTP response allocation failed"); close(fd); return; }
  for (;;) {
    ssize_t n = recv(fd, raw + used, RESPONSE_LIMIT - used, 0);
    if (n < 0) { snprintf(r->error, sizeof r->error, "HTTP receive errno=%d", errno); break; }
    if (!n) break;
    used += n;
    if (used == RESPONSE_LIMIT) { strcpy(r->error, "HTTP response too large"); break; }
  }
  close(fd); raw[used] = 0;
  finish_response(r,raw,used);
}
static void work(void *arg) {
  (void)arg;
  for (;;) {
    mutexLock(&lock);
    while (!pending) condvarWait(&wake, &lock);
    Request *r = pending;
    pending = r->next; if (!pending) pending_tail = NULL;
    mutexUnlock(&lock);
    fetch(r);
    mutexLock(&lock);
    r->next = NULL;
    if (complete_tail) complete_tail->next = r; else complete = r;
    complete_tail = r;
    mutexUnlock(&lock);
  }
}
static void release_request(JObj *self) {
  Request *r = self->p;
  free(r->url); free(r->post); free(r->response); free(r);
}
jvalue m3_www_init(JObj *self, const jvalue *a, const JMethod *m) {
  (void)m;
  Request *r = calloc(1, sizeof *r);
  if (!r) { jni_throw("java/lang/OutOfMemoryError", "WWW allocation failed"); return jv_none(); }
  r->id = a[0].i; r->url = strdup(jni_utf(a[1].l));
  JObj *post = a[2].l;
  if (post && post->kind == JK_ARRAY) {
    r->post_len = post->a.len; r->post = malloc(r->post_len + 1);
    if (r->post) memcpy(r->post, post->a.data, r->post_len);
    else strcpy(r->error, "HTTP POST allocation failed");
  }
  self->p = r; self->finalize = release_request;
  /* Exclude URL queries, which may contain account values. */
  debugPrintf("[www] create id=%d path=%.*s\n", r->id, (int)strcspn(r->url, "?"), r->url);
  return jv_none();
}
jvalue m3_www_start(JObj *self, const jvalue *a, const JMethod *m) {
  (void)a; (void)m;
  Request *r = self->p;
  if (!r) return jv_none();
  mutexLock(&lock);
  if (!worker_ready && m3_net_init()) {
    Result rc = threadCreate(&worker, work, NULL, NULL, 64 * 1024, 44, -2);
    if (R_SUCCEEDED(rc)) rc = threadStart(&worker);
    worker_ready = R_SUCCEEDED(rc);
  }
  if (!r->started) {
    r->started = 1; r->owner = jni_retain(self);
    if (!worker_ready || r->error[0]) {
      if (!r->error[0]) strcpy(r->error, "NX local transport unavailable");
      if (complete_tail) complete_tail->next = r; else complete = r;
      complete_tail = r;
    } else {
      if (pending_tail) pending_tail->next = r; else pending = r;
      pending_tail = r; condvarWakeOne(&wake);
    }
  }
  mutexUnlock(&lock);
  return jv_none();
}
void m3_www_pump(void) {
  typedef void (*Error)(void *, void *, jint, void *);
  typedef jboolean (*Header)(void *, void *, jint, void *);
  typedef jboolean (*Read)(void *, void *, jint, void *, jint);
  typedef void (*Done)(void *, void *, jint);
  Error error = (Error)jni_native(WWW, "errorCallback");
  Header header = (Header)jni_native(WWW, "headerCallback");
  Read read = (Read)jni_native(WWW, "readCallback");
  Done done = (Done)jni_native(WWW, "doneCallback");
  if (!error || !header || !read || !done) return;
  void *cls = jni_class(WWW)->obj;
  mutexLock(&lock);
  Request *r = complete; complete = complete_tail = NULL;
  mutexUnlock(&lock);
  while (r) {
    Request *next = r->next;
    JObj *owner = r->owner;
    if (r->error[0]) {
      JObj *message = jni_str(r->error);
      debugPrintf("[www] failed id=%d: %s\n", r->id, r->error);
      error(g_jni_env, cls, r->id, message); jni_release(message);
    } else {
      JObj *h = jni_str_fmt("content-length: %lu\n\r", (unsigned long)r->response_len);
      int abort = header(g_jni_env, cls, r->id, h); jni_release(h);
      h = jni_str("content-type: application/json\n\r");
      if (!abort) abort = header(g_jni_env, cls, r->id, h);
      jni_release(h);
      JObj *buf = jni_array('B', 32768);
      if (!abort) abort = read(g_jni_env, cls, r->id, buf, 0);
      for (size_t pos = 0; !abort && pos < r->response_len;) {
        size_t n = r->response_len - pos; if (n > 32768) n = 32768;
        memcpy(buf->a.data, r->response + pos, n);
        abort = read(g_jni_env, cls, r->id, buf, n); pos += n;
      }
      jni_release(buf);
      if (!abort) done(g_jni_env, cls, r->id);
      debugPrintf("[www] HTTP id=%d bytes=%lu abort=%d\n", r->id, (unsigned long)r->response_len, abort);
    }
    jni_release(owner); r = next;
  }
}
