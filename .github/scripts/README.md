# CI helper scripts

## Native sanitizer measurement (issue #744)

`sanitizer-measure.sh` backs `.github/workflows/sanitizer-measure.yml`, a
**manual-dispatch-only** measurement for the planned native ASan + UBSan
matrix. It is not part of the required CI, and nothing in it runs on push,
pull request or a schedule.

Per compiler (Clang 20, GCC 13) the workflow runs on a standard GitHub-hosted
`ubuntu-24.04` runner and does one sanitized build of the full native-core
configuration, using the sanitizer flags, CMake options and `ASAN_OPTIONS` /
`UBSAN_OPTIONS` from the closed PR #749. It then runs the full configured CTest
roster. CMake's per-test `TIMEOUT` values stay authoritative, because no
`--timeout` is passed.

Every toolchain, build, pack, unpack and test command runs inside
`docker run --cpus=2 --memory=2g --memory-swap=2g`, which reproduces the
2 CPU / 2 GiB sandbox. The base image is `ubuntu:24.04` pinned by digest. GCC 13
is Ubuntu's `g++-13`, and Clang 20 comes from apt.llvm.org via `llvm.sh`, the
same sources the existing CI uses. The toolchain is installed once per job
into a local image.

For each phase, `phases.tsv` records:

- wall time and exit status
- the peak RSS of the largest single process (GNU `time -v`)
- the container cgroup's `memory.peak`, `memory.max` and `oom_kill` delta

`containers.tsv` adds docker's `OOMKilled` flag. `<shard>-tests.tsv` lists
per-test times from `--output-junit`, joined to each test's `TIMEOUT`. All of
these upload as `sanitizer-measure-*` artifacts (7-day retention).

Exit codes:

- **124**: the phase hit its inner deadline.
- **137** together with `oom_kill_delta > 0` or `OOMKilled=true`: out of memory.

A build tool whose compiler or linker child was OOM-killed usually exits 1 or
2, so read the `oom_kill` delta.

The workflow builds once and tests in shards. The `build` job uploads the
build tree with a manifest (sha, compiler, image, workspace path, archive
sha256). Each `test` shard verifies that manifest and runs
`ctest -I <shard>,,<shard_count>`. `shard_count` (1-8, default 1) sets the
shard matrix through `fromJSON`.

If a heavy translation unit or link runs out of memory, change the dispatch
inputs rather than raising any limit:

- `build_jobs=1`
- `generator=Ninja` with `link_jobs=1`, which sets up separate `CMAKE_JOB_POOLS` compile and link pools
- `debug_info=line-tables` (`-g1`)
- `ctest_jobs=1`

No input drops a sanitizer or loosens a deadline. Each job stays within
90 minutes, and every heavy step has an inner deadline so the measurements
still upload when the step does not fit.

Lint:

```sh
actionlint .github/workflows/sanitizer-measure.yml
shellcheck .github/scripts/sanitizer-measure.sh
```
