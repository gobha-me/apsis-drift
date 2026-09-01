#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || ! -f "$1" ]]; then
  echo "usage: $0 AMBIENT_SOURCE.wav" >&2
  exit 2
fi
if ! command -v ffmpeg >/dev/null; then
  echo "ffmpeg is required to prepare the bounded ambient loop" >&2
  exit 1
fi

APSIS_REPOSITORY_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
APSIS_AMBIENT_SOURCE=$(realpath "$1")
APSIS_ASSET_TMP=$(mktemp -d /tmp/apsis-drift-first-light-assets.XXXXXX)
trap 'rm -r "${APSIS_ASSET_TMP}"' EXIT

ffmpeg -hide_banner -loglevel error -y \
  -i "${APSIS_AMBIENT_SOURCE}" -i "${APSIS_AMBIENT_SOURCE}" \
  -filter_complex \
  "[0:a]aresample=48000,pan=mono|c0=0.5*c0+0.5*c1,volume=-6.1dB,atrim=start=2:end=30,asetpts=PTS-STARTPTS[main];[1:a]aresample=48000,pan=mono|c0=0.5*c0+0.5*c1,volume=-6.1dB,atrim=start=0:end=2,asetpts=PTS-STARTPTS[head];[main][head]acrossfade=d=2:c1=tri:c2=tri[out]" \
  -map "[out]" -c:a pcm_s16le "${APSIS_ASSET_TMP}/ambient-loop.wav"

SF2CUTE_REVISION=3c5fc83b6ba3d1feb377f9c86021fd77499eb7c0
git clone --quiet https://github.com/gocha/sf2cute.git "${APSIS_ASSET_TMP}/sf2cute"
git -C "${APSIS_ASSET_TMP}/sf2cute" checkout --quiet "${SF2CUTE_REVISION}"
cmake -S "${APSIS_ASSET_TMP}/sf2cute" -B "${APSIS_ASSET_TMP}/sf2cute-build" \
  -DCMAKE_BUILD_TYPE=Release -DSF2CUTE_INSTALL_EXAMPLES=OFF
cmake --build "${APSIS_ASSET_TMP}/sf2cute-build" --target sf2cute --parallel 2

"${CXX:-c++}" -std=c++23 -O2 -Wall -Wextra -Wpedantic -Werror \
  -Wno-error=pessimizing-move \
  -I"${APSIS_ASSET_TMP}/sf2cute/include" \
  "${APSIS_REPOSITORY_ROOT}/tools/issue230_asset_builder.cpp" \
  "${APSIS_ASSET_TMP}/sf2cute-build/libsf2cute.a" \
  -o "${APSIS_ASSET_TMP}/first-light-asset-builder"

mkdir -p "${APSIS_REPOSITORY_ROOT}/assets/music"
"${APSIS_ASSET_TMP}/first-light-asset-builder" \
  "${APSIS_REPOSITORY_ROOT}/assets/music/first-light-bank.sf2" \
  "${APSIS_REPOSITORY_ROOT}/assets/music/first-light-score.mid" \
  "${APSIS_ASSET_TMP}/ambient-loop.wav"

sha256sum \
  "${APSIS_REPOSITORY_ROOT}/assets/music/first-light-bank.sf2" \
  "${APSIS_REPOSITORY_ROOT}/assets/music/first-light-score.mid"
