# Cockpit control clearance 07

2026-09-18. Follow-up to the FLIGHT/JUMP pods obscuring the lower instrument
displays. Flight-cell geometry remains revision 5; the shared contract records
control-layout revision 2. This is an asset refinement, not a new flight model
or functioning instrument simulation.

The pods are 110 mm lower and 90 mm closer to the pilot, with their faces tilted
upward approximately 63 degrees from vertical instead of 26 degrees. Their
risers and guarded primary buttons move with them. Grip locations, display
positions/sizes, seat fixtures, pilot eye, canopy and exterior mount are unchanged.

Native 4K inspection:

- [1.60 m fixture — clear displays](media/freedom-cockpit-clear-small-v7.png)
- [1.78 m fixture — clear displays](media/freedom-cockpit-clear-medium-v7.png)
- [1.96 m fixture — clear displays](media/freedom-cockpit-clear-tall-v7.png)
- [Side cutaway — lowered pods and seated reach](media/freedom-cockpit-controls-side-v7.png)

The new Blender regression samples 17 × 11 points on each of three active
display areas from each of three adjusted eye positions: 1,683 sightline rays.
It fails on the previous master at the small fixture's left display, hitting
the FLIGHT pod. The revised master passes all samples. This is sampled coverage,
not a mathematical proof of every pixel or every possible body/head position.

Existing forward-window, helmet, sampled leg/equipment, floor and forward-deck
checks pass, with the pod risers added to leg-clearance checks. Reach remains
within the synthetic fit-fixture limits; these are not certified anthropometric
measurements. Exported interior/exterior glazing still matches within 0.02 mm.
All four cockpit GLB tiers pass finite/buffer/budget checks; triangle counts are
unchanged. Other ship/station assets and historical PNG/film reviews are retained.

Reproduce the relevant geometry checks:

```sh
python tools/flight_cell.py
blender -b --factory-startup --python-exit-code 1 --python tools/verify_flight_cell.py
python tools/verify_hero_assets.py
```

The [controller foundation](CONTROLLER_FOUNDATION_06.md) now supports
`--start-paused=true`, opening its settings screen before flight so a controller
can be connected and configured safely. A synthetic streamed-flight smoke
checks this startup mode; physical-device feel still needs a human playtest.
