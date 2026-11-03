/* Draw a high-contrast cursor at presentation, preserving all touched GL state. */
#include <GLES2/gl2.h>
#include "controller_bridge.h"
#include "rt_window.h"
static void rect(int x,int y,int w,int h,int vw,int vh) {
  glScissor(x*vw/1280,vh-(y+h)*vh/720,w*vw/1280,h*vh/720);
  glClear(GL_COLOR_BUFFER_BIT);
}
void port_gl_before_swap(void) {
  float x,y; int visible,cursor,w,h;
  m3_controller_cursor(&x,&y,&visible,&cursor);
  if(!visible)return;
  dcr_window_size(&w,&h);
  GLint fbo,box[4]; GLfloat color[4]; GLboolean mask[4];
  GLboolean enabled=glIsEnabled(GL_SCISSOR_TEST);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);
  glGetIntegerv(GL_SCISSOR_BOX,box);
  glGetFloatv(GL_COLOR_CLEAR_VALUE,color);
  glGetBooleanv(GL_COLOR_WRITEMASK,mask);
  glBindFramebuffer(GL_FRAMEBUFFER,0);
  glEnable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  /* Top-centre blue = cursor, orange = battle. Small enough to leave UI usable. */
  glClearColor(0,0,0,1); rect(622,4,36,10,w,h);
  glClearColor(cursor?0.1f:1.0f,cursor?0.8f:0.45f,cursor?1.0f:0.1f,1);
  rect(625,7,30,4,w,h);
  if(cursor) {
    glClearColor(0,0,0,1);
    rect((int)x-10,(int)y-3,21,7,w,h); rect((int)x-3,(int)y-10,7,21,w,h);
    glClearColor(1,1,1,1);
    rect((int)x-9,(int)y-1,19,3,w,h); rect((int)x-1,(int)y-9,3,19,w,h);
  }
  glClearColor(color[0],color[1],color[2],color[3]);
  glColorMask(mask[0],mask[1],mask[2],mask[3]);
  glScissor(box[0],box[1],box[2],box[3]);
  if(!enabled)glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)fbo);
}
