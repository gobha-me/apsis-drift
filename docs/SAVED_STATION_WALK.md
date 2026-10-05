# Saved supported station walking

2026-10-01, #344. Ordinary fresh New Game starts a first-person actor on Origin
Station's hub floor. Walking reaches the workshop and D1 vestibule while the
actual Wayfarer stays at its qualified roof port. C++ owns the actor, craft,
station recipe, shared clock and save; Godot presents those same results.

## Movement and contact

`OriginWalkerState` stores actor identity 1, contact-geometry version 1, a
station-relative foot position and velocity, and heading. Heading zero faces
station -Z; positive yaw rotates about +Y. The owning flight/history clock
advances at 120 Hz. There is no separate actor tick, world or random stream.

This normal supported-interior slice is kinematic: requested speed is 2 m/s,
diagonal intent is normalized, neutral input stops, and blocked travel stops
at the current supported pose. It does not prescribe artificial-gravity
acceleration or implement gravity failure, inertia in a cabin, EVA or falls.

The contact package contains 55,204 indexed vertices and 108,437 actual source
triangles from the bounded route. The read-only exporter uses the admitted
station's source selection and canonical `(x-.97,z,-y+.978)` conversion. Visiting
craft and authoring fixtures are excluded. Integer-micrometre rounding has at
most 0.5 micrometre coordinate error; there is no geometric simplification.
The source master and presentation GLB remain unchanged. Package provenance,
license, counts and reproducible extraction are in
`assets/native/origin-walk-01/`. CMake rejects geometry bytes that differ from
the pinned version-one SHA256 before embedding them in the application core.

The standing reservation is an axis-aligned 0.64 m-wide, 1.93 m-high box, with
0.045 m floor clearance. Triangle/box separating-axis checks cover the full
swept reservation, conservatively including diagonal motion. Five source-floor
probes check the centre and four corners against nearly horizontal triangles
within 0.03 m of the foot datum. These are a bounded support policy, not a
general foot/contact solver for arbitrary terrain. Two-metre spatial bins
bound candidate queries; the immutable mesh is parsed once.

The contact crop is X[-24.5,0.45], Y[-0.05,2.1], Z[-0.875,0.875] metres. The
complete body must stay inside it. This deliberately limited corridor does not
make the whole station walkable. Its actual open D1 well has no added floor;
westward input stops at about X=-22.4167 m, before the shaft and before the crop
limit. Ladder traversal needs an explicit subsequent action/state.

The first-person eye is 1.70 m above the foot datum within this reservation.
Visible character art and its painted anchors do not define physical support.
No unqualified Hero model or pixel study is admitted by this increment.

## Fresh state and journey persistence

Fresh New Game generates the selected seed's existing physical Origin world,
history and identities. It places the actor at local `(0,0,0)` on the supported
hub floor facing west toward the workshop/D1 and resolves Wayfarer frame 2/version1 at the actual same-tick D1
constraint. It does not reuse a study snapshot, select a second home world or
place the pilot in a cockpit by changing cameras.

Explicit format20 composes the existing docking/flight payload with a closed
actor record. It requires the attached D1 craft, station frame identity and
valid supported actor. The root location is `station_interior`; the body stays
in its existing nonrotating home-planet frame. There is one clock and canonical
craft pose. Actor geometry, identity, pose, velocity, heading and frame are
validated before Continue or writing.

New Game now persists that unchanged journey in format21 with the explicit
[parked starting assembly](NATIVE_STARTING_ASSEMBLY.md). Historical format20
journeys keep their original presentation and format; absence of a hardware
selection means unknown hardware, not an inferred parked pose.

The existing mutable session advances walking and neutral constrained craft
co-motion as one transaction. Invalid input, state, terminal clock or failed
projection leaves the whole session unchanged. Outside-craft release, capture,
retargeting, propulsion, hold and assistance requests refuse. Sitting/boarding
must eventually change that state through real actions.

Save As uses the existing C++ atomic writer and preserves the selected source.
Continue restores the actor and voyage and begins paused. Fresh New Game runs
when its current walking/look controls are neutral; held input starts paused.
Pause, focus loss and the save chooser prevent stepping. Quit does not autosave.
Formats16/17/18/19 preserve their meanings and encodings. Format17 Continue
remains the historical frozen station inspector; older files do not acquire
an inferred actor or migrate merely by loading.

## Native presentation and qualification

The view imports the same verified station and Wayfarer models used by saved
flight. C++ projects the actor eye, station and craft at the shared tick in
binary64 before native renderer casts. Godot receives input and handles look,
materials, UI and camera placement; it runs no alternate locomotion body.
WASD/left-stick requests use the same station movement channels. Right-drag/right
stick looks. These remain the station mappings, independent of saved-flight
controls and preferences.

2026-10-03, #382: Escape or the selected controller's Start opens pause/Resume.
Arrows or Tab and the selected D-pad navigate Resume and Save As; Enter, Space
or A/Cross selects. B/Circle also requests explicit Resume while paused. Every
Resume checks the controls held now: each W/A/S/D key, right mouse and both
selected sticks must be neutral. Opposed keys remain held even if their summed
movement is zero; a prior neutral paused frame cannot authorize a later press.
Start and menu confirmation are UI actions, not walking-neutral axes.

The view selects its initially connected controller once. Other controllers
cannot steer, navigate or steal an available selection. Selected-device loss or
reconnection pauses; an absent/unselected controller can be selected explicitly
with its Start, which stays paused until a separate neutral Resume. Keyboard
Resume remains available with a disconnected selected pad. No device change,
focus return, Save As cancellation or completed save resumes automatically.
The chooser invokes the real C++ atomic Save As owner. These paused paths
preserve the committed actor/craft/shared-clock state and inspection heading,
pitch and camera. A consumer error remains paused and cannot recover through
Resume. Physical controller hardware qualification remains separate.

2026-10-03, #392: the station view keeps these same two actions and mappings in
a bounded dark scroll panel. Telemetry, movement hints and save/error status
wrap within the panel, clear of the visible scroll gutter. Text targets about
18 physical pixels and Pause/Resume and Save As target at least 40 pixels high
when the window resizes. Focus navigation scrolls either action into view;
mouse-wheel scrolling also leaves the paused journey unchanged. Smaller windows
retain readable type and vertical scrolling rather than shrinking controls.
Long messages remain reachable by scrolling; all content need not fit at once.
There is no new flight reference, actor phase or boarding shortcut.

The affected software contract checks 1280, 960, 800 and 640-pixel layouts,
long save/error text, real wheel and keyboard/selected-pad action navigation,
and exact C++ actor/craft/shared-clock state and saved bytes. Displayed review
records actual raster dimensions separately from requested window dimensions;
it qualifies readability, not physical controller hardware or the boarding
route.

Tests reject nonfinite data, invalid dimensions/buffers/indices, excessive
nesting, stale geometry, unsupported spawns, malformed fields, corrupt saves,
bad steps and terminal clocks before visual checks. Journey decoding bounds JSON
nesting to 64 levels before parsing/delegated serialization; quoted delimiters
and escapes remain data. A 100,000-level unknown-field regression exercises both
direct decode and native file load without source mutation. Out/back traces and exact
Save As/Continue bytes compose actor, craft and history at different native
cadences. Historical station and physical-flight/docking paths remain required.
Cross-host math-library qualification remains separately tracked by #255.
The station-input contract injects real software key, mouse and pad events,
including individual held/opposed controls, deferred presses, menu navigation,
focus and connection callbacks and dialog return. Paused navigation is checked
against exact C++ state and independent Save As fixture bytes; accepted station
input still matches C++ motion and the independent long/cadence traces.

The six-part owner handoff still needs open-hatch/ladder boarding, actual sitting,
departure from New Game, surface contact and the composed home-return journey.
Reaching D1 or choosing a camera does not satisfy those gates. No NPC simulation,
dialogue, weapons, economy, missions, AG failure or generic actor engine is
implemented here.

Run `tools/test_godot_native.py --test native_walk` with an explicit Godot binary
and matching build to prepare an isolated project/assets and C++ fixtures. On a
working rendering display, review actual fresh-game input with absolute paths:

```sh
"$GODOT_BIN" --rendering-method gl_compatibility --path "$CONTRACT_DIR/project" --audio-driver Dummy \
  --script res://native_walk_capture.gd -- "$CONTRACT_DIR/native-assets" "$CAPTURE_DIR"
```

Prepare `CAPTURE_DIR` first. The script refuses headless rendering and records
four actual traversal images, actor/ship ticks, poses, renderer and source/image
hashes. It stages the selected complete assembly before session activation and
records its actual model/frame/contact pins. Render waits do not step the actor;
the declared traversal commands advance the shared C++ clock.
Offline/software captures establish visible registration and movement;
they do not establish target-hardware performance or the complete journey.

Local qualification on 2026-10-01 passes all 35 CTest contracts and all 41 native
contracts with GCC and Clang. Final contact/actor hardening was rechecked in the
affected C++ contracts. The full native runs passed 40/41 before correcting one
historical inspector test that still requested New Game's old format17 view;
that affected contract then passed in both builds. No deadline was increased.
Pinned formatter20, suppression policy and runner tests pass. Local tidy20 is
unavailable; hosted analysis remains a required publication check. Its initial
integer-widening finding in the mesh byte limit is fixed without suppression.
After the depth guard and spawn orientation change, all four affected C++ and
five affected native contracts pass again with both compiler builds.

Displayed Godot 4.7.2 compatibility-renderer review produced four 1280x720
images per compiler: hub, workshop, downward look into the actual D1 well and
return. All image/script/bridge receipts verify; corresponding GCC/Clang images
are byte-identical on the same Mesa software renderer. The real C++ actor
reaches X=-16 m at tick961, stops at X=-22.4166666666667 m at tick1602, and returns
to the hub at tick2948. The hub image uses the ordinary untouched tick-zero
spawn, already facing the workshop. Actor and craft ticks agree throughout. The downward
look changes only presentation pitch, not the supported actor pose. Evidence
is retained locally in `build-native/walk-review-gcc` and
`build-native/walk-review-clang`; no target-hardware acceptance is implied.
