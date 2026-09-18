# Native checkpoint 12: location is not trajectory

2026-09-18. Local development checkpoint, not a release. This collects the
approved Freedom/Godot studies, original editable visual assets and export
tiers, preview, controller/thrust lab, orbital practice, instruments/stars and
terrain continuity work. Source masters and historical evidence are not a
minimal runtime distribution. No engine binary, build cache, credentials or
private access configuration belongs in this checkpoint.

## Flight status

Exterior and NAV now share one read-only status formatter:

- **Location:** named planet; near surface, atmospheric flight, upper atmosphere
  with trace air, or space. Airless worlds are explicit.
- **Height:** altitude above the reference sphere, separately from clearance
  above sampled terrain, with metre/kilometre units.
- **Trajectory:** suborbital return path, coasting orbit, maneuvering orbit,
  orbit with assist active, or escape trajectory. Altitude alone never implies
  orbit, and firing thrusters is not called coasting.

The atmosphere thins continuously. UI thresholds are authored labels:
0.001 kg/m³ separates ordinary/trace atmosphere; 0.000001 kg/m³ marks the modeled
space boundary. These are not universal physical definitions. The NAV footer
shows that planet's modeled air-edge altitude. A black sky can coexist with
trace atmosphere. Orbit/return/escape labels are instantaneous coast forecasts,
not autopilot promises; thrust, gravity assist and drag affect what follows.

No control bindings or flight forces were changed. The clean cockpit remains
free of a permanent overlay; look down at NAV for the same information.

## Verification and handoff

`flight_status_test.gd` covers missing/nonfinite telemetry, exact atmosphere
thresholds, airless planets, space without orbit, orbit under thrust/assist,
escape and unit formatting. Run it with:

```sh
godot --headless --path experiments/godot-freedom --script res://flight_status_test.gd
```

The native presentation smoke test passed with zero failures, followed by direct
inspection of cockpit NAV and exterior orbital frames. Earlier GCC/Clang native
contracts and [terrain review 11](TERRAIN_CONTINUITY_11.md) remain relevant.
Full-suite results must be read alongside the unresolved
[historical numerical compatibility finding](NUMERICAL_COMPATIBILITY_FINDING.md);
the checkpoint does not rewrite goldens or claim a fully green legacy suite.

Checkpoint build/test results: GCC 97/105 passed; Clang 95/103 passed. In each
run, seven legacy test targets failed (core tests, intersystem planetfall,
intersystem contract, origin-system contract, planetfall, signal collection,
signal run). The core target reports eight failed assertions. The remaining
onboarding target was deliberately interrupted after roughly five minutes of
the overall run and is **unverified**, not an additional demonstrated regression.
CTest counts that interruption as its eighth failed target. All five native
C++ contracts and both asset-manifest/provenance targets passed in both builds.
The different suite sizes reflect build configuration, not skipped failures.

Before checking in the visual bundle, private source-path metadata was removed from
22 historical Blender PNGs. Decoded pixel signatures were compared before/after
and matched. Provenance records retain the edit; original visual content and
editable masters are preserved. Regenerated Blender previews should undergo
the same metadata review before publication.

Next review: whether the location/trajectory distinction is understandable
during ascent and re-entry. Next implementation remains terrain transition
refinement and physical consequences, not an economy or mandatory missions.
Landing/collision, local damage/fractures, propellant and native ship audio
remain unfinished. No release or remote publication is implied by this commit.
