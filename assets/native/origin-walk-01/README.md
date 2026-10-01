# Origin walk geometry 01

This immutable engine contact derivative contains the actual admitted station
triangles intersecting the bounded hub-to-D1 standing corridor. It does not
change the Blender master or presentation GLB. `provenance.json` binds source,
presentation, selection, dimensions, counts, license and geometry SHA256.

Reproduce with Blender 5.2.2 (source remains read-only):

```sh
blender -b --python tools/export_origin_walk_geometry.py -- \
  --source path/to/station-reference.blend --output assets/native/origin-walk-01
```

Coordinates are station-relative metres: Blender `(x,y,z)` becomes
`(x-.97,z,-y+.978)`. Vertices use integer micrometres; rounding error is at
most 0.5 micrometres per axis. Spatial crop retains entire intersecting
triangles, without decimation. Evaluated meshes match `station_export` selection
at frame 1, excluding all visiting craft and authoring guides/gauges/work.

The deliberately bounded supported-interior slice reserves a 0.64 m wide,
1.93 m high axis-aligned standing box, 0.045 m above its floor datum. Movement
is fixed 120 Hz, 2 m/s, normalized diagonal intent; neutral input stops.
Heading zero faces -Z and positive heading rotates about +Y. Five support
probes use actual nearly horizontal source triangles within 0.03 m of the
foot datum. Swept triangle/box separation checks stop blocked commands at the
current supported pose. Two-metre XZ bins bound local contact queries.

The corridor crop is X[-24.5,0.45], Y[-0.05,2.1], Z[-0.875,0.875] metres.
Its lateral boundary is deliberately conservative and is not the whole
station's walkable interior. The actual open D1 shaft has no invisible floor.
This package implements no ladder, cabin, boarding, artificial-gravity failure,
EVA or general dynamic rigid-body collider. Ordinary interior movement is
kinematic supported traversal; it does not prescribe AG physics.
