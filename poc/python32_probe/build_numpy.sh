#!/bin/bash
set -euo pipefail
export PATH=/py/build-tools:$PATH
export PYTHONPATH=/py/build-tools/cython-3.0.12
cd /py
ninja -C numpy-build-target -j8 > numpy-build-target.log 2>&1
