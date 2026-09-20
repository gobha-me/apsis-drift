# Headless editor import timing workaround

2026-09-19, native study 25 preparation. This is an editor-only workaround,
not a change to the game, bridge, physics, audio timing or runtime frame rate.

The selected Godot 4.7.2 binary aborted during immediate headless editor exit
after importing the native extension. The console reached script registration
and editor layout without a diagnostic; the retained core reported SIGABRT.
Do not count that run as a successful import just because its output looked
complete. Raw crash metadata stays local.

Isolation used fresh independent projects, never the running playtest:

| Import fixture | Immediate exit result |
| --- | --- |
| Empty project, no native bridge | Exit 0 |
| Study 20 source, no native bridge | Exit 0 |
| Study 20 source with bridge | Exit 134 (also reproduced from a fresh source copy) |
| Empty project with bridge only | Exit 134 |
| Study 20 with bridge, 1000 ms frame delay | Exit 0 in three fresh independent imports |

The extension-dependent, immediate-exit behavior is consistent with upstream
[Godot #111048](https://github.com/godotengine/godot/issues/111048) and
[Godot #111645](https://github.com/godotengine/godot/issues/111645), which report
an editor documentation-generation timing failure. That is a supported
inference, not an exact local symbolized-stack identification. The former
reports that extra frame time can avoid the failure; required timing varies
by project.

For this project's isolated import step, use the selected Godot executable
with:

```sh
godot --headless --path experiments/godot-freedom --editor --import --quit --frame-delay 1000
```

Require a clean process exit and inspect the log. This is not an automatic
retry that hides a crash, and the delay is not a universal correctness fix.
Do not add it to gameplay, runtime tests, audio cadence probes or performance
benchmarks. Revalidate it when the engine, native extension or import workload
changes; remove it only after clean first-import testing establishes that the
underlying issue no longer applies.

Independent frozen-runtime controller and audio checks remain separate
evidence. Passing them does not make an editor crash harmless, and a successful
delayed import does not qualify the game's listening or rendering performance.
