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

Boarding, unboarding and in-session Load retain the current controller selection
while creating fresh input latches. The chosen device owns both mapped flight actions and the
next station-walking view; another connected pad cannot take over through that
view change. Initial launch still uses the first connected controller.

Assistance changes explicitly through C++. In paused controls, **Hold current
orbit radius and plane** selects a circular-orbit target from the current C++
radius and angular-momentum plane. Selection requires a stable bound orbit above
the actual air boundary, a seated pilot, and no attachment, landing constraint,
surface maneuver, active jump or pending loss. It changes only the saved request;
position, momentum, clock and fuel remain unchanged until explicit resume.

Correction uses the existing bounded thrusters and consumes actual gross flight
fuel. It cannot insert, freeze or guarantee an orbit. Manual translation or
assistance OFF pauses correction while retaining the target; insufficient fuel
retains passive motion. **Disable orbit-hold request** clears the target. The
menu labels the saved request separately from active correction, including its
radius and limitations. Save As / Continue retain its exact planet, radius and
signed plane without a new save version. Ordinary assistance still coasts.

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
threshold or additional save field is introduced by the forecast.

The [native information hierarchy](NATIVE_HUD.md) keeps a compact mode, motion,
fuel/jump and contextual-action card visible during ordinary flight or ground
walking. **Esc / Start · Controls** opens the existing paused menu; choose
**Flight instruments & navigation** for complete telemetry and every existing
command. **Back to controls** stays paused. The detailed panel wraps long
station, port and save-status text; its visible scrollbar keeps actions reachable
in short windows. The orbital forecast stays alongside it when width permits
and joins the scrolling stack at narrower widths. Type and actions compensate
for physical canvas scale. Read-only layout/scrolling does not advance simulation;
opening controls retains the existing pause/cancel-aid and neutral-resume rules.

The displayed full body attitude and home-station vector are projected in C++
from the saved nonrotating planet frame into the generated rotating planet's
same-tick tangent frame. Projection is query data, never a new saved frame or
physics handoff. Global coordinates stay binary64 until the local projection is
passed to Godot. C++ supplies the actual fixed terrain owner to `PlanetStream`,
with source LOD 8 and relief 0; this consumer does not introduce the study's
experimental relief into saved worlds. A failed projection/import refuses
actionably rather than generating a substitute universe.

The planetary sky now consumes a versioned, body-qualified observation from the
same C++ flight projection: current tick and system/body identities, canonical
star direction, actual generated star color/angular size, and the selected
planet's atmosphere palette and version-one density/scale-height/boundary.
Flight and suited surface walking share this presenter. Both camera depth ranges
receive the same ambient lighting; the background sky is drawn once. Station
walking retains its authored interior fill and uses the actual star color.
These are read-only presentation values. Pausing, looking around, and rebuilding
the material do not advance time or change saves. Unsupported identities,
versions and malformed coefficients refuse before presentation.

The atmospheric material uses a bounded eight-sample appearance approximation,
not weather or a radiative-transfer simulation. Background stars, distant-body
phase and full planet-shadow qualification remain work under #213. The existing
physics, terrain heights and atmosphere recipe are unchanged.

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
Saved controls maintain at least 18 physical pixels for body text and 44 for
interactive control height at the checked 1280×720, 800×450 and 640×450 sizes.
Below 1000 physical pixels wide, action and remapping lists stack with independent
focus-following scroll; instructions, remapping headings and refusals wrap.
Resizing or navigating these lists stays paused and preserves the complete save.
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
cancellation never resume automatically. The current saved profile also teaches fuel/jump separation, chart travel,
landing/liftoff, suited return, recorded-loss replacement, station boarding and
explicit Load/Title confirmations. The historical
thrust lab keeps its separate limitations; manual/controller and wider journey
qualification remain in their existing work items.

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

The transparent close-camera viewport uses a premultiplied-alpha canvas
compositor (`native_close_composite.gdshader`). The 3D pass already weights
exhaust RGB by surface alpha; ordinary canvas blending applies alpha again and
can erase a low-thrust additive plume. Opaque ship/station pixels retain their
existing depth tests. This changes presentation only. Godot documents the
[canvas blend mode](https://docs.godotengine.org/en/4.7/tutorials/shaders/shader_reference/canvas_item_shader.html).

`studies/captures/native_exhaust_alpha_capture.gd` is an opt-in render-only
regression fixture: pass an absolute output directory on a rendering display.
It compares identical raw 3D attachments with ordinary and premultiplied canvas
composition, checks visible 10% thrust and zero-thrust darkness, and records
PNG hashes, shader identity and renderer metadata. It neither loads a journey
nor advances physics. The composed boarding capture separately records actual
main/withdrawal forces and close-camera attachments during the real round trip.

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

In-game [Load…](NATIVE_LOAD.md) now reaches the same transactional Continue
seam from paused station, saved flight and suited views. Cancel/refusal keeps
the current journey; confirmed successful replacement remains paused.

**Title…** in paused controls returns to the [native start screen](NATIVE_TITLE.md)
only after explicit discard confirmation. Cancel and focus loss keep the journey
paused; neither path autosaves or resumes it.
