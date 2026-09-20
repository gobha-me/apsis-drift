# Experimental finest-triangle surface query

This standalone C++ prerequisite serves #202 and future #253 work. It is not a
landing-capable build and does not close either issue. Nothing invokes it from
live flight: the 16 m experiment floor guard remains unchanged.

`experiments/godot-freedom/contact_surface.hpp` names an opt-in recipe:
experiment 1, terrain generator 1, source LOD 8, relief 1, mesh LOD 13,
32 intervals per tile, and the existing anti-diagonal triangulation. Other
recipes are rejected rather than silently substituted. This is not a new
supported-save terrain recipe and adds no generator detail.

## What the query establishes

- Stable identity is **recipe + planet + cube face + tile X/Y + cell X/Y + half**.
- Extraction reconstructs three native top-surface vertices directly from
  authoritative C++ sampling. It does not inspect a resident rendered mesh.
- The normal is the outward geometric triangle normal, not the renderer's
  smooth lighting normal. Skirts and shader detail are excluded.
- A direction query intersects a radial ray from the planet center with that
  triangle and returns Cartesian barycentric coordinates and metres.

The generated planet descriptor must be unchanged. Finite direction components
have maximum magnitude in `[1e-12, 1e9]`; vectors need not be unit. All topology
indices are checked before indexing or shifts. Work is bounded to three vertex
samples; the caller's bounded source cache can change residency, not answers.

Ownership follows existing cube addressing: largest absolute axis, ties X
before Y before Z. Internal tile/cell edges belong to the positive side; a
face's upper endpoint belongs to its last cell. Within a cell, `u + v <= 1`
selects `(00,10,01)`, otherwise `(10,11,01)`. These are binary64 input rules:
no epsilon selection band or guarantee that approximately equivalent inputs
have the same identity. Shared boundaries have multiple adjacent triangles;
only the radial lookup uses canonical ownership. Explicit extraction can name
any valid adjacent triangle, which will be useful for later footprint work.

The ray hit is not snapped to an edge. Barycentric roundoff up to `1e-7`
outside `[0,1]` is tolerated at a boundary; greater disagreement is a refused
query, not guessed support. Degenerate/nonfinite geometry is rejected. This
numerical budget is not a pad gap or terrain-support tolerance.

## What remains unproved

A point is not a footprint. This API does not establish material, bearing
capacity, fluid support, slope everywhere beneath a pad, belly clearance,
continuous/swept contact, time of impact, impulse, or a contact episode. A
surface below generated sea level is still only geometry, not a landable
surface. Whole-pad coverage must enumerate intersected triangles or refuse
ambiguity under a separate bounded contract. The new
[single-triangle patch experiment](EXPERIMENTAL_CONTACT_PATCH.md) proves normal
projection containment and plane-gap bounds only when the whole registered pad
fits strictly inside one facet; it refuses uncertain/crossing cases and still
cannot establish physical support. No frame/owner adapter, landed
state, gear action, condition/damage, save transition or recovery is added.

Before live consequences, near-contact presentation must match this surface;
coarser resident meshes can differ. The native lab's nonrotating planet-centered
frame must not be relabeled canonical `planet_fixed` without an explicit
adapter. The broader local-detail work in #208/#211 remains open.

## Qualification

`contact_surface_test.cpp` compares every top triangle of one complete finest
native tile and representative triangles on all six faces with the unchanged
stream builder. Independent ray/triangle intersection and barycentric
reconstruction check the geometry. Corner/edge/diagonal and neighboring-input
fixtures cover ownership and continuity; cache eviction/order and malformed
recipes, planet, directions and indices are checked separately. Exact new
experimental output fixtures are compared across GCC and Clang, without
altering existing generator, streaming or flight goldens. Compile this target
with floating-point contraction disabled; no legacy translation-unit flags
are changed.
