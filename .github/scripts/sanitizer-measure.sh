#!/usr/bin/env bash
# Measurement helpers for .github/workflows/sanitizer-measure.yml (issue #744).
#
# Host-side subcommands (run on the GitHub-hosted runner, need docker):
#   toolchain <compiler-id> <image@sha256:...> <local-tag> <outdir>
#       Install the compiler toolchain into the digest-pinned base image inside
#       a container limited to --cpus=2 --memory=2g --memory-swap=2g, then
#       commit it as <local-tag> for this job only.
#   run <local-tag> <outdir> <phase> -- <command> [args...]
#       Run one measured phase inside a fresh container with the same limits,
#       the workspace mounted at the same absolute path, as the runner's uid.
#       Records docker's OOMKilled flag and exit code in containers.tsv.
#   tests <outdir> <name> <ctest-json> <junit-xml>
#       Join per-test times from the JUnit file with each test's TIMEOUT.
#
# Container-side subcommands (called by "run"):
#   env <outdir>
#   phase <outdir> <name> -- <command> [args...]
#       Wall time, exit status, peak RSS of the largest single process and the
#       container cgroup's memory.peak, memory.max and oom_kill delta.
#
# Exit status: the measured command's own status. 124 = inner deadline hit;
# 137 = SIGKILL (with oom_kill_delta > 0 or OOMKilled=true: out of memory).
# A build tool whose child was OOM-killed usually exits 1 or 2, so always read
# the oom_kill delta. Nothing here changes a limit, deadline or compiler flag.
set -euo pipefail

readonly LIMITS=(--cpus=2 --memory=2g --memory-swap=2g)

die() { printf 'sanitizer-measure: %s\n' "$*" >&2; exit 2; }

check_name() {
  case "$1" in
    *[!A-Za-z0-9._-]* | '') die "bad name '$1'" ;;
  esac
}

summary() {
  # Append one line to the job summary when the runner provides one; never fail.
  if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    { printf '%s\n' "$1" >> "$GITHUB_STEP_SUMMARY"; } 2>/dev/null || true
  fi
}

cg_read() {
  local f="/sys/fs/cgroup/$1"
  if [[ -r "$f" ]]; then tr '\n' ' ' < "$f" | sed 's/ *$//'; else printf 'na'; fi
}

cg_oom_kills() {
  local f=/sys/fs/cgroup/memory.events
  if [[ -r "$f" ]]; then
    awk '$1 == "oom_kill" { print $2; found = 1 } END { if (!found) print "na" }' "$f"
  else
    printf 'na\n'
  fi
}

have_gnu_time() { [[ -x /usr/bin/time ]] && /usr/bin/time -v true >/dev/null 2>&1; }

cmd_env() {
  local out=$1
  mkdir -p "$out"
  {
    printf 'date_utc\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'kernel\t%s\n' "$(uname -r)"
    printf 'nproc_visible\t%s\n' "$(nproc 2>/dev/null || printf 'na')"
    printf 'cgroup_cpu_max\t%s\n' "$(cg_read cpu.max)"
    printf 'cgroup_memory_max\t%s\n' "$(cg_read memory.max)"
    printf 'cgroup_memory_swap_max\t%s\n' "$(cg_read memory.swap.max)"
    printf 'cgroup_oom_kill_at_start\t%s\n' "$(cg_oom_kills)"
    printf 'disk_workspace\t%s\n' "$(df -Pk . | awk 'NR == 2 { print $4 " KiB free of " $2 }')"
    if have_gnu_time; then printf 'rss_source\tGNU time -v\n'
    else printf 'rss_source\tpython getrusage(RUSAGE_CHILDREN)\n'; fi
  } | tee "$out/environment.tsv"
}

cmd_phase() {
  local out=$1 name=$2
  shift 2
  [[ ${1:-} == -- ]] || die 'phase: expected -- before the command'
  shift
  (($# > 0)) || die 'phase: missing command'
  check_name "$name"
  mkdir -p "$out"
  local tsv="$out/phases.tsv" log="$out/$name.log" rusage="$out/$name.rusage"
  if [[ ! -e "$tsv" ]]; then
    printf 'phase\texit\twall_s\tmax_rss_kb_largest_process\tcgroup_memory_peak_bytes\tcgroup_memory_max\tcgroup_oom_kill_delta\tstart_utc\n' > "$tsv"
  fi
  local oom0 start t0 t1 rc=0
  oom0=$(cg_oom_kills)
  start=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  t0=$(date +%s.%N)
  printf '::group::%s\n' "$name"
  set +e
  if have_gnu_time; then
    /usr/bin/time -v -o "$rusage" "$@" 2>&1 | tee "$log"
    rc=${PIPESTATUS[0]}
  else
    python3 - "$rusage" "$@" <<'PY' 2>&1 | tee "$log"
import resource, subprocess, sys
out, cmd = sys.argv[1], sys.argv[2:]
rc = subprocess.call(cmd)
ru = resource.getrusage(resource.RUSAGE_CHILDREN)
with open(out, "w") as fh:
    fh.write(f"Maximum resident set size (kbytes): {ru.ru_maxrss}\n")
    fh.write(f"Exit status: {rc}\n")
sys.exit(rc if rc >= 0 else 128 - rc)
PY
    rc=${PIPESTATUS[0]}
  fi
  set -e
  printf '::endgroup::\n'
  t1=$(date +%s.%N)
  local wall rss peak max oom1 delta
  wall=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.1f", b - a }')
  rss=$(awk -F': ' '/Maximum resident set size/ { print $2 }' "$rusage" 2>/dev/null || true)
  peak=$(cg_read memory.peak)
  max=$(cg_read memory.max)
  oom1=$(cg_oom_kills)
  if [[ $oom0 =~ ^[0-9]+$ && $oom1 =~ ^[0-9]+$ ]]; then delta=$((oom1 - oom0)); else delta=na; fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$rc" "$wall" "${rss:-na}" "$peak" "$max" "$delta" "$start" >> "$tsv"
  printf 'phase=%s exit=%s wall_s=%s max_rss_kb=%s cgroup_peak=%s cgroup_max=%s oom_kill_delta=%s\n' \
    "$name" "$rc" "$wall" "${rss:-na}" "$peak" "$max" "$delta"
  return "$rc"
}

toolchain_script() {
  # Same package sources as the existing CI: Ubuntu 24.04's GCC 13, and
  # Clang 20 from apt.llvm.org via llvm.sh.
  cat <<'SH'
set -eux
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends ca-certificates cmake make ninja-build git python3 time zstd xz-utils file
SH
  case "$1" in
    gcc-13)
      cat <<'SH'
apt-get install -y --no-install-recommends g++-13
g++-13 --version
SH
      ;;
    clang-20)
      cat <<'SH'
apt-get install -y --no-install-recommends wget lsb-release software-properties-common gnupg g++
wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
bash /tmp/llvm.sh 20
apt-get install -y libclang-rt-20-dev
clang++-20 --version
SH
      ;;
    *) die "toolchain: unknown compiler '$1'" ;;
  esac
  cat <<'SH'
cmake --version
dpkg-query -W -f '${Package}\t${Version}\n' | grep -E '^(g\+\+(-13)?|gcc-13|clang-20|libclang-rt-20-dev|libstdc\+\+-[0-9]+-dev|libasan[0-9]+|libubsan1|cmake|ninja-build|make)\s' || true
SH
}

cmd_toolchain() {
  local id=$1 image=$2 tag=$3 out=$4
  check_name "$id"
  [[ $image == *@sha256:* ]] || die "toolchain: image must be pinned by digest: $image"
  mkdir -p "$out"
  local name="sanitizer-tc-$id-$$" rc=0
  docker pull -q "$image"
  set +e
  local script
  script="$(cd "$out" && pwd)/toolchain-$id.sh"
  toolchain_script "$id" > "$script"
  docker run --name "$name" "${LIMITS[@]}" -v "$script:/tmp/toolchain.sh:ro" "$image" \
    timeout -s TERM -k 30 11m bash /tmp/toolchain.sh 2>&1 | tee "$out/toolchain-$id.log"
  rc=${PIPESTATUS[0]}
  set -e
  if ((rc == 0)); then
    docker commit -q "$name" "$tag" >/dev/null
  fi
  docker rm -f "$name" >/dev/null
  {
    printf 'base_image\t%s\n' "$image"
    printf 'base_image_id\t%s\n' "$(docker image inspect -f '{{.Id}}' "$image")"
    printf 'toolchain_exit\t%s\n' "$rc"
  } | tee -a "$out/toolchain-$id.tsv"
  return "$rc"
}

cmd_run() {
  local tag=$1 out=$2 name=$3
  shift 3
  [[ ${1:-} == -- ]] || die 'run: expected -- before the command'
  shift
  check_name "$name"
  mkdir -p "$out"
  local ws="${GITHUB_WORKSPACE:-$PWD}" cname="sanitizer-$name-$$" rc=0
  set +e
  docker run --name "$cname" "${LIMITS[@]}" --ulimit core=-1 \
    --user "$(id -u):$(id -g)" -e HOME=/tmp -e CXX -e ASAN_OPTIONS -e UBSAN_OPTIONS \
    -v "$ws:$ws" -w "$ws" "$tag" \
    bash .github/scripts/sanitizer-measure.sh phase "$out" "$name" -- "$@"
  rc=$?
  set -e
  local oomk exitc
  oomk=$(docker inspect -f '{{.State.OOMKilled}}' "$cname" 2>/dev/null || printf 'na')
  exitc=$(docker inspect -f '{{.State.ExitCode}}' "$cname" 2>/dev/null || printf 'na')
  docker rm -f "$cname" >/dev/null 2>&1 || true
  if [[ ! -e "$out/containers.tsv" ]]; then
    printf 'phase\tdocker_exit\tdocker_oom_killed\tlimits\n' > "$out/containers.tsv"
  fi
  printf '%s\t%s\t%s\t%s\n' "$name" "$exitc" "$oomk" "${LIMITS[*]}" >> "$out/containers.tsv"
  local row
  row=$(awk -F'\t' -v p="$name" '$1 == p { r = $0 } END { print r }' "$out/phases.tsv" 2>/dev/null || true)
  if [[ -n "$row" ]]; then
    summary "| ${row//$'\t'/ | } | docker OOMKilled=$oomk |"
  else
    # The measuring shell itself died (e.g. OOM-killed): no phase row.
    printf '%s\t%s\tna\tna\tna\tna\tna\tna\n' "$name" "$exitc" >> "$out/phases.tsv"
    summary "| $name | $exitc | (no in-container row) | docker OOMKilled=$oomk |"
  fi
  printf 'container phase=%s docker_exit=%s docker_oom_killed=%s\n' "$name" "$exitc" "$oomk"
  return "$rc"
}

cmd_tests() {
  local out=$1 name=$2 json=$3 junit=$4
  check_name "$name"
  mkdir -p "$out"
  python3 - "$json" "$junit" "$out/$name-tests.tsv" <<'PY'
import json, sys, xml.etree.ElementTree as ET
json_path, junit_path, out_path = sys.argv[1:4]
timeouts = {}
try:
    with open(json_path) as fh:
        for test in json.load(fh).get("tests", []):
            limit = "default"
            for prop in test.get("properties", []):
                if prop.get("name") == "TIMEOUT":
                    limit = str(prop.get("value"))
            timeouts[test.get("name")] = limit
except (OSError, ValueError) as exc:
    print(f"tests: no ctest json ({exc})", file=sys.stderr)
rows = []
try:
    for case in ET.parse(junit_path).getroot().iter("testcase"):
        status = case.get("status") or ("fail" if case.find("failure") is not None else "run")
        rows.append((float(case.get("time") or 0.0), case.get("name"), status))
except (OSError, ET.ParseError) as exc:
    print(f"tests: no junit ({exc})", file=sys.stderr)
rows.sort(reverse=True)
def pct(secs, limit):
    try:
        value = float(limit)
    except ValueError:
        return "na"
    return f"{100 * secs / value:.0f}" if value > 0 else "na"
with open(out_path, "w") as fh:
    fh.write("test\ttime_s\ttimeout_s\tpct_of_timeout\tstatus\n")
    for secs, test, status in rows:
        limit = timeouts.get(test, "default")
        fh.write(f"{test}\t{secs:.2f}\t{limit}\t{pct(secs, limit)}\t{status}\n")
total = sum(r[0] for r in rows)
print(f"tests: {len(rows)} cases, {total:.1f} s summed test time; slowest 25:")
for secs, test, status in rows[:25]:
    print(f"  {secs:9.2f}s  timeout={timeouts.get(test, 'default'):>7}  {status:7}  {test}")
PY
}

sub=${1:-}
shift || true
case "$sub" in
  env) (($# == 1)) || die 'usage: env <outdir>'; cmd_env "$1" ;;
  phase) (($# >= 4)) || die 'usage: phase <outdir> <name> -- <command>'; cmd_phase "$@" ;;
  toolchain) (($# == 4)) || die 'usage: toolchain <id> <image@digest> <tag> <outdir>'; cmd_toolchain "$@" ;;
  run) (($# >= 5)) || die 'usage: run <tag> <outdir> <phase> -- <command>'; cmd_run "$@" ;;
  tests) (($# == 4)) || die 'usage: tests <outdir> <name> <ctest-json> <junit-xml>'; cmd_tests "$@" ;;
  *) die "unknown subcommand '$sub'" ;;
esac
