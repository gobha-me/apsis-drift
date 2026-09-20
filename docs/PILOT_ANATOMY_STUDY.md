# Pilot anatomy workflow checkpoint

2026-09-19, issue #267. Neither this experiment nor the existing in-game pilots
meets the approved concept's production-quality gate. The live playtest keeps
its explicitly labeled fit placeholders until an actual replacement passes
art review and cockpit integration checks.

## Preserve the unsuccessful second suit study

`tools/build_pilot_suit_v2.py` is an original procedural Blender experiment,
not the next production pilot generator. It writes isolated outputs under
`build-godot/pilot-suit-v2`, including source/dependency hashes and BSD-3-Clause
provenance. No external human mesh or film/game texture is included in it.

Useful experiments include connected bent garment surfaces and deterministic
woven color, normal and roughness textures. However, its face remains stylized;
the shoulder transitions, helmet and boots do not read as credible human-scale
equipment. A dense 870,464-triangle result did not repair those problems.
It has no accepted native appearance, deformation or cockpit-fit certification.
Preserve its source for lessons and possible material reuse, not as evidence
that the cinematic-pilot issue is complete.

## Anatomy-first proof, isolated from the game

The next experiment uses a pinned MPFB authoring tool with separately identified
CC0 human-mesh inputs. Start with a neutral portrait and body proportions before
tailoring the project's suit. Do not spend the art budget adding fine hardware
to incorrect anatomy. A believable base mesh is a starting point, not automatic
approval of a finished character.

The project-local authoring download and proof scripts stay in ignored tooling
directories. MPFB is not a runtime dependency. Its GPL program code must not be
relabeled as project BSD code; the explicit asset/output licensing is a separate
matter. Any later tracked authoring integration needs its own license review.
Graphical outputs must identify upstream-derived anatomy and exact used inputs,
not claim that all geometry was authored from scratch for this project.

Pinned source and input audit:

- MPFB commit `80919fa4682335c41847f761a4d79dcad4124732`;
  archive SHA-256 `ab42cbf9827abe58a26c051ec001bdad71642673441ae8cbdc457d7036ad2740`.
- Official system-asset pack archive SHA-256
  `b542127a8e25547c7c29c19f2d1d2adb9a664c80396ecd694095dbc8028a0107`.
- The selected skin, eye, brow and eyelash inputs were checked against both the
  pack inventory and available asset headers. Output provenance must still
  list the actual files used and their hashes; this is not a blanket clearance
  for unrelated community downloads.

Primary records: [pinned program/asset license split](https://github.com/makehumancommunity/mpfb2/blob/80919fa4682335c41847f761a4d79dcad4124732/LICENSE.md),
[pinned CC0 terms](https://github.com/makehumancommunity/mpfb2/blob/80919fa4682335c41847f761a4d79dcad4124732/LICENSE.ASSETS.md),
and [official system-pack inventory](https://static.makehumancommunity.org/assets/assetpacks/makehuman_system_assets.html).

The [approved art direction and acceptance sequence](PILOT_ART_DIRECTION.md)
still apply: distinct male/female character design, practical tailored garments,
consistent interfaces, real restraint/grip contact, and verified seated fit.
No changes to cockpit datums, player-camera behavior or gameplay fiction are
authorized by this asset experiment.

## Local proof and review boundary

The isolated run produces `build-godot/mpfb-proof/portrait-front.png`,
`portrait-three-quarter.png`, and an editable `female-anatomy-proof.blend`.
Its neighboring provenance records the pinned archives, macro settings,
graphical inputs, output hashes and authoring-isolation findings. Keep that
record with the images when sharing them. Do not copy the entire authoring
download or its asset library into a game distribution.

The reviewed face has a substantially stronger anatomical foundation than the
procedural mannequin. Hair is omitted to expose both eyes and facial form;
the plain review cover is not a proposed suit. Skin, eyes, expression, character
identity, helmet and tailored clothing still need art work. No rigging,
cockpit fitting, runtime export, animation or LOD acceptance is claimed by a
portrait. This is an anatomy workflow proof, not the promised finished pilot.
