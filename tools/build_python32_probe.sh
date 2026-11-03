#!/usr/bin/env bash
# Build CPython and zlib for AArch32 Horizon in the pinned toolchain.
# Usage: bash build_python32_probe.sh /absolute/disposable/build-root
set -euo pipefail
TOP=${1:?Pass the Python build workspace}
cd "$TOP"
VERSION=3.11.18
SHA=9120e0a653868f6e3698d36cdd99e5a58903ec9469e448f21d98f7b16d8fa290
printf '%s  %s\n' "$SHA" "Python-$VERSION.tar.xz" | sha256sum -c -
SRC="$TOP/Python-$VERSION"
if [ ! -d "$SRC" ]; then tar -xJf "Python-$VERSION.tar.xz"; fi

# Explicitly identify Horizon, rather than pretending this is Linux.
python3 - "$SRC" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
p = root / 'configure'
s = p.read_text()
if 'ac_sys_system=Horizon' not in s:
    old = 'case "$host" in\n\t*-*-linux-android*)'
    assert old in s
    s = s.replace(old, 'case "$host" in\n        arm-*-eabi) ac_sys_system=Horizon ;;\n\t*-*-linux-android*)', 1)
    old = 'case "$host" in\n\t*-*-linux*)'
    assert old in s
    s = s.replace(old, 'case "$host" in\n        arm-*-eabi) _host_cpu=arm ;;\n\t*-*-linux*)', 1)
    p.write_text(s)
p = root / 'Modules/timemodule.c'
s = p.read_text()
s = s.replace('#ifdef _MSC_VER\n#  define _Py_timezone',
              '#if defined(_MSC_VER) || defined(__SWITCH__)\n#  define _Py_timezone', 1)
p.write_text(s)
p = root / 'Python/bootstrap_hash.c'
s = p.read_text()
if 'switch random adapter' not in s:
    s = s.replace('#include "Python.h"', '#include "Python.h"\n#ifdef __SWITCH__\n#include <switch.h> /* switch random adapter */\n#endif', 1)
    old = '    if (size == 0) {\n        return 0;\n    }\n\n#ifdef MS_WINDOWS'
    assert old in s
    s = s.replace(old, '    if (size == 0) {\n        return 0;\n    }\n#ifdef __SWITCH__\n    randomGet(buffer, (size_t)size);\n    return 0;\n#endif\n\n#ifdef MS_WINDOWS', 1)
    p.write_text(s)
p = root / 'Python/thread_pthread.h'
s = p.read_text()
if 'newlib provides pthread_attr_setstacksize' not in s:
    s = s.replace('/* The POSIX spec requires that use of pthread_attr_setstacksize',
        '/* newlib provides pthread_attr_setstacksize but omits this feature macro. */\n#if defined(__SWITCH__) && !defined(_POSIX_THREAD_ATTR_STACKSIZE)\n#define _POSIX_THREAD_ATTR_STACKSIZE 200809L\n#endif\n\n/* The POSIX spec requires that use of pthread_attr_setstacksize', 1)
    p.write_text(s)
p = root / 'Modules/selectmodule.c'
s = p.read_text()
if 'not advertised by this offline probe' not in s:
    s = s.replace('#include "Python.h"', '#include "Python.h"\n#ifdef __SWITCH__\n/* BSD poll needs a live network service; not advertised by this offline probe. */\n#undef HAVE_POLL\n#endif', 1)
    p.write_text(s)
PY

mkdir -p host-build target-build
if [ ! -x host-build/python ]; then
    (cd host-build
     "$SRC/configure" --without-ensurepip --disable-test-modules > configure.log 2>&1
     make -j8 python > make.log 2>&1)
fi
host-build/python -V

export CC=arm-none-eabi-gcc AR=arm-none-eabi-ar RANLIB=arm-none-eabi-ranlib
ARCH='-march=armv8-a+crc+crypto -mtune=cortex-a57 -mfloat-abi=softfp -mfpu=neon-fp-armv8 -mtp=soft -fPIE -ftls-model=local-exec'
export CFLAGS="-O2 $ARCH -D__SWITCH__ -fno-short-enums -I$DEVKITPRO/libnx32/include"
export LDFLAGS="-Wl,-z,notext $ARCH -specs=$DEVKITPRO/libnx32/switch32.specs -L$DEVKITPRO/libnx32/lib"
export LIBS='-lnx -lm'
export ac_cv_file__dev_ptmx=no ac_cv_file__dev_ptc=no ac_cv_buggy_getaddrinfo=no
# zlib is project-local, not installed into the SDK or system Python.
printf '%s  %s\n' d7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3 zlib-1.3.2.tar.xz | sha256sum -c -
if [ ! -d zlib-1.3.2 ]; then tar -xJf zlib-1.3.2.tar.xz; fi
(cd zlib-1.3.2
 ./configure --static --prefix="$TOP/zlib-prefix" > configure.log 2>&1
 make -j8 libz.a > make.log 2>&1
 mkdir -p "$TOP/zlib-prefix/include" "$TOP/zlib-prefix/lib"
 cp zlib.h zconf.h "$TOP/zlib-prefix/include/"
 cp libz.a "$TOP/zlib-prefix/lib/")
cd target-build
"$SRC/configure" --build=x86_64-pc-linux-gnu --host=arm-none-eabi \
    --with-build-python="$TOP/host-build/python" --disable-shared \
    --without-ensurepip --disable-test-modules --without-pymalloc --disable-ipv6 > configure.log 2>&1
python3 - <<'PY'
from pathlib import Path
p = Path('pyconfig.h')
s = p.read_text()
for name in ('HAVE_GETRLIMIT', 'HAVE_SETRLIMIT', 'HAVE_STATVFS',
             'HAVE_FSTATVFS', 'HAVE_SYS_RESOURCE_H', 'HAVE_FDATASYNC',
             'HAVE_FCHDIR', 'HAVE_CHROOT', 'HAVE_SETGROUPS'):
    s = s.replace('#define '+name+' 1', '/* disabled '+name+' */')
s += '\n#ifndef SSIZE_MAX\n#define SSIZE_MAX 2147483647\n#endif\n'
p.write_text(s)
PY
{
    echo '*static*'
    grep -E '^(_bisect|_heapq|_json|_pickle|_random|_struct|_zoneinfo|_contextvars|math|_datetime|_sha256|_sha512|_sha1|_md5|_sha3|_blake2|select|_socket|array) ' Modules/Setup.stdlib
    printf 'binascii binascii.c -I%s/zlib-prefix/include -DUSE_ZLIB_CRC32 -L%s/zlib-prefix/lib -lz\n' "$TOP" "$TOP"
    printf 'zlib zlibmodule.c -I%s/zlib-prefix/include -L%s/zlib-prefix/lib -lz\n' "$TOP" "$TOP"
} > Modules/Setup.local
make -j8 libpython3.11.a > make.log 2>&1
make -j8 python > link.log 2>&1
make pybuilddir.txt > sysconfig.log 2>&1
