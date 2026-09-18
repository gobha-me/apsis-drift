# Freedom checkpoint: tracker reconciliation

2026-09-18. Follow-up to [checkpoint 12](NATIVE_CHECKPOINT_12.md) and
[PR #252](https://github.com/gobha-me/apsis-drift/pull/252). This records existing
owner decisions and evidence boundaries, not another expansion of the playable.

## Decisions and owners

The audit covered all 93 then-open issues, their dated scope amendments and
the Freedom milestone. Historical specifications remain available; the current
amendments take precedence over conflicting old release/presentation wording.

| Topic | Reconciliation |
| --- | --- |
| Engine | Godot selected, C++ authoritative; #244 stays open for remaining qualification. Removed the repeated pending-selection gate from 36 active issues and updated milestone 10 / epic #243. No parallel SDL renderer. |
| Controls | #50 now explicitly owns controller-first plus keyboard-complete input. Layout 4, direct roll, weaker retro, hold-L3 look/recenter, remapping and safety gates recorded in #50/#135/#180. No silent atmosphere binding swap; no implemented boost. |
| Cockpit/craft | #171/#190/#197 record useful unobscured instruments, human-scale reach, pilot-facing panels, closed interior surfaces, attached structure, shared interior/exterior fit and stowed flight gear. |
| Physics | #192/#201/#238 distinguish demonstrated lab behavior from save-supported production contracts. Assist-off still damps angular rate; it is not fully torque-free Advanced flight or an orbit-hold autopilot. |
| Contact/consequences | #202 is pure contact assessment, #203 landed lifecycle, new #253 bounded impact/local condition and fracture presentation, #247 recovery. The floor guard is none of these. #248 retains jump-interference research. |
| Terrain/weather | #212 records bounded spherical streaming and LOD-normal improvements, not geomorphing or full circumnavigation qualification. #177/#212 preserve planet-specific clouds/storms/dust follow-up without turning mutable weather into immutable ambient truth. |
| Stars/sensors | #174/#175 distinguish preview star identities from future spatial navigation. #171 preserves protected direct/camera views and equipment/compute-limited spectra; neither grants omniscient discoveries. |
| Audio | New #254 owns a small native atmosphere/vacuum ship sound prototype, preserving procedural/MIDI/First Light assets and provenance. Offline film sound is not live audio integration. |
| Compatibility | New #255 owns the host math-library replay finding. Do not replace goldens, hide state differences or ship a host-specific preload workaround. |
| Economy/fiction | #250 preserves future mining/material collection without choosing a lone-engineer or populated cyberpunk setting. Economy, missions and earned drives do not gate Freedom. |

The settled resource/recovery rules remain unchanged: separate flight/jump
fuel, three starter charges, one per valid in-range committed jump, initially
free station refueling, and standard safe-station recovery with loss-cause-aware
wreck recoverability. #133 specifies fuel arithmetic; #251 implements it;
#246 services it. Ship construction/carriers remain #249, not a builder UI
requirement. No broad feature issue was closed as completed by this audit.

## Publication verification

The checkpoint was based on the previously merged audio branch. Current main
was merged without rewriting history, preserving its TermForge update and
clang-format/clang-tidy 20 policy. The two analog-input merge conflicts retained
the optional native input API and legacy path; repository C++ was formatted
with the pinned formatter.

Hosted CI now enables `APSIS_DRIFT_GODOT_SPIKE=ON` in all four compiler/audio
configurations so it actually runs the five native C++ contracts. This does
not install Godot or qualify a GPU/display/controller. Post-integration local
GCC and Clang builds passed, and each passed all seven focused native and
asset/provenance CTests. Full historical local-suite caveats remain in
[checkpoint 12](NATIVE_CHECKPOINT_12.md) and [the numerical finding](NUMERICAL_COMPATIBILITY_FINDING.md).

Consult the PR checks for the final hosted result rather than treating a
recorded in-progress status as a permanent pass. No release or tag is implied.
