# Starter shuttle frame contract

Issue #190, 2026-09-19. This is an immutable C++ physical recipe and its
standalone persistence boundary, not completed landing, docking, fuel, damage
or a new saved flight model. The current thrust lab and legacy saves remain
unchanged. The cockpit camera consistency fix in #256 is independent.

## Identity, ownership and coordinates

`CraftFrameId{1}`, descriptor version 1, diagnostic name `freedom-shuttle-v1`.
`resolve_craft_frame` accepts only that registered pair. A physically valid
candidate is not automatically a registered frame; changing a known frame's
properties or name is refused by diagnostic/checksum projection. The returned
descriptor's members are const. No RNG is consumed and no world seed, vehicle
pose, installed upgrade, consumable, mutable condition or renderer resource is
stored here.

Body coordinates are right-handed **+X right, +Y up, +Z back**; the nose/main
thrust direction is -Z. Blender authoring maps `(x,y,z)` to body `(x,z,-y)`.
Principal inertia is diagonal in these body axes. The asset datum is provisionally
the center of mass, an authored simulation approximation rather than a measured
mass distribution. A later loaded-mass/COM model must be explicit and versioned.

The shuttle is rated for up to four occupants with one pilot station. This does
not add crew, multiplayer or additional modeled seats. The compact shuttle has
no sleeping quarters or galley; capacity does not imply long-haul habitation.

## Authored version-one properties

All stored quantities are fixed-width integers with units in their field names.
No NaN, infinity or fractional integer encoding can enter this API. A future
floating-point/JSON importer must validate before conversion; it is not provided
by this contract. The diagnostic JSON is output-only.

| Property | Value / meaning |
| --- | --- |
| Dry mass | 8,000 kg, preserving the lab's unloaded reference mass |
| Stowed hull bounds | min `(-7000,-600,-10000)`, max `(7000,3335,9320)` mm |
| Principal inertia X/Y/Z | 260,000 / 380,000 / 140,000 kg m²; authored approximations |
| Positive X/Y/Z forces | 112,000 / 208,000 / 72,000 N |
| Negative X/Y/Z forces | 112,000 / 144,000 / 360,000 N |
| Torque bounds X/Y/Z | 728,000 / 1,064,000 / 560,000 N m |
| Angular-rate bounds X/Y/Z | 1,150 / 900 / 1,700 milliradians/s |
| Effective drag Cd*A X/Y/Z | 54 / 80 / 3.84 m², stored as square millimetres |
| Surface gravity / pressure rating | 18 m/s² / 2,500 millibars |
| Touchdown limits | 2 m/s vertical, 3 m/s horizontal, 0.1 rad/s angular, 12° slope |
| Landing supports | Three pads, half-width 375 mm, half-length 550 mm, stroke 300 mm, rated load 160,000 N each |

The force/rate/drag values preserve the reviewed lab tuning; inertia, landing
ratings and load/stroke values are deliberately provisional authored recipe
values for subsequent contact tests, not a claim of real engineering certification
or balanced gameplay. They are not silently installed into live flight. A changed
registered recipe requires a new descriptor version rather than changing old
saves on load.

The stowed bounds conservatively round outward the measured source in
`assets/visual/hero-ship-geometry.json`. They are an enclosing proxy, not a detailed
collision shape. Nominal deployed contact points are `(0,-1328,-6700)`,
`(-3500,-1328,2700)`, `(3500,-1328,2700)` mm, derived from the archived gear
construction in `tools/build_hero_assets.py`. The archived shoe bottoms are
rounded downward from -1.3275 m. These support fixtures do not deploy the gear:
all normal flight exports keep it stowed. Cabin/eye anchors remain owned by the
shared cockpit layout, not copied into this simulation descriptor.

Vacuum, atmosphere, terrain contact, landed, liftoff, ascent, orbital and docking
are independent operation bits. These are **rated design capabilities**, not
indicators that each runtime transition already exists. Contact/landed/liftoff
have explicit dependencies; ascent requires vacuum capability, not landability;
orbital/docking require vacuum. Atmosphere is not required for a space-only
craft. Generic validation accepts a non-landable space-only fixture without
registering a second craft or building a catalog.

## Validation and numerical limits

- Capacity: 1–64 occupants; at least one pilot and no more pilots than occupants.
- Mass: 1–1,000,000 kg; each principal inertia 1–10¹² kg m² with triangle
  inequalities checked after bounds, avoiding overflow. Moments must also fit
  within mass times the squared maximum COM distances allowed by the hull.
- Coordinates: each component within ±1,000,000 mm; positive hull extents and
  center of mass strictly inside the hull.
- Each directional force/torque: 1–10⁹ in its units; each angular rate
  1–10,000 milliradians/s. Drag area is bounded by 10⁹ mm² and positive for
  atmosphere-capable frames.
- Environment ratings: gravity and pressure each at most 100,000 in their
  declared units. Atmosphere support requires a positive pressure rating.
- Contact: 3–4 ordered convex, non-collinear supports whose X/Z polygon strictly
  encloses the center-of-mass projection. Contacts lie below the stowed hull.
  Unused array slots are zero. Pad half-extents and stroke are 1–10,000 mm;
  individual load ratings are 1–10⁹ N. Stroke means vertical compression and
  full compression must still leave positive hull clearance. Total static load supports rated gravity;
  liftoff requires upward thrust greater than rated dry weight.
- Contact-rated frames need positive bounded touchdown limits: vertical at most
  20,000 mm/s, horizontal 50,000 mm/s, angular 1,000 milliradians/s, slope
  45,000 millidegrees. Non-contact frames carry no supports or touchdown limits.

These are bounded version-one representation/validation limits, not a universal
ban on future frame designs. Static support checks are not terrain assessment,
impact absorption, gear animation or per-pad dynamic load simulation.

## Recipe projection and compatibility

The standalone 16-byte native recipe is little-endian:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 4 bytes | Recipe format, 1 |
| 4 | 8 bytes | Stable frame ID, 1 |
| 12 | 4 bytes | Descriptor version, 1 |

Encoding and decoding require exact length and a supported registered identity.
An encoding refusal leaves the entire caller-provided buffer untouched. Decoding
returns a value; the caller can validate the rest of a future save before replacing
live state. Short, oversized, unknown-format/ID/version projections reject.

The checksum is FNV-1a over the `apsis-craft-frame-v1` domain bytes, identity,
canonical name plus NUL, and every field in declaration order with explicit
little-endian widths. Signed coordinates are encoded as their uint32 values;
all four support slots participate. Never hash C++ object padding or `std::hash`.
Fixed diagnostics use ordered JSON and string representations of 64-bit ID and
checksum to avoid downstream floating-point rounding.

This does **not** add a field to legacy save format 16, bump world generators,
rewrite existing goldens or load the unsaved thrust lab as a supported career.
#191 owns composition into native physical-state persistence. Loading unknown
frame versions must remain a refusal, not a silent fallback to this shuttle.

## Verification and next consumers

`craft-frame-contract` runs in ordinary CTest, independently of Godot. It covers
boundary/overflow rejection before geometry checks, exact binary bytes and
canaries, immutable identity, checksum/diagnostic goldens, future space-only
representation and unchanged world generation around frame resolution.

Build/test this contract under both GCC and Clang. The source/asset comparison
above explains the physical recipe; it does not qualify landing or change live
visuals. #191 adds canonical physical state; #192/#201 consume forces; #202/#203
own contact and landed transitions; #253 owns impact/local condition. Fuel units
and burn remain #133/#251. No economy, generic builder or fleet framework is
introduced here.
