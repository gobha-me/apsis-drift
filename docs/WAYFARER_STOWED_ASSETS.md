# Static stowed Wayfarer asset delivery

[`assets/native/wayfarer-stowed-01`](../assets/native/wayfarer-stowed-01)
delivers the frozen candidate19 parked harness for
[issue #411](https://github.com/gobha-me/apsis-drift/issues/411), under the
[static assembly contract](WAYFARER_STOWED_JOINTS.md). Python 3 with standard
`lzma` support prepares it offline. Linux `renameat2` provides atomic
installation without replacing another output.

```sh
python3 tools/prepare_wayfarer_stowed.py \
  --package assets/native/wayfarer-stowed-01 --verify-only
python3 tools/prepare_wayfarer_stowed.py \
  --package assets/native/wayfarer-stowed-01 \
  --output build-native/prepared-stowed
python3 test/wayfarer_stowed_package_test.py
```

The closed package contains eleven independently selected inputs: the current
model, contact and face attribution; the two baseline07 emitted buffers needed
for reproducible preservation checks; five inherited licenses; and compact
qualification provenance. The licenses and compact qualification are directly
readable; bounded single-stream XZ preserves the geometry payloads' exact bytes.
Verification rejects changed decoded identities even when a substitute's
container manifest has been rehashed. It follows only the named package roles.

The validator binds **69 objects, 14 flat identity roots and 16,968 source
faces**. Source positions retain exact binary32 values, including signed zero;
indices, UVs, material attribution and the complete emitted occurrence mapping
are checked before installation. All **57 retained objects and 15,568 faces**
reproduce baseline07's complete emitted corner bits and material descriptors.
The service manifold is compared against its already shifted parked baseline.
The seven changed parts retain their accepted correspondence and evaluated
normal profile; this does not claim unchanged normals for modified ribbons.

Preparation adds `frame.json`, `preservation.json`, `replacement-contact.json`
and a receipt. Replacement contact includes only the eight superseded original
objects and five added connectors: **13 objects and 1,508 faces**. It preserves
source-face identities and exact points without quantization. The eventual
collision consumer must retain every other original obstacle and the existing
lower-cockpit halo once, using the
[eight-range C++ partition](ORIGIN_STOWED_CONTACT_PARTITION.md).

Coordinates are once-baked direct REST, canonical metres, +Y up/-Z forward.
The frame binds original catalog group11 `craft_seat_lift` to motion index10
`seat_lift`; its complete ancestor-composed delta applies once. Source object
matrices are provenance, rather than another transform to apply to these points.

An existing prepared output is reused only when its entire roster and bytes
match. Changed outputs refuse replacement. Validation failure or a concurrently
created destination leaves that destination intact.

## Runtime integration remains separate

This delivery prepares static assets. Ordinary game startup still uses its
existing ship/contact selection. Supported boarding, sitting, occupied hardware
motion, equipment persistence and departure remain under
[First Flight #245](https://github.com/gobha-me/apsis-drift/issues/245).

The live starter batches the old seat inside `HopperStructure`. Appending this
assembly would duplicate it. The operating base provides a separate seat-lift
mesh and matches the calibrated eye, screens and gear data. Its exterior atlas
is bit-exact, but Godot imports it at surface14 rather than surface18; shared
presentation, exhaust and LOD must use the verified selected surface before that
base replaces the live model. The matched renderer/contact installer is still
required.

Qualification provenance records the accepted finite results and correspondence
identities, with their actual limits and public links. It preserves the older
candidate receipt's unresolved status as history. No source master, provider
account, private journal, authoring checkout or numerical-owner replay is needed
to verify this delivery.
