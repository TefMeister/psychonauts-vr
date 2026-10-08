#!/usr/bin/env bash
# Builds and runs tests/camfollow_test.c against the SHIPPED head-follow code: the basis block of
# px_07_engine_camera.c.inc (from the PSY_CAM_BASIS_OFF define up to the fpcam section) is cut out as
# it stands, so the test can never drift from the proxy. (2026-10-08, /pd)
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
awk '/^#define PSY_CAM_BASIS_OFF/{on=1} /notes\/69: FIRST PERSON ON THE CAMERA OBJECT/{exit} on' px_07_engine_camera.c.inc \
    | sed '$d' > build/tests/basis_follow_extract.inc
grep -q "static void PsyBasisFollowHead" build/tests/basis_follow_extract.inc
i686-w64-mingw32-clang -O2 -Wall -Wno-unused-function -Wno-unused-variable -o build/tests/camfollow_test.exe tests/camfollow_test.c
./build/tests/camfollow_test.exe
