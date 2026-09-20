# Owner-qualified experimental contact queries

This is a context-aware extension of the standalone finest-triangle and
single-triangle pad experiments. It admits the existing authored origin-home
planet without accepting substitute descriptors or creating another world.
It is not wired into the native bridge and is not a production/save format.

## Authority and identity

`locate_owned_contact_triangle`, `build_owned_contact_triangle` and
`query_owned_contact_surface` accept an authoritative `LocalSystemDescriptor`.
They resolve the requested planet solely through `find_local_system_planet`,
which validates catalog identity, generator-derived descriptors and orbits.
The authored home descriptor is accepted only at the existing designated
ordinal in an `origin_home` catalog and must exactly match
`generate_origin_home_planet(system.seed)`. Merely satisfying tutorial-safe
ranges is not sufficient. Other planets in that catalog remain procedural.

`generate_origin_home_planet` deliberately retains the seed and PlanetId of
its unmodified procedural counterpart while changing physical/terrain fields.
Consequently contextual results must retain the complete experimental owner:

| Field | Supported contract |
| --- | --- |
| Owner format | 1, this experimental wrapper only |
| System | Validated `SystemId` |
| Catalog kind | `procedural` or `origin_home` |
| Planet | Validated catalog member `PlanetId` |
| Seed derivation | `kSeedDerivationVersion`, currently 1 |
| System generator | `kLocalSystemGeneratorVersion`, currently 1 |
| Planet generator | `kPlanetGeneratorVersion`, currently 1 |
| Descriptor variant | `procedural` or `origin_home` |
| Origin-home generator | 0 for procedural descriptors; otherwise `kOriginHomePlanetGeneratorVersion`, currently 1 |

Full triangle identity is this owner **plus** `ContactSurfaceRecipe` **plus**
`ContactTriangleId`. Extraction compares every supplied owner field with a
fresh context-derived owner before sampling; a label supplied by the caller
does not grant authority. Unknown versions, mismatched planets and topology
indices are rejected. Do not strip the wrapper and treat its inner standalone
triangle identity as unique across catalog variants.

The existing standalone APIs retain their stricter unchanged-procedural-
descriptor requirement and authored-origin refusal. Contextual procedural
geometry matches standalone geometry bit for bit. All geometry is generated
through the same private address/sample/relief/intersection kernels; no
alternate terrain arithmetic is added.

## Cache lifecycle

The caller scopes each source cache and derived geometry store to one resolved
catalog/descriptor variant. On a context change, validate and construct the new
context and fresh caches before atomically replacing the active session. Do not
reuse a same-PlanetId procedural cache for an authored home variant.

The existing `TerrainTileCache::get` same-key/different-descriptor refusal is
preserved. A conflicting entry produces a sampling failure; the provider does
not replace the entry, flush the cache, or retry with another descriptor.
Capacity/eviction independence is qualified within a compatible context, not
for deliberately mixed incompatible catalogs. Cache state is not world state.

## Pad extension and remaining gates

`certify_owned_contact_patch` first validates the canonical planet-fixed rigid
state against its world context, then derives the registered support rectangle.
It returns `ExperimentalOwnedContactPatch`, retaining the exact owner with
the existing geometric payload. The old `certify_contact_patch` remains strict.
All conditioning, gap allowances, boundary refusals and nonclaims remain in
force: no material/bearing or nearest-normal-surface proof, and no permission
to set `footprint_supported=true`.

The current native `LiveWorld` still starts from a standalone planet seed and
has no owning catalog. It must not invent one from that seed or the visible
stars. A future explicit C++ bootstrap must obtain the real origin catalog
from the universe recipe, or a real procedural catalog from the selected
system recipe, then resolve the selected planet. The nonrotating-lab to
canonical-frame adapter remains separate. No bootstrap, live bridge, floor
guard, landing, damage, bearing, recovery or save transition changes here.

## Tests

Tests distinguish same-ID authored/procedural variants; preserve all old
standalone goldens and refusals; reject forged owner/version/catalog/ordinal/
planet inputs before terrain-cache access; and check cache conflict refusal
without replacement. Authored vertices are compared with the unchanged native
stream builder on all six faces. Radial geometry and registered pad cases use
the existing independent geometric oracles, including all three supports,
tilted gaps, exact procedural equivalence and context-scoped cache eviction.
New owner-inclusive fixtures are qualified separately on GCC and Clang.

The universe-seed-42 origin fixture and its same-system-seed procedural
counterpart produce these matching new GCC/Clang hashes:

| Fixture | Owner-inclusive hash |
| --- | --- |
| Origin radial point | `17524982012417750703` |
| Procedural counterpart radial point | `13132756775747483476` |
| Origin level pad | `7643701639146807608` |
| Origin tilted pad | `4396711435420471693` |

These are observed bit-regression fixtures, qualified alongside independent
geometry checks. They do not imply formal interval arithmetic, production
terrain adoption or safe touchdown.
