# Seated pilot and engine growl study 20

2026-09-19; follow-up to the first native ship-audio audition (#254) and empty
pilot seat report (#267). This remains an opt-in art/sound study, not final
character acceptance, character creation, EVA, spatial audio or new physics.

Human review supersedes the pending acceptance notes below: the pilot art is
placeholder-only and the engine reads as an air filter/fan, not convincing
loaded machinery. Neither appearance nor sound is accepted. Automated
integration and safety results remain useful, but are not artistic approval.
See [the revised art brief](PILOT_ART_DIRECTION.md) for the character direction.

## Engine character

The first audition established useful thrust response but exposed a dominant
single tone with too much pitch movement. The revised voice combines a low
54–68 Hz body with audible overtones, filtered uneven mechanical texture and
quiet coolant detail. Thrust primarily changes intensity and texture, with
approximately 0.55 s upward / 0.9 s downward spool travel. The existing 60 ms
safety gain fade and bounded queue/work limits remain intact.

Cockpit machinery and conducted thrust remain in vacuum. Atmospheric airflow
and buffeting add over that same core; exterior vacuum stays silent. Stereo
and mono-compatible playback are the baseline. The engine is centered and the
air has restrained stereo width, not physically localized sources. Spatial
engine/cabin/condition placement is a 1.0 target, not implemented here.

Offline matching 85%-thrust fixtures move the strongest frequency bin from
approximately 140 to 66 Hz and reduce concentration in the six strongest bins
from 96% to 60%. That establishes a less pure-tone-heavy waveform, not a human
judgment that the sound is finished. The revised fixture is about 4.65 dB lower
in RMS level; no normalization hides that comparison. Audition on ordinary TV
speakers remains essential, without requiring a subwoofer.

## Pilot preview integration

`--pilot-asset=ABSOLUTE_GLB_PATH` loads a separately generated seated pilot under
the existing cabin mount. Pair it with `--pilot-cabin=ABSOLUTE_GLB_PATH` for the
occupied-cabin export with only the empty-seat restraint removed. The original
cockpit GLB is material-merged, so hiding objects by their Blender names at
runtime cannot remove that restraint. Canonical cockpit files remain unchanged.
The pilot GLB must have exactly one `PilotHead` and one
`PilotBody` mesh-bearing group, neither nested inside the other. The importer
does not rescale the cabin or move the shared eye point. No pilot asset is
enabled by default or silently substituted for an invalid requested file.

Exterior view shows the whole figure. First-person view hides only the local
head/helmet group, retaining body, gloves and boots. Returning to an exterior
or inspection view restores the head. This is a single-camera presentation
mask, not multiplayer visibility, reflection/shadow qualification or animated
head tracking. The asset carries no flight statistics or gameplay gender rules.

Use `tools/build_seated_pilots.py` for the editable original geometry study and
its own provenance. Generated previews remain separate from the canonical ship
and cockpit files. The prior ergonomic mannequins remain available unchanged.
Source geometry and visible seated fit are review stages; they must not be
called finished characters merely because they occupy the seat.

`pilot_presentation_test.gd` checks malformed/ambiguous mesh groups and own-head
visibility behavior. `pilot_visual_review.gd` captures exterior left/right,
forward cockpit, body/control view and low-light inspection using the actual
native cabin. It requires explicit pilot and output paths and records that
these are static inspections, not flown maneuvers or collision qualification.

## Verification and remaining acceptance

Both GCC and Clang builds passed the nine selected native contract tests.
Godot audio synthesis, playback lifecycle, guidance/audio integration, pilot
hierarchy and inherited view-transition tests passed. Hidden native Vulkan
captures exercised both pilot variants in five views each with the occupied
cabin. The capture harness explicitly refreshes both compositor cameras before
recording an image; changing only the inspection camera is insufficient when
the normal frame loop is paused. A controller smoke test also passed with the
new sound generator and occupied-cabin presentation enabled.

These checks do not replace a real-device audition. The next human checkpoint
is engine growl and spool response on ordinary stereo speakers, atmosphere
versus vacuum, and seated visibility through cockpit/exterior/head-look
transitions. The procedural pilots still need substantial character-art work;
neither the audio nor character issue is complete at this checkpoint.
