/* Startup-only status: console before EGL, state-preserving overlay afterwards.
 * Log callbacks publish text only. No GL calls or file writes on loader threads. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <switch.h>
#include <GLES3/gl3.h>
#include "util.h"
#include "rt_window.h"
#include "gl_layer.h"
#include "boot_ui.h"
#include "boot_ui_font.h"
#define LINES 1
#define WIDTH 960
#define HEIGHT 112
static Mutex lock;
static unsigned active, version;
static char history[LINES][128];
static unsigned count;
static void (*previous_present)(void);
static GLuint retained, retained_fb;
static unsigned captured, attempts;
static int retained_w, retained_h;
extern void (*dcr_present_hook)(void);
static GLuint program, texture, vao, vbo;
static unsigned uploaded;
static int gl_failed;
static unsigned char pixels[WIDTH*HEIGHT];

static unsigned next_code(const unsigned char **p) {
  unsigned c=*(*p)++;
  if(c<128)return c;
  if((c&0xe0)==0xc0 && **p){unsigned b=*(*p)++;return ((c&31)<<6)|(b&63);}
  if((c&0xf0)==0xe0 && (*p)[0] && (*p)[1]){unsigned b=*(*p)++,d=*(*p)++;return ((c&15)<<12)|((b&63)<<6)|(d&63);}
  return '?';
}
static unsigned glyph(unsigned c) {
  for(unsigned i=0;i<sizeof glyph_codes/sizeof *glyph_codes;i++)if(glyph_codes[i]==c)return i;
  return '?'-32;
}
static void text_pixels(const char *s,int x,int y) {
  const unsigned char *p=(const unsigned char *)s;
  while(*p && x+32<=WIDTH){unsigned c=next_code(&p),g=glyph(c);
    for(int dy=0;dy<32 && y+dy<HEIGHT;dy++)for(int dx=0;dx<32;dx++)
      if(glyph_tiles[g][(dy/16)*32+(dx/16)*16+dy%16]&(0x8000>>(dx%16)))pixels[(y+dy)*WIDTH+x+dx]=255;
    x+=c<128?16:28;
  }
}
static void text_console(PrintConsole *con,const char *s,int x,int y) {
  const unsigned char *p=(const unsigned char *)s;
  while(*p && x+2<con->consoleWidth){unsigned c=next_code(&p),g=glyph(c);
    for(int ty=0;ty<2;ty++)for(int tx=0;tx<2;tx++)con->renderer->drawChar(con,x+tx,y+ty,g*4+ty*2+tx);
    x+=2;
  }
}
static void snapshot(char rows[LINES][128],unsigned *n,unsigned *v) {
  mutexLock(&lock);memcpy(rows,history,sizeof history);*n=count;*v=version;mutexUnlock(&lock);
}
static void console_draw(void) {
  if(!log_console_active())return;
  char rows[LINES][128];unsigned n,v;snapshot(rows,&n,&v);(void)v;
  PrintConsole *con=consoleSelect(NULL);consoleSelect(con);
  if(!con || !con->consoleInitialised || !con->renderer)return;
  unsigned short fg=con->fg,bg=con->bg;int flags=con->flags;
  ConsoleFont old=con->font,custom={glyph_tiles,0,(sizeof glyph_codes/sizeof *glyph_codes)*4,16,16};
  consoleClear();consoleSetFont(con,&custom);
  con->fg=14;con->bg=0;con->flags=0;
  text_console(con,"崩壊3rd 2017",10,10);
  con->fg=7;text_console(con,"游戏正在启动，请稍候",10,14);
  for(unsigned i=0;i<n;i++){con->fg=i+1==n?14:8;text_console(con,rows[i],10,19+i*3);}
  consoleUpdate(con);consoleSetFont(con,&old);
  con->fg=fg;con->bg=bg;con->flags=flags;
}
void m3_boot_ui_stage(const char *text) {
  if(!__atomic_load_n(&active,__ATOMIC_ACQUIRE)||!text)return;
  mutexLock(&lock);
  if(count && !strcmp(history[count-1],text)){mutexUnlock(&lock);return;}
  if(count==LINES){memmove(history,history+1,(LINES-1)*sizeof history[0]);count--;}
  snprintf(history[count++],sizeof history[0],"%s",text);version++;
  mutexUnlock(&lock);
  debugPrintf("[boot-ui] stage=%s\n",text);
}
void m3_boot_ui_init(void) {
  __atomic_store_n(&active,1,__ATOMIC_RELEASE);
  previous_present=dcr_present_hook;dcr_present_hook=m3_boot_ui_draw;
  m3_boot_ui_stage("正在启动本地服务");
}
/* Called only by main before Unity owns the window. */
void m3_boot_ui_console_stage(const char *text){m3_boot_ui_stage(text);}
void m3_boot_ui_finish(void){__atomic_store_n(&active,0,__ATOMIC_RELEASE);debugPrintf("[boot-ui] finished\n");if(M3_SOFTWARE_ETC)dcr_gl_request_capture_named("startup-status-finished");}

static GLuint shader(GLenum kind,const char *s){GLuint id=glCreateShader(kind);glShaderSource(id,1,&s,0);glCompileShader(id);GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);if(!ok){glDeleteShader(id);return 0;}return id;}
static int init_gl(void){
  const char *vs="#version 300 es\nlayout(location=0) in vec2 pos;out vec2 uv;void main(){uv=vec2((pos.x+1.0)*0.5,(1.0-pos.y)*0.5);gl_Position=vec4(pos,0,1);}";
  const char *fs="#version 300 es\nprecision mediump float;in vec2 uv;uniform sampler2D image;out vec4 color;void main(){float a=texture(image,uv).r;color=vec4(mix(vec3(0.025,0.045,0.075),vec3(0.70,0.91,1.0),a),1.0);}";
  GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs);
  if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return 0;}
  program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);glDeleteShader(v);glDeleteShader(f);
  GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(program);program=0;return 0;}
  glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
  const GLfloat points[]={-1,-1,1,-1,-1,1,1,1};glBufferData(GL_ARRAY_BUFFER,sizeof points,points,GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);
  glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  return 1;
}
static void retain_background(int w,int h){
  GLint readfb,pack,packbuf,prow,px,py;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readfb);
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&packbuf);glGetIntegerv(GL_PACK_ALIGNMENT,&pack);
  glGetIntegerv(GL_PACK_ROW_LENGTH,&prow);glGetIntegerv(GL_PACK_SKIP_PIXELS,&px);glGetIntegerv(GL_PACK_SKIP_ROWS,&py);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
  if(!captured && attempts++<32){
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
    unsigned char sample[4];int visible=0;
    for(int y=1;y<=3&&!visible;y++)for(int x=1;x<=3;x++){
      glReadPixels(w*x/4,h*y/4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,sample);
      if(sample[0]+sample[1]+sample[2]>60){visible=1;break;}
    }
    if(visible){
      glGenTextures(1,&retained);glBindTexture(GL_TEXTURE_2D,retained);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glCopyTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,0,0,w,h,0);
      glGenFramebuffers(1,&retained_fb);glBindFramebuffer(GL_READ_FRAMEBUFFER,retained_fb);
      glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,retained,0);
      captured=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;retained_w=w;retained_h=h;
      debugPrintf("[boot-ui] retained first visible frame %dx%d valid=%u\n",w,h,captured);
    }
  }
  if(captured){
    glBindFramebuffer(GL_READ_FRAMEBUFFER,retained_fb);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glBlitFramebuffer(0,0,retained_w,retained_h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER,readfb);glBindBuffer(GL_PIXEL_PACK_BUFFER,packbuf);
  glPixelStorei(GL_PACK_ALIGNMENT,pack);glPixelStorei(GL_PACK_ROW_LENGTH,prow);glPixelStorei(GL_PACK_SKIP_PIXELS,px);glPixelStorei(GL_PACK_SKIP_ROWS,py);
}
void m3_boot_ui_draw(void) {
  if(previous_present)previous_present();
  if(!__atomic_load_n(&active,__ATOMIC_ACQUIRE)||gl_failed)return;
  /* Executed only on the engine's present thread and its current GLES3 context. */
  GLint oldprog,oldvao,oldbuf,oldfb,view[4],unit,tex,sampler,unpack,pbo,row,skipx,skipy;
  GLboolean mask[4];GLenum caps[]={GL_BLEND,GL_DEPTH_TEST,GL_STENCIL_TEST,GL_CULL_FACE,GL_SCISSOR_TEST,GL_RASTERIZER_DISCARD};GLboolean enabled[6];
  glGetIntegerv(GL_CURRENT_PROGRAM,&oldprog);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldvao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldbuf);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&oldfb);glGetIntegerv(GL_VIEWPORT,view);glGetBooleanv(GL_COLOR_WRITEMASK,mask);
  glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex);glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
  glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&pbo);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skipx);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skipy);
  for(unsigned i=0;i<6;i++){enabled[i]=glIsEnabled(caps[i]);glDisable(caps[i]);}
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
  glBindSampler(0,0);
  if(!program && !init_gl())gl_failed=1;
  if(program){
    int w,h;dcr_window_size(&w,&h);retain_background(w,h);
    glBindTexture(GL_TEXTURE_2D,texture);
    char rows[LINES][128];unsigned n,v;snapshot(rows,&n,&v);
    if(v!=uploaded){memset(pixels,0,sizeof pixels);text_pixels("崩壊3rd 2017  |  正在启动",24,8);
      for(unsigned i=0;i<n;i++){if(i+1==n)text_pixels(">",24,56+i*32);text_pixels(rows[i],56,56+i*32);}
      glTexImage2D(GL_TEXTURE_2D,0,GL_R8,WIDTH,HEIGHT,0,GL_RED,GL_UNSIGNED_BYTE,pixels);uploaded=v;
      if(M3_SOFTWARE_ETC){char name[64];snprintf(name,sizeof name,"startup-status-%u",v);dcr_gl_request_capture_named(name);}
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);glViewport(w/8,h/16,w*3/4,h*112/720);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
    glBindVertexArray(vao);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
  }
  glBindVertexArray(oldvao);glBindBuffer(GL_ARRAY_BUFFER,oldbuf);glUseProgram(oldprog);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,oldfb);glViewport(view[0],view[1],view[2],view[3]);
  glColorMask(mask[0],mask[1],mask[2],mask[3]);for(unsigned i=0;i<6;i++)if(enabled[i])glEnable(caps[i]);
  glBindTexture(GL_TEXTURE_2D,tex);glBindSampler(0,sampler);glActiveTexture(unit);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);glPixelStorei(GL_UNPACK_ROW_LENGTH,row);glPixelStorei(GL_UNPACK_SKIP_PIXELS,skipx);glPixelStorei(GL_UNPACK_SKIP_ROWS,skipy);
}
