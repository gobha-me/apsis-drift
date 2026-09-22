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

## Explicit physical-home bootstrap

The separate [schema2 physical-home study](NATIVE_PHYSICAL_HOME.md) now resolves
the authored home through its complete C++ universe/catalog/rotation owner.
It shares mesh/replay construction, replaces terrain caches transactionally,
and supplies same-tick star geometry to native presentation. Snapshot-v1 bytes
and defaults remain unchanged; injecting `world_context` into schema1 is
explicitly refused. A JSON seed or ID alone is not ownership evidence.

The lab's geodetic lighting probe remains an explicit visual approximation over
planet-centred nonrotating motion. This does not relabel its state canonical
`planet_fixed`, add surface/atmosphere co-rotation, restore a career, or complete
rotating 6DOF integration.
