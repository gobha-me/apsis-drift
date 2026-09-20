# Native snapshot-v1 identity boundary

The experimental native bridge accepts snapshot-v1 planet metadata only when
the complete `planet` object equals the existing canonical
`planet_descriptor_json(generate_planet_descriptor(seed))` projection. Identity,
versions, name, radius, gravity, atmosphere, water, terrain and palette all come
from C++; missing or additional descriptor keys are rejected. JSON object order
and equivalent integral numeric encodings do not alter identity. Numeric
request fields reject booleans and strings before conversion; integer fields
also retain their existing finite, integral and range checks.

This preserves valid exporter bytes, recorded replay checksums and flight
arithmetic. Failed initialization retains the running world and its stream;
only the diagnostic changes. Tests reject altered metadata after actual flight
progress, including nested fields and boolean/fractional inputs.

An authored origin-home planet deliberately shares its procedural base's seed
and PlanetId while changing physical fields. It is **not** a valid standalone
snapshot-v1 substitute. Future catalog-backed native initialization must resolve
the exact descriptor through its authoritative system/variant context, with
fresh context-scoped caches. This check does not add that initialization path,
restore a career save, validate every presentation buffer against regenerated
terrain, or migrate the nonrotating flight lab to a canonical rotating frame.

## Next explicit bootstrap boundary

The next native path must be opt-in and separately versioned. Its first bounded
target is the physical origin universe's authored home planet, not arbitrary
caller-supplied descriptors or a restored career. Its recipe must retain the
physical catalog family/version, source catalog and ephemeris versions,
origin-universe seed, exact selected planet identity and rotation-owner/spin
versions. Resolve those through C++ before allocating terrain or replacing the
current session. A JSON seed or ID alone is insufficient ownership evidence.

The exporter and live bridge should share descriptor resolution and the existing
mesh/replay construction. A successfully constructed replacement receives fresh
terrain/stream caches; failed validation leaves the running session and caches
intact. Reset uses the retained selected recipe, not a default standalone seed.
Snapshot-v1 exports, defaults and refusals remain unchanged.

Presentation must query one authoritative tick and star vector for direct light,
sky and ambient illumination together. During the current flight-lab phase, a
geodetic lighting probe is an explicitly visual approximation over the lab's
planet-centred nonrotating motion; it is not permission to label that state
canonical `planet_fixed` or claim surface/atmosphere co-rotation. Actual rotating
6DOF integration remains a separate requirement.

Qualification must include malformed owner/version refusal, authored-home versus
same-ID procedural substitution, reset ownership, failed replacement after live
flight/streaming, unchanged flight state after lighting queries, both compiler
contracts, and native daylight/twilight/night views. Diagnostic fixed-epoch
views can establish lighting without adding a gameplay time-acceleration mode.
This section records the next implementation boundary, not completed behavior.
