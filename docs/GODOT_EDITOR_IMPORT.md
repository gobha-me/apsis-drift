# Headless editor import shutdown defect

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

The initial extension-dependent, immediate-exit behavior was consistent with upstream
[Godot #111048](https://github.com/godotengine/godot/issues/111048) and
[Godot #111645](https://github.com/godotengine/godot/issues/111645), which report
an editor documentation-generation timing failure. At that checkpoint this was
an inference, not an exact local symbolized-stack identification. The former
reports that extra frame time can avoid the failure; required timing varies
by project.

## Confirmed local cause, 2026-09-20

A follow-up used fresh bridge-only projects, with independent project and XDG
configuration/data/cache directories. Neither the active source project's
import cache nor a running/frozen playtest was modified. The selected executable
was `4.7.2.stable.official.ed1daf0bf` (SHA-256
`8d106cbe6144c2dc7e881d61d2429c1a8a76e6b22ef48bd5e48dcf934953f71e`).

| Fresh fixture, current bridge | Result |
| --- | --- |
| Empty project, no extension | Exit 0 |
| Empty project plus extension, immediate import/exit | SIGABRT in all three independent runs |
| Same extension, independent project, 1000 ms frame delay | Exit 0 |
| Independent project under debugger | Underlying SIGSEGV reproduced |
| Independent project with documentation-pointer watchpoint | Pointer cleared before deferred callback; SIGSEGV reproduced |

The debugger caught the original segmentation fault before Godot's crash handler
converted it into the abort seen by the shell. A pointer watchpoint recorded:

1. The editor documentation pointer was allocated.
2. Editor destruction cleared it to null.
3. The deferred extension-documentation callback ran with that pointer still null.
4. Documentation generation dereferenced the resulting null-derived hash-map
   receiver and faulted.

The executable is stripped: this is **not** a DWARF-symbolized stack. The callback
was identified independently through its embedded
`&EditorHelp::_gen_extensions_docs` callable name and associated function address,
then matched against its machine-code body and the selected-version source.
The inspected [Godot documentation lifecycle implementation](https://github.com/godotengine/godot/blob/4.7.2-stable/editor/doc/editor_help.cpp)
queues the callback from its worker, while `cleanup_doc()` waits for the worker,
deletes the documentation object and clears the pointer. The queued callback
does not check that pointer before calling `generate()`.
[Editor destruction](https://github.com/godotengine/godot/blob/4.7.2-stable/editor/editor_node.cpp)
invokes that cleanup; the later
[shutdown message-queue flush](https://github.com/godotengine/godot/blob/4.7.2-stable/main/main.cpp)
can execute the already-queued callback. The local fault is therefore an
upstream editor shutdown-lifetime defect, not merely an unexplained timing
correlation or evidence of a simulation failure.

Debugger process exit 0 was **not** counted as target success: its captured
inferior signal was SIGSEGV. Raw debugger/core records remain local because they
contain process and machine metadata. No engine binary, extension registration,
bridge implementation or shared build configuration was patched. Disabling
editor-visible bridge classes would hide functionality rather than fix this
lifecycle defect.

## Bounded workaround; issue remains open

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

[Issue #276](https://github.com/gobha-me/apsis-drift/issues/276) remains open.
The delay allows the callback to finish before teardown; it does not repair
ownership or cancel deferred work. Closure requires a corrected engine/import
path and repeated clean **fresh, immediate** imports, not a warm cache, a debugger
launcher exiting successfully, or suppression of the crashing callback. An
upstream fix still needs validation against the selected engine and extension.

Independent frozen-runtime controller and audio checks remain separate
evidence. Passing them does not make an editor crash harmless, and a successful
delayed import does not qualify the game's listening or rendering performance.
