# Experimental single-triangle pad geometry

This opt-in C++ prerequisite builds on the finest-triangle point query. It
proves a deliberately narrower geometric fact than terrain support: the normal
projection of the complete nominal rectangular landing pad lies inside one
declared binary64 triangle. It also encloses the pad's signed distances from
that triangle's plane. Nothing calls it from live flight.

## Inputs and ownership

`certify_contact_patch` accepts a canonical `planet_fixed` rigid-body state,
authentic world context, registered support index, experimental surface recipe
and bounded source cache. The immutable craft defines the support location
relative to COM and its body-X/body-Z half extents; the current starter pad is
0.75 by 1.10 metres. Quaternion rotation divides by its squared norm and never
renormalizes or modifies the authoritative state.

The nominal uncompressed pad is queried regardless of whether gear is deployed.
This helper does not infer or change gear state. Invalid identity, unsupported
frame/recipe, noncanonical/nonfinite state, invalid support and excessive datum
altitude are rejected before touching the triangle cache.

The underlying point query currently accepts only unchanged procedural planet
descriptors. The valid authored tutorial-safe origin-home variant is therefore
refused as `surface_query_failed`, not substituted with a different planet.
The separate [owner-qualified context extension](EXPERIMENTAL_CONTACT_CONTEXT.md)
now supports that exact variant through `certify_owned_contact_patch`. The
original `certify_contact_patch` retains its refusal and identity contract.

### Next provider boundary: authoritative catalog ownership

The authored home descriptor and its unmodified procedural base share a
planet seed/ID, but not necessarily radius or terrain. A context-aware extension
must therefore qualify results by their validated system/catalog variant and
supported generation versions. Resolve the exact descriptor through
`find_local_system_planet`; neither regeneration from the planet seed nor a
"tutorial-safe" range check establishes that identity. Keep the existing
standalone API and its origin-variant refusal unchanged.

Scope caches to one validated catalog variant and retain the terrain cache's
same-key/different-descriptor rejection. Before integration, test both variants,
wrong owners/versions, forged catalogs, conflicting cache contents and matching
procedural geometry across GCC and Clang. This is a proposed next boundary,
not an implemented extension or a new save identity.

The native lab currently owns a standalone descriptor reconstructed from its
snapshot planet seed, not an authoritative local-system context. Live use needs
an explicit C++ session/bootstrap and frame adapter first. The displayed stars
must not be used to invent that missing ownership; snapshot-v1 behavior and the
floor guard remain intact in this study.

## Why this covers the rectangle, not just sample points

The center's radial direction selects one candidate triangle from the existing
experimental surface. For each triangle edge, construct its inward unit vector
`h` in the triangle plane. With pad center `c`, edge origin `v`, and rotated
half-extent vectors `a` and `b`, the minimum signed edge distance over every
point of the rectangle is

`h·(c-v) - |h·a| - |h·b|`.

All three minima must be strictly positive beyond the numerical inset. This
is an affine half-plane/convexity argument over the whole rectangle—not an
assumption that successful terrain samples at four corners prove the interior.
Opposite-vertex orientation handles the native face-dependent winding.

Likewise, for outward triangle normal `n`, the whole rectangle's plane gaps
are bounded by `n·(c-v) ± (|n·a| + |n·b|)`. Returned minimum and maximum are
expanded outward by the numerical allowance. A tilted pad therefore has a
nonzero span even when its center lies exactly on the plane.

## Conservative numerical policy

- Pad-center altitude relative to the spherical datum is within ±100 km.
- Local center-to-triangle-origin distance is at most 200 km.
- Each triangle edge is between 1 m and 10 km; the sine between the two
  independent edges is at least 0.001. Poorly conditioned geometry refuses.
- A projection with `|body_up·normal| <= 1e-6` refuses as collapsed/ambiguous.
- Every raw minimum edge distance must be **greater than 0.0001 m**.
- Plane-gap bounds are expanded by 0.0001 m in each direction.

The 0.1 mm allowance is a conservative experimental binary64 budget under
these local-distance/conditioning gates, not a formal interval-arithmetic
proof. Subtractions occur in local coordinates before dot products. The target
is the triangle through its declared binary64 vertices, not an unknown ideal
continuous geological surface. Conditioning gates limit normal error amplified
by separation. This allowance is not a landing capture band, suspension stroke,
or evidence of geological accuracy.

Crossings of diagonals, cells, tiles or cube faces receive no neighboring search:
if the one candidate cannot certify the whole pad, the helper returns
`boundary_not_certified`. Near-edge uncertainty also refuses; it never snaps a
pad inward. This is absence of a certificate, not a verdict that the terrain is
unsafe. More capable bounded multi-triangle coverage can extend this later.

## Explicit nonclaims

The certificate does **not** establish the nearest/first surface along the
normal projection rays; a different facet could obstruct them. It does not
prove bearing or material, water support, slope suitability, whole-craft
clearance, swept contact, impact energy, or landed state. An inverted but
noncollapsed rectangle may satisfy this geometric test. It must not set
`TouchdownPadObservation::footprint_supported=true` or imply touchdown readiness.

Production recipes/saves, coordinate adaptation of the native nonrotating lab,
gear controls, the 16 m floor guard and damage remain unchanged. #202/#253 are
not completed by this helper.

## Qualification

The focused tests use real registered craft/world identities and generated
triangles on all six cube faces. They check independent projected-point
barycentrics, whole-rectangle extrema, tilted/inverted/collapsed rectangles,
center-and-corner sampling traps, strict inset refusal and an adjacent actual
world-coordinate ULP transition. Cache eviction/capacity and authoritative state
immutability are checked separately. Malformed requests leave the cache empty.

Some defensive triangle-conditioning guards cannot currently be forced through
valid generated terrain. Tests do not admit forged triangles or claim those
branches were exercised. Exact new output fixtures are qualified across GCC
and Clang; compile the experiment with contraction disabled without changing
legacy source flags or golden references.

The procedural-system seed-42 level and tilted fixtures produce matching
GCC/Clang hashes `3330668434011759318` and `11232463919126072566`, respectively.
These observed bit goldens complement the independent matrix/geometry oracles;
they do not turn this numerical allowance into a formal arithmetic proof.
