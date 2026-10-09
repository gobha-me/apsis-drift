# Generated terrain pad assessment

`TerrainTouchdownSnapshot` is the read-only C++ provider for #202. It connects
actual generated terrain and a same-tick physical flight snapshot to the
existing `assess_touchdown_envelope`. #203 owns deployment, safe landing and
liftoff; #253 owns unsafe impact consequences. This provider changes none of
those states, flight trajectories, clocks, saves or floor guards.

Support policy **1** explicitly adopts the current native finest top surface:
terrain generator 1, source LOD8, relief0, mesh LOD13, 32 intervals and the
existing anti-diagonal. Metres remain physical metres. Relief0 preserves base
elevation; it adds no experimental detail. Existing experimental recipes and
historical saves remain unchanged. The existing geometry types retain their
historical namespace and version; the production support policy is an additional
contract, not permission to treat every experimental result as physical support.
Local geological detail and the wider visual scale contract remain #211/#208.

The factory validates the actual physical catalog, selected descriptor,
registered craft and rotation owner, then reframes the flight state into
planet-fixed coordinates at the same tick. The snapshot owns its geometry,
provenance and bounded source cache. A result retains the complete owner and
original checksum alongside policy version; numeric PlanetId alone is
insufficient to distinguish authored origin terrain from its procedural namesake.
Malformed ownership, state, frame, recipe or cache capacity refuses before
terrain sampling. There is no renderer, camera, shader or GPU input.

For each registered deployed pad, the existing analytic rectangle certificate
provides signed minimum/maximum plane gaps and a geometric outward normal.
The provider additionally requires the whole pad and its normal projection
inside the selected triangle's **radial cone**, with strict 0.1 mm numerical
inset. Affine extrema cover the entire rectangles. Their connecting normal
segments remain inside that convex cone. The current top mesh is a positive
radial graph: neighbouring facets occupy other cone interiors, so another
top facet cannot precede the selected facet along a certified segment.
The planet origin is the cone origin; treating facet edge planes as radial
planes would be incorrect. Cross products use edge differences to avoid
subtracting nearly equal world-scale products.

This is the existing bounded binary64 numerical policy, not a formal
directed-rounding proof. It retains the geometry experiment's 100 km datum
query bound and conditioning limits. Cross-triangle rectangles, seams, ridges
that cannot fit one facet, collapsed projection and uncertain cone containment
return indexed refusal. No unbounded neighbour search hides a failed foot.
Each pad samples at most three vertices, at most twelve per batch. A source
cache miss still generates the existing fixed-size terrain tile; these are not
claims about GPU frame rate or worst-case terrain-generation latency.

Dry facets use a deliberately simple **rigid bedrock bearing assumption**.
Projection onto an anchor radial unit vector lower-bounds distance from the
planet origin over the entire facet. If every vertex projection exceeds sea
datum plus the numerical allowance, the facet is solid. Convexity bounds the
whole facet's radius by the maximum vertex radius: an entirely sub-datum facet
is water. Shoreline or numerical uncertainty is unsupported. Only certified
dry bedrock receives `footprint_supported=true`; neither palette nor shader
grit establishes soil strength. Generated water currently colours sub-datum
top geometry; this policy provides neither a separate ocean collision plane,
buoyancy nor water landing.

When every pad has an observation, the unchanged envelope checks deployment,
compression, separation, slope, attitude, contact-centre ground-relative speed,
angular rate and the craft's gravity, pressure and full-weight static-load
ratings. Existing upper bounds are inclusive; positive gap and separating
velocity fail readiness. Any pad minimum gap at or below zero is a contact
candidate. All-positive gaps mean `pads_clear`; a candidate is ready only if
every margin passes, otherwise unsafe. Failures remain named bit margins.
When geometry is unresolved, `assessment` is absent with indexed reasons;
this means **unknown**, not clear flight or an invented unsafe surface.

`gear_deployed` is an explicit caller declaration, including prospective
inspection. The provider never invents saved gear state from an asset preview.
The Wayfarer has three registered deployed pads; the unused fourth slot stays
empty. Pad readiness does **not** establish hull or swept clearance, actual
load distribution or a landed state. A landing consumer must handle unknown
geometry, check the hull/trajectory and own actual deployment before committing
a transition. A conservative refusal near a facet boundary is a documented
prototype limitation, not a requirement for another asset study.

Validation uses generated seed42 dry terrain and a separate generated water
fixture, exact binary64 gap/normal golden vectors, repeated queries and cache
capacities 1/64, real mesh-diagonal coverage refusal, malformed and finite/buffer
boundaries, synthetic level/sloped/sea-datum facets, and independent long-double
ray intersections through pad interiors and neighbouring generated facets.
`touchdown-envelope-contract` retains the full threshold-edge and simultaneous
margin tests. Run `terrain-touchdown-contract` and the existing contact and
envelope contracts under both GCC and Clang. These headless assessments do not
qualify near-contact rendered alignment, controller play or physical landing.
