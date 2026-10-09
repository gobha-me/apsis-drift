# Freedom jump targeting

Policy 1, recorded 2026-10-09 for the in-range portion of #193. This is the
numerical provider for native travel #176, using the existing nearby pair
from [the starting chart](FREEDOM_TOPOLOGY.md). It does not yet enable jumps
in Godot. Existing jump-version-4 contracts, missions and saved games retain
their previous behavior.

## Actual geometry

The request names the universe seed, seed-derived starter craft, actual
canonical Wayfarer rigid state, selected destination, Assisted/Pilot profile
and nonzero attempt ordinal. The source is the named planet-relative inertial
frame in physical catalog/ephemeris 2. Its local position must fit the existing
100-billion-metre coordinate domain. System barycentric body positions are
not constrained to that local domain.

Resolve the source body's position at the commitment tick and add the actual
local ship position. Resolve the destination at commitment plus 240 ticks
(the retained two-second transit convention). Outbound nominal arrival is
ten radii along system +Z from the generated neighbor's first planet. Return
nominal arrival is 40 km radially outward from the actual origin station.
These are technical arrival references, not surface objectives or discoveries.
Neither requires a mission or earned drive upgrade.

Subtract actual system-local endpoint positions and then add the integer
system-anchor difference. The resulting distance and direction drive reach
and heading assessment. Anchor separation alone is insufficient. Invalid IDs,
owners, frames, noncanonical/nonfinite state and overflowing clocks refuse
before resolving a point. No coordinate is silently clamped.

Arrival velocity is the source's named-body-relative velocity vector plus the
destination reference's ephemeris velocity. The drive adopts that reference
velocity as part of the jump fiction; this is not an inertial momentum
conservation model. Native handoff must subtract the destination owning
planet's velocity to install its local rigid state, retain actual orientation
and angular velocity, and validate the complete result.

## Rated reach and uncertainty

The provisional starter rating is 360,000 light-seconds / 100 light-hours.
It gives the 48–96-light-hour technical pair room for actual local offsets.
This is a versioned test policy, not a balanced fleet specification or a
universal hard limit. Beyond-rated distance is explicitly **unqualified**:
preview reports that status without an invented radius, and policy 1 cannot
freeze a point there. #248 must select the extended-risk policy before such
travel can be committed. No wrong-system outcome or hidden extra charge is
introduced here.

Heading is the angle between the actual quaternion-rotated forward axis (-Z)
and the endpoint direction, rounded to integer millidegrees. Drift is the
magnitude of actual source-body-relative linear velocity divided by the
explicit provisional 60 km/s reference, capped at one and rounded to basis
points. This is speed relative to that body, not velocity-direction alignment
or the legacy keyboard trim. Rounding boundaries are part of the policy.

Pilot uses the existing thresholds: aligned at heading ≤3° and drift ≤2%;
offset at heading ≤45° and drift ≤20%; opposed otherwise. Assisted records
the real assessment but compensates its envelope grade to aligned. It does
not erase speed, rotate the ship, or modify its resources.

For positive distance `d` within the rating `R`:

```
fraction = ceil(d / R * 1,000,000)
base_radius_metres = 1,000 + ceil(fraction / 1,000)
radius = base_radius_metres * grade_multiplier
```

Multipliers are 1, 10 and 100 for aligned, offset and opposed. The base radius
is 1,001–2,000 metres and never decreases as distance increases. These authored
numbers make geometric risk observable; they do not claim engineering realism
or final play balance. No fuel, health, maintenance or cooldown curve is hidden
inside the envelope.

## Frozen resolution and hazards

Preview resolves the complete arrival sphere against the actual star and all
generated planet spheres at the arrival tick, including tangency. It returns
the intersecting identities rather than moving the target to a safer location.
The sampled point receives a separate zero-radius assessment. Surface terrain,
stations, atmosphere, gravity, minor bodies and condition consequences are
outside this sphere test; `clear` means clear of these tested stellar/planetary
spheres, not a certificate of safe operation.

Point resolution uses independent seed domain `jump_arrival=14`; existing
domains retain their IDs. A fixed little-endian FNV-1a fold binds policy,
attempt, craft/frame owners, endpoints, commitment tick, profile and actual
pose/velocity/angular velocity. Derive the seed from that fold and the universe
seed. Three 16-bit seed components, centered at 32,768, select a point within
an inscribed cube scaled by `radius / (1.75 * 32,768)`. This bounded distribution
is deliberately simple; it is not uniform over the sphere. No rejection loop,
mutable RNG cursor, safety reroll or wall clock participates.

The frozen value retains the complete request, preview, seed, point and hazard
assessment. Validation regenerates that entire value and rejects any mismatch.
This proves reproducible binding in the provider; actual persisted native
spool/commit/arrival phases remain #176/#194. Callers must freeze once at
commitment, keep that value immutable and never permit retries of a committed
attempt. The resource owner bills exactly one charge at that same commitment.

This API returns internal truth. Presentation must use the knowledge ledger
to redact unresolved body identities/positions and must not reveal the hidden
sample before commitment. Selection and preview cannot award observations.
Intersecting or unqualified results require the explicit #248/#253 consequence
and #247 recovery contract before hazardous commitment is enabled.

## Evidence and remaining work

The headless contract covers both physical route directions, actual endpoint
offsets, heading/drift grades, monotonic reach envelopes, exact stellar
tangency, generated planet intersections, deterministic repeat resolution,
tampered frozen values and invalid state boundaries. Run
`apsis-drift-freedom-jump-targeting-tests`, or CTest
`freedom-jump-targeting-contract`, under GCC and Clang. Floating-point
contraction is disabled for this provider even in builds without tests.

#193 remains open for native commitment and presentation acceptance. #176 owns
active system/craft lineage, UI and saved travel phases; #251 owns the charge;
#189 owns arrival observations; #194 owns the composed outward/return/station
refill proof. #248 owns environmental/condition and beyond-rated risk. The
existing starter resources and old saves do not change when this provider is
compiled.
