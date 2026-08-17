# Planetary Flight Regime Contract

Planetary flight uses one application-owned state from orbit through approach
and low-level terrain flight. The state is independent of terminal events,
render cadence, renderer selection, and terrain-cache residency. Version 1 is
an arcade flight contract intended for deterministic gameplay rather than an
orbital-mechanics integrator.

## Authoritative state and inputs

The craft stores a planet identity, geodetic position in double-precision
metres and radians, local east/north/up velocity, local heading, held controls,
flight mode, current regime, clearance, and the most recent regime transition.
Heading zero points east and positive heading turns north. Positions use the
coordinate conventions in [Coordinate and Terrain LOD Contract](COORDINATE_SYSTEM.md).

Commands retain the existing ordered, tick-addressed contract. The caller also
supplies one deterministic surface elevation at the craft's current subpoint
for each step. Generated terrain owns that sample; the flight simulation does
not observe renderer state or mutate the terrain tile cache.

## Regimes and transition bands

The three regimes are `orbital`, `atmospheric`, and `terrain-flight`. Airless
planets retain a short atmospheric-labelled approach band so every planet has
the same orbit-to-surface state-machine shape, but they do not thereby acquire
an atmosphere.

| Planet atmosphere | Descending approach ceiling |
| --- | ---: |
| Airless | 20,000 m |
| Tenuous | 60,000 m |
| Temperate | 100,000 m |
| Dense | 160,000 m |

Descending at or below the ceiling enters atmospheric flight. Ascending enters
orbit above the ceiling plus the greater of 10,000 metres or ten percent of
the ceiling. Atmospheric flight enters terrain flight at or below 2,000 metres
of surface clearance; terrain flight returns to atmospheric flight at or above
2,500 metres. Exact entry boundaries transition, while values inside each
hysteresis gap retain the current regime. At most one transition occurs per
simulation step, and its tick is the tick of the produced state.

## Bounded controls

Forward/reverse, strafe, turn, and rise/fall preserve their current meanings.
Each regime has fixed horizontal and vertical velocity, acceleration, and turn
limits:

| Regime | Horizontal speed | Vertical speed | Horizontal acceleration | Vertical acceleration | Turn rate |
| --- | ---: | ---: | ---: | ---: | ---: |
| Orbital | 4,000 m/s | 2,000 m/s | 1,000 m/s² | 1,000 m/s² | 0.35 rad/s |
| Atmospheric | 500 m/s | atmosphere-dependent | 180 m/s² | atmosphere-dependent | 0.75 rad/s |
| Terrain flight | 120 m/s | 45 m/s | 100 m/s² | 60 m/s² | 1.15 rad/s |

Atmospheric vertical speed is the greater of 180 m/s or the approach-band
height divided by 100 seconds. Vertical acceleration reaches that limit in
1.8 seconds. This preserves the existing airless response while normalizing
the much taller tenuous, temperate, and dense bands to the same playable leg;
deterministic acceptance allows no more than 120 seconds from atmosphere entry
to terrain flight.

Opposed controls cancel while remaining explicit in state. Diagonal input is
normalized to the regime's horizontal speed. Autopilot retains the established
72 percent forward and gentle right-turn intent. Velocity approaches the
commanded target by the regime acceleration bound. A newly sampled terrain rise
first reconciles the craft to the minimum 16 metre clearance and cancels
downward velocity; ordinary contact applies the same clamp after motion. A
transition clamps carried velocity to the new regime's limits before the
resulting state is committed.

## Determinism and failure behavior

Advancement applies commands, motion, clearance, tick increment, and any
transition transactionally. Non-finite state or environment values, invalid
planet properties, malformed regimes or transitions, invalid step lengths,
unknown or mistimed commands, coordinate failures, and tick overflow leave the
input state unchanged. A stable checksum covers all authoritative state,
including control holds and transition telemetry.

The current landscape flyover and orbital benchmark remain separate
presentation paths. Their integration with this state belongs to the
orbital/atmospheric/local presentation handoff.
