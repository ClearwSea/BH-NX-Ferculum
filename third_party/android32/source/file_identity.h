/* FAT/fsdev does not provide inodes. Intern canonical translated paths so
 * IL2CPP's truncated 32-bit inode comparison is collision-free. The caller
 * serializes access; entries live for the process lifetime, including closes.
 */
#ifndef DCR_FILE_IDENTITY_H
#define DCR_FILE_IDENTITY_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct DcrFileIdentity {
  struct DcrFileIdentity *next;
  uint32_t inode;
  char path[];
} DcrFileIdentity;
typedef struct {
  DcrFileIdentity *buckets[512];
  uint32_t last_inode;
} DcrFileIdentities;

static uint32_t dcr_file_identity(DcrFileIdentities *table, const char *path) {
  char canonical[4096];
  size_t n = 0;
  const unsigned char *p = (const unsigned char *)path;
  if (!p) return 0;
  while (*p) {
    while (*p == '/' || *p == '\\') p++;
    if (!*p) break;
    const unsigned char *start = p;
    while (*p && *p != '/' && *p != '\\') p++;
    size_t len = (size_t)(p - start);
    if (len == 1 && start[0] == '.') continue;
    if (len == 2 && start[0] == '.' && start[1] == '.') {
      while (n && canonical[n-1] != '/') n--;
      if (n) n--;
      continue;
    }
    if (n + len + 1 >= sizeof canonical) return 0;
    if (n) canonical[n++] = '/';
    for (size_t i = 0; i < len; i++) {
      unsigned char c = start[i];
      canonical[n++] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    }
  }
  uint64_t h = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < n; i++) {
    h ^= (unsigned char)canonical[i];
    h *= UINT64_C(1099511628211);
  }
  canonical[n] = 0;
  size_t bucket = h % 512;
  for (DcrFileIdentity *e = table->buckets[bucket]; e; e = e->next)
    if (!strcmp(e->path, canonical)) return e->inode;
  if (table->last_inode == UINT32_MAX) return 0;
  DcrFileIdentity *e = malloc(sizeof *e + n + 1);
  if (!e) return 0;
  memcpy(e->path, canonical, n + 1);
  e->inode = ++table->last_inode;
  e->next = table->buckets[bucket];
  table->buckets[bucket] = e;
  return e->inode;
}
#endif
