#!/usr/bin/env bash
# Explicit Freedom station/flight selection. Studies use run_godot_study.sh.
set -euo pipefail

usage() {
    echo 'usage: tools/run_godot_native.sh (--new-game=SEED | --continue=ABSOLUTE_SAVE_PATH) [--headless-validate] [--audio-hum=ABSOLUTE_WAV --audio-propulsion=ABSOLUTE_WAV [--audio-persist=false]]' >&2
}

selection=''
headless_validate=false
audio_arguments=()
audio_hum=''
audio_propulsion=''
audio_persist=''
for argument in "$@"; do
    case "$argument" in
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
if [[ -z "$selection" ]]; then
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

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
native_build="${repo_dir}/build-native"
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
if [[ ! -f "$cache_file" ]] || \
   ! grep -Fqx 'APSIS_DRIFT_TERMINAL:BOOL=OFF' "$cache_file" || \
   ! grep -Fqx 'APSIS_DRIFT_GODOT_SPIKE:BOOL=ON' "$cache_file" || \
   ! grep -Fqx 'APSIS_DRIFT_GODOT_LIVE:BOOL=ON' "$cache_file"; then
    cmake -S "$repo_dir" -B "$native_build" -DCMAKE_BUILD_TYPE=Release \
        -DAPSIS_DRIFT_TERMINAL=OFF -DAPSIS_DRIFT_RTAUDIO=OFF \
        -DAPSIS_DRIFT_GODOT_SPIKE=ON -DAPSIS_DRIFT_GODOT_LIVE=ON
fi
cmake --build "$native_build" --target apsis_freedom_bridge --parallel 4

engine_args=(--path "${repo_dir}/godot" \
    --scene res://scenes/native_start_shell.tscn)
script_args=("$selection" "${audio_arguments[@]}")
if [[ "$headless_validate" == true ]]; then
    engine_args+=(--headless --audio-driver Dummy)
    script_args+=(--validate-only)
else
    python3 "${repo_dir}/tools/prepare_freedom_native_assets.py" \
        --output "${native_build}/native-freedom-assets"
    script_args+=("--assets=${native_build}/native-freedom-assets")
fi
exec "$engine" "${engine_args[@]}" -- "${script_args[@]}"
