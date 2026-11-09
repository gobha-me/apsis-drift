#!/usr/bin/env bash
# Measurement helpers for .gitea/workflows/sanitizer-measure.yml (issue #744).
#
#   sanitizer-measure.sh env    <outdir>
#   sanitizer-measure.sh phase  <outdir> <name> -- <command> [args...]
#   sanitizer-measure.sh tests  <outdir> <name> <ctest-json> <junit-xml>
#
# "phase" runs one command and appends a row to <outdir>/phases.tsv with its
# wall time, exit status, the peak RSS of its largest single process (GNU time
# when present, otherwise getrusage(RUSAGE_CHILDREN) via Python) and the job
# cgroup's memory.peak, memory.max and oom_kill delta. Output goes to
# <outdir>/<name>.log and is echoed to the job log. The command's exit status
# is returned unchanged. It never alters limits, deadlines or flags.
set -euo pipefail

die() { printf 'sanitizer-measure: %s\n' "$*" >&2; exit 2; }

cg_read() {
  # cg_read <file> -> value or "na"
  local f="/sys/fs/cgroup/$1"
  if [[ -r "$f" ]]; then tr '\n' ' ' < "$f" | sed 's/ *$//'; else printf 'na'; fi
}

cg_oom_kills() {
  local f=/sys/fs/cgroup/memory.events
  if [[ -r "$f" ]]; then awk '$1 == "oom_kill" { print $2; found = 1 } END { if (!found) print "na" }' "$f"
  else printf 'na\n'; fi
}

cmd_env() {
  local out=$1
  mkdir -p "$out"
  {
    printf 'date_utc\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'host\t%s\n' "$(uname -n)"
    printf 'kernel\t%s\n' "$(uname -r)"
    printf 'nproc\t%s\n' "$(nproc 2>/dev/null || printf 'na')"
    printf 'cgroup_cpu_max\t%s\n' "$(cg_read cpu.max)"
    printf 'cgroup_memory_max\t%s\n' "$(cg_read memory.max)"
    printf 'cgroup_memory_swap_max\t%s\n' "$(cg_read memory.swap.max)"
    printf 'cgroup_memory_peak_at_start\t%s\n' "$(cg_read memory.peak)"
    printf 'cgroup_oom_kill_at_start\t%s\n' "$(cg_oom_kills)"
    printf 'meminfo_total_kb\t%s\n' "$(awk '/^MemTotal:/ { print $2 }' /proc/meminfo)"
    printf 'meminfo_available_kb\t%s\n' "$(awk '/^MemAvailable:/ { print $2 }' /proc/meminfo)"
    printf 'disk_workspace\t%s\n' "$(df -Pk . | awk 'NR == 2 { print $4 " KiB free of " $2 }')"
    if [[ -x /usr/bin/time ]] && /usr/bin/time -v true >/dev/null 2>&1; then
      printf 'rss_source\tGNU time -v\n'
    else
      printf 'rss_source\tpython getrusage(RUSAGE_CHILDREN)\n'
    fi
  } | tee "$out/environment.tsv"
  if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    printf '| phase | exit | wall s | max RSS KiB (largest process) | cgroup memory.peak | oom_kill delta |\n|---|---|---|---|---|---|\n' \
      2>/dev/null >> "$GITHUB_STEP_SUMMARY" || true
  fi
}

cmd_phase() {
  local out=$1 name=$2
  shift 2
  [[ ${1:-} == -- ]] || die 'phase: expected -- before the command'
  shift
  (($# > 0)) || die 'phase: missing command'
  case "$name" in
    *[!A-Za-z0-9._-]* | '') die "phase: bad phase name '$name'" ;;
  esac
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
  if [[ -x /usr/bin/time ]] && /usr/bin/time -v true >/dev/null 2>&1; then
    set +e
    /usr/bin/time -v -o "$rusage" "$@" 2>&1 | tee "$log"
    rc=${PIPESTATUS[0]}
    set -e
  else
    set +e
    python3 - "$rusage" "$@" <<'PY' 2>&1 | tee "$log"
import resource, subprocess, sys
out, cmd = sys.argv[1], sys.argv[2:]
rc = subprocess.call(cmd)
ru = resource.getrusage(resource.RUSAGE_CHILDREN)
with open(out, "w") as fh:
    fh.write(f"Maximum resident set size (kbytes): {ru.ru_maxrss}\n")
    fh.write(f"User time (seconds): {ru.ru_utime:.2f}\n")
    fh.write(f"System time (seconds): {ru.ru_stime:.2f}\n")
    fh.write(f"Exit status: {rc}\n")
sys.exit(rc if rc >= 0 else 128 - rc)
PY
    rc=${PIPESTATUS[0]}
    set -e
  fi
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
  if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
    printf '| %s | %s | %s | %s | %s | %s |\n' "$name" "$rc" "$wall" "${rss:-na}" "$peak" "$delta" \
      2>/dev/null >> "$GITHUB_STEP_SUMMARY" || true
  fi
  return "$rc"
}

cmd_tests() {
  local out=$1 name=$2 json=$3 junit=$4
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
    root = ET.parse(junit_path).getroot()
    for case in root.iter("testcase"):
        status = case.get("status") or ("fail" if case.find("failure") is not None else "run")
        rows.append((float(case.get("time") or 0.0), case.get("name"), status))
except (OSError, ET.ParseError) as exc:
    print(f"tests: no junit ({exc})", file=sys.stderr)
rows.sort(reverse=True)
with open(out_path, "w") as fh:
    fh.write("test\ttime_s\ttimeout_s\tpct_of_timeout\tstatus\n")
    for secs, test, status in rows:
        limit = timeouts.get(test, "default")
        pct = f"{100 * secs / float(limit):.0f}" if limit.replace(".", "", 1).isdigit() and float(limit) > 0 else "na"
        fh.write(f"{test}\t{secs:.2f}\t{limit}\t{pct}\t{status}\n")
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
  tests) (($# == 4)) || die 'usage: tests <outdir> <name> <ctest-json> <junit-xml>'; cmd_tests "$@" ;;
  *) die "unknown subcommand '$sub'" ;;
esac
