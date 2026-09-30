# Wayfarer physical frame

2026-09-30, #336. **Frame ID 2, descriptor version 1**, `wayfarer-v1`, binds the
selected Wayfarer geometry to an explicit application-owned physical recipe.
Frame ID 1/version 1 remains `freedom-shuttle-v1` with its original properties,
wire bytes, checksum and default selections. This provider does not change
New Game, migrate saves, implement landing or add a craft-selection screen.

The native [starter package](NATIVE_STARTER_ASSETS.md) supplies the model and
archived measurement descriptor. Their exact source bindings are:

| Record | SHA-256 |
| --- | --- |
| Wayfarer model | `12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8` |
| Presentation descriptor | `17c2bc23d4f43602f85a7951dd7c8a3aaceed691e1a1b8a2ef823df703c446b7` |
| Export measurement receipt | `212d1ae46b1c698636fb3b1e8cf5538520ace0e979e09511e377252ba1067c76` |
| Authoring master | `87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677` |

The stowed hull is rounded outward to millimetres from those measured bounds:
minimum **(-4040,-1259,-5210)** and maximum **(4040,2955,7620)** mm in the
existing right/up/back body axes. Source Blender `(x,y,z)` maps to `(x,z,-y)`.
This replaces the earlier larger envelope only when ID 2 is explicitly selected.

The descriptor's sample-zero transforms are the fully deployed authored gear.
Applying them to the admitted export identifies these horizontal pad datums:

| Foot mesh | Contact centre, body metres |
| --- | --- |
| `HopperGear05` | `(0,-2.080,-0.700)` |
| `HopperGear16` | `(-2.500,-2.080,5.070)` |
| `HopperGear27` | `(2.500,-2.080,5.070)` |

Each pad has a measured **0.64×0.52 m** footprint. Compression stroke **0.30 m**
and rated load **160,000 N** per support remain explicit gameplay ratings from
the existing starter. Fully compressed supports remain below the stowed hull.
Their ordered triangle contains the nominal centre-of-mass projection.

Mass, principal inertia, directional thrust/torque, angular-rate limits,
effective Cd*A, environment limits and operating/landing ratings carry the
existing starter's authored values. The mesh and modeled gear do not prove
these ratings or engineering durability. Fuel, jump capacity, cargo, equipment
and progression are separate owners. No authoring geometry is modified.

The aligned stowed hull fits both immutable D1/D2 withdrawal reservations at
0, 0.15, 6 and 12 m separation. This proves hull/reservation agreement, not a
swept collision or capture authorization. #220 still owns those transitions.

`wayfarer-frame-contract` checks refused IDs/versions, edited immutable fields,
recipe buffer canaries, existing ratings, both-port fit, canonical physical
state and explicit flight18 roundtrips. The immutable descriptor checksum is
`15216801238891810296`. The isolated Godot `wayfarer_frame` check independently imports
the exact model and descriptor, compares the C++ envelope with the full stowed
mesh, then measures the three real deployed pads against C++ contact/patch
properties. The existing frame and flight goldens remain independent gates.
