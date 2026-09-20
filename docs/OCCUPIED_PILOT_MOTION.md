# Opt-in occupied cockpit motion

This is a seated interaction increment for #277, not final character acceptance
under #267. It preserves the default static pilot path and canonical assets.

## Explicit setup

Add `--pilot-motion=true` only with live thrust controls and compatible absolute
`--pilot-asset=/path/occupied-pilot.glb` and
`--pilot-cabin=/path/occupied-cabin-near.glb` arguments. The assets must be a
matched occupied-cabin pair, not a studio GLB inserted into the ordinary cockpit.
Missing or incompatible requested assets fail clearly; there is no automatic
fallback to an apparently functioning animated pilot.

The rig must contain exactly one authored `Scene` neutral clip. Explicit setup
seeks its beginning and pauses playback before configuring the semantic arm
driver. A refused startup can have posed that candidate before refusal; setup
does not promise transactional restoration of an incompatible imported model.
The shared skeleton must remain outside the hidden own-head hierarchy.

## Import visibility partition

Godot can reparent imported skinned meshes directly under their shared skeleton,
discarding authored mesh placement beneath `PilotHead` and `PilotBody` groups.
Only the explicit rig-study loader repairs this layout. It requires one shared
skeleton, the two disjoint groups, and case-sensitive `PilotHeadMesh__` or
`PilotBodyMesh__` tags on every skinned mesh. Source skeleton paths, nonempty
mesh surfaces, finite transforms and hierarchy ownership are checked before
reparenting; this is not a mesh-buffer or deformation validator.

Normalization preserves global mesh transforms and each original Skin resource,
then restores a path to the same skeleton. It does not duplicate bones, infer
anatomical boundaries, rescale the pilot or mount the cabin twice. Untagged,
ambiguous, cyclic, empty or wrongly bound skin partitions refuse. The static
loader contract is unchanged. First-person masks only the own-head group;
exterior view restores it without resetting the control pose.

## Live command boundary

The main scene passes its already-resolved seven-axis command to the
[semantic driver](PILOT_SEMANTIC_MOTION.md) once per presentation frame. It does
not poll input again. Pause, focus loss, disconnect, neutral rearming, inspection
mode and terrain readiness cannot retain a stale active demand. Pause and quit
reset immediately. Slow visual frames cap only the animation settling interval;
they do not feed additional time into C++ flight.

The guidance selector intentionally leaves flight active. Its navigation-only
inputs do not move the grips, while concurrent permitted flight inputs still
do. Paused controls/help are a different state. Repeated identical driver errors
do not flood the log every frame.

The inherited-main integration test uses a synthetic rig and the real C++ bridge
to check one update per frame, command equivalence, unchanged flight results,
view/head visibility continuity, pause/focus/disconnect safety and invalid input.
The import partition test separately checks original Skin/skeleton identity,
unchanged transforms, refusal before partial reparenting and head-only masking.
These tests do not establish anatomical fit, fingertip or garment clearance,
GPU cost or physical-controller feel. Actual occupied assets need their own
native inspection and provenance receipts.

## Occupied export proof

The paired export study replaces the source cockpit's enumerated seat/restraint
and movable grip pieces before material batching, retains fixed consoles,
gimbals, bellows, pedals and structure, and removes the studio floor and duplicate
fixture grips. Both pilots retain one shared skeleton. The original body skin
is partitioned into complementary face sets at its authored neck boundary;
eyes/brows/lashes follow the head group. No anatomy scaling or new cabin mount
is introduced.

Actual native helper/driver checks pass for both variants. Fixed-eye forward,
downward and side views plus exterior/neck views were inspected; no neck seam
gap was visible in those captures. Ninety rendered semantic-motion frames per
variant report zero driver refusals and wrist-target transform errors below
0.00025 mm. Four own-head meshes hide while all body meshes remain visible,
and exterior view restores them. These are sampled views/transform checks, not
whole-cabin collision qualification.

The companion cabin is 6,919,608 bytes. Complete occupied-pilot fixtures are
14,599,480 bytes / 169,568 triangles (female) and 22,045,724 bytes / 121,092
triangles (male); these include their corrected seat/control assemblies.
Editable masters, PNGs, three-second 1080p clips, source/license receipts and
223 output hashes remain in isolated ignored authoring output. The female
original garment and male borrowed fit coverall are deliberately distinguished.
Canonical source assets remain unchanged. Neither variant has its final
restraint, helmet, gloves, hair or cinematic material treatment.

Discrete reach/press/return, character locomotion, final suits/restraints and
cinematic character quality remain outside this increment.
