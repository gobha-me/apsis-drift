# Saved terrain pad geometry experiment

`SavedContactGeometry` in `godot` snapshots an existing
`NativeFreedomFlightSession`. It retains the complete physical system, selected
planet descriptor, physical rotation recipe, original flight state and checksum.
Its temporary query state uses the existing physical-owner frame handoff into
planet-fixed coordinates at the same simulation tick. Queries do not alter the
session, flight state, clock or save document.

The explicitly experimental recipe is version 2, terrain generator 1, source
LOD 8, relief 0, mesh LOD 13, 32 intervals and anti-diagonal 1. Relief 0 retains
generated elevation; it does not flatten terrain. These primitives match the
finest native terrain **top** geometry for that recipe, excluding skirts and
shading normals. Variable renderer LOD does not define contact geometry. This
experiment does not adopt a production physical terrain recipe or change saves.
Query vertices are absolute planet-fixed coordinates; renderer vertices are
anchor-relative. Tests compare anchor reconstruction with a 0.1 micrometre
tolerance, without claiming bit-identical subtraction/addition roundtrips.

Every registered nominal landing pad is derived from its authored contact point
relative to craft COM, its full rectangular half-extents and the temporary craft
orientation. Pads are queried regardless of visual gear deployment. Each active
result slot contains its support index and either rectangle geometry or a
specific refusal. Once constructed, the selected triangle is retained even if
the rectangle subsequently crosses an edge or has a degenerate projection.
Unused slots stay empty. A snapshot owns its source cache; cache capacity and
eviction cannot change successful geometry. Each pad samples at most three
terrain vertices, with at most four active pads and twelve samples per batch.

For one triangle selected by the pad's radial centre, the shared arithmetic
computes analytic affine extrema over the **whole rectangle**. All projected
edge minima must exceed the existing 0.1 mm numerical allowance; signed plane
gap bounds include that allowance outwards. The existing finite altitude,
primitive conditioning and projection limits remain in force. This binary64
policy is not a formal directed-rounding proof or a touchdown capture band.
No adjacent-triangle search or aggregation hides a refused foot.

The factory resolves actual `PhysicalLocalSystem` membership and its physical
rotation owner directly. Numeric planet IDs alone do not identify authored
origin versus procedural descriptors. The legacy relief-1 contact APIs retain
their original recipe validation and results; their legacy catalog validation
is not used to authorize a physical catalog.

Successful rectangle geometry says nothing about material, bearing load,
nearest/first surface, slope suitability, hull or swept clearance, compression,
velocity response or landing readiness. It cannot populate
`TouchdownObservations`, set `footprint_supported`, clamp flight or select a
landed state. No native bridge or HUD consumes this experiment yet.

`godot-saved-contact-geometry-contract` tests physical provenance, an independent
nonzero-tick frame oracle, origin/procedural separation, full rectangle extrema,
retained indexed refusals, authored pad dimensions, cache independence, session
lifetime and unchanged saved document bytes. Existing contact surface and patch
contracts continue to check the legacy APIs and their golden outputs.

The application-owned [terrain touchdown provider](TERRAIN_TOUCHDOWN.md) adds
an explicit support policy, whole-segment radial-cone qualification and material
semantics before composing these primitives with the pad envelope. Geometry
alone retains the experimental refusal and non-support contract above.
