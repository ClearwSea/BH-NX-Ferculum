/* Keep a real FILE/fd; only change libc read buffering before the first I/O. */
#pragma once
#include <stdio.h>
#include <string.h>
#include <errno.h>
static inline int m3_apk_stdio_buffer(FILE *fp, const char *path, const char *mode) {
  int saved=errno, result=0;
  if(fp && path && mode && mode[0]=='r' && !strchr(mode,'+')) {
    size_t n=strlen(path);
    if(n>=9 && !strcmp(path+n-9,"/base.apk") && fileno(fp)>=0)
      result=setvbuf(fp,NULL,_IOFBF,32768)==0;
  }
  errno=saved;return result;
}
