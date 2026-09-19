#!/usr/bin/env bash
# Read-only native renderer study. Does not install an engine or change saves.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
study_build="${repo_dir}/build"
study_data="${repo_dir}/build-godot"
engine="${GODOT_BIN:-}"
if [[ -z "$engine" ]]; then
    if command -v godot >/dev/null 2>&1; then
        engine="$(command -v godot)"
    elif [[ -x "${study_data}/tools/Godot_v4.7.2-stable_linux.x86_64" ]]; then
        engine="${study_data}/tools/Godot_v4.7.2-stable_linux.x86_64"
    else
        echo 'Set GODOT_BIN to a Godot 4 executable; see experiments/godot-freedom/README.md.' >&2
        exit 1
    fi
fi
if [[ ! -x "$engine" ]]; then
    echo 'GODOT_BIN must be an executable path.' >&2
    exit 1
fi
live_mode=OFF
relief_mode=false
stream_mode=false
prepare_only=false
for argument in "$@"; do
    if [[ "$argument" == '--live=true' ]]; then live_mode=ON; fi
    if [[ "$argument" == '--stream=true' ]]; then live_mode=ON; stream_mode=true; fi
    if [[ "$argument" == '--relief=true' ]]; then relief_mode=true; fi
    if [[ "$argument" == '--prepare-only=true' ]]; then prepare_only=true; fi
done
cache_file="${study_build}/CMakeCache.txt"
# Avoid needlessly regenerating third-party bindings on every launch.
if [[ ! -f "$cache_file" ]] || \
   ! grep -Fqx 'APSIS_DRIFT_GODOT_SPIKE:BOOL=ON' "$cache_file" || \
   { [[ "$live_mode" == ON ]] && ! grep -Fqx 'APSIS_DRIFT_GODOT_LIVE:BOOL=ON' "$cache_file"; }; then
    configure_args=(-DAPSIS_DRIFT_GODOT_SPIKE=ON)
    if [[ "$live_mode" == ON ]]; then configure_args+=(-DAPSIS_DRIFT_GODOT_LIVE=ON); fi
    cmake -S "$repo_dir" -B "$study_build" "${configure_args[@]}"
fi
build_targets=(apsis-drift-godot-snapshot)
if [[ "$live_mode" == ON ]]; then build_targets+=(apsis_freedom_bridge); fi
cmake --build "$study_build" --target "${build_targets[@]}" -j 4
if [[ "$live_mode" == ON ]]; then
    # Restore this build's plugin even if another compiler last staged its own.
    cmake -E copy \
        "${study_build}/experiments/godot-freedom/bin/libapsis_freedom_bridge.so" \
        "${repo_dir}/experiments/godot-freedom/bin/libapsis_freedom_bridge.so.launch.new"
    cmake -E rename \
        "${repo_dir}/experiments/godot-freedom/bin/libapsis_freedom_bridge.so.launch.new" \
        "${repo_dir}/experiments/godot-freedom/bin/libapsis_freedom_bridge.so"
fi
mkdir -p "$study_data"
snapshot="${study_data}/snapshot-42.json"
if [[ "$relief_mode" == true ]]; then snapshot="${study_data}/snapshot-42-relief-1.json"; fi
if [[ "$stream_mode" == true ]]; then snapshot="${study_data}/snapshot-42-stream-${relief_mode}.json"; fi
if [[ ! -f "$snapshot" ]]; then
    if [[ "$stream_mode" == true ]]; then
        relief_version=0
        if [[ "$relief_mode" == true ]]; then relief_version=1; fi
        "${study_build}/experiments/godot-freedom/apsis-drift-godot-snapshot" "$snapshot" 42 3 32000 "$relief_version"
    elif [[ "$relief_mode" == true ]]; then
        "${study_build}/experiments/godot-freedom/apsis-drift-godot-snapshot" "$snapshot" 42 513 32000 1
    else
        "${study_build}/experiments/godot-freedom/apsis-drift-godot-snapshot" "$snapshot"
    fi
fi
if [[ "$prepare_only" == true ]]; then exit 0; fi
exec "$engine" --path "${repo_dir}/experiments/godot-freedom" --audio-driver Dummy -- \
    "--snapshot=${snapshot}" "--assets=${repo_dir}/assets/visual" "$@"
