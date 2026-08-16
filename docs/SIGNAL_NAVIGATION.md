# Signal Navigation Contract

The v0.4 scanner derives cockpit navigation from authoritative planetary
flight state and the immutable surface-signal catalog. Selection, navigation,
and presentation do not modify the generated signal recipe and do not create
discovery, mission, scan-progress, or collection state.

## Selection and visibility

The scanner stores the selected `SurfaceSignalId`, not a mutable catalog index.
Next and previous commands traverse the versioned catalog order and wrap at
both ends. In the terminal cockpit, Tab selects the next target and Shift-Tab
selects the previous target. Only semantic key-press events change selection;
repeat and release events are ignored.

Navigation resolves the selected signal's generated approach altitude into
planet-fixed space. It measures straight-line distance, computes the initial
east/north bearing in the craft's local tangent frame, and normalizes the
heading-relative bearing to `[-pi, pi)`. The generated strength is presented
as a percentage, while kind, reward, canonical ID, and later discovery data
remain absent from the cockpit readout.

Status precedence is:

1. invalid input is returned as an error without changing scanner state;
2. an empty selection reports `NO SIGNAL`;
3. a target within 1,000 metres reports `REACHED`;
4. a target beyond 2,000 kilometres reports `OUT RANGE`;
5. a nearer target below the reference-sphere horizon reports `OCCLUDED`;
6. every other selected target reports `TRACKING`.

The reference-sphere test is deliberately independent of terrain cache state.
Generated terrain still owns the approach altitude and the flight environment;
terrain-scale line-of-sight obstruction is not part of this first scanner.

## Cockpit presentation

Both Kitty and truecolor ANSI paths use the same cell-native fixed-width
scanner lines: target slot, absolute bearing, distance, strength, and a textual
cue. `TURN LEFT`, `TURN RGHT`, `AHEAD`, `OUT RANGE`, `OCCLUDED`, and `REACHED`
keep direction and state understandable without color.

## Deterministic acceptance

The `v0.4-signal-navigation` scenario uses planet seed `42`, selects catalog
ordinal `0` at tick `0`, and starts two kilometres west of its generated
approach point. One `press forward` command drives the fixed 120 Hz planetary
simulation until the scanner first reports reached at tick `1072`.

Run the two supported presentations with:

```bash
./build/apsis-drift --signal-navigation-acceptance \
  --driver ansi --profile remote --report signal-navigation-ansi.json
./build/apsis-drift --signal-navigation-acceptance \
  --driver kitty --profile local --report signal-navigation-kitty.json
```

Both reports must retain target `signal-945eaa623b2b8497`, reached tick `1072`,
and final flight checksum `9743322914782455886`. Framebuffer checksums and
timings remain presentation diagnostics and cannot affect selection or flight.
