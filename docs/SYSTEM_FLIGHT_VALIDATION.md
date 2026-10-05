# System-flight validation cost

The system-flight API authenticates the complete catalog, target membership and
dynamic state at each public call. Guidance and bounded authoritative substeps
then borrow that target only for the duration of the call. No cached descriptor
survives to the next call, so changes to any planet or orbit are checked again.

This removes repeated whole-catalog validation from radius and ephemeris lookups.
It preserves the existing circular-orbit kernel, numerical operation order,
120 Hz substeps, approach slowdown, command checks, failure atomicity and save
versions. Public local-system queries keep their existing validation. This does
not resolve the separate host-math compatibility work in #255.

A local release comparison uses seed 42 in both procedural and origin systems.
For each scale 1/4/16, initialize tick 600 at the first planet's position plus
100 planet radii along X, with matched ephemeris velocity, forward (-1,0,0),
up (0,0,1) and autopilot. Resolve guidance and advance 1,200 host steps, retaining
all intermediate state checksums and all public guidance fields. Three original
and three candidate repetitions per compiler produce identical authoritative
paths. The observed median speedups are:

| Time scale | GCC | Clang 20 |
| --- | --- | --- |
| 1 | 6.03–6.11× | 5.83–5.95× |
| 4 | 11.01–11.33× | 10.82–11.03× |
| 16 | 20.03–20.75× | 19.27–20.25× |

These measurements describe this small local fixture, not total CI runtime,
renderer throughput or a portable performance guarantee. Existing complete
terminal acceptance matrices and their golden reports continue to run.

The native `system-flight-validation-contract` tests malformed catalogs,
non-target changes, error precedence, non-finite/bounded state, command and tick
failures, mutations between calls, orbit insertion and independent compressed
versus single-step replay. The final tests pass against both original and
candidate implementations; full publication checks are recorded in #450.
