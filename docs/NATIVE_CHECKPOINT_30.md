# Native follow-through — study 30

The project owner requested continued implementation from the approved
direction without repeated routine fit approvals. PR #279 merged after all five
hosted checks passed. This checkpoint is not a release or a new complete
playable build; the accepted recording mix and frozen playtest remain unchanged.

## Contact ownership

The [context provider](EXPERIMENTAL_CONTACT_CONTEXT.md) now resolves the actual
catalog-owned planet, including the authored home variant. Experimental result
wrappers preserve ownership where two descriptor variants share a PlanetId.
The original standalone API, its strict refusal behavior, old goldens and
geometry arithmetic remain intact. Cache conflicts refuse rather than silently
substituting terrain. Independent review found no blocker; GCC/Clang fixtures
qualify the new path separately.

Both full local builds and all 16 focused C++ contracts pass. Pinned format 20
and focused clang-tidy 20 pass; the standard full lint command cannot run with
the locally available tool names/compiler, so complete pinned lint remains a
hosted CI gate. The historical replay portability finding #255 remains open;
these focused results do not claim it fixed or replace its reference goldens.

This does not finish #202. Live session/bootstrap and frame adaptation,
multi-triangle coverage, first-hit/bearing truth and complete contact assessment
remain separate gates. The existing floor guard is not removed.

## Repeatable native regression run

`tools/test_godot_native.py` stages a selected prebuilt native bridge/exporter
and an isolated project/preferences/cache, generates two terrain fixtures and
runs 24 headless contracts using Dummy audio. Both the GCC and Clang native
builds passed all 24. Runner unit tests check crashes/nonzero exits, false
zero-exit success, missing completion markers, deadlines and leaked-resource
diagnostics. The unit tests run in hosted CI; the actual native suite remains
an explicit local check, not a newly claimed hosted engine job.

This provides reproducible input, camera, guidance, orbit/coasting, streaming,
pilot visibility and audio-lifecycle checks without private recordings or
visible/audible playtest interruption. It does not qualify hardware input,
listening comfort, GPU performance or character quality.

## Male seated motion in Godot

The male anatomy now follows the same provisional arm/control sweep in an
isolated native scene. The unresolved seated garment gap was addressed through
a male-specific upholstery thickness adjustment, retaining the existing cushion
underside, pan/lifts, eye, grips, pedal datums and unstretched anatomy.

A Blender-only correction initially caused about 3.869 mm penetration in the
native skinned mesh. The final adjustment was evaluated in Godot over 90 frames
using the actual deformed garment mesh: its selected central buttock vertices
remain about 0.500 mm above the cushion. This is a sampled vertex-envelope check,
not a whole-triangle, continuous-collision or soft-tissue pressure proof. The
same source yields about 4.869 mm clearance in Blender; the deformation methods
are different, and neither measurement silently replaces the other.

The native three-second 1080p excerpt retains wrist/control transform alignment
within about 0.0324 mm. Its complete fixture GLB is 22,059,108 bytes and the clip
is 246,672 bytes. These include the seat/controls/pedals/studio fixture, not a
final pilot-only budget. Editable masters, the source/native gap measurements,
CC0 source and separate original-fixture licenses, and independently checked
output hashes stay in ignored authoring output. Female study 29 is preserved.

Original suit construction, skin/hair/gloves, restraint fit and actual gameplay
semantic animation remain unfinished. Neither #267 nor #277 is closed by this
fit/motion increment, and canonical gameplay pilots are not replaced here.

## Editor import defect

[The import investigation](GODOT_EDITOR_IMPORT.md) now identifies a deferred
Godot documentation callback running after its documentation object was cleared.
Independent fresh immediate imports failed three times; the isolated delayed
import still passes. No simulation/bridge or engine patch is justified by this
finding. #276 stays open for a corrected upstream lifecycle and clean fresh
immediate-import validation. Raw process/core metadata remains private.
