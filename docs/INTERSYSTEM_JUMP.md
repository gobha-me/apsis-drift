# Deterministic FTL Transit and Pilot Alignment

Version 4 implements the bounded first-contract route without continuous
interstellar flight. The application owns the authoritative fixed-step state,
arrival solution, and transit rendering; TermForge only presents pixels and
semantic input events.

## Authoritative sequence

After mission acceptance, `LAUNCH` creates a live station-relative craft five
kilometres from the Origin Station. Normal fixed-step flight, assist, guidance,
and redocking remain available before departure. `J` freezes that exact craft
state and begins a 360-tick (three-second) spool; pressing it again cancels the
spool, retimes the unchanged relative state, and returns to free flight. At
the exact boundary, the destination system is validated and the complete
canonical arrival is regenerated before destination, commit tick, and solution
are published atomically. The following 240 ticks (two seconds) are
irreversible transit; pause, skipped frames, terminal capability, and a
headless/no-animation run cannot change the result.

Raw universe-time batches may advance to either exact timing boundary but may
not cross one. Commitment and arrival remain owned by the one-tick jump driver,
which rejects late execution rather than stretching either fixed-duration
phase.

Outbound commitment resolves the mission planet ephemeris at the future
arrival tick. Assisted mode places the handoff ten planet radii behind its
velocity vector and matches the planet's system-inertial velocity. Pilot mode
derives its base alignment sample from the independent `jump_alignment=11`
seed domain, then deterministically adds the live craft's station-local yaw and
normalized relative speed. During the cancelable spool, A/D changes heading
error by 250 millidegrees per tick and W/S changes velocity error by 10 basis
points per tick. The cockpit and code-rendered reticle show both errors, the
projected grade, and the next correction.

Commit uses signed fixed-point integers and inclusive boundaries:

- `ALIGNED`: absolute heading error at most 3,000 millidegrees and velocity
  error at most 200 basis points. Placement is identical to Assisted.
- `OFFSET`: absolute heading error at most 45,000 millidegrees and velocity
  error at most 2,000 basis points. Placement is 10–100 planet radii away,
  with the approach direction rotated by heading error and planet-relative
  velocity scaled by velocity error.
- `OPPOSED`: any other valid sample. Placement uses the opposite
  star-centered orbital phase and reversed planet velocity.

Every grade arrives in the correct generated system and retains the same
planet, ephemeris, terrain, objective, and mission state. OFFSET remains a
normal sub-light approach. For OPPOSED, the bounded recovery is to cancel
before commitment or use the existing target-hold system-flight assist after
arrival; propulsion-specific travel-time progression is measured by #95.
These are approach corridors, not orbit insertion or objective placement. The
bounded return route resolves the Origin Station at the committed arrival
tick, places the craft 40 km along the station-relative positive-X corridor,
and matches the station's system-space velocity. The immutable arrival is
therefore tied to the moving station rather than a launch-era or system-center
coordinate.

## Persistence and presentation

Save format 15 stores the frozen outbound origin craft, active Pilot alignment,
and the immutable committed assessment in addition to the arrival solution's
canonical finite binary64 decimal strings, destination/reference identities,
and arrival tick. A save
after commitment restores the same solution rather than rerolling it. Earlier
alpha formats are rejected rather than receiving invented alignment or
assessment state.

Every current committed, target-flight, return-spool, and origin-return phase
that depends on an arrival requires the solution. Validation regenerates the
applicable local system and compares the complete destination, reference,
tick, assessment, position, and velocity before gameplay or save commit. A
cancelable outbound spool retains the launch-side craft at its phase-start
tick. A cancelable return spool retains the outbound solution so cancellation
can restore the exact target-system state; return commitment atomically
replaces it with the origin solution.

The transit image and reticle are bounded code-rendered RGBA derived only from
the semantic jump snapshot. Kitty and ANSI consume the same pixels and cockpit
text. The snapshot exposes phase, destination, progress, commitment,
cancelability, error, and quality without relying on color.
`--intersystem-jump-acceptance --report PATH` runs Assisted plus all Pilot
placements, saves and resumes a committed Pilot result, and reports
authoritative placement and framebuffer checksums separately. Its schema 3
report identifies `evidence_scope: application_framebuffer`; actual Kitty/ANSI
encoding is covered by the headless benchmark and system-navigation acceptance.

Invalid systems, mission references, out-of-range alignment, non-finite
arrival values, mistimed or unrelated controls, tick overflow, invalid
dimensions, and framebuffer-size mismatches reject transactionally before
rendering or state commit.
