#pragma once
void m3_boot_init(void);
int m3_boot_running(void);
void m3_boot_mark(const char *name);
void m3_boot_frame(unsigned frame, unsigned long long duration_ns);
void m3_boot_finish(const char *reason);
void m3_boot_emit(const char *fmt, ...) __attribute__((format(printf,1,2)));
