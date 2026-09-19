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
