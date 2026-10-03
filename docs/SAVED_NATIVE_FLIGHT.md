# Ordinary saved physical flight

2026-09-30, #340; port lifecycle added 2026-10-01 in #342. The ordinary launcher
selects format-17 station state, format-18 physical flight or explicit
[format-19 port state](NATIVE_PORT_LIFECYCLE.md). New Game still starts at Origin Station. Continuing
flight never constructs a study snapshot, relocates the craft, resets its clock
or changes its frame recipe. Station walking/boarding/release and planetary
contact remain required journey work before the owner's six-part handoff.

## Ownership and time

`NativeFreedomFlightSession` remains the sole mutable flight/save owner. The
native bridge owns its existing fixed 120-Hz clock; presentation sends twelve
independent gross actuator fractions, each finite and in `[0,1]`, in this order:
positive translation XYZ, negative translation XYZ, positive rotation XYZ,
negative rotation XYZ. Axes are body right/up/back. Main thrust is negative Z.
Validate every channel and elapsed time before scheduling. A candidate batch
qualifies through atmosphere/gravity/hold, persistence and projection before
commit, including the clock accumulator. Failed input leaves the session intact.

Paused presentation schedules no time and shows no firing exhaust. Continue
opens paused. At most fifteen ticks catch up per render call, using the existing
bounded scheduler; excess elapsed time is reported as dropped, never simulated
silently. Pause/residual presentation time is not serialized. Save As preserves
the authoritative last committed tick, pose, history, frame and physical model.
Reopen starts a fresh presentation clock without changing those saved values.
Application focus loss immediately pauses flight and hides both exhaust groups
without advancing their visual phase. Focus return stays paused: release all
mapped keys, buttons and axes, then resume explicitly. Unfocused elapsed callbacks schedule
no flight ticks or catch-up backlog; these presentation latches are not saved state. Selected-controller loss/replacement,
remapping, opening controls and Save As also pause immediately and require
current individual neutrality before explicit resume; opposed inputs cannot
cancel through that gate. Removed held mappings remain release barriers. That
history is bounded to 128 events; overflow conservatively requires complete
physical input release before rearming.
Assistance changes explicitly through C++; the persisted orbit-hold request
continues to use the existing owner and its normal manual-input/environment gates.

The saved HUD now shows the same-tick C++ **orbital forecast**: stable orbit,
atmosphere entry, reference-surface intersection or escape, with periapsis and
optional apoapsis expressed as altitude above the selected reference radius.
Thrust and atmospheric drag change the trajectory; this central-body prediction
is not terrain or landing clearance. Current altitude, surface speed, radial rate and air stay
separate from that forecast. Near-parabolic cases are labelled explicitly.
Unavailable apoapsis is shown as unavailable even for a bound trajectory beyond
the reporting domain. An outbound escape can have a below-surface *past*
periapsis; the display retains C++ escape classification rather than inventing
an impending impact from that number. No lab NAV widget, additional 20 km orbit
threshold, hold command or save field is introduced.

The displayed full body attitude and home-station vector are projected in C++
from the saved nonrotating planet frame into the generated rotating planet's
same-tick tangent frame. Projection is query data, never a new saved frame or
physics handoff. Global coordinates stay binary64 until the local projection is
passed to Godot. C++ supplies the actual fixed terrain owner to `PlanetStream`,
with source LOD 8 and relief 0; this consumer does not introduce the study's
experimental relief into saved worlds. A failed projection/import refuses
actionably rather than generating a substitute universe.

## Presentation and controls

`tools/run_godot_native.sh --continue=/absolute/path/to/flight-save.json` opens
the flight view. The approved layout-4 adapter now controls saved flight as well
as the existing study, with keyboard equivalents and remapping. RT/R2 requests
main thrust; LT/L2 requests weaker retro. Left stick controls pitch/roll (pull
back pitches up), right stick yaw/heave, and LB/RB lateral translation. Hold L3
and use right stick for cockpit look or exterior orbit. Release recenters;
shared yaw/heave channels remain suppressed until centered. Independent engines,
pitch/roll and bumpers remain usable during look. X/Square changes view;
Y/Triangle requests assistance, reflecting only accepted saved state. Flight
inputs request physical thrust/torque fractions, not the lab's target turn rates.
No atmosphere-dependent mapping change or boost is introduced.

Keyboard defaults are W/S main/retro, A/D yaw, Q/E strafe, Space/Ctrl rise/fall,
I/K pitch, Z/X roll, Alt+arrows look, C view, Home recenter and F assist.
This consolidates the earlier saved-only A/D strafe, arrow attitude and Q/E roll
map onto the already approved layout 4; both maps are not sampled concurrently.
F3 remains an explicit saved-view camera alias, including harmless inspection
while paused. A camera choice never changes boarding or seat ownership.

Esc/Start opens the controller-navigable paused controls menu. It exposes the
actual current binding names, existing deadzone/response/head-look preferences,
remapping, Resume, assistance, Save As and Quit. Wayfarer also exposes existing
port selection/capture/release with the same physical assessment and refusal
reasons as the HUD. The saved context has no experimental reset or relocation.
**Flight basics
(paused)** reuses the existing reference panel with explicit saved-flight pages:
physical thrust/torque and momentum, air/orbit observations, bounded rotational
assistance, current look/view bindings, Origin-port assessment and committed
saves. Historical frame 1 explains its unavailable Wayfarer port actions.
The lab profile retains its separate practice and prototype wording.

The saved reference explains C++ STABLE as bound periapsis clearing the actual
atmosphere boundary; it does not transfer the lab's additional 20 km criterion
or claim an absent NAV instrument. Altitude is relative to the reference sphere,
not terrain clearance. Assistance is actual bounded torque, without automatic
hover or neutral translation braking; a persisted orbit-hold request is separate.
Viewing help does not select or change it. Back returns to controls while paused;
resume still requires current individual neutrality. Focus return and Save As
cancellation never resume automatically. Fuel, jump travel, planetary touchdown
and the composed boarding/seating journey remain unqualified by this reference.

B/Circle or Resume requests explicit resumption after current
mapped inputs are neutral. Closing a chooser never resumes automatically.
Preferences remain external in `user://freedom-controls-v4.json`; installed v4
profiles load unchanged. Settings, devices, held input and camera offsets never
enter deterministic world saves. Defaults/invalid-profile behavior remain the
existing adapter's policy. Synthetic event contracts qualify software input
resolution, not physical SteamOS/Xbox/PlayStation hardware.

The view imports the selected Wayfarer export only for explicit frame 2/version
1, verifying its existing model hash before import. Registered historical
frame 1 retains a plainly labelled bounds placeholder rather than being
silently relabelled Wayfarer. Continue presents native terrain, same-tick star
lighting, air/orbit telemetry and a projected home-station identity/range cue.
The cue is a projection, not line-of-sight or docking authorization. Physical
station approach, exterior registration and capture integration remain #220.

The native consumer borrows the asset session's `hopper_presentation.gd` loader
(source SHA-256 `a459fbcf859332f1a4b18bb98b562864f675b4d0b30a14fc91950f928b01c155`),
retaining its measured descriptor/gear validation and deliberate clear-glass
material override on `HopperGlass`. Runtime mesh LOD generation is added.
This changes presentation response, not model geometry, textures, master files,
gear simulation or the pilot's physical seat. The eye point comes from the
source-bound descriptor. Raw glTF transmission otherwise appears opaque in
this native cockpit view.

Two registered cameras consume one scene: distant terrain uses an altitude-
bounded far range and a near plane keeping the ratio at most 500,000; the close
transparent pass draws the ship and nearby terrain within 200 m. Nearby terrain
therefore retains actual depth occlusion of the ship. Both cameras share pose,
FOV and viewport dimensions. This avoids a cockpit-scale near plane combined
with a planet-scale far plane degenerating Godot's float frustum extraction.

Save As opens the ordinary chooser while paused and writes through the C++
atomic format-18 writer. Failure leaves the session and selected source intact;
retry remains available. Cancel does not resume automatically. Quit does not
autosave. The existing unsaved study cannot use this save operation.

## Main-exhaust source and license

The original procedural plume shader/mesh arrangement is BSD-3-Clause under
the repository license. The underlying imported model remains under its
existing [native asset provenance and licenses](NATIVE_STARTER_ASSETS.md).
No paid generation, master edit or replacement asset export is involved.

The two anchors were measured read-only at assembled frame 42 in
`hopper-craft-09.blend`, SHA-256
`87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677`.
Objects `ENGINE04 -1 | HPE02 | Aft mouth machined rim` and the corresponding
`ENGINE04 1` have Blender X bounds `[-2.79,-1.71]` / `[1.71,2.79]`,
Y `[-7.62,-7.57]`, Z `[0.485,1.975]` metres. The centres of the outer aft
faces map by `(x,z,-y)` to Godot `(±2.25,1.23,7.62)`, plus 0.03 m aft
clearance for the effect. This binds the effect to model SHA-256
`12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8`.

Intensity reads the **actual gross negative-Z propulsion force** divided by
that frame's rated main force. It includes any applied hold firing and does not
turn gravity, drag, net acceleration or raw key state into exhaust. Opposing
firings are not erased by a zero net force. Visual phase advances only while
unpaused. This increment animates the two main nozzles; retro, VTOL shutters,
RCS allocation and their measured emitter locations remain further asset/runtime
integration. The plume is visual, without invented thrust or collision damage.

2026-10-02, #374 adds [vertical withdrawal exhaust](WITHDRAWAL_EXHAUST.md) on
two separately authored exterior cosmetic mouths. It consumes actual gross
negative-Y force independently of the existing main effect. Both groups validate
the selected flight owner and complete gross buffers, stay dark while attached
or paused, and freeze their phase on invalid state. The imported craft remains
unchanged; reversible lift-shutter motion and other propulsion channels remain
separate work.

## Verification

The isolated native contract rejects malformed/truncated/nonfinite actuator
buffers, elapsed time and missing owners before exercising normal controls.
An independently written C++ format-18 fixture applies 120 ticks with identical
commands and an assistance change. The actual Godot bridge trace, including a
mid-trace Save As/Continue, must reproduce its complete save bytes. A second
presentation cadence must reproduce the same final authoritative state and
applied propulsion. Source fixtures remain unchanged. Native import/controls
and exhaust semantics include real selected/foreign joypad events, all twelve
actuator signs, fractional/opposed triggers, remapping/persistence, current
neutral resume, focus/hotplug and exact fixed-schedule saved-owner parity. Those
software checks are distinct from hardware qualification and visible GPU evidence and from the
still-open composed station/surface journey.
