# Apsis Drift

Apsis Drift is a C++23 deterministic, procedurally generated spaceflight game.
The existing TermForge implementation is retained. The active next direction
is the mission-free native Freedom slice in `docs/ROADMAP.md`, using the Godot
native presentation path selected in `docs/GODOT_ADOPTION.md`.

- Keep terrain generation, flight, simulation, and 3D rendering
  application-owned in the existing implementation. Godot is the selected
  native presentation engine and must consume existing authoritative C++
  world/state rather than invent a second universe. Follow the ownership split
  in `docs/GODOT_ADOPTION.md`; do not build a competing SDL graphics engine.
- Keep terminal protocols, capability detection, input, and degradation in
  TermForge for the terminal path. Native presentation is primary; terminal
  parity is not a gate for new native features. Any terminal archive must be
  explicit and recoverable; preserve headless simulation/testing independently.
- Follow dated Freedom scope amendments on GitHub issues. Historical milestone
  text and deferred horizon placeholders are not active implementation orders.
  Do not gate Freedom movement or the starter drive on missions, upgrades or
  an economy. Discuss and decompose horizons before implementing them.
- Keep public instructions portable: no private hostnames, maintainer hardware
  inventory, access topology, or machine-specific paths as dependencies.
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
- Run clang-tidy 20 through `tools/lint.sh`. Suppressions must name exact checks
  and include an inline justification accepted by `tools/check_nolint.sh`.
- Keep generated visual, music, and audio assets accompanied by provenance and
  license metadata.
- Do not extract a generic engine before at least two working game systems show
  the same boundary.
- Do not publish a release or create additional remotes unless the user
  explicitly asks.
