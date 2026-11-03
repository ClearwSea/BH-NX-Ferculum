/* Forward exact bionic/libnx ABIs. No file handles or data are substituted. */
#include <switch.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdarg.h>
#include "bionic.h"
#include "load_profile.h"
int __real_b_open(const char *, int, ...);
ssize_t __real_b_read(int, void *, size_t);
ssize_t __real_b_pread(int, void *, size_t, b_off_t);
size_t __real_b_pread_all(int, void *, size_t, b_off64_t);
void *__real_b_mmap(void *, size_t, int, int, int, b_off_t);
void *__real_b_fdopen(int, const char *);
Result __real_fsFileRead(FsFile *, s64, void *, u64, u32, u64 *);

int __wrap_b_open(const char *path, int flags, ...) {
  int mode = 0;
  if (flags & L_O_CREAT) {
    va_list ap; va_start(ap, flags); mode = va_arg(ap, int); va_end(ap);
  }
  uint64_t t = m3_load_enter(LOAD_OPEN);
  int r = __real_b_open(path, flags, mode);
  m3_load_leave(t, 0); return r;
}
ssize_t __wrap_b_read(int fd, void *buf, size_t size) {
  uint64_t t = m3_load_enter(LOAD_FDREAD);
  ssize_t r = __real_b_read(fd, buf, size);
  m3_load_leave(t, r > 0 ? (uint64_t)r : 0); return r;
}
ssize_t __wrap_b_pread(int fd, void *buf, size_t size, b_off_t off) {
  uint64_t t = m3_load_enter(LOAD_PREAD);
  ssize_t r = __real_b_pread(fd, buf, size, off);
  m3_load_leave(t, r > 0 ? (uint64_t)r : 0); return r;
}
size_t __wrap_b_pread_all(int fd, void *buf, size_t size, b_off64_t off) {
  uint64_t t = m3_load_enter(LOAD_PREAD);
  size_t r = __real_b_pread_all(fd, buf, size, off);
  m3_load_leave(t, r); return r;
}
void *__wrap_b_mmap(void *addr, size_t size, int prot, int flags, int fd, b_off_t off) {
  uint64_t t = m3_load_enter(LOAD_MMAP);
  void *r = __real_b_mmap(addr, size, prot, flags, fd, off);
  /* Mapped length is not read bytes: pread/fsread record actual transfers. */
  m3_load_leave(t, 0); return r;
}
void *__wrap_b_fdopen(int fd, const char *mode) {
  uint64_t t = m3_load_enter(LOAD_FDOPEN);
  void *r = __real_b_fdopen(fd, mode);
  m3_load_leave(t, 0); return r;
}
Result __wrap_fsFileRead(FsFile *f, s64 off, void *buf, u64 size, u32 option, u64 *got) {
  uint64_t t = m3_load_enter(LOAD_FSREAD);
  Result r = __real_fsFileRead(f, off, buf, size, option, got);
  m3_load_leave(t, R_SUCCEEDED(r) && got ? *got : 0); return r;
}

