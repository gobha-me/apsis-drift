# Native rigid-body state contract

Issue #191, 2026-09-19. This adds a standalone C++ state and version-one native
projection. It does not replace legacy save format 16, migrate careers, install
new physics in the thrust lab, or implement a gameplay save menu. No force,
torque, damping, atmosphere, contact, control mapping or coordinate handoff is
performed here.

## Authoritative contents

`RigidBodyState` contains only the immutable craft recipe from #190, an owning
coordinate-frame identity, the authoritative 120 Hz tick, position, orientation,
linear velocity and angular velocity. Position and linear velocity are relative
to the owning frame in metres and metres/second. Angular velocity is relative
to that frame, expressed along the craft's body axes in radians/second.

The quaternion is **WXYZ**, using Hamilton multiplication and the active
right-handed rotation `q * (0, body_vector) * conjugate(q)` from body axes into
the owning frame. Body axes retain the craft contract: +X right, +Y up, +Z back;
the nose is -Z. There are no renderer axes, camera pose, interpolated state,
sub-tick clock accumulator, input commands, equipment, fuel, damage or world
generator state in this physical-state projection.

Identity and numerical validation receive a `RigidBodyWorldContext` containing
the existing authoritative local-system descriptor and, for the currently
supported origin station, its descriptor. This is not a second catalog. The
existing system validator checks the supplied system before owner IDs resolve.

| Frame | Required ownership | Coordinate meaning |
| --- | --- | --- |
| `system_inertial` | Existing system; planet/station absent | Existing system barycentric, right-handed inertial axes |
| `planet_fixed` | Existing system and one planet belonging to it; station absent | Existing planet-centered, rotating +Z north, +X zero longitude, +Y east axes |
| `station_relative_inertial` | Existing origin system and its named origin station; planet absent | Origin translated with station; axes remain system-inertial aligned, not station-body axes |
| `planet_relative_inertial` | Explicit physical catalog and a named planet belonging to it; station absent | Origin translated with planet; axes remain system-inertial aligned, nonrotating |

Station resolution also compares the supplied station with the canonical
descriptor generated from its universe seed, checks its system seed and origin
system kind, and confirms its host planet belongs to that system. Wrong-parent
IDs, unsupported frame kinds and contradictory owner fields reject. Numeric ID
zero is not globally forbidden: existing generator identities, not a fabricated
nonzero rule, determine membership. Unknown craft IDs/versions reject through
the #190 registry.

There is deliberately no implicit local tangent frame with an unsaved anchor.
The current experimental thrust lab uses a **planet-centered nonrotating** test
frame; it must not simply be relabeled `planet_fixed`. #200 owns the necessary
versioned coordinate transforms. This contract supplies no lab adapter and no
invented station-frame orientation.

## Canonical orientation and exact resume

Accepted stored orientations have finite components and
`abs((((w*w + x*x) + y*y) + z*z) - 1) <= 1e-12`. This is a representation
tolerance, not permission to renormalize during loading. The first nonzero
component in WXYZ order is positive. Every zero-valued double in the complete
state must be positive zero.

`canonicalize_rigid_body_state` changes quaternion sign and signed zeros only,
then validates the candidate. It never changes quaternion magnitude. Equivalent
accepted representations **q and -q**, including their signed-zero aliases,
therefore produce the same state/checksum/projection. Approximately scaled
floating quaternions are not promised to describe an exactly identical state;
floating-point rounding makes that stronger promise unsound.

Providers use this canonicalization boundary on initial state construction and
before each accepted authoritative state replacement. The operation is
idempotent on a canonical state; rendering and save/load never invoke it.

`normalize_rigid_orientation` is a separate explicit provider-side operation.
It accepts finite input with squared norm in [0.5, 2], divides each component
once by the square root in the documented evaluation order, canonicalizes the
sign/zeros and validates the result. This bounded envelope permits ordinary
integration drift without attempting to rescue arbitrary, zero, subnormal-norm
or huge input. Future integration must specify when it invokes this helper;
no integrator or normalization cadence is added by this issue.

GCC/Clang compile this implementation with source-local `-ffp-contract=off` so
FMA-capable targets cannot fuse its documented products and sums. This does
not change legacy simulation arithmetic or resolve the separate host-math
compatibility work in #255.

**Hydration never calls normalization or canonicalization.** It either accepts
the exact canonical bits from the projection or refuses the whole candidate.
For example a normalized quaternion derived from `(1,3,1,7)` can have squared
norm `0.9999999999999999`; renormalizing it again changes its component bits.
Repeated save/load must preserve that accepted state unchanged.

## Numerical and transactional boundaries

All state values are IEEE-754 binary64. The representation bounds apply to
**each component**, not the magnitude of its three-dimensional vector:

- Position: absolute component at most 10^15 metres.
- Linear velocity: absolute component at most 10^9 metres/second.
- Angular velocity: absolute component at most 100 radians/second.
- Orientation: finite, bounded components and the unit-norm tolerance above.
- Tick: 0 through `UINT64_MAX - 1`; `UINT64_MAX` is reserved as a refused,
  non-advanceable endpoint. No tick arithmetic or integration occurs here.

These are finite representation limits, not safe operating ratings, speed
clamps or angular-rate commands. Future damage may produce motion beyond
available actuator authority. Per-system coordinate ownership keeps these
bounds from imposing a finite size on the procedural universe.

Construction and decoding return a value in `std::expected`, never mutate the
caller's live state, and resolve the complete state before it can be committed.
Future save/session code must preserve that candidate-then-commit boundary.
No disk I/O or weakening of the existing atomic legacy save writer is added.

## Native JSON projection

The format is `apsis-drift-rigid-body`, version 1. It is separate from legacy
`SaveDocument` and its format number 16. Encoding has a fixed field order:

```json
{
  "format": "apsis-drift-rigid-body",
  "version": 1,
  "craft": {"id": "1", "version": 1},
  "frame": {"kind": "system_inertial", "system": "42", "planet": null, "station": null},
  "tick": "0",
  "position_metres": ["0", "0", "0"],
  "orientation_wxyz": ["1", "0", "0", "0"],
  "linear_velocity_metres_per_second": ["0", "0", "0"],
  "angular_velocity_radians_per_second": ["0", "0", "0"]
}
```

The example assumes the authoritative context is the procedural system generated
from system seed 42; it does not create that context while parsing. Real output
is compact JSON. Optional owner IDs are null or unsigned decimal strings; arrays
have exactly three/four elements. 64-bit integers are strings, avoiding a
downstream JSON consumer rounding them through binary64.

Doubles use locale-independent `to_chars(general, max_digits10)` decimal
strings. The decoder uses checked `from_chars` and requires re-encoding the
parsed double to reproduce its input spelling. Thus alternate spellings such
as `1.0` for canonical `1`, whitespace, hexadecimal, trailing junk, NaN/infinity
and overflow refuse. Unsigned IDs/ticks likewise require canonical decimal
spelling without sign or leading zeros. Negative zero reaches state validation
and is refused rather than silently changing its bits during hydration.

Input is capped at 4096 bytes before parsing, nesting at eight parser levels,
and scalar strings at 64 bytes. Raw NUL bytes are refused before parsing,
including after an otherwise valid document, so a parser end-of-input sentinel
cannot hide trailing content. Duplicate, missing and unknown keys are refused
at every defined object; unsupported formats/versions and incorrect JSON types
are refused. IDs are resolved against the supplied context only after structural
and decimal validation. No malicious or stale projection repairs a live state.

## Checksum and compatibility

The checksum uses FNV-1a with offset 14695981039346656037 and multiplier
1099511628211. Byte order is explicitly little-endian:

1. ASCII domain `apsis-rigid-body-v1`, without a terminator.
2. Craft ID u64 and descriptor version u32.
3. Frame kind u8, system ID u64, planet-present u8 and planet ID u64,
   station-present u8 and station ID u64. Absent IDs encode zero values.
4. Tick u64.
5. Thirteen binary64 bit patterns, each u64: position XYZ, quaternion WXYZ,
   linear velocity XYZ, angular velocity XYZ.

The checksum validates canonical state and ownership first. It never hashes
object padding, pointers, JSON layout or `std::hash`. Any change to the field
meaning, canonicalization, encoding or checksum recipe requires a new native
version. Existing world seeds/streams, legacy checksums and save fixtures remain
untouched.

## Explicit physical ownership (2026-09-30, #311)

Construct `RigidBodyWorldContext{physical_system, station_pointer}` to select
[physical circular catalogs](PHYSICAL_LOCAL_SYSTEM.md). The context references
the caller-owned `PhysicalLocalSystem`, its embedded catalog and the optional
station; keep those descriptors alive for every call. Passing only
`physical_system.catalog` selects the legacy path and refuses. Physical world
validation rederives the complete catalog and recipe metadata. An attached
station must be the canonical station of that physical origin universe, even
when the state is currently system-inertial. Procedural physical systems use a
null station pointer. Matching numeric IDs never select another catalog family.

All canonical values, numerical limits and hydration rules above are shared.
The **standalone physical projection is version 2**, with the same compact
field order as v1 followed by an `owner` object:

```json
"owner": {
  "family": "physical_circular",
  "version": 1,
  "generator": 1,
  "source_catalog_generator": 1,
  "ephemeris_version": 1,
  "system_seed": "677859337506523986",
  "catalog_kind": "origin_home",
  "origin_universe_seed": "42"
}
```

This example is the authored seed-42 home; procedural owners use
`"catalog_kind":"procedural"` and `"origin_universe_seed":null`. Versions are
unsigned JSON integers; seeds are canonical unsigned decimal strings. Every
owner field is required and compared against the selected authoritative
context. Unknown, missing, duplicate, malformed and mismatched owner fields
refuse. Neither decoding nor encoding constructs a world from these fields.
Physical v2 documents refuse legacy contexts, and legacy v1 documents refuse
physical contexts. **Legacy v1 output bytes, checksum and decoding rules are
unchanged.** `owner_mismatch` is appended to the existing state error enum.

The physical checksum uses the same FNV-1a and little-endian encoding, with this
prefix before the unchanged craft/frame/tick/thirteen-double payload:

1. ASCII domain `apsis-physical-rigid-body-v2`, without a terminator.
2. Family u8 (`physical_circular` = 1), owner version u32, physical generator
   u32, source catalog generator u32 and ephemeris version u32.
3. System seed u64, catalog kind u8 (`procedural` = 0, `origin_home` = 1),
   origin-universe-present u8 and origin universe seed u64 (zero if absent).

The domain and owner prefix distinguish physical meaning even where legacy
system/body IDs coincide. World validation precedes hashing, so forged metadata
cannot produce an accepted projection/checksum. No padding or pointer is hashed.

`physical-rigid-contract` checks independently calculated v2 JSON/checksum
goldens, legacy/physical separation, altered owners and attached stations,
canonical values and limits, bounded malformed documents and exact hydration.
Physical station and planet handoffs additionally use independent matrix
oracles across procedural/origin seeds, wrap ticks and the largest valid tick.

This is not a migration of Freedom save17, a flight spawn, adoption of a force
integrator or physical qualification of terrain contact. The saved native start
remains docked. A rotating handoff still requires its explicitly retained
`PhysicalPlanetRotationRecipe`; the standalone state document does not silently
infer a rotation interpretation for an old saved frame.

## Planet-relative nonrotating projection (2026-09-30, #319)

`planet_relative_inertial` is frame kind 4 and requires the explicit physical
catalog owner. Position and velocity are relative to its named planet's final
same-tick physical ephemeris; attitude maps body axes into system-aligned
nonrotating axes. Body angular velocity is relative to those axes. A legacy
catalog cannot validate or project this frame even if numeric planet IDs match.

Only this new frame uses standalone **projection version 3**. It has the same
bounded canonical field structure and physical `owner` object as v2, with
`"version":3`, `"kind":"planet_relative_inertial"` and a required planet ID.
The checksum changes its domain to `apsis-physical-rigid-body-v3`; its owner and
state field encoding otherwise follows the v2 recipe, including frame-kind byte
4 and the selected planet ID. No new numerical normalization is introduced.
Version 2 refuses the new kind; version 3 refuses relabeling any existing frame.
Existing legacy v1 and physical v2 output/checksum goldens remain unchanged.
Handoffs into system/fixed coordinates therefore encode their existing v2
projection, while the named nonrotating representation encodes v3 explicitly.

The axes do not rotate, but **the ephemeris-following origin accelerates**.
This coordinate boundary does not authorize the system-inertial force-free
vacuum provider in that frame; it continues to refuse it. #238 must explicitly
qualify its bounded central-body force approximation before advancing local
planetary state. Gravity, atmospheric composition, contact, physical launch
and gameplay saves are separate work. Freedom save17 remains docked-only.

`planet-relative-contract` covers origin and procedural catalogs at ordinary,
wrap and near-maximum ticks, independently computed v3 projection/checksum
goldens, ephemeris translation and matrix rotation oracles, local-scale
roundtrip limits, exact hydration/next-handoff continuation and refusal of
wrong family/body/recipe/frame/version/clocks, malformed input and unsafe
results. Existing state, station, rotating and coasting goldens remain gates.

## Verification and next consumers

`rigid-body-contract` is a regular headless CTest, independent of Godot. It must
cover equivalent signs/zeros, exact JSON/checksum goldens, repeated bitwise
round trips, awkward normalized quaternions, all owner kinds and wrong parents,
finite bounds, tick overflow, malformed/oversized/deep JSON and refusal without
state replacement under GCC and Clang.

The next providers may compose this contract into native persistence and
versioned integration. They must not smuggle in camera transforms, infer forces
from saved pose, or change existing coordinates while doing so. #192/#201 own
force laws, #200 coordinate handoffs, and #202/#203 contact/landed lifecycle.
