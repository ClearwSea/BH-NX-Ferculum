/* APK decoder fd-path compatibility and bounded sharedassets0 tracing. MIT.
 * Only fopen/fclose handles are tracked; untracked fd queries retain the
 * runtime's existing behavior. This is not a general /proc filesystem. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <switch.h>
#include "so_util.h"
#include "util.h"
#include "apk_stdio_buffer.h"

void *__real_b_fopen(const char *, const char *);
size_t __real_b_fread(void *, size_t, size_t, void *);
int __real_b_fclose(void *);
ssize_t __real_b_readlink(const char *, char *, size_t);
static Mutex trace_lock;
static struct { void *fp; int fd; char *path; } handles[128];
static unsigned reads;
#include "load_profile.h"

void *__wrap_b_fopen(const char *path, const char *mode) {
  uint64_t start = m3_load_enter(LOAD_OPEN);
  void *fp = __real_b_fopen(path, mode);
  m3_load_leave(start, 0);
  m3_apk_stdio_buffer((FILE *)fp,path,mode);
  if (fp && path) {
    mutexLock(&trace_lock);
    for (unsigned i = 0; i < 128; i++)
      if (!handles[i].fp) {
        handles[i].fp = fp;
        handles[i].fd = fileno((FILE *)fp);
        handles[i].path = strdup(path);
        break;
      }
    mutexUnlock(&trace_lock);
  }
  return fp;
}

size_t __wrap_b_fread(void *p, size_t size, size_t count, void *fp) {
  int trace = 0;
  mutexLock(&trace_lock);
  for (unsigned i = 0; i < 128; i++)
    if (handles[i].fp == fp && handles[i].path &&
        strstr(handles[i].path, "/sharedassets0.assets") && reads < 40) {
      trace = 1; reads++; break;
    }
  mutexUnlock(&trace_lock);
  long pos = trace ? ftell((FILE *)fp) : -1;
  uint64_t start = m3_load_enter(LOAD_READ);
  size_t n = __real_b_fread(p, size, count, fp);
  m3_load_leave(start, (uint64_t)n * size);
  if (trace) {
    uintptr_t lr = (uintptr_t)__builtin_return_address(0);
    so_module *m = so_find_module_by_addr((void *)lr);
    char hex[65];
    size_t bytes = n * size;
    if (bytes > 32) bytes = 32;
    for (size_t i = 0; i < bytes; i++)
      snprintf(hex + i * 2, 3, "%02x", ((unsigned char *)p)[i]);
    hex[bytes * 2] = 0;
    debugPrintf("[asset-read] offset=%ld size=%lu count=%lu got=%lu caller=%s+0x%lx head=%s\n",
                pos, (unsigned long)size, (unsigned long)count, (unsigned long)n,
                m ? m->base_name : "host",
                (unsigned long)(m ? lr - (uintptr_t)m->load_virtbase : lr), hex);
  }
  return n;
}

int __wrap_b_fclose(void *fp) {
  mutexLock(&trace_lock);
  for (unsigned i = 0; i < 128; i++)
    if (handles[i].fp == fp) {
      free(handles[i].path);
      memset(&handles[i], 0, sizeof handles[i]);
    }
  mutexUnlock(&trace_lock);
  uint64_t start = m3_load_enter(LOAD_CLOSE);
  int result = __real_b_fclose(fp);
  m3_load_leave(start, 0);
  return result;
}

int __real_b_fseek(void *, long, int);
int __real_b_fseeko(void *, int32_t, int);
struct b_stat;
int __real_b_stat(const char *, struct b_stat *);
int __wrap_b_fseek(void *fp, long offset, int whence) {
  uint64_t start = m3_load_enter(LOAD_SEEK);
  int result = __real_b_fseek(fp, offset, whence);
  m3_load_leave(start, 0); return result;
}
int __wrap_b_fseeko(void *fp, int32_t offset, int whence) {
  uint64_t start = m3_load_enter(LOAD_SEEK);
  int result = __real_b_fseeko(fp, offset, whence);
  m3_load_leave(start, 0); return result;
}
int __wrap_b_stat(const char *path, struct b_stat *out) {
  uint64_t start = m3_load_enter(LOAD_STAT);
  int result = __real_b_stat(path, out);
  m3_load_leave(start, 0); return result;
}

/* libulua's original stream decoder identifies the file via /proc/self/fd.
 * Preserve Android-visible names, not the translated Switch SD path. */
ssize_t __wrap_b_readlink(const char *path, char *buf, size_t cap) {
  const char prefix[] = "/proc/self/fd/";
  if (path && buf && cap && !strncmp(path, prefix, sizeof prefix - 1)) {
    char *end;
    long fd = strtol(path + sizeof prefix - 1, &end, 10);
    if (end != path + sizeof prefix - 1 && !*end && fd >= 0) {
      mutexLock(&trace_lock);
      for (unsigned i = 0; i < 128; i++) {
        if (handles[i].fp && handles[i].fd == fd && handles[i].path) {
          size_t len = strlen(handles[i].path);
          if (len > cap) len = cap;
          memcpy(buf, handles[i].path, len);
          static unsigned shown;
          if (shown++ < 12)
            debugPrintf("[asset-fd] %s -> %s (FILE fd=%d raw14=%d)\n", path,
                        handles[i].path, handles[i].fd,
                        *(const short *)((const char *)handles[i].fp + 14));
          mutexUnlock(&trace_lock);
          return (ssize_t)len;
        }
      }
      mutexUnlock(&trace_lock);
    }
  }
  return __real_b_readlink(path, buf, cap);
}
