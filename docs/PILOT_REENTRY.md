# Pilot Thermal Reentry

The v0.4.17 thermal path makes atmospheric entry meaningfully different under
the existing `ASSISTED` and `PILOT` rule profiles without adding damage, fuel,
or generated-world mutation. Terrain generation, flight integration, and the
consequence remain application-owned; terminal presentation consumes only a
derived readout.

## Authoritative model

Every planetary flight carries `load_units` in the closed interval
`[0, 1,000,000]` and one `abort_latched` bit. One million units is the 100%
limit. For each fixed simulation step, the integrator computes:

- normalized density from generated surface pressure divided by 2,500 mbar,
  multiplied by squared atmosphere depth;
- heating from normalized density, the cube of total speed relative to
  1,000 m/s, and a factor that increases with downward velocity;
- cooling from current load, with a 1.5% per-second baseline and up to a 6%
  per-second vacuum bonus.

Net load is integrated over the fixed step, rounded to fixed-point units, and
clamped to the valid interval. Airless planets have zero density and therefore
cannot add heat. Invalid load, non-finite state, malformed planet data, or an
invalid step rejects the whole flight advancement without mutation.

Both profiles accumulate and cool the same state. Assisted uses the 100% limit
as information only. Pilot latches at the limit and overrides only the vertical
descent intent with a bounded climb; horizontal controls remain available. On
entering the orbital regime, the latch clears, held descent is released, and
downward velocity is cancelled. The craft continues cooling and can make a
deliberate second entry.

## Cockpit guidance

The cockpit derives four fixed-width, text-complete instruments from the
authoritative state and current velocity:

- `HEAT nnn%` shows the bounded load;
- `TEMP +`, `TEMP =`, or `TEMP -` shows heating, steady, or cooling trend;
- `LIM 100%` states the limit explicitly;
- `FPA +nn` or `FPA -nn` shows signed flight-path angle in degrees.

`HEAT OK`, `SLOW+RISE`, `COOLING`, and `ABRT CLMB` communicate the required
action without relying on color. Percentage, trend, angle, and cue are derived
presentation and are not saved.

## Save compatibility

Save format 16 requires a `thermal` object in every encoded planetary flight
and preserves both fixed-point load and the Pilot abort latch exactly. Formats
1 through 10 are rejected by the current alpha boundary rather than receiving
invented thermal history. Assisted intersystem state and the bounded Signal
Run state reject a latched abort, preventing a Pilot-only consequence from
crossing rule-profile boundaries.

## Deterministic acceptance

`--intersystem-planetfall-acceptance` now reports schema 3 and scenario
`v0.4.17-pilot-thermal-reentry`. Its generated dense-atmosphere thermal fixture
uses universe seed `39` and `planet-237709a6a1fd198b`. It proves these
canonical values under GCC and Clang:

| Measurement | Value |
| --- | ---: |
| Nominal peak load | 374 units |
| Shallow-entry peak load | 58,770 units |
| Manual-correction peak load | 35,130 units |
| Assisted peak load | 1,000,000 units, no latch |
| Pilot forced-abort tick | 803 |
| Pilot recovery-to-orbit tick | 11,764 |
| Deliberate reentry tick | 13,108 |
| Save/resumed recovery checksum | `12793732928174323102` |

The same matrix retains the correct-side, early, and opposite-side Planetfall
routes, explicit orbital abort route, objective completion, and application
framebuffer diagnostics. Unit coverage separately proves
speed, descent angle, and pressure monotonicity; airless cooling; invalid-state
transactionality; exact format-16 save round trips; and unsupported-alpha
format rejection.
