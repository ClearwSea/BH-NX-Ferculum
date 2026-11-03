#!/bin/sh
# Build the MOV/H.264/AAC configuration used by this target.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
export TOOLCHAIN_IMAGE=${TOOLCHAIN_IMAGE:-ghcr.io/vita2hos/devcontainer/vita2hos@sha256:3860772020f697865b0476ef72d680230b6b4f40d3f84659f546159b32f859d6}
: "${LIBNX32:?Set LIBNX32 to the matching libnx32 prefix}"
export LIBNX32
export FFMPEG_COMPONENTS='--enable-demuxer=mov --enable-decoder=h264 --enable-parser=h264 --enable-decoder=aac --enable-parser=aac'
exec sh "$HERE/build.sh" reconfigure
