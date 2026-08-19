# Local-System Rendering and Navigation

Version 1 presents the generated target system without adding craft travel
physics or mutable celestial state. The application renderer consumes a
validated `LocalSystemDescriptor`, an explicit ephemeris query time, a
system-inertial camera pose and velocity, and one selected stable planet ID.
Terminal capabilities and presentation cadence are not renderer inputs.

## Camera and navigation

The camera uses the right-handed system frame from the coordinate contract.
`forward` and `up` may be unnormalized but must be finite, non-zero, and
non-collinear. Their cross product establishes camera right. The selected
planet's center and velocity come from `resolve_planet_ephemeris()` at the
supplied tick and optional presentation-only sub-tick fraction.

Navigation is derived without mutation:

- bearing is the signed camera-right angle around the camera up axis;
- elevation is the signed angle above the camera forward/right plane;
- range is the system-space center distance in metres;
- closing speed is positive when range is decreasing and negative when it is
  increasing, with speeds inside plus or minus 0.5 m/s reported as holding.

The cockpit formats target name, bearing, elevation, range, closing speed, and
an explicit steering/motion cue into fixed nine-character lines. These textual
cues and the light corner brackets around the selected body keep Kitty and ANSI
presentations information-complete without relying on color.

## Projection, LOD, and overlap

The default renderer uses a 60 degree horizontal field of view with a 1 km near
clip and a 100 billion metre far clip. Body centers outside that depth interval
or behind the camera are not rasterized, but a selected body still has a valid
navigation solution. Visible bodies are projected from their physical angular
radius. Planet impostors are bounded to 1–32 pixels and the star to 2–96 pixels;
those bounds preserve legibility and work rather than changing physical range.

Bodies are drawn far to near. Equal-depth ties use body kind and compatibility
catalog ordinal, making overlaps deterministic without sorting on display
color or allocation order. The star uses its generated color. Planet impostors
use their atmosphere color when present and generated highland color when
airless. Background stars are a stateless hash of system seed and pixel
coordinate.

## Orbital handoff

When the selected planet's unclamped projected radius reaches 24 pixels, the
system frame begins a deterministic crossfade to the existing orbital renderer.
The crossfade completes at 48 pixels. The orbital camera is the craft camera
relative to the selected ephemeris center, and light points from that center to
the system star. The selected brackets remain after composition, so the target
cannot disappear or exchange identity during the transition.

This alignment remains presentation-only. The application-owned system-flight
state now supplies craft acceleration, planet-fixed rotation, orbit insertion,
and persistence; entry targeting remains a Planetfall concern.

## Validation and measurement

All settings, system data, ephemeris time, camera vectors, target identity, and
exact framebuffer length are validated before caller pixels change. Failures
are typed and transactional. Tests cover non-finite values, invalid camera
bases, unknown targets, short buffers, clipping, behind-camera bodies,
multi-time motion, handoff continuity, and golden checksums at every named
viewport.

Run the pure application workload with:

```sh
./build/apsis-drift --benchmark 180 --profile remote --workload system
./build/apsis-drift --sweep 60 --sweep-viewports remote,local \
  --sweep-fps 30,60 --workload system --report system-sweep.json
```

Exercise the complete cockpit and terminal presentation paths with:

```sh
./build/apsis-drift --system-navigation-acceptance \
  --driver kitty --profile remote --report system-kitty.json
./build/apsis-drift --system-navigation-acceptance \
  --driver ansi --profile remote --report system-ansi.json
```

The JSON acceptance report records stable system/star/planet identity,
navigation values, visible-body coverage, final framebuffer checksum, renderer
timing, frame work, the active driver, and encoded bytes. The requested ANSI or
Kitty driver is constructed directly; acceptance fails instead of writing a
mislabeled report if the active identity differs. Renderer and full-frame
results measure application work through TermForge's selected driver; any PTY,
proxy, decoder, or display timing must be recorded separately.

### 2026-08-18 headless baseline

Release builds on Linux 6.12.74 and an Intel Core i9-13900H rendered 60 frames
per viewport with GCC 14.2.0 and Clang 20.1.8. These are application-to-driver
measurements, not live terminal or proxy timings.

| Compiler | Profile | Renderer avg/p95 | Complete frame avg/p95 | 30 FPS headroom | 60 FPS headroom |
| --- | --- | ---: | ---: | ---: | ---: |
| GCC | `remote` | 0.28 / 0.30 ms | 0.61 / 0.62 ms | 32.72 ms | 16.05 ms |
| GCC | `local` | 0.87 / 1.22 ms | 1.52 / 1.92 ms | 31.42 ms | 14.75 ms |
| Clang | `remote` | 0.30 / 0.32 ms | 0.63 / 0.65 ms | 32.68 ms | 16.01 ms |
| Clang | `local` | 0.51 / 0.54 ms | 1.01 / 1.05 ms | 32.28 ms | 15.61 ms |

Both compilers produced framebuffer checksum `15685277948684615095` at
`remote` and `10385222813185508075` at `local`. The corrected v0.4.18 ANSI and
Kitty acceptance runs also agree on the remote checksum while retaining actual
driver-specific encoded-byte measurements: about 54 KB for ANSI and 426 KB for
Kitty over six frames. The previously published values both came from Kitty and
were mislabeled. Their generated 320x240 PPM smoke snapshots both had SHA-256
`e3532664b61d51f9dd3bbb89f4dc7a7434afe9a68b0a180a656eb8df3e146cad`.
Those snapshots are generated verification artifacts and are not committed
media assets. No terminal decoder, display, network, or remote proxy timing was
available in this headless environment, so none is inferred from these values.
