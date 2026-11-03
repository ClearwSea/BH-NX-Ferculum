/* Multitouch Unity JNI input bridge. MIT. */
#ifndef M3_INPUT_BRIDGE_H
#define M3_INPUT_BRIDGE_H
#include "jni.h"
void m3_input_init(void);
void m3_input_cancel(void);
void m3_input_set_active(int active);
void m3_input_frame(unsigned long frame);
int m3_input_jni(JObj *self, JMethod *m, const jvalue *a, jvalue *out);
#endif
