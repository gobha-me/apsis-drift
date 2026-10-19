# Landed surface walking prototype

After a supported, stable Wayfarer landing, **Leave Wayfarer · suit** selects
first-person ground walking. Release controls and resume explicitly. Configured walking keys (WASD by default) and
the left stick walk; ordinary mouse movement, arrow keys and the right stick look. Pause and focus loss release the mouse; Resume captures it again. **Return through hatch**
works within 3.5 metres of the original ground access point. Reboarding pauses
again, after which the ordinary flight controls and thruster liftoff resume.
With walking and look controls released, **E**, captured **left click**, or the
selected controller's **A/Cross** uses that reachable hatch. Otherwise the same
action inspects a nearby [geological exposure](FREEDOM_SURFACE_LANDMARKS.md),
when one is in reach. Inspection opens a paused reader; Back keeps controls
paused until an explicit Resume. Both actions also remain in paused Controls.
Leaving is unavailable while airborne, attached, in a maneuver, in transit,
with a recorded pending loss, or without the supported native starter state.

A cyan **RETURN** marker points toward the actual terrain-supported aft ground
access while outside, with an edge arrow when it is off screen or behind the
camera. The compact HUD shows foot-to-access distance and **Within reach** at
the same 3.5-metre threshold used by Return through hatch. This is the existing
instant ground-transfer point, not the visible roof hatch itself. If the return
access is unavailable, the same marker instead reads **SHIP** and points only
to the retained craft centre; the HUD says Return access unavailable. One marker
avoids overlapping labels. It does not identify terrain obstructions or a safe
walking route, and hides after reboarding.

The forward-facing **Suit lamp** lights nearby terrain and the craft while on
foot, including at night. Esc/Start opens paused controls; select **Suit lamp:
On/Off** with keyboard or controller, then Resume explicitly. The white beam
follows the actual eye and reaches at most 22 metres. It requests ordinary mesh
shadows; shadow bias, mesh coverage and renderer limits can affect occlusion.
It does not brighten the sky or change sunlight, terrain, hazards or observations.
The lamp defaults on in every fresh view, including Continue/Load, and hides
when seated or flying. Its toggle is presentation state, with no saved equipment
choice, oxygen timer or resource cost.

C++ owns one planet-fixed ground actor, the existing landed ship and the sole
voyage clock. Ground contact uses the same finest top triangles, terrain recipe,
scale and planet identity as [touchdown](TERRAIN_TOUCHDOWN.md). The camera consumes
the actual actor eye and basis after subtraction in double precision; Godot
does not simulate another terrain or character body. Shared time continues
planet rotation, daylight and existing observations. Pause and focus loss freeze
that clock; focus restoration requires an explicit neutral resume.
Pausing, focus loss, opening controls and exporting a save retain the actor's
committed heading. The first neutral resumed tick continues facing that direction.

While on foot, **Flight instruments & navigation** shows your latitude and
longitude on the current planet. They follow the committed ground position
through walking and Continue. Reading them preserves the voyage clock and
saved state.

The paused controls also offer **Mark this spot** for your actual supported
foot position. One personal marker is retained per journey; **Replace marker**
explicitly overwrites it and **Clear marker** removes the point. These actions
preserve ship time and recorded sensor knowledge. Save/Continue, travel and
standard recovery retain the point and its original tick.

Choose **Show personal marker: On** in paused controls to follow **MARK** on
the current body, on foot or aboard. Its range is a straight-line distance from
your feet outside or the craft centre aboard. MARK replaces the existing
navigation cue: RETURN/SHIP outside or HOME aboard. The ground RETURN distance
and hatch action remain available. Turning the choice off restores the normal
cue; Continue starts with the normal cue. A point on another body remains listed
with already-known names and has no local bearing. A marker does not survey its
surroundings, steer the craft, identify obstacles or certify access or landing.

Tangent walking speed is 2 metres/second with normalized diagonal demand. Dry
support, a maximum 35-degree slope and a 0.30-metre step bound restrict individual
steps. A conservative standing capsule sweep against the registered craft box
stops ordinary movement through the hull. Unsupported terrain, water and blocked
steps retain the current foot position. Terrain queries use a four-tile cache;
there is no circular mission boundary around the ship. This is point ground
support and conservative box obstruction, not articulated human collision,
jumping, swimming or arbitrary terrain obstacle simulation.

The compact HUD explains the last committed blocked step: missing ground
support, water, slope, an excessive step or drop, or the craft hull. A supported
or neutral step clears the warning. Pause retains it; Continue and reboarding
clear this transient feedback without adding it to saved world state.

## Gear clearance while flying

With landing gear deployed, the compact HUD shows the **last gear gap** and its
sample age when the existing contact check finds supported dry terrain. It is
the minimum signed gap from the deployed pads to their terrain planes, measured
along those planes' normals. Reference altitude remains height above the
planet's reference sphere. Neither reading certifies hull clearance or a safe
landing.

The readout adds no terrain queries. An unsupported contact query shows
**Unavailable**, rather than a zero gap. Pause freezes the sample age. Stowing
gear retires the sample; Continue starts without one until flight samples again.
Landed, outside, liftoff and travel states do not display a retained flight gap.
The transient sample is excluded from saved world state.

## Save and recovery

Explicit exit selects format **30**, surface actor version **1** and suited
operation version **1**. It wraps the complete existing format-29 voyage,
including resources, knowledge, current-world travel and recovery lineage.
The optional ground actor supersedes the retained seated occupancy while outside;
the retained cabin record does not authorize flight until that actor returns.
The wrapper persists after reboarding, liftoff and station return. Historical
saves and ordinary New Game retain their existing explicit formats.

Recording a personal marker explicitly selects optional `surface_marker`
metadata version **1** in the existing knowledge document, alongside its fact
entries. The point stores system/body identities, planet-fixed metres and the
recorded simulation tick. Clearing retains the selected version with a null
point. Historical saves without this field retain their exact encoding; no
knowledge recipe or outer save format changes. This editable navigation record
is separate from retained sensor evidence and retired-craft history.

Save As writes through the existing bounded atomic file path. Continue stages
the actual actor, landed model and terrain while paused before committing the
new selection. Invalid dimensions, nonfinite input, wrong ownership, unsupported
versions, absent ground support, corrupt saves, duplicate keys and excessive
nesting refuse. Refused commands and selection cannot replace the active voyage.
Walking does not debit flight fuel or jump charges. Recorded destruction uses
the existing explicit [recovery](FREEDOM_RECOVERY.md) flow, with one replacement
station actor and preserved retired-craft history. Walking adds no automatic
fatality, rescue, or third suit fuel pool.

## Deliberate rough edges

Exit and return currently transfer instantly to/from supported ground behind
the registered aft hull, derived from the exact craft orientation and anchor.
The authored roof-hatch animation and a visible transfer route remain unfinished.
This is a useful ground walking prototype, not proof of a continuous climb
through the hatch. The four walking keys have separate choices in Settings / Controls, shared
with station walking. Apply saves them outside the world; Cancel keeps the
previous choices. Older controls files retain WASD defaults. E remains the
interaction key. Stick shaping consumes the existing device/deadzone/curve
settings; controller walking still uses the left stick. On foot, the existing keyboard look bindings
(default arrows) work without the flight look modifier and use the saved look
speed and vertical inversion. These bindings also apply on the station. Configured walking keys and E keep their walking/boarding meaning if a
flight look binding conflicts.
Menus retain arrow navigation while paused; release look keys before Resume,
including opposing keys held together. Right-stick look uses the same saved
speed and vertical inversion, retaining its default1.8 rad/s rate and per-axis
shaping. Mouse look uses the saved look speed and vertical inversion too.

The suited convention has **no oxygen timer**. Pack attachment, exposure damage,
instantaneous weather and a saved outside-operation hazard policy remain open
in #199/#102/#253. The shared **Surface conditions** reference remains available,
with unknown readings retained; potential environmental maxima do not become
fabricated current hazards. Stable landed support is checked, but it is not a
claim that the atmosphere, temperature or radiation is survivable outside.
No new character or ship asset is required. Far-distance planetary walking,
full-body animation, terrain materials and manual/controller acceptance remain
separate qualification.

## Checks

`planet-surface-walk-contract` compares actual terrain support, the complete
exit/walk/Continue/return/liftoff voyage and explicit recovery under GCC and Clang.
An independent long-double ray checks the foot against the selected top triangle.
The `--walk-fixtures DIRECTORY` mode emits explicit near-ground phase saves for
the public `native_surface_walk` runner contract. Its native root/button/input
route checks malformed buffers first, pause/focus, outside Continue, transactional
corrupt selection, camera frames and complete Save parity through real-thrust
liftoff. This initialized fixture does not claim a flown station-to-site journey.
Headless results do not establish GPU quality, listening or hardware controls.

Knowledge, landed-craft and native-first-jump contracts also cover marker schema
refusals, complete-save capacity, actual on-foot recording, travel and recovery
retention. The native walking tests exercise keyboard/controller mark, replace,
clear, projection and Continue while comparing the remaining complete state.

The separate `native_surface_loop` contract starts at the ordinary seed-42
station. Its recorded public commands walk to the craft, board, undock, descend
through atmosphere, cruise across generated terrain, land, walk away and return,
lift off, ascend and dock back home. It requests the existing landing and port
approach aids from their actual local envelopes; it does not assign the craft
pose or initialize a landed start. A bounded test pilot inspects nearby actual
dry contact geometry to select a site, without granting player survey knowledge.
The ground stop changes the flight plane, so the return pilot follows the current
great-circle arc toward the moving station rather than the departure orbit plane.

```sh
python3 tools/test_godot_native.py --godot /absolute/path/to/godot \
  --build-dir build --timeout 600 --test native_surface_loop
```

The C++ exporter selects this distinct schema with `--surface-loop --commands`.
An uninterrupted C++ copy and checkpoint-resumed session must produce identical
complete saves. The native root replays flight through its actual view/actuator
path and ground walking through physical W/S key events, retaining the same ship
model throughout. Every phase also independently continues for one neutral tick
and compares its full save with C++. Invalid command buffers, nonfinite values,
ownership, paths, phase order and tick totals refuse before playback.
The original planetary and neighboring-system traces retain their modes.
This is accelerated integration playback, not a gameplay autopilot, a suggested
journey duration, manual acceptance or continuous terrain collision proof.
The conservative test pilot has a 9,000-second simulation window; this is a test
bound, not a travel timer imposed on the player.
The neighboring jump route remains a separate contract; #245 retains broader
playable-handoff and hardware/controller qualification.
