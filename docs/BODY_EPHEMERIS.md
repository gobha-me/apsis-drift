# Body identity and circular hierarchy

The application provider in `body_ephemeris.hpp` implements the active #206
identity/transform boundary. It projects the current catalogs and can evaluate
explicitly declared circular hierarchies. It does not generate moons or minor
bodies, admit another native universe, or change rendered content.

## Identity and compatible worlds

`BodyId` version 1 stores a bounded kind (`star`, `planet`, `moon`, `minor`)
and the full unsigned 64-bit identity word. Its identity is scoped to a system.
StarId and PlanetId adapters preserve their original words, including zero.
Equal words with different kinds remain different identities; absence of a
parent uses an optional value, never a zero sentinel. Converting a nonplanet
BodyId to PlanetId fails.

`make_body_hierarchy` validates and retains the complete legacy or physical
catalog owner. Its root is the actual star and its children are the actual
planets, with their existing circular recipes. Revalidation rejects altered
IDs, parents, orbit fields or system identity. Physical catalog/ephemeris
versions 1 and 2 and authored origin catalogs keep their existing meanings.
Neither the generator nor its independent random streams change.

A hierarchy without this catalog context is declared analytic geometry. It
cannot establish a generated world, terrain owner, discovery or save hydration
context. Future body-generation consumers must supply their own versioned
recipes and ownership checks before admitting populations.

## Evaluation and limits

Each nonroot has a circular orbit relative to a stable parent. Relative vectors
use the common nonrotating system axes; parent spin is not applied. At the same
120 Hz simulation tick and fractional tick, the system pose is the parent's
system pose plus the child's relative pose. Positions are metres, velocities
metres/second and angles radians, all binary64. Surface-frame conversion remains
with the existing rotation/frame providers.

The existing internal circular kernel evaluates every relative orbit. Direct
star children preserve its exact results, including signed-zero bits. Version 1
retains quantized results; version 2 retains continuous results. Parent
composition adds positions and velocities without recursive evaluation.

Results are ordered by `(identity version, kind, unsigned word)`, independently
of input declaration order. Internally, parent depth precedes identity order.
A hierarchy has 1–64 bodies and one star root, with at most eight levels
including the root. These are this bounded provider's limits, not a cap on an
entire universe. Sorting and parent lookup are bounded; validation and
resolution have at most quadratic work over those 64 nodes.

Validation rejects unsupported versions/kinds, duplicate identities, missing
parents, self-parenting/cycles, multiple or invalid roots, excessive depth/count,
missing or zero-radius/zero-period orbits, periods beyond 2^53−1 ticks and
inclinations outside ±180 degrees. Query fractions must be finite and in [0,1).
Legacy and declared pure queries support the complete unsigned tick range
without an increment; physical adapters retain their owner's reserved maximum
tick rejection. Integer orbital fields cannot contain NaN. The kernel and composed
positions/velocities must remain finite before any result is returned. No
arbitrary external transform or partially resolved state is accepted.

## Target serialization and existing saves

`BodyTarget` pairs the system and tagged identity. `validate_body_target` requires
membership in the validated hierarchy; decoding alone does not establish that
membership. Its version-1 JSON component stores system and identity as decimal
strings, retaining all 64 bits through JSON/Godot boundaries:

```json
{"identity":"18446744073709551615","kind":"moon","system":"7","version":1}
```

The decoder admits only those four fields, integer version 1, a known kind and
canonical unsigned decimal strings. It rejects duplicate/nested/extra fields,
oversized input, narrowing, signs and leading zeros. Input is bounded to 256
bytes. This component supports future generalized navigation/save consumers;
it serializes no generated catalog and is not a new complete world save format.
Existing PlanetId navigation and supported saves remain unchanged and use the
explicit planet adapter when crossing this boundary. No implicit migration or
invented moon/minor-body save hydration is introduced.

## Verification

`body-ephemeris-contract` checks actual legacy and both physical catalog versions
across fixed procedural/origin seeds and tick boundaries against their original
providers, comparing planet position, velocity and phase bits. It also checks
star→planet→moon and root→minor composition, all declaration permutations,
supported count/depth limits, invalid graphs/elements/time, full-width target
serialization and malformed JSON. GCC and Clang checks are separate; passing
them on one host does not resolve the existing cross-libm replay question #255.

The [planetary scale contract](PLANETARY_SCALE_CONTRACT.md) supplies presentation
measurement targets. This identity provider does not certify those images.
