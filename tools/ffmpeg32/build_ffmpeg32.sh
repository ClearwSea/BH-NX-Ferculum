#!/bin/bash
# build_ffmpeg32.sh -- a small FFmpeg for 32-bit (AArch32) Switch programs
# (libnx32): static libavformat, libavcodec and libavutil, NEON on, no threads,
# no programs. LGPL (no --enable-gpl).
#
# Run inside the toolchain container (./build.sh does that):
#   docker run --rm --platform linux/amd64 -v "$PWD:/work" -w /work \
#     ghcr.io/vita2hos/devcontainer/vita2hos:latest bash -lc ./build_ffmpeg32.sh
# `./build_ffmpeg32.sh reconfigure` runs configure again.
#
# Downloads FFmpeg 7.1.1 (checked against its SHA-256) and installs into
# ./prefix. Built with -fno-short-enums: FFmpeg needs int-sized enums, while
# devkitARM defaults to short ones, so code that includes its headers must be
# compiled with -fno-short-enums too.
set -euo pipefail
cd "$(dirname "$0")"
TOP=$PWD
VERSION=7.1.1
SHA256=733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1
TARBALL=ffmpeg-$VERSION.tar.xz
SRC=$TOP/ffmpeg-$VERSION
PREFIX=$TOP/prefix
BUILD=$TOP/build
NX=$DEVKITPRO/libnx32
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mfloat-abi=softfp -mfpu=neon-fp-armv8 -mtp=soft -fPIE -ftls-model=local-exec"

# What to build. The default is an MP4 player: the mov demuxer, MPEG-4 Part 2
# video and AAC audio. Add what your program needs, e.g.
#   --enable-decoder=h264 --enable-parser=h264
# (h264 without hevc needs a stub, see README.md).
COMPONENTS="${FFMPEG_COMPONENTS:---enable-demuxer=mov --enable-decoder=mpeg4 --enable-decoder=aac
  --enable-parser=mpeg4video --enable-parser=aac}"

if [ ! -d "$SRC" ]; then
  if [ ! -f "$TARBALL" ]; then
    curl -fL -o "$TARBALL.part" "https://ffmpeg.org/releases/$TARBALL"
    mv "$TARBALL.part" "$TARBALL"
  fi
  echo "$SHA256  $TARBALL" | sha256sum -c -
  tar -xJf "$TARBALL"
fi

mkdir -p "$BUILD"
cd "$BUILD"
if [ ! -f config.h ] || [ "${1:-}" = reconfigure ]; then
  # shellcheck disable=SC2086
  "$SRC/configure" --prefix="$PREFIX" \
    --enable-cross-compile --cross-prefix="$DEVKITPRO/devkitARM/bin/arm-none-eabi-" \
    --arch=arm --cpu=armv8-a --target-os=none \
    --enable-static --disable-shared --enable-pic \
    --disable-runtime-cpudetect --enable-neon --enable-vfp \
    --disable-pthreads --disable-w32threads --disable-os2threads \
    --disable-programs --disable-doc --disable-debug --disable-autodetect \
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --disable-postproc \
    --disable-network --disable-iconv --disable-zlib --disable-bzlib --disable-lzma \
    --disable-everything $COMPONENTS \
    --extra-cflags="$ARCH -O2 -D__SWITCH__ -fno-short-enums -ffunction-sections -fdata-sections -isystem $NX/include" \
    --extra-ldflags="$ARCH -Wl,-z,notext -specs=$NX/switch32.specs -L$NX/lib" \
    --extra-libs="-lnx -lm"
fi
make -j"$(nproc)"
make install
ls -l "$PREFIX/lib"
