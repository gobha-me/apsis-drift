# Landed surface walking prototype

After a supported, stable Wayfarer landing, **Leave Wayfarer · suit** selects
first-person ground walking. Release controls and resume explicitly. WASD and
the left stick walk; right-drag and the right stick look. **Return through hatch**
works within 3.5 metres of the original ground access point. Reboarding pauses
again, after which the ordinary flight controls and thruster liftoff resume.
Leaving is unavailable while airborne, attached, in a maneuver, in transit,
with a recorded pending loss, or without the supported native starter state.

C++ owns one planet-fixed ground actor, the existing landed ship and the sole
voyage clock. Ground contact uses the same finest top triangles, terrain recipe,
scale and planet identity as [touchdown](TERRAIN_TOUCHDOWN.md). The camera consumes
the actual actor eye and basis after subtraction in double precision; Godot
does not simulate another terrain or character body. Shared time continues
planet rotation, daylight and existing observations. Pause and focus loss freeze
that clock; focus restoration requires an explicit neutral resume.

Tangent walking speed is 2 metres/second with normalized diagonal demand. Dry
support, a maximum 35-degree slope and a 0.30-metre step bound restrict individual
steps. A conservative standing capsule sweep against the registered craft box
stops ordinary movement through the hull. Unsupported terrain, water and blocked
steps retain the current foot position. Terrain queries use a four-tile cache;
there is no circular mission boundary around the ship. This is point ground
support and conservative box obstruction, not articulated human collision,
jumping, swimming or arbitrary terrain obstacle simulation.

## Save and recovery

Explicit exit selects format **30**, surface actor version **1** and suited
operation version **1**. It wraps the complete existing format-29 voyage,
including resources, knowledge, current-world travel and recovery lineage.
The optional ground actor supersedes the retained seated occupancy while outside;
the retained cabin record does not authorize flight until that actor returns.
The wrapper persists after reboarding, liftoff and station return. Historical
saves and ordinary New Game retain their existing explicit formats.

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
through the hatch. Ground keys are currently fixed WASD; stick shaping consumes
the existing device/deadzone/curve settings. Flight binding remapping does not
yet remap those ground keys.

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
