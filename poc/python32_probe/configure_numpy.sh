#!/bin/bash
set -euo pipefail
export PATH=/py/build-tools:$PATH
cd /py
python3 numpy-1.26.2/vendored-meson/meson/meson.py setup numpy-build-target numpy-1.26.2 --cross-file numpy-cross.ini -Dblas=none -Dlapack=none -Dallow-noblas=true -Db_lundef=false -Dbuildtype=release > numpy-config-target.log 2>&1
