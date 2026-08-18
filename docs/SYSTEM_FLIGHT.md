# Deterministic Sub-light System Flight

Version 1 owns the mutable craft state between a committed FTL arrival and the
existing target-planet orbital path. It remains application simulation: terminal
capabilities, render cadence, and skipped presentation frames never enter its
state or checksum.

## State and fixed-step motion

`SystemFlightState` records the authoritative universe tick, system and target
IDs, system-inertial position and velocity, a finite camera/attitude basis,
semantic controls, flight mode, and the selected time scale. The outbound state
is copied exactly from the immutable intersystem arrival solution. Releasing thrust
preserves momentum.

Forward/reverse thrust is bounded to 50 km/s², maneuver thrust to 25 km/s²,
turning to 0.75 radians/s, and target-relative speed to 1,000 km/s. Assisted
target hold is a direct intercept/braking controller for the bound mission
planet, not a general route planner.

Time compression has only three saved values: 1x, 4x, and 16x. Each host step
executes that many ordinary authoritative 120 Hz substeps rather than one large
integration step. Crossing six planet radii forces 1x before the next step so
the approach and insertion boundary cannot be skipped.

## Guidance and insertion

Every substep resolves the selected planet's analytic position and velocity at
the authoritative tick. Guidance derives range, signed closing speed, relative
speed, ETA when closing, stopping distance, and an explicit hold/closing/opening/
brake/orbit-ready cue.

Orbit insertion is an explicit Enter action. It is available at or inside three
planet radii when target-relative speed is at most 4 km/s and absolute radial
speed is at most 250 m/s. Refused insertion does not mutate either travel state.
Overshoot remains ordinary system flight and can be recovered by turning and
braking.

The handoff samples the planet center at the same tick, applies a deterministic
24-hour planet-frame rotation derived from stable planet identity, and converts
relative position and velocity into geodetic and local east/north/up values.
The resulting `PlanetaryFlightState` retains the arrival side, altitude,
velocity, and heading; it never moves the craft above the mission objective.

The v0.4.12 reverse handoff accepts only a valid orbital planetary state. It
applies the inverse planet spin and tangent-frame transform at the same
authoritative tick, restores a system-inertial craft state targeting the same
planet, and permits later re-entry without changing mission or world-delta
identity.

## Persistence and verification

The system-flight projection introduced by save format 5 records finite
binary64 values as canonical decimal strings and
requires exactly one matching system-flight state during target-system flight.
A released format-4 save already at the target initializes this state from its
immutable arrival solution. Formats 1–3 retain their existing migrations.

Run the cadence, save/resume, rendering, and insertion acceptance on both
supported presentation paths:

```sh
./build/apsis-drift --system-flight-acceptance --driver kitty \
  --profile remote --report system-flight-kitty.json \
  --snapshot system-flight-kitty.ppm
./build/apsis-drift --system-flight-acceptance --driver ansi \
  --profile remote --report system-flight-ansi.json \
  --snapshot system-flight-ansi.ppm
```

The report keeps authoritative system/orbital checksums separate from the
application framebuffer checksum. Terminal or proxy throughput requires a
separate live measurement and is never inferred from this acceptance run.
