#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 REPOSITORY_ROOT" >&2
  exit 2
fi

APSIS_REPOSITORY_ROOT=$(realpath "$1")
APSIS_ASSET_TMP=$(mktemp -d /tmp/apsis-drift-issue230-assets.XXXXXX)
trap 'find "${APSIS_ASSET_TMP}" -depth -delete' EXIT

readonly SF2CUTE_REVISION=3c5fc83b6ba3d1feb377f9c86021fd77499eb7c0

git clone --quiet https://github.com/gocha/sf2cute.git "${APSIS_ASSET_TMP}/sf2cute"
git -C "${APSIS_ASSET_TMP}/sf2cute" checkout --quiet "${SF2CUTE_REVISION}"
cmake -S "${APSIS_ASSET_TMP}/sf2cute" -B "${APSIS_ASSET_TMP}/sf2cute-build" \
  -DCMAKE_BUILD_TYPE=Release -DSF2CUTE_INSTALL_EXAMPLES=OFF
cmake --build "${APSIS_ASSET_TMP}/sf2cute-build" --parallel 2

c++ -std=c++23 -O2 -Wall -Wextra -Wpedantic \
  -I"${APSIS_ASSET_TMP}/sf2cute/include" \
  "${APSIS_REPOSITORY_ROOT}/tools/issue230_asset_builder.cpp" \
  "${APSIS_ASSET_TMP}/sf2cute-build/libsf2cute.a" \
  -o "${APSIS_ASSET_TMP}/issue230-asset-builder"

mkdir -p "${APSIS_REPOSITORY_ROOT}/assets/music"
"${APSIS_ASSET_TMP}/issue230-asset-builder" \
  "${APSIS_REPOSITORY_ROOT}/assets/music/issue230-tonal-prototype.sf2" \
  "${APSIS_REPOSITORY_ROOT}/assets/music/issue230-layer-demo.mid"

sha256sum \
  "${APSIS_REPOSITORY_ROOT}/assets/music/issue230-tonal-prototype.sf2" \
  "${APSIS_REPOSITORY_ROOT}/assets/music/issue230-layer-demo.mid"
