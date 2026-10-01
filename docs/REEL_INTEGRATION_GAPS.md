# Scripted Godot film integration: ready resources and gaps

Issue [#349](https://github.com/gobha-me/apsis-drift/issues/349) now composes
ready character and station assets inside scripted Godot scenes. Existing review
movies are readiness references, not the film's character or room inserts.
The owner requested this integration to expose missing motions and contacts.
NPC blocking and the pilot/clerk dialogue are editorial choreography; native
player walking and flight retain the authoritative C++ state.

## Character resources

The Hero session explicitly supplied reusable source APIs in the original
checkout's `docs/ASSET_COORDINATION_2026-10-01.md`. Stable casual walking,
female interactions and paired suits were delivered at `8a58368`; matching
casual held action keys followed at `d932d4939d87a9d6551bcfc407141eafca5aba26`.
This table describes checked source capabilities, not inferred Hero agreement
on station contact fit.

| Cast / entry relative to the isolated Godot root | Ready API | Limits |
| --- | --- | --- |
| Female casual: `actors/pixel_pilot.tscn` | `set_visual_pose(family,index,view="side")`: idle0, walk0–7, press0–3 side/quarter, service0–3 side. | Fixed artwork plane. RightGrip/RightFingertip are optional art landmarks. |
| Female directional: `trials/walk-04/pilot_presentation.gd` | Instantiate, add to tree; `present_movement(heading,to_camera,distance,moving)`; `present_contact(family,index,heading,view)` for press/touch/service. | Caller supplies pose phase/root. Touch0–3; front/rear/left walking has four coarse exposures, right has eight. |
| Male casual: `trials/casual-male-01/pilot.gd` | Instantiate, add to tree, `configure()`; `present(family,index,view)`: idle0/all four views, walk_side0–7/right-left, walk_axial0–3/front-rear, gesture0–3/right-left. | Greeting uses two distinct drawings. No service sequence or tool socket in this adapter. |
| Both casual action sets: `trials/casual-actions-01/pilot.gd` | `configure("female"/"male")`, `present(index,"right")`, `anchor_local(name)`. | Right profile only; no mirroring asymmetric clothes. Eight held keys, no walk or transition sequence. |
| Both flight suits: `trials/flight-pair-01/pilot.gd` | `configure("female"/"male")`; `present(family,index,view,helmet)` with idle0/all views, walk_side0–7/right-left, walk_axial0–3/front-rear, actions0–7/right-left. | Both helmet states authored for every family. Eight held action drawings, not completed interaction cycles. |

Both paired action adapters use indices 0–7 for seated neutral, seated push,
seated pull, standing press, service lean, float, handhold reach and push-off.
Casual float keys are for a pressurized AG-off interior. They supply no EVA,
gravity transition, grab constraint or propulsion. Missing `anchor_local`
landmarks return a non-finite Vector3; check `.is_finite()` before attaching a
prop. A landmark is not certification of skin contact or item ownership.

Female nominal standing reference is **1.78 m**, male **1.85 m**. Actor origin
is the registered floor/bottom datum, not the center of a standing body. Seated
drawings retain their shorter silhouette with the same sheet normalization.
Do not stretch actors, rooms or a seated frame to conceal mismatched fixtures.
Each actor has an independent transparent SubViewport with nearest filtering
and a shaded, depth-tested Sprite3D. Body-shaped shadows and foot IK are absent.

## Coordinate and contact contract

Fixed artwork faces actor-local **+X**, with **+Y up** and a side camera on
**+Z**. Its XY plane and any separately attached 3D tool must receive the same
physical station-frame transform. Directional helpers instead define heading
zero along **+Z**. The C++ walker uses heading zero along **−Z**; adapt this
explicitly rather than passing headings through unchanged. With the same local
axes the equivalent art heading is `cpp_heading + PI` modulo a full turn.

Suit and male locomotion cards default to fixed-Y billboarding. Set
`card.billboard = BaseMaterial3D.BILLBOARD_DISABLED` for fixture contact and
align the actor root. Female fixed contact cards and new casual action cards
already disable billboarding. A billboard shader does not rotate child props.
The female directional wrapper takes camera vectors and heading in its local
station frame; contact turns only its interaction child by `heading - PI/2`.

The alternative `trials/encounter-01/actor_view.tscn` snapshot wrapper rejects
rotated/scaled parent and root bases. It cannot be directly parented under a
rotated physical station frame. The lower-level presentation wrapper can be
used there with explicitly converted inputs. Encounter's separate driver/probe
construction is a reusable study example, not a production inventory system.

The existing female service sequence is ready/insert/work A/work B. Source
tool tip datum is **230 mm along +X from its grip**. Work grips differ by about
15.5 mm; the original fixture keeps a fixed midpoint socket with at most
7.75 mm art-marker offset. Moving the tool after every painted hand frame would
reintroduce jitter. The source reports about 90.5 mm ready-tip clearance for its
own illustrative cover. These measurements do not qualify a real station
control, instrument access port, or shutter. Record real target residuals and
inspect the approach/held/contact/recovery before claiming contact.

## Staging and validation

`tools/stage_reel_characters.py` copies the selected dependency closure into a
new directory, preserving original `res://actors/`, `res://trials/` and
`res://direction.gd` paths. It verifies runtime bytes against the selected Git
checkpoint and archives metadata from that same checkpoint. This allows other
sessions to update handoff prose without silently changing film sources. It
does not copy `project.godot`, `.godot` caches or source `.import` files and
refuses an existing output. Merge the resource paths into an isolated prepared
film project; retain its own native configuration and extension.

```sh
python3 tools/stage_reel_characters.py \
  --hero-root "$HERO_CHECKOUT/experiments/pixel-pilot" \
  --output "$NEW_CHARACTER_RESOURCE_DIR"
```

The default selected checkpoint is the complete `d932d49` delivery. Staged
`character-resources.json` records source checkpoint, worktree checkpoint at
copy time, entry resources, per-file SHA-256/size and source metadata roles.
The closure contains **49 runtime files, including 30 selected PNG textures**.
Rejected sheets are not used by the runtime. Existing 8a staging remains an
archived reproducibility reference, not the current film source selection.

Godot 4.7.2 imported the unchanged complete d932 closure and passed **339** checks
against the actual staged APIs: pose enumeration, root preservation, physical
height, world depth flags, service/touch anchors, nonfinite state, bounds and
unsupported requests, including all new paired casual held keys. Headless
correctness does not prove visible shading, occlusion or contact.
The integrated scene's rendered proofs must inspect foreground
occlusion, feet/floor registration, real fixture alignment and the chosen side
camera before final filming.

The optional film character test now uses guarded runtime loads, so an isolated
normal project without private Hero files imports cleanly in the editor. The
same staged test still passes 339 checks. Changed runtime source bytes, linked
resources and publication over a concurrently created empty output are refused.
The resource roster's 87 files were independently checked against their hashes.

The first actual integrated station raster proof rendered 20 images under
Godot Compatibility with **Mesa llvmpipe**. Its receipt binds scene/bridge/input
hashes, source-sized cards, preserved negative requests and the C++ shared clock
from tick0 to tick95. Independent inspection of proof00/03/11/17 found readable
distinct identities, upright station-local cards, plausible scale against the
doors/deck and no visible floor cutting. The male correctly occludes the female
in the opening view; both remain readable side by side later. Missing body
shadows weaken the impression of foot contact. This is software raster evidence,
not a hardware GPU or real-time performance claim. The initial side camera
proves inter-character depth only. The corrected outside-corridor capture at
station-local `(-8,1.2,2)` is blocked by actual opaque source geometry: neither
character draws through it with depth testing retained. Independent inspection
confirmed this negative against the visible positive views. It qualifies the
tested rendering behavior, not a full wall/capsule or body-clearance route.

The workshop's third proof visibly composes the same male's held service key,
the retained source instrument and a separate probe. It preserves source scale
and a fixed action plane. This inspection does not establish anatomy, swept
clearance or a complete service cycle. Final full-frame and contact evidence
belongs to the integrated scene receipt, not these provisional sampled views.
Source-specific [station readiness](GODOT_REEL_STATION_READINESS.md),
[workshop fit](GODOT_REEL_WORKSHOP.md) and [saved-flight evidence](GODOT_REEL_FLIGHT.md)
preserve the actual geometry, clocks, reproduction and receipt identities.
The full workshop chapter now contains 1,440 frames/60 seconds and fully decodes;
its saved C++ state stays byte-identical to the proof at tick 0. The independently
started flight chapter contains 2,400 frames/100 seconds and fully decodes;
its full capture and review Save As bytes match at tick9625, with no dropped
seconds. Chapter durations describe source captures, not a promised final edit.

The source receipts state that inherited references and derived-art
redistribution remain **uncleared**. Hero expressly supplies them for the
owner-requested local integration/review. This is the actual boundary, separate
from the station/Wayfarer grants and the First Light audio grant. No paid
generation is required to stage these resources.

## Scene choices and remaining gaps

The commons scene can show a female approach and stop, a male clerk's two-pose
greeting, a short held exchange and the female's departure. Block roots on the
registered floor and keep the camera at a side/front view supported by each
selected actor. A real foreground table, rail or door frame can test depth.
This is scripted NPC motion; it does not establish navigation or collision AI.

The workshop uses the **same male identity as the clerk**, now at an actual
station fixture with casual held service key4. This is a second scene role,
not a third cloned technician. A separate study probe can test the finite grip
landmark against a measured source target. Prefer an approach, visible target
inspection and withdrawal until fit is demonstrated. The male service lean is
one held key; a service work/recover cycle remains a gap. The earlier female
four-frame sequence is useful fit evidence, not tonight's selected technician.
Do not move a real panel per frame to manufacture contact.

The source station rooms and fixtures are metre-registered assets. Workshop
and passage authored animations are baked at **24 Hz** on glTF import, then
explicitly sought by source frame/24.0. Door cycles demonstrate hardware motion,
not proximity interaction, pressure eligibility or character crossing. New
observation commons/window art has now been delivered as a separate derivative
with no celestial backdrop. A bounded optional live-world proof is separate
from player route admission. The [observation capture](GODOT_REEL_OBSERVATION.md)
registers that room in the existing native C++ world, replaces exactly the old
exported commons geometry and aligns the existing near/far cameras. Endpoint
proofs show source shutter opening and clear glazing onto actual station
hardware and dark space. No planet is inserted to improve the composition.
The C++ actor stays at the hub; the camera reaches the bay editorially.
Its completed 20-second source chapter contains 480 actual frames and fully
decodes. Full and endpoint-proof Save As bytes match at tick 0; every frame
retains the shared world checksum. This does not extend the supported walker crop.

Missing for a continuous departure are a supported player boarding/climb route,
standing entry, sit-down transition, authored hand/hatch reach and retained
seat/tool contacts. Wayfarer operating poses remain mechanism previews; the
known 77.5 mm inner threshold seam is not qualified actor traversal. Also absent
are first-person arms, arbitrary-angle hands, general foot/hand IK, uneven-ground
registration, pack donning/removal and full casual/suit action parity. Film cuts
must leave these gaps visible rather than suggest a continuous departure.

## Follow-on Hero resources, outside the film freeze

Hero subsequently delivered `trials/crew-scene-01/actor.gd` at checkpoint
`032e745`, with a 46-file runtime map. `configure(hero,outfit,helmet=false)` and
`apply_snapshot(snapshot,camera_world)` provide hidden, movement and held modes.
Snapshots use immediate rigid parent-local metre positions, +Y up and heading 0
along +Z. Rotated/tilted/translated parents are supported; scale, shear and
reflection are refused. Invalid requests retain the last valid presentation.
Movement supplies position, heading, nonnegative travel distance and a moving
flag; held requests select keys 0–7, with right-profile view only. Optional art
landmarks can remain nonfinite. Source evidence includes 938 wrapper checks and
539 commons assertions. This is an available next-integration API, without
switching the current `d932d49` film resources or claiming NPC navigation.

Later source deliveries add a male ready/work/recover service study and bounded
stop/turn/greeting timing. They remain separate from tonight's held-pose
workshop. Missing directional service drawings, planted stop/turn artwork,
carry-tool walking and sit/climb remain gaps. Optional grounding decals are
deliberately hidden under Compatibility and are not enabled in this film.
