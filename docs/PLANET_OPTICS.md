# Static planetary cloud and dust cover

Optics1 adds visible cloud and dust regions to the authoritative C++ planet.
It is a static field fixed to the rotating body. Sky and terrain consume the
same field at the actual camera position, including cockpit, chase and walking
eye offsets. It changes appearance without changing flight, terrain, contact,
resources, hazard exposure or recovery rules.

## Field and ownership

`planet_optics.hpp` binds a versioned recipe to the validated physical/ambient
owner. Append-only planet stream `optical_cover = 15` leaves streams1–14 intact.
The C++ provider generates a 64×32 byte density atlas using an 8×5 periodic
integer lattice, Q16 cubic interpolation and coherent pole rows. Longitude
repeats; latitude includes both poles. Generation uses no host floating-point
noise or mutable random stream. Godot receives the bytes, never a seed from
which to invent another distribution.

Ambient1 describes potential conditions. Optics1 separately selects an actual
opacity cap between one quarter and three quarters of its potential obscuration
bound, rounded down in integer basis points. That cap bounds every rendered
ray; it is not a promise that the current column reaches it. Clear and airless
profiles have zero density and opacity. Cloud layers occupy reference heights
2–6km; dust occupies0–3km. Bands are clipped to the existing atmosphere edge.
Mountains can extend through or above a band.

Cloud tint is RGB8 `(214,220,225)`. Dust blends the body's existing lowland
palette with `(170,127,76)` by integer averaging. No encoded asset, generated
music or third-party texture is included. The procedural field and shaders use
the repository's BSD 3-Clause license.

## Sampling and presentation

Atlas node `(x,y)` corresponds to longitude `-pi + 2*pi*x/64` and latitude
`-pi/2 + pi*y/31`. Bilinear density is multiplied by `4*t*(1-t)` across the
height band. The shared shader clips a ray against both spherical band surfaces
before integrating at eight midpoints per occupied segment, at most16 samples.
Ray opacity is `cap * (1-exp(-integral/band_width))`, with finite bounded inputs.
Scattering uses the actual C++ star direction and color. This is a bounded
rendering approximation, not photometric or meteorological accuracy.

The terrain path ends at the actual rendered surface, including LOD morphs.
The sky path uses its existing atmosphere/ground bounds. Density is not tied to
screen coordinates, frame cadence or a shader clock. Terrain heights, normals,
collision and accepted tile ownership remain untouched. Each live/pending world
retains at most one current-body C++ atlas; the presenter reuses one texture.
New terrain materials receive the field before becoming visible.

This first increment does not add evolving storms, cloud shadows, precipitation,
wind forces, lightning, nearby craft/cabin fog or a new compositor. The close
craft/station layer remains readable. Broader terrain and weather acceptance
remains under #212.

## Disclosure and persistence

Visible-optics disclosure1 permits current optical scenery while recorded hazard
knowledge is UNKNOWN. The packet contains only current-body optical appearance
and authenticated transforms. It contains no ambient maximum, radiation,
electrical/gust/thermal envelope, world seed or hidden body roster. Looking at
clouds does not grant ledger evidence, a visit or a survey result. Existing
sensor acquisition and knowledge redaction are unchanged.

Native New Game explicitly selects optics1. Flight, docking and station-journey
formats39/40/41 carry the selection and explicit frame policy. Existing enclosing
save owners retain it through boarding, surface walking, jumps and replacement
recovery. Persist recipes/selections, not atlas pixels. Historical unselected
saves retain their clear optical appearance and original encode/decode semantics;
Continue does not silently migrate them. No live preferences toggle rerolls a
saved optical world.
