# Orbital Planet Rendering

The orbital renderer is an application-owned CPU rasterizer for the broadest
planet representation. It consumes the immutable `PlanetDescriptor`, the
planet-fixed coordinate vocabulary, a camera, lighting, and a validated
viewport. It does not inspect terminal capabilities or depend on RasterForge.

## Camera and lighting contract

`OrbitalCamera` uses metres in the planet-fixed frame. Its position must be
outside the descriptor radius. `forward` and `up` may have any non-zero length,
but must be finite and non-collinear. `OrbitalRenderSettings` uses a horizontal
field of view between 1 and 179 degrees and a planet-to-light direction.

`OrbitalRenderer::render()` validates the viewport, exact framebuffer length,
descriptor domains, camera, field of view, and light before touching the
destination. Successful pixels are opaque RGBA. The result reports surface and
atmosphere pixel counts so callers and tests can distinguish a visible,
clipped, or off-screen planet without inspecting presentation colors.

## Presentation model

Each pixel casts a pinhole-camera ray against the spherical descriptor radius.
Surface color combines:

- the descriptor's terrain stream, terrain character, and water coverage;
- its deep/shallow water and lowland/highland/peak palette colors;
- diffuse lighting, view-dependent limb shading, and atmospheric scatter.

The two-octave spherical field is orbital-scale presentation, not generated
terrain compatibility data. It consumes no mutable random state and does not
define the terrain-tile algorithm tracked by issue #14. Later LOD blending can
retain descriptor identity while replacing this broad field with tile-backed
detail near the surface.

Airless descriptors have no halo. Other atmosphere classes derive halo width
and intensity from pressure while using the descriptor atmosphere color. Rays
outside the planet produce an opaque near-black field with deterministic sparse
stars.

## Reproducible benchmark path

The existing benchmark and sweep modes accept an explicit workload:

```bash
./build/apsis-drift --benchmark 1 --profile local \
  --workload orbital --snapshot orbital.ppm
./build/apsis-drift --sweep 60 --sweep-viewports remote,local \
  --sweep-fps 30,60 --seed 42 --workload orbital \
  --report orbital.json
```

`landscape` remains the default workload. `--workload` is rejected outside
benchmark and sweep modes so the current playable flyover never presents
orbital imagery with local-flight controls or telemetry.

The sweep measures application CPU rendering and TermForge submission through
the headless Kitty driver. It does not measure a terminal, PTY, proxy, decoder,
display, or network path. The dated GCC and Clang reports and environment are
recorded in [the orbital headless measurements](performance/2026-08-16/README.md).
