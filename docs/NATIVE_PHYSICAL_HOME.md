# Opt-in physical-home lighting study

This is a native bootstrap and presentation increment for #213/#245, not a
saved career, complete Freedom loop or rotating 6DOF integration. Existing
snapshot-v1 exports and the standalone seed42 demo retain their original path.
No default startup, save16 meaning, audio mix or controller binding changes.

## Explicit world selection

The snapshot exporter supports a separate named mode:

```sh
apsis-drift-godot-snapshot physical-home.json --physical-origin=42 \
  --samples=3 --span-metres=32000 --relief-version=1 \
  --latitude=0.25 --longitude=0.4
```

The existing positional invocation remains snapshot-v1. Both modes refuse to
overwrite an output. Physical mode resolves the canonical authored home from
the physical origin universe; it cannot select an arbitrary caller-created
planet by borrowing a seed. Schema2 retains a complete `world_context` projection
of family/version dependencies, universe/system/planet/star identity, stellar
mass/GM and rotation recipe. Large identity values are decimal strings.

The live bridge regenerates and compares the complete context and descriptor.
Altered fields, missing/extra context keys, unsupported versions, or substitution
of the same-ID procedural home are refused. Replacement is constructed before
commit; success gets fresh terrain/stream caches, while failure preserves the
current session. Reset reuses the selected snapshot recipe. Schema1 does not
accept a `world_context` injection. This interchange is still not a save format.

Native schema2 rendering requires streaming thrust flight. The ordinary
`--snapshot=/absolute/path/physical-home.json --stream=true --flight-model=thrust`
options select it explicitly; the standalone demo is not silently migrated.
Validation-only mode also checks schema2 ownership through the C++ bridge.

## One star direction, no separate lighting clock

`get_world_lighting()` is read-only. It uses the current authoritative flight
tick, retained physical catalog/rotation recipe, and displayed geodetic probe.
It supplies local toward-star direction, local-to-system orientation, solar
elevation, star color/angular radius and owner metadata. Godot transfers those
to terrain sun, nearby-craft sun, sky, fog and ambient illumination without
advancing the simulation or inventing an orbital phase.

Godot's light emits along local -Z, so its +Z axis points toward the resolved
star. The sky consumes that same vector and the C++ orientation, with no shader
`TIME` input. See the official [DirectionalLight3D convention](https://docs.godotengine.org/en/stable/classes/class_directionallight3d.html)
and [sky shader interface](https://docs.godotengine.org/en/stable/tutorials/shaders/shader_reference/sky_shader.html).

Nearby craft receive a presentation-only spherical-limb visibility factor.
Terrain keeps its directional light so a craft inside the planet's shadow does
not extinguish a visible sunlit crescent. The old independent fill is disabled
for this mode. Both depth layers share ambient energy. The eight-sample sky
approximation applies the same sun direction along its atmospheric samples;
night fog is not left at daytime brightness. This is coarse parallel-ray
scattering and approximate disc visibility, not physical radiometry, climate,
terrain occlusion certification or a general eclipse system.

## Deliberate limits

The flight lab remains planet-centred **nonrotating**. Its displayed geodetic
position locates a visual lighting probe; this does not turn its state into a
canonical rotating frame or add Coriolis forces, surface rotation or atmospheric
co-rotation. The chase-view study label keeps that distinction visible. The
background stars remain the identified experimental preview catalog, not a
completed navigable universe/chart integration.

Physical days can take hours. Diagnostic captures use different geodetic
locations at a fixed epoch to examine day, twilight and night; that is not a
gameplay time-acceleration feature. Whole-simulation acceleration remains later
work. Station startup, walking/reboarding, persistent ownership, rotating
handoffs and the complete day/night issue acceptance remain open.

## Qualification boundary

C++ and real-bridge tests cover owner/descriptor refusal, reset, fresh/retained
cache boundaries and query noninterference. Presentation tests check light sign,
coordinate agreement, observer shadow versus terrain light, ambient transfer,
non-finite/reflected/contradictory inputs and legacy isolation. Native GPU
captures are still required in addition to those contracts; a Dummy renderer
does not establish pixels, headset/controller behavior or artistic acceptance.

This increment passed both complete compiler builds, 20 focused C++/CLI
contracts and all 32 isolated native contracts per compiler. The CLI contract
adds 48 cases per executable, including existing-output byte preservation.
Two snapshot-v1 fixtures (seed42, relief disabled/enabled) also match the
pre-change exporter byte-for-byte under both compilers. Pinned format20,
focused tidy20 and independent review passed; hosted full CI remains the merge
gate. Historical #255 replay portability findings are unchanged.

Actual-main hidden GPU captures cover cockpit day/twilight/night, low orbital
views and fixed inspection-camera whole-globe day/terminator/night. The complete
night-side view is dark, instruments remain readable and the illuminated globe
phase follows the same resolved direction. These are sampled presentation checks,
not final terrain/material quality, continuous traversal or full planet-scale
shadow certification. No change to authoritative terrain generation was made.
