# Canonical rigid-body frame handoffs

These contracts define issue #200's coordinate providers and their persistent
native flight integration. `reframe_rigid_body` changes coordinates of an
existing validated [rigid-body state](RIGID_BODY_STATE.md) at one authoritative
tick. The provider does not advance time, dock, launch, integrate forces or
change held controls. The saved session owns those actions and commits a
complete voyage only after its transition validates. Native presentation
consumes that C++ identity through relative observations; it owns no second
world origin.

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

Legacy v1 rigid-state JSON still stores only its original fields. The caller
must retain the **explicitly selected recipe/context alongside it** and use the
same recipe after hydration. Tests prove bit-identical next-handoff continuation
under that selection, not automatic historical save interpretation. No rotation
recipe is inferred from old JSON, and no save-16 migration is added. A future
persistent native world must bind the rotation version explicitly.

## Physical catalog ownership (2026-09-30, #311)

The [physical rigid context and version-2 projection](RIGID_BODY_STATE.md#explicit-physical-ownership-2026-09-30-311)
select the catalog family explicitly. The original three-argument station
handoff dispatches to the physical station ephemeris when that owner is present;
legacy contexts retain the legacy resolver. The same final p/v arithmetic,
translation-only axes and bit preservation apply. A physical ephemeris failure
retains `PhysicalLocalSystemError` in the appended `physical_ephemeris_error`
field; existing error codes/details keep their meaning.

A distinct four-argument overload takes `PhysicalPlanetRotationRecipe`. It
resolves physical rotation geometry at the source tick, then uses the same
numerical transform as the legacy recipe overload. A physical recipe in a
legacy context, or a legacy recipe in a physical context, refuses, including
same-planet identity. Matching IDs and unwrapping the nested spin recipe do not
opt in. The complete recipe owner/version remains validated by the existing
physical rotation provider before the shared kernel is invoked.

`physical-rigid-contract` compares p/v, attitude and body spin to independent
matrix calculations using the selected physical C++ geometry. It covers
procedural and origin catalogs, wrap and near-maximum ticks, station-origin
cancellation, canonical identity, malformed ownership/clocks/recipes,
unsupported pairs and legal-source/illegal-result magnitudes. Both translation
and rotating handoffs resume bit-identically after physical v2 JSON hydration
with the same retained context/rotation recipe. Historical legacy JSON and
handoff corpus goldens remain unchanged regression gates.

## Named nonrotating planet boundary (2026-09-30, #319)

The physical-only `planet_relative_inertial` frame stores planet-relative p/v
with nonrotating, system-aligned axes and an explicit planet owner. The original
three-argument API now also supports physical system ↔ this named frame. Use
that planet's final C++ physical ephemeris once: relative → system adds planet
p/v, and system → relative subtracts it. Craft, tick, quaternion and body spin
keep their exact bits. Planet-centre co-motion cancels exactly. Position/velocity
roundtrip tolerance uses the same `4*eps*max(1,original,origin_offset)` component
budget as station translation. Direct station/planet-relative and cross-planet
shortcuts remain unsupported; a canonical same-frame identity remains exact.

The physical rotation-recipe overload also supports **same-planet** fixed ↔
nonrotating relative frames. It uses the existing rotation kernel with origin
position/velocity zero: rotate local radius/velocity, add/subtract `Omega × r`,
compose/normalize candidate attitude once, and resolve the frame-spin correction
in body axes. It never adds planet system translation to this pair. Relative
roundtrip precision therefore uses the existing rotation bounds with `P=V=0`,
scaling against local radius/velocity rather than cancellation at a system
origin. Source/hydration are untouched. System/fixed transforms retain their
original translation and arithmetic.

A physical recipe is required for the rotating pair; wrong family/body/version,
cross-planet pairs or an ignored-recipe nonrotating identity refuse. Use the
three-argument API for nonrotating identity/translation. Standalone JSON
explicitly uses [v3 for the new frame](RIGID_BODY_STATE.md#planet-relative-nonrotating-projection-2026-09-30-319)
and preserves v1/v2 for existing meanings. Exact next-handoff continuation is
qualified with the same caller-retained world/rotation recipe.

Neither translating coordinates nor their `inertial` axes label supplies a
force integrator for the moving origin. The force-free vacuum provider still
refuses this frame. A later central-body approximation must specify that motion
before saved planetary flight is adopted. There is no launch/dock/contact gate,
gravity call, implicit local tangent anchor or renderer-derived origin here.

## Persistent native integration

The [explicit system-frame import](SYSTEM_FLIGHT_IMPORT.md) consumes a complete
format35 source into ordinary saved native flight at the same tick. Format35
remains input-only and does not select the release frame policy or change force
laws. The separately selected policy below preserves live system departure,
relative insertion and their valid saved reverse paths through explicit craft
actions. Entry/ascent continue through the same authoritative atmosphere and
rotation providers; no altitude/SOI switch resets pose or motion.

Saved docking/release, surface actions and anchored tangent presentation keep
their existing application owners. Canonical flight stays system-inertial or
nonrotating planet-relative; fixed/tangent representations are qualified views,
not a new unsaved canonical anchor. Body-resolved semantic controls retain their
meaning across the aligned translation. Pause, focus and Continue retain their
existing explicit input-neutralization rules without changing physical motion.

The retained native lab is distinct from this persistent journey and remains
planet-centred **nonrotating**. Physical rotating transforms do not authorize a
rotating-frame force integrator. #255 owns cross-host math-library dependence;
FTL/contact physics and camera/hardware acceptance remain separate. No
renderer-derived transform or synthetic spin fallback is introduced here.

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

## Explicit release-selected system flight

Frame policy **2** is an explicit saved selection, with formats **36** (flight),
**37** (flattened docking) and **38** (flattened station journey). Historical
policy **1**, all previous formats and ordinary New Game keep their existing
relative representation. This increment selects no new altitude/SOI threshold.

For the selected policy without an active impact condition, successful physical
undocking transforms the complete body into system coordinates at the same tick. The saved active-world owner
retains the actual selected force/terrain planet even though a system frame has
no planet field. Ordinary ticks derive a transient relative state, run the
unchanged central-gravity/atmosphere/hold/actuator/resource providers, then
transform their accepted result back at the final tick. This retains the same
physical approximation; it does not switch to force-free space or add N-body
physics. Quaternion and body-resolved spin survive the aligned translation.

Admission authenticates the complete saved owner and retains its physical system
and rotation recipe in the session. Ordinary system observations use that owner
with explicit policy/world/model/tick checks and the same validated rigid
transform. They avoid resolving a new document-level world for every read;
downstream providers retain their own context validation. Ordinary system ticks
retain the relative transaction's flight/history, resource and scheduled
knowledge checks. The final canonical body's relative atmosphere and orbit-hold
selection are checked again after system-origin cancellation, before the whole
candidate commits. Unchanged voyage histories travel with that candidate.

These ticks do not project and JSON-encode the entire voyage solely to repeat
those proofs. Public document helpers, untrusted Continue admission, actual
Save As and explicit frame/owner-changing commands retain their strict complete
validation. This reuse changes no default, arithmetic, tick, ledger or save
format; relative policy1 follows its existing path.

Explicit frame/owner-changing commands validate the entire candidate, including
attachment, pilot/assembly, resources, knowledge, travel and recovery, before
commit. Save/Continue retains the
canonical system state through the existing complete voyage wrappers; it never
reconstructs a bare flight and drops the remaining owners. Real port assessment
uses the qualified relative observation; capture commits the existing relative
port constraint and attachment together. Presentation consumes relative local
vectors and publishes separate canonical frame/vector metadata and checksum.

For policy2, explicit gear deployment converts a system body to its saved
planet's nonrotating relative frame at the same tick, then enables the existing
contact path. A landing request uses this same deployment seam before its
existing aid checks. Direct system-frame touchdown still refuses. A real
airborne deployed-to-stowed gear transition returns the body to system
coordinates; a selected impact condition or active port approach instead
retains its supported relative frame. Stow preserves those owners and cancels
the existing surface maneuver. Landed stow and already-stowed system requests
refuse. Neither event changes world selection, attitude, body spin or time.

The complete candidate must serialize before either gear handoff commits;
refusal preserves body, gear, aid state and every voyage owner. Recovery,
active travel, outside actors and station attachment retain their existing
guards. This is an explicit gear event, with no automatic altitude boundary,
new force law or new terrain/contact physics.

For policy2, **Approach port with thrusters** assesses its existing physical
bounds through a read-only relative view. Selecting it converts the whole voyage
to the saved planet's relative frame at the unchanged tick, invokes the existing
aid and validates the complete saved candidate before commit. Cancel retains
that valid relative frame; Continue retains the body and starts with the
transient aid off. Existing port capture constrains the relative body, and actual
port release selects system coordinates again unless an existing condition
retains its supported relative body. No automatic frame boundary,
new approach threshold or force law is introduced.

For policy2, selecting an existing known jump destination keeps the exact
canonical system body. Preview and availability query only the same-tick
relative view. Beginning the jump commits that relative frame with the existing
spool action after complete voyage validation. The unchanged starter rules own
charges, transit and arrival; cancel retains the relative source, and arrival
installs its existing relative target body. No new route, risk, force law or
frame-selection default is introduced.

For policy2, impact-policy availability inspects a strict read-only relative
view. Explicit activation commits that same-tick relative body together with
the existing condition and complete saved voyage after validation. Already
selected conditions keep their idempotent activation behavior. The condition
remains in its supported relative frame; activation does not add an impact,
change damage/recovery physics or select a new ordinary default.

With a selected condition, physical port release keeps the validated relative
constrained body at the same tick, preserves the condition and complete voyage,
and validates the detached candidate before commit. Existing condition damage
permissions are unchanged. Contact and active-jump release refusals remain.
Ordinary New Game keeps policy1. The selected policy is an explicit persisted
representation, not an extra game mode, player debug control or automatic
migration. Unsupported direct frame pairs continue to refuse; no additional
frame policy or altitude gate is implied.

`native-system-release-contract` exercises real station walking/boarding,
undock, held actuator steps, complete resume and actual capture. Its independent
oracle adds/subtracts the final C++ ephemeris once, using the existing
4×binary64-epsilon component/offset roundtrip budget; complete resumed saves,
craft/tick, quaternion and body spin retain exact equality. The paired native
`native_system_release` consumer checks the actual shell/model and canonical
saves. These automated contracts do not qualify GPU pixels or controller feel.

## Selected surface-loop composition contract

The existing nineteen-phase surface-loop fixture has an opt-in selected-policy
route, invoked through the [native runtime runner](DEVELOPMENT.md#native-runtime-contracts).
It selects the real complete station starter and existing knowledge/world owner;
its native replay uses Continue on that actual station-start save. Ordinary
New Game and the original policy1 route retain their existing startup.

The route uses the same public commands and physical pilot for station walking,
boarding, release, atmospheric entry/cruise, certified landing, on-foot departure
and return, reboarding, thrust-driven liftoff, ascent and real home capture.
Canonical checkpoint saves record the actual frame; local pilot calculations
and prospective landing queries use the qualified relative state. A prospective
query never replaces the live craft or supplies a fabricated travelled history.

At each phase the contract checks complete uninterrupted/Continue saves and an
independent next tick. Native playback compares full Save bytes and checksums,
plus canonical pose/spin against the C++ Continue oracle. It does not require a
separate Godot JSON decimal parse to reproduce binary64 bits. View refresh and
rendering retain the authoritative save. Qualification requires both compilers'
complete route/native receipts and unchanged ordinary regression outputs within
the existing process deadline; a partial route or a matching screenshot is
insufficient. Hardware/controller and GPU appearance acceptance remain separate.
