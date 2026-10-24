# Experimental terrain impacts

The native Freedom flight menu offers an explicit, saved terrain-impact
selection for the registered Wayfarer. It does not change the default flight
path or migrate historical saves. C++ owns contact, response, condition and
recovery; Godot displays their results. This is a bounded gameplay prototype,
not a general destruction or material-fracture engine.

## Contact and motion

Version one uses the registered hull box and deployed landing-support
footprint/stroke boxes as collision proxies. These are declared gameplay
shapes, not an exact collision mesh of the visual asset. Each fixed 120 Hz
step freezes the initial orientation in the planet-fixed frame and translates
the proxies linearly between the actual endpoints. Each endpoint is reframed
at its own authoritative tick. This approximation is explicit; excessive
motion or rotation falls outside its coverage. Near-ground motion is bounded
to 64 metres per tick, quaternion chord 0.018 and relative spin of at most
2 rad/s. The complete proxy footprint and one-cell halo must fit one cube
face and at most 128 cells (256 triangles). Deployed support boxes extend
upward through their registered compression stroke.

A bounded sweep tests actual selected terrain triangles. A nonpositive
clearance bound or unsafe touchdown assessment alone cannot create damage.
The earliest witnessed contact retains craft/world identity, both source
checksums, triangle, local contact point, geometric outward normal, fraction
and closing speed. Unsupported coverage, seams or material refuse the
experimental tick before commit. They do not invent a collision or death.
This includes water and shoreline intersections. Equal-time contacts use a
stable cell, triangle-half and craft-part order. A conservative far-field
clearance proof uses the versioned generator's derived 17,070-metre height
bound without sampling a second terrain.

Safe qualified touchdown keeps its existing whole-pad and landed-state path.
For an inward collision, the prototype stops at the witnessed pose, retreats
two millimetres along the terrain normal, removes inward ground-relative
velocity and relative spin, and preserves tangential velocity. Separating or
resting contact does not force that response.
Initial overlap can certify a contact, but the two-millimetre retreat is not a
general penetration solver for a craft that already intersects several facets.

## Condition and feedback

Two permanent region identities establish a small shared condition model:
forebody and landing structure. The hull proxy maps to forebody; support
proxies map to landing structure. More detailed regional hull geometry remains
future work. These areas belong to one individual craft, its frame recipe and
lineage, rather than a fleet-wide hull percentage.

The following closing-speed thresholds are gameplay rules, not claims about
real material strength. A contact adds the assigned class to the affected
area, capped at severity three.

| Closing speed | Damage class | Outcome |
| --- | --- | --- |
| At most 2 m/s | 0 | Contact without damage |
| Above 2, at most 8 m/s | 1 | Localized damage |
| Above 8, at most 20 m/s | 2 | More severe localized damage |
| Above 20, at most 60 m/s | 3 | Severe localized damage |
| Above 60 m/s | 3 | Classified catastrophic loss |

Forebody severity one displays a cockpit glazing crack cue; severity two or
three reduces visibility. Landing-structure severity two or three refuses
safe touchdown. Cracks do not declare cabin pressure loss or oxygen exposure.
Camera motion, display frames and UI callbacks cannot generate a damage event.

One contact episode can charge ordinary damage once. A new certified
catastrophic hit can escalate that episode to loss; the contact gate does not
grant immunity to destruction. Two consecutive complete sweeps
with the separate five-centimetre clear margin rearm contact. Resting gravity,
retrying a tick or Save/Continue cannot rearm it. A certified clear sweep within
the five-centimetre margin permits movement without rearming; unknown coverage
cannot prove separation. Only the latest event receipt is retained, with a
monotonic serial.

## Saves and recovery

An additive format-34 envelope retains the complete existing voyage and the
explicit consequence policy, craft condition and bounded retired condition.
Old nested formats, terrain selection, resources, walking and knowledge remain
authoritative. Condition uses the shared voyage tick; it does not own a second
simulation clock.

Physics response, gross fuel debit, condition and any classified loss commit
together. A failed query, validation or recovery staging leaves the live
session untouched. Catastrophic contact uses the existing recorded-loss and
explicit standard replacement path. The replacement has a new individual
identity and pristine condition; retired history remains bound to the lost
craft. Repeating recovery cannot grant another replacement.

A live catastrophic tick stops the native frame at that committed loss, even
when the frame had several physics ticks scheduled. Unsimulated frame time is
discarded; it cannot produce extra damage, fuel charges or replacement motion.
The flight presenter pauses controls and exhaust and opens the same explicit
standard recovery choice used by Continue. Waiting there leaves the pending
save intact. Continuing returns to a paused replacement actor at Origin Station.

[Repair at Origin](ORIGIN_REPAIR.md) now clears noncatastrophic damage on the
same surviving ship, while retaining its bounded impact history. Technical
service is free; existing paid journeys review a cash-only quote. Nanites,
drones, combat, per-fragment physics and jump-stress mechanics remain separate
work. The stable condition identities remain shared across impacts and repair.

## Validation boundary

The provider, reducer, strict codec and native session have separate headless
contracts for invalid/nonfinite state, actual crossings, threshold edges,
repeat contacts, exact historical bytes, selected Continue and recovery.
Native cue checks consume fixtures produced by authoritative contact events.
The native impact test also flies the actual pre-impact fixture at single- and
multi-tick cadences, compares complete pending and recovered saves with independent
C++ results, invokes the recovery button and checks replacement walking.
Paired compiler determinism, software visual evidence and manual hardware
playtesting are reported separately; none substitutes for the others.

Actual Godot Compatibility software captures of unselected flight and forebody
severities one and two completed with zero presentation assertion failures.
Process shutdown reported six retained 512×512 R8 sky textures, including after
the bounded two-frame teardown. The captures demonstrate the current cue;
clean rendered-run qualification is not claimed.
