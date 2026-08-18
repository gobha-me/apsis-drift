# Deterministic Assisted FTL Transit

Version 1 implements the bounded first-contract route without continuous
interstellar flight. The application owns the authoritative fixed-step state,
arrival solution, and transit rendering; TermForge only presents pixels and
semantic input events.

## Authoritative sequence

After mission acceptance, `LAUNCH` enters origin-system flight. `J` begins a
360-tick (three-second) spool and cancels it while it remains uncommitted. At
the exact boundary, destination and arrival are bound atomically. The following
240 ticks (two seconds) are irreversible transit; pause, skipped frames,
terminal capability, and a headless/no-animation run cannot change the result.

Outbound commitment resolves the mission planet ephemeris at the future
arrival tick. Assisted mode places the handoff ten planet radii behind its
velocity vector and matches the planet's system-inertial velocity. This is an
approach corridor for #86, not orbit insertion or objective placement. The
bounded return route uses `(0, -80,000,000,000, 0)` metres with zero velocity
in origin-system coordinates. It is explicitly not the station waypoint;
station rendezvous and docking remain #88.

## Persistence and presentation

Save format 4 records the immutable arrival solution as canonical finite
binary64 decimal strings together with destination/reference identities and
arrival tick. A save before commitment has no solution; a save after
commitment restores the same solution rather than rerolling it. Released
format 3 profiles remain readable and are not assigned synthetic progress.

The transit image is a bounded code-rendered RGBA field derived only from the
semantic jump snapshot. Kitty and ANSI consume the same pixels and cockpit
text. The snapshot exposes phase, destination, tick progress, commitment, and
cancelability without relying on color. `--intersystem-jump-acceptance` runs
the save/resume transit without a terminal clock and reports authoritative and
framebuffer checksums separately.

Invalid systems, mission references, non-finite arrival values, tick overflow,
invalid dimensions, and framebuffer-size mismatches reject transactionally
before rendering or state commit.
