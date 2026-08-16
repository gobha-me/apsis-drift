# Planetary Presentation Handoff

The Planetfall presentation path connects the immutable planet descriptor,
planet-fixed coordinates, deterministic terrain tiles, and planetary flight
state without making rendering or cache residency authoritative simulation
inputs. It is application-owned and uses TermForge only to present the final
opaque framebuffer.

## Surface identity

`sample_planet_surface()` resolves a planet-fixed position through the existing
cube-sphere address contract, acquires the addressed immutable tile, and uses
16-bit quantized bilinear interpolation over its inclusive 65 by 65 samples.
The interpolation is presentation-only: it does not change terrain generator
version 1 or any stored tile checksum. Exact aligned samples remain identical
across LODs, face edges, and corners.

The tile-backed orbital pass uses LOD 2 for bounded whole-planet coverage. The
local pass selects LOD with `select_terrain_lod()`, clamping negative reference
altitudes to the contract's zero-altitude floor. Both passes derive their
camera from the craft's geodetic position, local east/north/up frame, heading,
and presentation-only pitch. This keeps the surface anchor and view direction
stable while detail increases near the ground.

All tiles read by a frame are held through immutable shared ownership for that
frame. Rendering occurs in scratch framebuffers, so a coordinate, tile, camera,
or buffer failure cannot expose a partial destination.

Each pass uses a frame-scoped sampler that validates its planet and LOD once,
pins unique tiles, and reuses the orbital surface normal as a cube-sphere
direction. At widths of 640 pixels and above, the tile-backed orbital pass uses
one centered ray for each two-column span. The stride is constant throughout
the orbital, atmospheric, and terrain transition stages, while the remote
320x240 profile retains one ray per output pixel.

## Transition bands

Presentation weights reuse the simulation's existing hysteresis boundaries:

- atmospheric response is zero at the ascending orbit boundary and reaches
  full strength at the descending atmosphere boundary;
- local terrain is absent at 2,500 metres clearance and reaches full strength
  at 2,000 metres clearance;
- weights clamp outside those intervals and depend only on authoritative
  position, clearance, and the immutable descriptor;
- airless planets keep the atmospheric approach regime but apply no invented
  atmosphere color.

Blend coefficients are converted once per frame to 16-bit fixed-point weights.
A source pass is omitted only when its maximum possible contribution is below
half of one 8-bit channel value, meaning it cannot change the rounded output.
This removes the otherwise full local pass at the canonical first mixed frame
without introducing a visible threshold or changing the reported mode.

The reported modes are `orbital`, `atmospheric`, `terrain-blend`, and
`local-terrain`. Mode, blend weights, selected LOD, anchor address, tile counts,
and per-pass CPU timings are diagnostics. None enters the flight checksum.

## Cockpit and benchmark path

Planetary cockpit formatting reads heading, altitude, clearance, speed, mode,
regime, and last transition from one `PlanetaryFlightState`. Fixed-width
sentinels remain available for invalid telemetry.

The headless scripted descent cycles a fixed surface location through all four
presentation modes:

```bash
./build/apsis-drift --benchmark 4 --profile remote \
  --workload planetary --snapshot planetary.ppm
./build/apsis-drift --sweep 60 --sweep-viewports remote,local \
  --sweep-fps 30,60 --seed 42 --workload planetary \
  --report planetary.json
```

Reports retain the existing renderer, complete-frame, and encoded-byte
measurements and add mode counts, orbital/local/composite averages, total
presentation average/p95, and maximum tiles touched. Headless measurements do
not include a terminal, PTY, proxy, decoder, compositor, display, or network.
The fully integrated orbit-to-flyover replay, deterministic stage identities,
and measured profile envelope are recorded in the
[Planetfall acceptance path](PLANETFALL_ACCEPTANCE.md).
