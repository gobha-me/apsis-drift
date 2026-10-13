# Contributing to Apsis Drift

The current goal is [native Freedom](docs/ROADMAP.md): station departure,
flight, exploration, landing, walking and a persistent return journey. Godot
presents authoritative C++ world and simulation state. The terminal frontend
and headless tests remain supported; terminal parity does not gate native work.

## Choose a bounded change

Read the [roadmap](docs/ROADMAP.md), [ownership decision](docs/GODOT_ADOPTION.md)
and the relevant [GitHub issue](https://github.com/gobha-me/apsis-drift/issues),
including its dated scope amendments. Historical milestones and deferred
horizons are preserved ideas, not the active implementation queue. Discuss a
new horizon's scope before implementing it; ordinary fixes can use the existing
issue scope. If no issue covers the change, the issue form asks for an observable
outcome, scope and verification.

Keep one PR focused on one result. Record newly discovered work as a linked
follow-up instead of growing the change indefinitely. Documentation, bug
reports and reproducible playtest feedback are useful contributions too.

## Find the code and build

| Area | Start here |
| --- | --- |
| Authoritative world, flight and saves | `src/`, `include/apsis_drift/`, `test/` |
| Native C++ bridge and adapters | `src/godot/`, `include/apsis_drift/godot/`, `test/godot/` |
| Native scenes, input and presentation | [Godot guide](godot/README.md), `godot/scripts/`, `godot/tests/` |
| Build options and target registration | [CMake map](cmake/README.md) |
| Product and system contracts | [Documentation index](docs/README.md) |
| Assets and licensing | [Provenance guide](docs/ASSET_PROVENANCE.md), `assets/` |

Use [development setup and commands](docs/DEVELOPMENT.md). The current native
extension and runtime runner target **Linux x86_64 with Godot 4.7.2**; other
native platforms remain unqualified. C++ headless tests do not require Godot.
Optional studies live in `godot/studies/` and are separate from saved gameplay.

Preserve deterministic seeds, independent random streams, stable identities
and explicit save/generator versions. Put authoritative behavior in C++; Godot
translates input and presents the results. Changes to compatibility must explain
how existing saves and recipes behave. Asset contributions need their source,
provenance and license metadata.

## Branch, verify and submit

Fork the repository on GitHub, clone your fork, then create a descriptive branch
from current `main`. Contributors with repository access can use a branch in
the project repository instead.

```sh
git switch main
git pull --ff-only
git switch -c fix/short-description
```

Use checks that exercise the changed behavior; the exact build and runner
commands are in [development and testing](docs/DEVELOPMENT.md).

- C++ publication changes require GCC and Clang build/test results. Test invalid
  dimensions, nonfinite inputs and buffer boundaries before visual checks.
- Format C/C++ with `tools/format.sh --fix`, then `tools/format.sh --check`;
  clang-format 20 is required. Run `tools/lint.sh` with Clang/clang-tidy 20.
- Native presentation changes need the relevant
  `python3 tools/test_godot_native.py --godot "$GODOT_BIN" --build-dir build-native --test NAME`
  contracts after building the native targets. Headless success does not prove
  controller feel, GPU appearance, audio devices or frame rate.
- For documentation, verify local links and compare command examples with the
  tools they describe. Keep public instructions free of private machine paths.

Keep build caches, generated captures and prepared assets out of commits. Reuse
existing build directories rather than creating a new dependency copy for each
check; keep GCC and Clang caches separate.

Push your branch to your fork and open a PR against the project's `main`.
Describe the problem and resulting behavior, link the issue, record the checks
actually run, and identify remaining limits. Keep the PR updated while CI and
review run; resolve failures against its current commit. Finish with a merged
PR or close it with the reason and any follow-up issue if the approach is
superseded. Retire merged branches rather than leaving stale work active.

Code is [BSD 3-Clause licensed](LICENSE.md); third-party content keeps its own
[notices](THIRD_PARTY_NOTICES.md) and asset licenses.
