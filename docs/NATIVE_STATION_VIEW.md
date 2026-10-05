# Native Origin Station view

2026-09-30, #219. Historical format17 Continue renders the selected station-owned
export at one metre per unit. It consumes the read-only C++ station geometry
query and the verified [starter package](NATIVE_STARTER_ASSETS.md); missing or
changed model bytes refuse before scene creation. The launcher prepares the
package automatically. Headless selection validation skips model loading.

Right-drag orbits the inspection camera; the wheel changes range within
2–1000 metres. The model receives C++'s hub-floor translation once, with no
rescaling or invented station rotation. D1/D2 aperture rings and numbered
labels use the same C++ port coordinates. The markers obey scene depth;
the screen corner retains each port's range/identity and reports off-screen
projection. Projection does not claim an unobstructed line of sight or docking
clearance. Capture/release and walking remain separate implementation work.

Runtime import generates distance-based mesh LODs. The original mesh, metre
scale, root transform and independent port landmarks remain fixed. Camera
near/far planes are 0.05/2000 metres. A sky shader draws the host silhouette
using its C++ direction and radius/separation ratio; this separates planetary
distances from station depth precision without feeding presentation scale into
simulation. It is a silhouette, not a procedural terrain surface renderer.

The ordinary shell retains the selected save clock, identities and history.
Inspection never advances simulation. Save As uses the existing C++ atomic
writer; source saves and unsupported-flight refusals retain their meanings.
The owner’s six-point playtest minimum in #245 is still the acceptance boundary
for the eventual playable journey.

Fresh New Game uses the [saved station walking view](SAVED_STATION_WALK.md)
and the [matched starting assembly](NATIVE_STARTING_ASSEMBLY.md). Its actor and
craft share the mutable C++ clock; this historical inspector remains frozen.

## Evidence and rendered review

`native_station_view` in `tools/test_godot_native.py` checks invalid dimensions,
short/nonfinite vectors, bounds, ownership, port indices/normals/dimensions,
asset paths and camera inputs before the selected asset import. It checks exact
mesh count, metre registration and both port datums, then front/rear/side and
above/below views at 2, 30, 100, 500 and 1000 metres at 640×360 and 1920×1080.
These are headless geometry/projection checks, separate from rendered evidence.

The opt-in `native_station_capture.gd` requires a real rendering display driver
and refuses dummy/headless capture. Run it in an isolated copy of the Godot
project with the matching bridge, prepared assets and an existing output folder:

```sh
godot --path /absolute/path/to/isolated-project --audio-driver Dummy \
  --script res://studies/captures/native_station_capture.gd -- \
  /absolute/path/to/prepared-assets /absolute/path/to/captures
```

The active capture stages a fresh matched walking scene and changes only its
inspection camera. It captures eight bounded views while retaining the actor,
craft and clock, and records the selected assembly binding, renderer,
asset hash, source license references, poses, viewport dimensions and image
hashes in `capture.json`. Logical viewport dimensions are distinct from output
pixels when Godot canvas stretching is active. Generated images stay in build
output with this provenance record. Compare silhouettes and port registration
with visual tolerances across drivers; image hashes are receipts, not a
cross-driver exact-pixel requirement. Software-rendered captures establish
actual rendering, not target hardware performance or a playable flight loop.
Prepare the complete assets with `tools/prepare_freedom_native_assets.py`;
the old constituent-only starter directory does not contain the selected
operating/stowed companions. Parked reel scripts remain historical artifacts.
