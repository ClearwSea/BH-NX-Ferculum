#!/bin/bash
set -euo pipefail
root=${BH3_BUILD_ROOT:?Set BH3_BUILD_ROOT to your build workspace}
mkdir -p "$root/BH-NX-Ferculum/release-reference"
image=${TOOLCHAIN_IMAGE:-ghcr.io/vita2hos/devcontainer/vita2hos@sha256:3860772020f697865b0476ef72d680230b6b4f40d3f84659f546159b32f859d6}
docker run --rm --pull=never --platform linux/amd64  -v "$root:/build" -v "$root/python32-probe:/py"  -v "$root/libnx32/prefix:/nx:ro"  -v "$root/libnx32/prefix/include/switch:/opt/devkitpro/libnx32/include/switch:ro"  -v "$root/libnx32/prefix/include/switch.h:/opt/devkitpro/libnx32/include/switch.h:ro"  -v "$root/libnx32/prefix/lib/libnx.a:/opt/devkitpro/libnx32/lib/libnx.a:ro"  -v "$root/libnx32/prefix/lib/libnxd.a:/opt/devkitpro/libnx32/lib/libnxd.a:ro"  -v "$root/libnx32/prefix/switch32.ld:/opt/devkitpro/libnx32/switch32.ld:ro"  -v "$root/BH-NX-Ferculum/third_party/android32:/runtime:ro" -w /build/BH-NX-Ferculum/poc/m3_boot "$image"  bash -lc 'make -j4 BUILD=build-release-reference M3_TABLE_WORKER_PROBE=6 PROGRAM_ID=0x01000C4015030000 M3_STANDALONE=1 RUNTIME=/runtime PORTLIBS=/build/mesa32/prefix M3_EMBEDDED_SERVER=1 M3_AVPRO_BRIDGE=1 M3_CG_DECODE=1 M3_CG_SELFTEST=0 M3_INPUT_SAMPLER=1 M3_SOFTWARE_ETC=0 PORT_NPDM_ADDRSPACE=2 CG_FFMPEG=/build/ffmpeg32-src/prefix'  > "$root/BH-NX-Ferculum/release-reference/release-build.log" 2>&1
cp "$root/BH-NX-Ferculum/poc/m3_boot/bh3_m3_boot.nsp" "$root/BH-NX-Ferculum/release-reference/release.nsp"
cp "$root/BH-NX-Ferculum/poc/m3_boot/bh3_m3_boot.elf" "$root/BH-NX-Ferculum/release-reference/release.elf"
sha256sum "$root/BH-NX-Ferculum/release-reference/release.nsp" > "$root/BH-NX-Ferculum/release-reference/release.sha256"
