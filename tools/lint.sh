#!/usr/bin/env bash
set -euo pipefail

if (($# != 0)); then
  echo "usage: tools/lint.sh" >&2
  exit 2
fi

tidy=${CLANG_TIDY:-clang-tidy-20}
runner=${RUN_CLANG_TIDY:-run-clang-tidy-20}
compiler=${CLANGXX:-clang++-20}
build_dir=${APSIS_DRIFT_TIDY_BUILD_DIR:-build-tidy}
jobs=${APSIS_DRIFT_TIDY_JOBS:-2}

for tool in "$tidy" "$runner" "$compiler"; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: $tool was not found; install Clang/clang-tidy 20.x" >&2
    exit 1
  fi
done

tidy_version=$($tidy --version)
if [[ ! "$tidy_version" =~ version[[:space:]]20\. ]]; then
  echo "error: linting requires clang-tidy 20.x; found: $tidy_version" >&2
  exit 1
fi

compiler_version=$($compiler --version)
if [[ ! "$compiler_version" =~ version[[:space:]]20\. ]]; then
  echo "error: linting requires Clang 20.x; found: $compiler_version" >&2
  exit 1
fi

if [[ ! "$jobs" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: APSIS_DRIFT_TIDY_JOBS must be a positive integer" >&2
  exit 2
fi

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

tools/check_nolint.sh --self-test
tools/check_nolint.sh

file_regex="^${repo_root}/((src|test)/(?!godot/).*|(src|test)/godot/((contact_(surface|patch)|saved_contact_geometry)(_test)?|freedom_planetary_fixture|freedom_minor_fixture|saved_minor_flight_test))[.]cpp$"
# APSIS_DRIFT_TIDY_FILES accepts newline/space-separated repository-relative .cpp
# paths. Unset retains the original full-run regex and invocation; an empty value
# explicitly selects nothing. Escape filenames before constructing the regex.
if [[ ${APSIS_DRIFT_TIDY_FILES+x} ]]; then
  file_regex=$(python3 - "$repo_root" "$file_regex" <<'PY'
import os
from pathlib import Path
import re
import sys

root, allowed = sys.argv[1:]
files = []
for name in os.environ["APSIS_DRIFT_TIDY_FILES"].split():
    path = Path(name)
    if path.is_absolute() or any(part in (".", "..") for part in name.split("/")):
        sys.exit(f"error: expected repository-relative TU path: {name}")
    absolute = f"{root}/{name}"
    if path.is_file() and re.search(allowed, absolute):
        files.append(re.escape(absolute))
if files:
    print("^(?:" + "|".join(sorted(set(files))) + ")$")
PY
  )
  if [[ -z "$file_regex" ]]; then
    echo "Skipping clang-tidy: no changed files match the existing TU scope."
    exit 0
  fi
fi

cmake -S . -B "$build_dir" \
  -DCMAKE_CXX_COMPILER="$compiler" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DAPSIS_DRIFT_RTAUDIO=ON \
  -DAPSIS_DRIFT_MIDI_SPIKE=ON \
  -DAPSIS_DRIFT_GODOT_SPIKE=ON \
  -DAPSIS_DRIFT_GODOT_LIVE=OFF

"$runner" \
  -p "$build_dir" \
  -j "$jobs" \
  -clang-tidy-binary "$tidy" \
  -warnings-as-errors='*' \
  -quiet \
  "$file_regex"
