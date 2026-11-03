#!/bin/sh
# build.sh -- build bh3_m1_probe.nsp in the AArch32 toolchain image.
#
#   LIBNX32      a built libnx32 prefix/   (default ../../third_party/libnx32/prefix)
#   RUNTIME_DIR  the android32 checkout    (default ../../third_party/android32)
#   PORTLIBS     mesa32's prefix           (default ./portlibs32)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
IMAGE=${TOOLCHAIN_IMAGE:-ghcr.io/vita2hos/devcontainer/vita2hos@sha256:3860772020f697865b0476ef72d680230b6b4f40d3f84659f546159b32f859d6}
LIBNX32=${LIBNX32:-$HERE/../../third_party/libnx32/prefix}
RUNTIME_DIR=${RUNTIME_DIR:-$HERE/../../third_party/android32}
PORTLIBS=${PORTLIBS:-$HERE/portlibs32}
PROGRAM_ID=${PROGRAM_ID:-0x0100000000010000}
NX=/opt/devkitpro/libnx32

[ -f "$LIBNX32/lib/libnx.a" ] || { echo "build.sh: no libnx32 at $LIBNX32" >&2; exit 1; }
[ -f "$RUNTIME_DIR/runtime.mk" ] || { echo "build.sh: no android32 at $RUNTIME_DIR" >&2; exit 1; }
[ -f "$PORTLIBS/lib/libEGL.a" ] || { echo "build.sh: no mesa32 portlibs at $PORTLIBS" >&2; exit 1; }

exec docker run --rm --platform linux/amd64 \
  -v "$HERE:/work" \
  -v "$RUNTIME_DIR:/runtime:ro" \
  -v "$PORTLIBS:/work/portlibs32:ro" \
  -v "$LIBNX32/include/switch:$NX/include/switch:ro" \
  -v "$LIBNX32/include/switch.h:$NX/include/switch.h:ro" \
  -v "$LIBNX32/lib/libnx.a:$NX/lib/libnx.a:ro" \
  -v "$LIBNX32/lib/libnxd.a:$NX/lib/libnxd.a:ro" \
  -v "$LIBNX32/switch32.ld:$NX/switch32.ld:ro" \
  -w /work "$IMAGE" bash -lc "make -j\$(nproc) RUNTIME=/runtime PROGRAM_ID=$PROGRAM_ID"
