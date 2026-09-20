# Canonical rigid-body frame handoffs

This is the first bounded provider slice of issue #200, not completion of its
planetary or live-regime integration. `reframe_rigid_body` changes coordinates
of an existing validated [rigid-body state](RIGID_BODY_STATE.md) at one
authoritative tick. It does not advance time, dock, launch, integrate forces,
change held controls, or modify a save format. The native flight lab and its
16-metre terrain guard are untouched.

## Supported boundary

`system_inertial` and `station_relative_inertial` have identical axes. The latter
is translated with the registered origin station; it is **not** the rotating
station body frame. For the authoritative station system-space position `S` and
velocity `V` resolved at `(state.tick, sub_tick_fraction = 0)`:

| Direction | Position | Linear velocity |
| --- | --- | --- |
| Station-relative to system | `p + S` | `v + V` |
| System to station-relative | `p - S` | `v - V` |

The station ephemeris already contains host-planet translation and velocity.
Use its final quantized system-space fields once; adding the host again, or
reconstructing them from separately rounded host-relative fields, is wrong.
Quaternion, body angular velocity, craft recipe and tick preserve their exact
bits. No angular correction arises from the station's orbital motion because
the frame axes do not rotate. Station-relative coordinates are nevertheless
accelerating origins: this coordinate-change provider is **not** authorization
to run the system-inertial vacuum force provider there without the appropriate
acceleration terms.

An already canonical same-frame request is an exact identity after source,
destination and tick validation. This includes a planet-fixed identity, not
permission to transform to or from that frame.

## Validation and numerical contract

The API receives the existing authoritative `RigidBodyWorldContext`, a const
state, and a destination/tick request. It validates source state, requires the
request tick to equal the source tick, and validates destination ownership using
the same rigid-state rules before querying ephemerides. A missing, forged or
wrong-system station is rejected. Other systems, unknown bodies, invalid frame
field combinations, invalid craft recipes, noncanonical values and the reserved
maximum tick remain rejected by the existing contract. A same-frame request
does not bypass these checks.

Supported transforms use componentwise binary64 addition/subtraction. Only the
complete candidate is canonicalized, to remove any signed zeros; no quaternion
renormalization is performed. The result is validated against existing position
and velocity bounds before it can be returned. Errors identify source,
destination, tick, unsupported pair, ephemeris or resulting-state failure and
retain the underlying validation detail where applicable. Failure never changes
the source or context. There is no hidden neutralization of caller-owned held
intents; future gameplay transition code must define and test that policy.

Roundtrips are not promised bit-identical for position/linear velocity: adding a
small local coordinate to a distant system origin can lose low bits. For each
component, tests bound roundtrip absolute error by
`4 * epsilon(binary64) * max(1, abs(original), abs(station offset))`.
The same bound applies to velocities using the station velocity. It is not a
relative error promise for a tiny local displacement, and repeated unnecessary
handoffs should not be used as an integrator. Orientation and body angular
velocity remain exact. The provider has source-local `-ffp-contract=off`; it
consumes the unchanged existing ephemeris arithmetic. GCC/Clang fixture agreement
is qualified by focused tests, not a universal cross-libm guarantee.

Existing standalone rigid-state JSON can be encoded/decoded on either side and
continued through the next handoff with identical results. This is not a legacy
save-16 migration or saved-career integration.

## Explicit remaining dependency

Issue #213 is still needed before full #200 rotating-frame implementation.
The legacy system-flight path privately assumes a 24-hour Z spin with
PlanetId-derived phase; the existing local-lighting recipe uses a different
accelerated local-day phase and sun declination. Neither can silently become a
new unified canonical per-body rotation contract. The native lab is explicitly
planet-centred **nonrotating**, not `planet_fixed`.

This provider therefore rejects nonidentity planet-fixed transitions. Future
work must first define the authoritative period/phase/axis recipe and owner
identity, then transform full quaternion and body angular velocity together
with translation and rotating-frame velocity at the same tick. Local-tangent
anchor ownership, physical launch/entry/docking gates, held-intent policy and
live bridge adaptation remain separate integration work. No renderer-derived
transform or synthetic rotation fallback is introduced here.

## Tests

`rigid-frame-handoff-contract` covers authoritative same-tick station p/v,
co-moving station-origin cancellation, nontrivial quaternion/spin preservation,
three tick fixtures including the largest valid tick, roundtrip bounds, exact
standalone JSON continuation, same-frame validation, unsupported planetary
transitions, malformed identity/numerics/tick and legal-input/illegal-output
position and velocity boundaries in both directions. New checksum fixtures are
pinned only after both compilers agree; existing ephemeris and rigid-state
goldens are not changed.
