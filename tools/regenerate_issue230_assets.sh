#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 REPOSITORY_ROOT" >&2
  exit 2
fi

APSIS_REPOSITORY_ROOT=$(realpath "$1")
APSIS_ASSET_TMP=$(mktemp -d /tmp/apsis-drift-issue230-assets.XXXXXX)
trap 'find "${APSIS_ASSET_TMP}" -depth -delete' EXIT

readonly MECHSOUNDS_URL=https://johnoestmannmusic.com/wp-content/uploads/2026/03/260301-MechSounds.zip
readonly MECHSOUNDS_SHA256=d0817d9c2c1f05cef0ea06c29c51e519df72a23a4e1e2fbdd3681024dec9a6c1
readonly SF2CUTE_REVISION=3c5fc83b6ba3d1feb377f9c86021fd77499eb7c0

curl --fail --location --silent --show-error \
  --output "${APSIS_ASSET_TMP}/mechsounds.zip" "${MECHSOUNDS_URL}"
echo "${MECHSOUNDS_SHA256}  ${APSIS_ASSET_TMP}/mechsounds.zip" | sha256sum --check

unzip -q "${APSIS_ASSET_TMP}/mechsounds.zip" \
  'Samples/002 - Ambience/AMB-DARKFIRE.wav' \
  'Samples/000 - Tonal/TNL-DATAPLUK.wav' \
  'Samples/003 - Percussion/PRC-INDSTHIT.wav' \
  'Samples/000 - Tonal/TNL-RUSTECHO.wav' \
  -d "${APSIS_ASSET_TMP}/source"

ffmpeg -v error -y -i "${APSIS_ASSET_TMP}/source/Samples/002 - Ambience/AMB-DARKFIRE.wav" \
  -ac 1 -ar 48000 -f s16le "${APSIS_ASSET_TMP}/ambient.raw"
ffmpeg -v error -y -i "${APSIS_ASSET_TMP}/source/Samples/000 - Tonal/TNL-DATAPLUK.wav" \
  -ac 1 -ar 48000 -f s16le "${APSIS_ASSET_TMP}/pulse.raw"
ffmpeg -v error -y -i "${APSIS_ASSET_TMP}/source/Samples/003 - Percussion/PRC-INDSTHIT.wav" \
  -ac 1 -ar 48000 -f s16le "${APSIS_ASSET_TMP}/percussion.raw"
ffmpeg -v error -y -i "${APSIS_ASSET_TMP}/source/Samples/000 - Tonal/TNL-RUSTECHO.wav" \
  -ac 1 -ar 48000 -f s16le "${APSIS_ASSET_TMP}/tension.raw"

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
  "${APSIS_ASSET_TMP}/ambient.raw" \
  "${APSIS_ASSET_TMP}/pulse.raw" \
  "${APSIS_ASSET_TMP}/percussion.raw" \
  "${APSIS_ASSET_TMP}/tension.raw" \
  "${APSIS_REPOSITORY_ROOT}/assets/music/issue230-mechsounds.sf2" \
  "${APSIS_REPOSITORY_ROOT}/assets/music/issue230-layer-demo.mid"

sha256sum \
  "${APSIS_REPOSITORY_ROOT}/assets/music/issue230-mechsounds.sf2" \
  "${APSIS_REPOSITORY_ROOT}/assets/music/issue230-layer-demo.mid"
