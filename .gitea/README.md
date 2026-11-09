# Gitea CI

GitHub CI and its required checks are unchanged. Nothing here runs on push,
pull request or a schedule.

## Native sanitizer measurement (issue #744)

`workflows/sanitizer-measure.yml` is a **manual-dispatch-only** measurement
for the planned native ASan + UBSan matrix. Per compiler (Clang 20, GCC 13) it
does one sanitized build of the full native-core configuration (same flags and
CMake options as the closed PR #749) and runs the full configured CTest roster
at the existing per-test deadlines. It records, per phase (configure, build,
pack, unpack, ctest), the wall time, the exit status, the peak RSS of the
largest single process and the job cgroup's `memory.peak`, `memory.max` and
OOM-kill count, plus per-test times joined to each test's `TIMEOUT`. Everything
is uploaded as `sanitizer-measure-*` artifacts (upload-artifact v4).

It is shaped as build once, test in shards: the `build` job uploads the build
tree as `sanitizer-tree-<compiler>` and each `test` shard downloads it and runs
`ctest -I <shard>,,<shard_count>`. `shard_count` defaults to 1; the matrix is
static (1-4) because Gitea 1.25.4 has no needs-dependent matrix, so unused
shards finish as no-ops. The shard count for the real matrix is to be set from
these measurements.

The runner sandbox is 2 CPU and 2 GiB. If a heavy translation unit or link is
killed (exit 137, or a non-zero `oom_kill` delta), retry with the dispatch
inputs instead of raising any limit: `build_jobs=1`, `generator=Ninja` with
`link_jobs=1` (separate compile and link job pools), `debug_info=line-tables`
(`-g1`), and `ctest_jobs=1`. No sanitizer is dropped and no CTest deadline is
loosened by any input. Each job stays within 90 minutes, and each heavy step
has an inner deadline so the measurements upload even when it does not fit.

The `ci-build` runner must already provide Bash, Make (or Ninja), Git,
CMake 3.28+, Python 3, GCC 13 and Clang 20 with their ASan/UBSan runtimes, tar
and network access to the pinned CMake dependencies. The workflow installs
nothing. Lint locally with the declared runner labels:

```sh
actionlint -config-file .gitea/actionlint.yaml .gitea/workflows/*.yml
```
