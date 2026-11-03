#pragma once
#include <stdint.h>
enum { LOAD_OPEN, LOAD_READ, LOAD_SEEK, LOAD_STAT, LOAD_CLOSE,
       LOAD_FDOPEN, LOAD_FDREAD, LOAD_PREAD, LOAD_MMAP, LOAD_FSREAD,
       LOAD_INFLATE, LOAD_KINDS };
uint64_t m3_load_enter(unsigned kind);
void m3_load_leave(uint64_t token, uint64_t bytes);
void m3_asset_load_begin(void);
void m3_asset_load_phase(unsigned phase);
void m3_asset_load_end(void);

void m3_asset_load_end_to(void (*emit)(const char *, ...));
