#ifndef M3_BOOT_UI_H
#define M3_BOOT_UI_H
void m3_boot_ui_init(void);
void m3_boot_ui_stage(const char *text);
void m3_boot_ui_console_stage(const char *text);
void m3_boot_ui_draw(void);
void m3_boot_ui_finish(void);
#endif
