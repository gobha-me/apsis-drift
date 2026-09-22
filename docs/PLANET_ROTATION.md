# Planet rotation and actual-star geometry

Bounded standalone provider for #213, following the 2026-09-20 decision:
planet-specific physical rotation periods share authoritative simulation time.
Whole-simulation time acceleration is deferred. This implementation does not
switch native gameplay, alter existing saves, or complete #213's presentation
integration. It supplies the missing rotation geometry for future #200 work.

## Ownership and compatibility

`generate_planet_rotation_recipe(system, planet, version)` resolves the planet
only through the validated authoritative local-system catalog. Both procedural
and authored origin-home variants work. It accepts no caller-created planet,
axis, orbital basis or star substitute. A recipe carries rotation version 1,
`SystemId`, `LocalSystemKind`, `PlanetId` and all generated integer parameters.
Resolution rederives and compares the complete recipe before arithmetic. An
origin-home recipe cannot be reused against the procedural catalog variant even
where the numeric system/planet IDs coincide.

Version 1 depends on seed derivation 1, planet descriptor generator 1, local
system generator 1, analytic ephemeris 1, and origin-home planet generator 1 for
that catalog variant. The context validator enforces their exact generated
descriptors and orbits. Changing any interpretation needs an explicit new
rotation recipe version/dependency decision, not a silent rebuild of saved
meaning. No new serialization or general `BodyId` framework is introduced.

Append-only `PlanetDescriptorStream::rotation = 8` leaves streams 1–7 unchanged.
Derive this parent with the existing seed function, then independently derive
field seeds using `SeedDomain::planet` and ordinals 1–4. Apply the fixed SplitMix64
finalizer to each child; no mutable world random stream is consumed. The exact
integer mappings are:

| Field | Mapping |
| --- | --- |
| Sidereal period | `(360 + field1 % 3961)` minutes, converted to 120 Hz ticks |
| Epoch spin phase | `field2 % period_ticks`, at simulation tick zero |
| Obliquity | `field3 % 45000001` microdegrees (0–45° inclusive) |
| Pole azimuth | Low 32 bits of field4, with 2³² units per turn |

These are bounded prograde terrestrial-world defaults, not a reconstruction of
formation history. Integer modulo mapping has negligible modulo bias; no
statistical uniformity theorem or astrophysical population model is claimed.
The pole is fixed in system space; no precession, tides or locking are simulated.

The legacy ten-minute `resolve_local_sun` recipe, the private 24-hour legacy
system-flight rotation and every existing generator/save version remain intact.
This new provider never calls either legacy spin/light recipe. The native lab
remains planet-centred **nonrotating**, not relabeled `planet_fixed`.

## Frame and arithmetic conventions

Planet-fixed axes remain right-handed: +Z spin north, +X zero longitude and +Y
90° east. `fixed_to_system` is an active Hamilton WXYZ quaternion. Let `N` and
`I` be the existing orbit's ascending node and inclination, `A` the generated
pole azimuth, `T` obliquity, and `P` spin phase:

`Q = Qz(N) * Qx(I) * Qz(A) * Qy(T) * Qz(P)`

Products are evaluated left to right. The first two factors match the existing
circular ephemeris basis. Positive spin carries the body-fixed +X direction
toward +Y about the fixed +Z pole. The meridian at zero spin phase is specified
by the complete pole basis, including its azimuth even at zero obliquity.

The period is at most 31,104,000 ticks. Compute
`cycle = ((tick % period) + epoch_phase_tick) % period` in integers first;
both summands are smaller than the period. Then `P = 2*pi*cycle/period`.
No large absolute tick is converted to double and no addition can overflow.
Ticks 0 through `UINT64_MAX-1` are supported; the rigid-state reserved maximum
tick is refused. There are no fractional-render/wall-clock inputs.

Each axis quaternion uses sine/cosine of its half-angle. The pre-spin pole
product is explicitly normalized once with the existing rigid orientation
normalizer; multiply that by spin and normalize the final orientation once,
including canonical sign/zeros. This is new geometry, not hydration
or reinterpretation of stored craft attitude. Vector rotations evaluate
`q * vector * conjugate(q) / norm_squared(q)` so the accepted floating norm
residual does not scale coordinates.

System-space angular velocity is the normalized pre-spin pole orientation applied
to `(0, 0, 2*pi*120/period)` radians/second. Its bits are constant across ticks:
phase multiplication cannot introduce roundoff wobble into the fixed pole.
Full-period quaternion and angular velocity
repeat exactly; the independent tests check positive rotational derivatives
through a phase wrap. Source-local `-ffp-contract=off` protects the specified
binary64 evaluation order without changing legacy arithmetic.

## Sidereal versus solar day

A sidereal period measures rotation relative to inertial axes. A solar day
depends on that rotation **and** the actual orbit around the star. Fixed tilt
also changes substellar latitude as the star's apparent direction moves. This
is geometric illumination, not a season/climate/weather simulation.

Existing local-system version 1 uses deliberately short orbital periods:
6–8 hours for the first planet, plus an 8-hour band per ordinal, independent of
the generated orbital radii. The star descriptor has no mass. Those legacy
orbits are preserved here; therefore this is internally coherent geometry over
the current catalog, **not** a claim of realistic astronomical years or a
guaranteed 6–72-hour apparent solar-day range. No independent accelerated light
clock is introduced. The separately selected
[physical circular catalog](PHYSICAL_LOCAL_SYSTEM.md) supplies physical orbital
periods for new native universes without changing these legacy ephemerides.

## Explicit physical-catalog adapter

The `PhysicalLocalSystem` overloads return a distinct
`PhysicalPlanetRotationRecipe` and `PhysicalPlanetRotationGeometry`. The recipe
retains a physical-family discriminator, owner-format version, physical catalog,
source catalog and ephemeris versions, optional origin-universe seed, and the
complete existing spin recipe. Resolution validates the complete physical world
context and rederives its recipe before arithmetic. Matching numeric system and
planet IDs alone do not authorize substituting a legacy or procedural-home
context.

The spin stream, period, tilt, phase and pole interpretation are unchanged. For
corresponding legacy and physical planets at the same tick, orientation and
angular velocity therefore match exactly. Planet position, velocity and star
direction instead use the explicitly selected physical ephemeris. A shared
private numeric helper preserves the legacy quaternion and illumination
operation order; the public legacy validators are not widened to accept the
embedded physical catalog.

Retain the physical geometry wrapper alongside its numeric payload. Unwrapping
the spin recipe does not make the physical catalog valid for legacy frame
handoffs, terrain contacts or persistence. This adapter alone does not enable
native startup, change the flight lab's nonrotating frame, migrate saves or
introduce a second lighting clock.

The physical adapter checks procedural and authored origin catalogs, nonzero
ticks and near-maximum ticks, independent matrix/star-vector oracles, exact spin
parity, origin/recipe/context forgeries and observer bounds. Its new corpus
checksum is `3325105109563507698` under both local compilers; historical rotation
and rigid-handoff goldens are unchanged. Both full compiler builds, 19 focused
C++ contracts and 29 isolated native contracts per compiler pass. Those native
tests establish unchanged consumers, not live adoption of physical rotation.
Focused pinned lint and independent review pass; full lint remains hosted CI.

## Illumination

The current catalog has exactly one validated star at the system origin.
At the same integer tick, obtain the planet's position and velocity from the
existing analytic ephemeris, preserving its final quantized fields exactly.
Inverse-rotate `-planet_position` to obtain `star_position_fixed`.

An optional planet-fixed observer defaults to the centre. Subtract its position
from that star position and normalize to obtain `observer_to_star_fixed`;
rotate and normalize the direction for `observer_to_star_system`. These point
**toward** the star; directional-light ray travel has the opposite sign.
Finite-distance surface/orbit observation is not silently replaced by a
planet-centre direction. A spherical substellar coordinate may be obtained from
the centre query using the existing latitude/longitude convention.

Every observer component must be finite and within ±10¹² metres, inclusive.
The calculated star distance must be finite and exceed one micrometre; a
coincident point-star query is refused. Signed-zero observer aliases are
canonicalized in the returned copy only. This point-star query does not model a
stellar disc, eclipses, refraction, terrain occlusion, exposure or visibility;
it may geometrically return a direction even from inside a star or planet.

Malformed recipes, contexts, owner aliases, ticks and observers fail without
mutating any inputs. The current catalog cannot represent a starless system or
unsupported minor-body kind: invalid stars fail context validation and unknown
planet IDs fail lookup. There is no invented default sun/parent/axis.

## Evidence and remaining integration

`planet-rotation-contract` independently constructs full-angle rotation matrices
and checks quaternion agreement, handedness, norm/sign, angular velocity,
phase-wrap derivatives, same-tick ephemerides and finite-distance star vectors.
It checks surface noon, geometric tangent and midnight, substellar coordinates,
near-maximum ticks, owner aliases, every recipe parameter and malformed observer
axes. Deterministic recipe/tick reconstruction is tested; this is not a new
save/reload format. A multi-seed corpus checks noninterference with existing
streams and catalog generation; six historical stream seed goldens remain
explicitly asserted. New recipe/geometry checksum fixtures are pinned only after GCC and
Clang agree; existing goldens are not rewritten.

The existing ephemeris and this recipe use host math-library trigonometry.
Same-host GCC/Clang agreement is not universal cross-libm replay certification;
#255 remains the owner of that limitation. Near a floating rounding boundary,
another libm can produce different exact geometry bits.

Publication checks for this increment: both complete local compiler builds,
all 18 focused C++ contracts and all 29 isolated native contracts pass. The
native checks qualify unchanged consumers, not adoption of the rotation
provider. Pinned format 20, focused clang-tidy 20 and independent review pass;
complete pinned lint remains a hosted CI gate. Existing historical replay
portability failures under #255 are not reclassified or hidden by this result.

Native shaders, world/sky transforms, walkable-surface presentation and rotating
6DOF handoffs must subsequently consume this one recipe/tick. Before persistent
native use, the world/save context must select and retain the rotation recipe
version explicitly. Do not silently attach version 1 to an old saved frame or
change a planet texture's rotation without the corresponding physical frame
and light transform. #213 remains open until those integrations and actual
day/terminator/night views are qualified.
