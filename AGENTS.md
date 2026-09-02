# Apsis Drift

Apsis Drift is a C++23 deterministic, procedurally generated spaceflight game
rendered through TermForge. Its current playable foundation is a 640x480
voxel-space terrain flyover; the next vertical slice adds a cockpit and an
orbit-to-surface objective.

- Keep terrain generation, flight, simulation, and 3D rendering
  application-owned.
- Keep terminal protocols, capability detection, input, and degradation in
  TermForge.
- RasterForge is optional and should enter only when encoded image assets,
  fitting, resizing, or compositing become demonstrated reusable needs.
- Preserve deterministic seeds, independent random streams, and versioned save
  generation.
- Preserve the headless benchmark and live capture path; do not confuse
  renderer throughput with terminal/proxy throughput.
- Test invalid dimensions, non-finite state, and buffer boundaries before
  visual smoke checks.
- Build and test with both GCC and Clang for publication changes.
- Format repository C and C++ files with clang-format 20 through
  `tools/format.sh`; do not run an unpinned formatter.
- Keep generated visual, music, and audio assets accompanied by provenance and
  license metadata.
- Do not extract a generic engine before at least two working game systems show
  the same boundary.
- Do not publish a release or create additional remotes unless the user
  explicitly asks.
