# Pure touchdown-envelope groundwork

2026-09-19. Prerequisite #265 supplies the craft-envelope portion of #202.
It is not generated-terrain collision, a landed state, or permission to remove
the native experiment's 16 m floor guard.

## Why this is separate

The existing `TerrainSurfaceSampler` is explicitly presentation-only and takes
a caller-selected LOD. Its integer bilinear interpolation produces whole-metre
base elevations. The starter shuttle's pads are 0.75 m wide and their suspension
stroke is 0.30 m. Treating that presentation sample as a sub-metre support
measurement would manufacture steps and misleading touchdown judgments.

Native relief version 1 is also explicitly experimental, not a production
physical terrain recipe. Its height adjustment does not establish an independent
water/material contract. Neither framebuffer colors nor a rendered normal can
decide whether a pad rests on solid ground.

The evaluator therefore consumes explicit, validated landing-pad observations.
Future #208/#211 work must supply the versioned physical height, normal and
material truth at a declared support-footprint resolution. #202 must then
compose and qualify those observations against actual generated terrain.

## Ownership and limits

The application owns the assessment. It uses the immutable starter craft's
support geometry and landing ratings, along with same-tick planet-fixed pose
and motion. A small fixed-size set of observations reports the terrain beneath
each support. No renderer, random stream, cache residency, camera, or wall clock
is an input.

The result describes **pad contact only**. Clear pads do not establish a clear
hull: a ridge could strike the belly between them. A sampled footprint likewise
does not prove continuous swept collision or rule out unobserved obstructions.
Do not turn this result directly into a general collision certificate.

Gear deployment is an explicit input, not inferred from a static mesh or the
presence of supports in the craft recipe. The current flight asset has stowed
gear; this evaluator does not deploy or animate it.

The assessment is pure: it does not move the craft, cancel velocity, consume
fuel, mutate terrain, create landed state, award progress, or apply damage.
Named unmet margins explain why contact is unsuitable. Malformed observations
are errors, distinct from a valid observation of dangerous contact.

## Version 1 inputs and threshold policy

`assess_touchdown_envelope` requires canonical `RigidBodyState` in the named
`planet_fixed` frame. Observations name the same planet and simulation tick;
each active support appears exactly once in recipe order. The fixed four-slot
buffer must have the recipe's support count, with unused slots zero-valued.
Unknown material values and malformed observations are rejected, not converted
into apparently clear ground.

Each observation supplies minimum/maximum signed gaps over the **whole** pad
footprint, a representative outward unit normal, material (`solid`, `water`,
or `unsupported`), and a support-adequacy flag. Positive gap means separation;
negative gap means the nominal uncompressed pad lies inside the support plane.
The adequacy flag must include geometric/bearing suitability. Gap extrema and
a representative normal alone do not prove that one rigid foot can bear on an
arbitrarily rough patch.

Version 1 uses radial up at the craft centre. Surface normals must be outward
and unit within the declared validation tolerance; accepted normals are
normalized once. Pad-centre ground-relative velocity is computed as
`v + R * (omega × offset)`, where the offset comes from the immutable support
location relative to craft centre of mass. Positive normal velocity separates
from the ground. Tangential speed is the remaining component, not airspeed.

| Margin | Starter rule; equality passes unless stated otherwise |
| --- | --- |
| Gear | Must be explicitly deployed |
| Upright | Body up must have strictly positive dot products with radial up and each pad normal |
| Material/support | Solid and explicitly adequate over the footprint |
| Separation | Every pad's maximum gap must be at most zero; no implicit proximity capture band |
| Compression | Minimum gap must fit 0.30 m stroke projected from body up onto the pad normal |
| Slope | At most 12 degrees from radial up |
| Separating motion | Positive pad-normal velocity is not a capture opportunity |
| Descent | At most 2 m/s inward at each pad centre |
| Sideways motion | At most 3 m/s tangential at each pad centre |
| Rotation | At most 0.10 rad/s body angular-speed magnitude |
| Environment | Nominal generated surface gravity at most 18 m/s² and pressure at most 2,500 mbar |
| Support load | Each pad must conservatively support the entire dry craft weight within its 160,000 N rating |

The environment test uses nominal descriptor values, not a second atmospheric
or altitude-dependent gravity model. Full-weight-per-pad qualification is a
conservative rating check, not an equal-load assumption or a solved suspension
load distribution. Cargo, fuel mass and future craft assemblies need their own
explicit extension before using this dry-frame rule.

The registered 12-degree slope boundary uses a fixed binary64 cosine rather
than a runtime inverse-trigonometric comparison. A future registered frame with
a different slope rating requires an explicit numerical-policy extension.
All unmet margins are evaluated even when pads are clear. `pads_clear` means
only that no observed pad has reached terrain; `pad_contact_ready` requires all
margins; any observed pad contact with an unmet margin is `pad_contact_unsafe`.

## Verification

The dedicated contract tests cover malformed/nonfinite state and observations,
wrong frame/body/tick, active and unused buffer slots, support order, inward or
degenerate normals, unsupported materials, inverted/sideways craft, and
bitwise input immutability. Inclusive thresholds are checked at the boundary
and its adjacent representable values. A regression at both the original
planet-radius-plus-gear position and an exact power-of-two radius protects the
12-degree slope boundary against reciprocal-normalization rounding.

Point-velocity fixtures cover a safe centre descent with an unsafe nose pad,
yaw-induced sideways overspeed, and rotating the complete reference frame.
Combined failures must retain every named margin rather than stopping at the
first failure. Clear-pad results still report unmet readiness margins.

The following explicit assessment hashes match GCC and Clang. These are new
fixture goldens, not replacements for legacy references. The stationary-level
hash was also computed independently from its analytic output fields.

| Fixture | Assessment hash |
| --- | --- |
| Level supported pads | `12330518051017978207` |
| Sloped support | `43715311420859247` |
| Rotated reference | `3324266018647820293` |
| Combined failed margins | `31461596898727366` |

Generated-population fixtures exercise the reachable gravity/pressure extrema;
forged descriptor changes are rejected by existing world validation. Current
generated gravity tops out below the shuttle's gravity and full-weight support
ratings, so no test claims to reach those failure flags through a valid current
world. Future frame/environment extensions need their own reachable fixtures.
These cross-compiler checks do not resolve the separate historical host-math
compatibility investigation in #255.

Local full GCC/Clang builds pass, and all eleven focused craft/state/vacuum,
touchdown and native C++ contracts pass on each compiler. Pinned clang-format
20 and suppression-policy checks pass. Hosted static analysis/full-matrix CI
remains a separate publication gate; no new GPU check is claimed for this
non-rendering provider.

## Integration gates that remain open

- #208/#211: physical surface scale, versioned fine height/normal/material
  sampling, seam behavior, and old-recipe compatibility.
- #202: actual generated footprint and hull contact, provider identity and
  sampling adequacy, including safe behavior between discrete samples.
- #203: touchdown commitment, supported landed state, gear/liftoff/ascent.
- #253: unsafe-impact consequences and craft-local damage; #247: recovery.
- #200/#201: canonical frame handoffs and atmospheric composition where used.

This standalone provider does not change supported saves or the current
playable flight model. Analytic pad fixtures prove the envelope rules, not
terrain fidelity or a landing-capable build.

## Next provider: experimental point geometry, not footprint support

The standalone [experimental finest-triangle query](EXPERIMENTAL_CONTACT_SURFACE.md)
now implements bounded triangle extraction and radial point intersection for
the native reference recipe below. It is not wired into the pad evaluator or
live flight and does not establish whole-footprint or hull contact. The
remaining coverage, fluid, frame and presentation requirements still apply.

Study 29 adds a [one-triangle normal-projection certificate](EXPERIMENTAL_CONTACT_PATCH.md)
for the complete registered pad rectangle. It provides conservative plane-gap
bounds while refusing edge/crossing ambiguity. It is not an observation adapter:
first-hit obstruction, material/bearing support and multi-triangle coverage are
still unproved, so it cannot set `footprint_supported` or enable landing.

An independent audit identified a narrow route to contact that need not invent
different mountains: explicitly version the current finest surface
triangulation and query those triangles in application-owned double precision.
The current native reference uses base generator 1, source LOD 8 and relief 1;
the finest stream tiles use LOD 13 with 32 intervals and a fixed diagonal.
A physical recipe would need to freeze and validate all those choices rather
than silently accepting arbitrary presentation settings.

Ray/triangle intersection on that fixed surface could provide continuous
sub-metre contact positions. It would **not** create sub-metre geological
features. Full pad coverage would still require every intersecting triangle,
deterministic seam/diagonal ownership, geometric rather than shading normals,
and conservative refusal when a bounded query cannot establish adequacy.

The current renderer can show coarser meshes, skirts and shader-only grit.
Before enabling consequences it would need a qualified matching near-contact
patch; using whichever rendered tile happens to be resident is not acceptable.
Ocean/fluid support also needs explicit physical semantics. This proposal is
not adopted as a production terrain/save recipe by the pad evaluator and does
not close the broader local-detail work in #211.
