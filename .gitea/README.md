# Gitea CI migration

The workflows here are additive during migration. GitHub CI and its required
checks remain in place. Runner activation and branch protection changes require
owner coordination; adding these files does not complete that migration.

## Native sanitizers

`workflows/sanitizers.yml` runs nightly at 08:17 UTC, on pushes to `main`, and
on manual dispatch. The `ci-build` runner must provide Bash, Make, Git,
CMake 3.28+, Python 3.11+, GCC 13 and Clang 20 with their ASan/UBSan runtimes,
a Node runtime compatible with the retained `actions/checkout@v7`, and network
access to that action and the existing pinned CMake dependencies. The workflow
does not install packages or change dependency pins.

Before activation, verify that the selected Gitea server and runner enforce
workflow concurrency cancellation, matrix serialization (`strategy.max-parallel: 1`)
and the 180-minute job deadline. Support is version-dependent: the
[Gitea 1.26 compatibility guide](https://docs.gitea.com/1.26/usage/actions/comparison/)
lists job `timeout-minutes` as ignored. Declaring a deadline in YAML does not
qualify such a deployment. Verify that only one compiler entry runs at a time,
even when multiple runner slots are available. Keep these jobs disabled until all
three limits are proven.

Both compiler entries build the complete native-core configuration, including
the live bridge and the retained research contracts, then run the full configured
CTest roster with two workers. Compiler entries run one at a time. Address and
undefined-behavior instrumentation applies to all C++ translation units and
executable/shared-library links, with frame pointers, leak detection and fatal
sanitizer findings. CMake's test deadlines remain authoritative. Core dumps are
enabled for test processes; runner core storage and collection must be configured
when activating the runner.

Main had no checked-in sanitizer matrix or TSan configuration when this workflow
was added. The historical partial ASan/UBSan evidence in
[`ORBIT_ASSIST_18.md`](../docs/ORBIT_ASSIST_18.md#recorded-local-evidence) is not a
full-suite result. This workflow adds full native ASan/UBSan instrumentation;
it does not claim a pre-existing full sanitizer suite passed or add a new TSan
policy.

The Gitea runners are not live yet. This workflow is **unexercised on Gitea**;
local syntax or configuration checks do not prove runner compatibility, complete
sanitizer success or that both jobs finish within the 180-minute job limit.

This is issue [#744](https://github.com/gobha-me/apsis-drift/issues/744).
It does not modify the GitHub CI work in PRs #739, #742 or #743; future migration
steps must reconcile that overlap without dropping coverage.

Validate the Gitea workflows locally with the declared runner labels:

```sh
actionlint -config-file .gitea/actionlint.yaml .gitea/workflows/*.yml
```
