#!/usr/bin/env bash
# Native title or explicit Freedom selection. Studies use run_godot_study.sh.
set -euo pipefail

usage() {
    echo 'usage: tools/run_godot_native.sh [--new-game=SEED | --continue=ABSOLUTE_SAVE_PATH] [--build-dir=DIR] [--headless-validate] [--audio-hum=ABSOLUTE_WAV --audio-propulsion=ABSOLUTE_WAV [--audio-persist=false]]' >&2
}

selection=''
requested_native_build=''
headless_validate=false
audio_arguments=()
audio_hum=''
audio_propulsion=''
audio_persist=''
for argument in "$@"; do
    case "$argument" in
        --build-dir=*)
            if [[ -n "$requested_native_build" || "$argument" == *= ]]; then
                usage
                exit 2
            fi
            requested_native_build="${argument#*=}"
            ;;
        --new-game=*|--continue=*)
            if [[ -n "$selection" || "$argument" == *= ]]; then
                usage
                exit 2
            fi
            selection="$argument"
            ;;
        --headless-validate)
            if [[ "$headless_validate" == true ]]; then
                usage
                exit 2
            fi
            headless_validate=true
            ;;
        --audio-hum=*|--audio-propulsion=*)
            value="${argument#*=}"
            if [[ "$value" != /* ]]; then usage; exit 2; fi
            if [[ "$argument" == --audio-hum=* ]]; then
                if [[ -n "$audio_hum" ]]; then usage; exit 2; fi
                audio_hum="$value"
            else
                if [[ -n "$audio_propulsion" ]]; then usage; exit 2; fi
                audio_propulsion="$value"
            fi
            audio_arguments+=("$argument")
            ;;
        --audio-persist=*)
            value="${argument#*=}"
            if [[ -n "$audio_persist" || ( "$value" != true && "$value" != false ) ]]; then usage; exit 2; fi
            audio_persist="$value"
            audio_arguments+=("$argument")
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done
if [[ -z "$selection" && "$headless_validate" == true ]]; then
    usage
    exit 2
fi
if [[ ( -n "$audio_hum" && -z "$audio_propulsion" ) || \
      ( -z "$audio_hum" && -n "$audio_propulsion" ) || \
      ( -n "$audio_persist" && -z "$audio_hum" ) ]]; then usage; exit 2; fi
if [[ "$selection" == --continue=* && "${selection#--continue=}" != /* ]]; then
    echo 'Continue requires an absolute save path.' >&2
    exit 2
fi

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
if ! native_build="$(python3 - "$repo_dir" "$requested_native_build" <<'PY'
from pathlib import Path
import sys

root = Path(sys.argv[1])
value = sys.argv[2] or 'build-native'
if any(ord(character) < 32 or ord(character) == 127 for character in value):
    raise SystemExit('Build directory contains a control character.')
path = Path(value)
try:
    path = (path if path.is_absolute() else root / path).resolve()
except (OSError, RuntimeError):
    raise SystemExit('Cannot resolve the build directory.')
reserved = [root / name for name in
            ('godot', 'src', 'include', 'test', 'tools', 'docs', 'assets', '.git')]
if path == root or path == Path(path.anchor) or any(
        path == directory or directory in path.parents for directory in reserved):
    raise SystemExit('Choose a build directory outside the repository source directories.')
print(path)
PY
)"; then
    usage
    exit 2
fi
engine="${GODOT_BIN:-}"
if [[ -z "$engine" ]]; then
    if command -v godot >/dev/null 2>&1; then
        engine="$(command -v godot)"
    elif [[ -x "${repo_dir}/build-godot/tools/Godot_v4.7.2-stable_linux.x86_64" ]]; then
        engine="${repo_dir}/build-godot/tools/Godot_v4.7.2-stable_linux.x86_64"
    else
        echo 'Set GODOT_BIN to a Godot 4 executable.' >&2
        exit 1
    fi
fi
if [[ ! -x "$engine" ]]; then
    echo 'GODOT_BIN must be an executable path.' >&2
    exit 1
fi

cache_file="${native_build}/CMakeCache.txt"
if [[ -f "$cache_file" ]] && \
   ! grep -Fqx "CMAKE_HOME_DIRECTORY:INTERNAL=${repo_dir}" "$cache_file"; then
    echo 'Selected build cache does not belong to this checkout.' >&2
    exit 2
fi
if [[ ! -f "$cache_file" ]] || \
   ! grep -Fqx 'APSIS_DRIFT_TERMINAL:BOOL=OFF' "$cache_file" || \
   ! grep -Fqx 'APSIS_DRIFT_GODOT_SPIKE:BOOL=ON' "$cache_file" || \
   ! grep -Fqx 'APSIS_DRIFT_GODOT_LIVE:BOOL=ON' "$cache_file"; then
    cmake -S "$repo_dir" -B "$native_build" -DCMAKE_BUILD_TYPE=Release \
        -DAPSIS_DRIFT_TERMINAL=OFF -DAPSIS_DRIFT_RTAUDIO=OFF \
        -DAPSIS_DRIFT_GODOT_SPIKE=ON -DAPSIS_DRIFT_GODOT_LIVE=ON
fi
cmake --build "$native_build" --target apsis_freedom_bridge --parallel 4
# POST_BUILD does not run for an unchanged target. Install the explicitly
# selected artifact even when a previous launcher used another build.
mkdir -p -- "${repo_dir}/godot/bin"
bridge_stage="${repo_dir}/godot/bin/libapsis_freedom_bridge.so.launch.$$.new"
trap 'rm -f -- "$bridge_stage"' EXIT
cp -- "${native_build}/src/godot/bin/libapsis_freedom_bridge.so" \
    "$bridge_stage"
mv -f -- "$bridge_stage" \
    "${repo_dir}/godot/bin/libapsis_freedom_bridge.so"
trap - EXIT

engine_args=(--path "${repo_dir}/godot" \
    --scene res://scenes/native_start_shell.tscn)
script_args=("${audio_arguments[@]}")
if [[ -n "$selection" ]]; then script_args+=("$selection"); fi
if [[ "$headless_validate" == true ]]; then
    engine_args+=(--headless --audio-driver Dummy)
    script_args+=(--validate-only)
else
    python3 "${repo_dir}/tools/prepare_freedom_native_assets.py" \
        --output "${native_build}/native-freedom-assets"
    script_args+=("--assets=${native_build}/native-freedom-assets")
fi
exec "$engine" "${engine_args[@]}" -- "${script_args[@]}"
