# Canonical rigid-body frame handoffs

These are bounded provider slices of issue #200, not completion of its
live-regime integration. `reframe_rigid_body` changes coordinates
of an existing validated [rigid-body state](RIGID_BODY_STATE.md) at one
authoritative tick. It does not advance time, dock, launch, integrate forces,
change held controls, or modify a save format. The native flight lab and its
16-metre terrain guard are untouched.

## Original three-argument station boundary

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

The original three-argument transforms use componentwise binary64
addition/subtraction. Only the
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

## Explicit four-argument rotating boundary

The new overload receives an additional `const PlanetRotationRecipe&`. It uses
the [owner-qualified planet rotation provider](PLANET_ROTATION.md), never the
legacy 24-hour system-flight spin or ten-minute local-lighting recipe. Selecting
this overload explicitly declares the planet-fixed side's interpretation. The
original three-argument API still rejects all nonidentity planet transitions;
linking the new implementation does not reinterpret existing callers or states.

The explicit overload supports system-inertial ↔ the recipe's planet-fixed frame,
and exact identity within that planet-fixed frame. Station/planet pairs,
cross-planet pairs, and system/station identities are deliberately unsupported:
there is no fallback that ignores the supplied recipe. Use the original API for
inertial-aligned station changes. A same-planet identity validates the full recipe
first, then returns every source bit unchanged, including a permitted quaternion
norm residual.

Validate source, tick and destination first, then check the selected planet and
resolve the exact owner-qualified recipe at that tick. Unknown versions, altered
parameters, mismatched catalog variants or the wrong selected planet refuse.
`invalid_rotation_recipe` is appended to the existing error codes and retains a
`PlanetRotationError` detail; original error ordinals are unchanged.

Let `R`/`Q` be the active planet-fixed → system rotation/quaternion, `P`/`V` the
same-tick final planet ephemeris, and `Omega` its system-space angular velocity.
The body orientation `q` maps body axes into its owning frame. Angular velocities
below are always resolved in **body axes**, but relative to their owning frame.

| Quantity | Fixed → system | System → fixed |
| --- | --- | --- |
| Relative radius | `r = R*p_f` | `r = p_s-P` |
| Position | `p_s = P+r` | `p_f = inverse(R)*r` |
| Linear velocity | `v_s = (V+R*v_f) + cross(Omega,r)` | `v_f = inverse(R)*((v_s-V)-cross(Omega,r))` |
| Attitude | `q_s = normalize(Q*q_f)` | `q_f = normalize(conjugate(Q)*q_s)` |
| Body-resolved angular velocity | `w_s = w_f + inverse(q_s)*Omega` | `w_f = w_s - inverse(q_s)*Omega` |

The `q_s` used to resolve frame spin is the newly composed system attitude in
the forward direction and the original system attitude in reverse. Do not add
system components directly to body rates, rotate the existing body-rate vector,
or use only `Q` to resolve frame spin. A surface-fixed craft has both tangential
velocity and nonzero inertial spin; an inertially nonspinning craft has the
opposite frame spin when represented in rotating coordinates.

Quaternion composition uses Hamilton multiplication. Each newly composed
attitude is explicitly normalized once; source state and JSON hydration are
never normalized. Vector rotations use `q*v*conjugate(q)/norm_squared(q)`.
Only the complete candidate is canonicalized and validated before return.
Legal source magnitudes can produce an illegal result after rotation,
translation, `Omega × r`, or body-spin correction; those cases refuse without
clamping, truncating or mutating the source. This is a coordinate transformation,
not a rotating-frame force integrator or a landing/docking transition gate.

### Precision and projection continuation

Unlike the original station API, rotated quaternion and body-rate roundtrips
are not bit-preserving. A legal input quaternion may have a norm-squared residual
near 1e-12; normalizing the newly composed attitude legitimately removes that
residual. Tests compare the represented rotations as norm-correct matrices
(maximum element error 2e-14), not quaternion component identity. Source and
same-frame identity remain bit-exact. Body-rate fixtures use 2e-14 radians/second
absolute roundtrip tolerance; this is measured fixture qualification, not a
formal interval-arithmetic proof over every legal state.

For the tested fixed → system → fixed component roundtrips, let `eps` be
binary64 epsilon and `maxabs` the maximum absolute component. Position tolerance
is `128*eps*max(1,maxabs(p_f),maxabs(P))`. Velocity tolerance is
`256*eps*max(1,maxabs(v_f),maxabs(V),maxabs(Omega)*max(maxabs(p_f),maxabs(P)))`.
These intentionally account for cancellation against the system origin and its
contribution to rotational-speed error; they do not promise relative accuracy
for a tiny local displacement. This is not an integrator to call repeatedly.

Existing rigid-state JSON still stores only its original fields. The caller
must retain the **explicitly selected recipe/context alongside it** and use the
same recipe after hydration. Tests prove bit-identical next-handoff continuation
under that selection, not automatic historical save interpretation. No rotation
recipe is inferred from old JSON, and no save-16 migration is added. A future
persistent native world must bind the rotation version explicitly.

## Remaining integration

The native lab is still planet-centred **nonrotating**, not `planet_fixed`.
It is not switched by this provider. Local-tangent anchor ownership, physical
launch/entry/docking gates, held-intent policy and live bridge adaptation remain
separate integration work. #213 still owns native lighting/rotation consumers;
#255 still owns cross-host math-library dependence. No renderer-derived
transform or synthetic spin fallback is introduced here.

## Tests

`rigid-frame-handoff-contract` covers authoritative same-tick station p/v,
co-moving station-origin cancellation, nontrivial quaternion/spin preservation,
three tick fixtures including the largest valid tick, roundtrip bounds, exact
standalone JSON continuation, same-frame validation, unsupported planetary
transitions, malformed identity/numerics/tick and legal-input/illegal-output
position and velocity boundaries in both directions. New checksum fixtures are
pinned only after both compilers agree; existing ephemeris and rigid-state
goldens are not changed.

The explicit rotating tests additionally cover independent matrix position,
velocity, attitude and body-spin oracles, a local finite-difference velocity
check, corotating rest/inertial nonspin, all catalog planets at wrap and
near-maximum ticks, near-tolerance quaternion norms, recipe/context refusal,
output magnitude failures and exact continuation with a caller-retained recipe.
The finite-difference check excludes planet-centre displacement because the
existing ephemeris rounds positions to metres: its sampled position is not an
exact differentiable function of its separately quantized velocity.
