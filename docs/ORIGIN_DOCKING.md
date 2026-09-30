# Canonical Origin-port docking

2026-09-30, #338. C++ owns the approach assessment and mechanical attachment
contract for the explicit [Wayfarer frame](WAYFARER_PHYSICAL_FRAME.md) and
[Origin Station ports](ORIGIN_STATION_GEOMETRY.md). This provider does not
complete #220's native lifecycle/persistence or the #245 playable handoff.

The query validates the physical world owner, immutable geometry, one qualified
port identity and the full canonical craft state at its authoritative tick.
Supported source frames are station-relative, system-inertial and named
planet-relative nonrotating frames. Planet-fixed input requires a separate,
explicit rotation handoff before assessment.

The selected port reference is expressed in the source's validated frame using
the existing C++ ephemeris/handoffs. Relative position and velocity are compared
there, avoiding two subtractions of large global origins. This preserves exact
coincidence/co-motion for a reference pose and leaves authoritative state
unchanged. These relative error vectors are queries, not a new direct
station/planet state handoff. Rendering remains a consumer of the same port.

The actual body quaternion rotates the roof collar and body angular velocity.
Collar velocity includes `omega × collar_offset`; the centre's linear closure
alone cannot authorize capture. Full quaternion alignment also rejects a craft
whose legacy yaw matches while roll/pitch are wrong.

Capture requires all existing inclusive limits: at most 0.15 m separation,
3° full alignment, 0.2 m/s lateral collar motion, 0–0.3 m/s inward collar
closure and 0.02 rad/s body angular speed. Comparisons use the computed binary64
metrics with no hidden tolerance widening. A nominal decimal separation can
round to the adjacent position outside its limit; fixtures test the neighbouring
representable positions on each side. The collar must be on the outward
approach side. The complete rotated **stowed** hull must also fit the selected
reserved withdrawal column. Attitude tolerance does not waive clearance.

Valid queries report identity/tick, position/rate/alignment metrics, reservation
fit and an actionable decision: incompatible craft, wrong side/attitude,
outside reservation, too far, retreating or excessive closure/lateral/angular
motion. Invalid inputs return errors before any attachment exists. Refusal is
side-effect free; it does not guide, brake, snap or advance the ship.

Authorized capture creates one constraint containing geometry version,
station-qualified port, explicit craft recipe and tick. Resolving it supplies
the fixed mechanical port pose and zero motion relative to the station.
The small accepted position/attitude/rate error is arrested by that mechanical
attachment, not by a distance-only teleport or uncommanded flight assistance.
The station ephemeris is the kinematic attachment owner; this provider does not
simulate station recoil or derive coupling load ratings from the mesh.

Release returns the **identical same-tick constrained planet-relative pose**,
including station co-motion. It never substitutes a planetary practice start,
steps the clock or supplies a propulsion impulse. The caller owns the actual
release lifecycle, subsequent flight commands and persistence/synchronization
of the attachment with world time. Docking grants no resources, money or mission
completion.

Clearance here is bounded to the immutable port reservation. It does not certify
arbitrary exterior swept collisions, deployed equipment, blocked doors, moving
cargo or other dynamically occupied space. Those owners must gate their
transitions explicitly; a successful reservation query is not a global
collision exemption. The station's coarse exterior bound is never treated as
a solid box filling the boarding route.

`origin-docking-contract` tests invalid/nonfinite/foreign state first, then
inclusive/just-outside pose/rate boundaries, equal-distance rear approaches,
full attitude, collar angular-point velocity, whole-hull clearance and exact
capture/release agreement for both ports, multiple seeds/ticks and all three
supported frames. Rigid projection/hydration retains the decision. Existing
reference geometry, physical flight and player-save goldens remain separate.
