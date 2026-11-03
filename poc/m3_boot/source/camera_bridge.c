/* Feed the game's own camera controller on its live LateUpdate instance.
 * No cached managed object is dereferenced and no camera transform is forced.
 * Method RVAs/fields are pinned to the verified original 1.9 A32 runtime. */
#include <stdint.h>
#include "camera_axis.h"
#include "controller_bridge.h"
#include "rt_applet.h"
#include "so_util.h"
#include "util.h"
typedef void (*CameraCall)(void *,const void *);
typedef void (*RotationCall)(void *,CameraDelta,const void *) __attribute__((pcs("aapcs")));
typedef float (*DeltaCall)(void *,const void *) __attribute__((pcs("aapcs")));
static CameraCall original_update,start_rotate,exit_rotate;
static RotationCall set_rotation;
static DeltaCall delta_time;
static uintptr_t owner_identity;
static unsigned updates;
extern unsigned char m3_tramp_camera[];

static void camera_update(void *self,const void *method) {
  CameraDelta axes={0,0};
  m3_controller_camera_axes(&axes.x,&axes.y);
  /* Manual input must never take over story/animation cameras or muted control. */
  char *o=self;
  void *follow=*(void **)(o+0x13c);
  int allowed=rt_focused() && follow && *(void **)(o+0x24)==follow &&
              !*(unsigned char *)(o+0x9c) && !*(unsigned char *)(o+0x9d);
  CameraDelta delta=allowed ? camera_delta(axes,delta_time(NULL,NULL)) : (CameraDelta){0,0};
  if(delta.x || delta.y) {
    if(owner_identity!=(uintptr_t)self)start_rotate(self,NULL);
    set_rotation(self,delta,NULL);
    owner_identity=(uintptr_t)self;
    if(!updates++)debugPrintf("[camera] native rotation active on live LateUpdate; no touch strokes\n");
  } else if(owner_identity==(uintptr_t)self) {
    if(allowed)exit_rotate(self,NULL); /* original touch-up timeout behavior */
    owner_identity=0;
  }
  original_update(self,method);
}

int m3_camera_install(so_module *mod) {
  uintptr_t base=(uintptr_t)mod->load_virtbase;
  const struct { uint32_t offset,word; } guards[]={
    {0x1011db8,0xe92d4ff0},{0x1011dbc,0xe28db01c}, /* LateUpdate movable prologue */
    {0x1014d90,0xe92d4c70},{0x1014d94,0xe28db010}, /* RotateStart */
    {0x1014ddc,0xe594013c},{0x1014e04,0x05d4009c}, /* follow / mute guards */
    {0x1015164,0xe92d4df0},{0x1015168,0xe28db018}, /* Vector2 in r1/r2 */
    {0x101539c,0xe92d4c70},{0x10153a0,0xe28db010}, /* EnableExitTimer */
    {0x25ecfa8,0xe92d4830},{0x25ecfac,0xe28db008} /* scaled Time.deltaTime */
  };
  for(unsigned i=0;i<sizeof guards/sizeof guards[0];i++)
    if(*(const uint32_t *)(base+guards[i].offset)!=guards[i].word) {
      debugPrintf("[camera] unsupported instruction at %x; keeping touch fallback\n",guards[i].offset);
      return 0;
    }
  uintptr_t resume=base+0x1011dc0;
  so_patch_code(m3_tramp_camera+12,&resume,sizeof resume);
  original_update=(CameraCall)m3_tramp_camera;
  start_rotate=(CameraCall)(base+0x1014d90);
  set_rotation=(RotationCall)(base+0x1015164);
  exit_rotate=(CameraCall)(base+0x101539c);
  delta_time=(DeltaCall)(base+0x25ecfa8);
  hook_arm(base+0x1011db8,(uintptr_t)camera_update);
  m3_controller_native_camera_enable();
  debugPrintf("[camera] native LateUpdate adapter installed; radial linear input\n");
  return 1;
}

