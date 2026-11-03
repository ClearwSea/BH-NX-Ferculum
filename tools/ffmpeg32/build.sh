#!/bin/sh
# build.sh -- build ffmpeg32 in the AArch32 toolchain image and install it into
# ./prefix. Arguments are passed to build_ffmpeg32.sh.
#
# FFmpeg is configured against libnx32. Point LIBNX32 at an installed libnx32
# (its prefix/ folder); a libnx32 checkout next to this one, built, is used by
# default. Without one, the image's stock libnx32 is used.
# FFMPEG_COMPONENTS replaces the default component list (build_ffmpeg32.sh).
# Set TOOLCHAIN_IMAGE to use another image.
set -e
IMAGE="${TOOLCHAIN_IMAGE:-ghcr.io/vita2hos/devcontainer/vita2hos:latest}"
HERE="$(cd "$(dirname "$0")" && pwd)"
LIBNX32="${LIBNX32:-$HERE/../libnx32/prefix}"
NX=/opt/devkitpro/libnx32
ENV=""
[ -n "${FFMPEG_COMPONENTS:-}" ] && ENV="-e FFMPEG_COMPONENTS"
if [ -f "$LIBNX32/lib/libnx.a" ] && [ -f "$LIBNX32/include/switch.h" ]; then
  LIBNX32="$(cd "$LIBNX32" && pwd)"
  echo "build.sh: using libnx32 from $LIBNX32"
  exec docker run --rm --platform linux/amd64 $ENV \
    -v "$HERE:/work" \
    -v "$LIBNX32/include/switch:$NX/include/switch:ro" \
    -v "$LIBNX32/include/switch.h:$NX/include/switch.h:ro" \
    -v "$LIBNX32/lib/libnx.a:$NX/lib/libnx.a:ro" \
    -v "$LIBNX32/lib/libnxd.a:$NX/lib/libnxd.a:ro" \
    -v "$LIBNX32/switch32.ld:$NX/switch32.ld:ro" \
    -w /work "$IMAGE" bash -lc "bash ./build_ffmpeg32.sh $*"
fi
echo "build.sh: no libnx32 at $LIBNX32, using the image's stock libnx32" >&2
exec docker run --rm --platform linux/amd64 $ENV -v "$HERE:/work" -w /work "$IMAGE" \
  bash -lc "bash ./build_ffmpeg32.sh $*"
