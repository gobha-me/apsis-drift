# Numerical compatibility finding — 2026-09-18

The native frontend evaluation exposed a pre-existing **host math-library
dependency in authoritative floating-point flight state**. Do not fix this by
replacing reference checksums with the current machine's answers.

## Selected contract — issue #255, 2026-10-27

Three guarantees are distinct:

1. **Cadence independence:** identical commands at identical simulation ticks
   produce equal complete states regardless of presentation cadence.
2. **Same-environment replay:** a recorded numerical profile, initial state and
   command stream reproduce state in a supported build/math environment.
3. **Historical cross-environment compatibility:** the unchanged planetary replay
   golden `5033951390750856009` must pass in each qualified environment. Cadence
   equality alone does not satisfy this requirement.

The selected implementation is `FlightNumericalProfile::controlled_hypot_v1`
(value `1`). Authoritative planetary flight explicitly dispatches its
**two-argument binary64 hypot** through `flight_numerics::hypot`. This owns the
non-FMA kernel adapted from glibc **2.41** `sysdeps/ieee754/dbl-64/e_hypot.c`.
The source, LGPL-2.1-or-later notice and complete license texts are retained;
see [third-party notices](../THIRD_PARTY_NOTICES.md). There is no preload,
symbol interposition, runtime CPU math dispatch or replacement of `std::hypot`.
The kernel's arithmetic evaluation order is part of profile 1. Changing it,
including selecting the upstream FMA kernel, requires another numerical profile
and qualification against the historical fixtures.

The profile follows the complete planetary state, including thermal rates,
velocity limiting and target horizontal velocity, and is passed explicitly into
geodetic reconstruction and tangent-frame construction. Signal navigation,
rendezvous construction and system/planet flight handoffs carry the same
selection. Shared coordinate APIs default to `legacy_host` (value `0`), so
Freedom's existing physical/rigid save families and Godot presentation helpers
retain their existing arithmetic. Integer generation versions and seeds do not
change. This is a bounded planetary-flight numerical contract, not a new
whole-game physics version.

### Build and execution environment

The qualified target is baseline x86-64 IEEE-754 binary64 with SSE evaluation,
round-to-nearest/ties-to-even, gradual underflow and masked floating-point
exceptions. CMake fixes `-fno-fast-math -ffp-contract=off` for the controlled
kernel and the planetary flight/coordinate/navigation translation units.
Do not add fast math, reassociation, excess precision, flush-to-zero,
denormals-are-zero, alternate rounding, or architecture-specific FMA variants
when claiming profile-1 compatibility. The kernel statically checks the
binary64 representation and evaluation precision. A profile number does not
certify an arbitrary compiler or architecture; qualify a new environment by
running the unchanged golden and cadence checks with both GCC and Clang.

Only two-argument hypot is controlled here. Three-argument `std::hypot`, square
root and the other transcendentals retain the supported environment's existing
implementation. This removes the demonstrated glibc hypot dependency, without
claiming universal transcendental portability. CI provenance fingerprints remain
useful comparison evidence and never substitute for state/golden checks.

### Save and continuation policy

- `legacy_host` preserves the historical call to host `std::hypot`. Format **16**
  records this implicitly by its envelope; decoding selects profile 0 even in a
  new build. Re-encoding retains format16 and does not add a profile field.
  Continuation remains dependent on the original math environment. Profile 0
  cannot identify or recreate a missing historical host library; reproducing
  such a save requires its original environment. Loading never changes its
  numerical state or upgrades its arithmetic.
- Newly created career games and newly initialized planetary flights select
  profile **1**. Career saves use format **65**, with required unsigned JSON
  `numerical_profile: 1` at the root. The in-memory recipe retains the selection
  while docked or in system flight, and newly entered planetary flight inherits
  it. Profile-0 documents cannot contain profile-1 active flight or vice versa.
  New Signal Run scenario documents also select profile 1; “legacy” in that
  factory name describes the scenario. Aggregate and raw recipe defaults remain
  profile 0 for historical and Freedom identity callers.
- Format65 rejects missing, malformed, zero and unknown profiles. Format16 rejects
  an explicit root profile field. Earlier builds reject format65 as unsupported;
  do not relabel it format16. No automatic migration, quantization, seed rewrite,
  source-file rewrite or continuation under a substituted profile is allowed.
  Future upgrades must retain the old dispatch and use an explicit new envelope
  or migration operation with separately qualified continuation semantics.
- Historical `planetary_flight_state_checksum` remains the unchanged digest of
  physical state. Its historical byte recipe excludes the new profile metadata
  to preserve goldens. Replay identity is the **profile plus state digest**, not
  the digest alone; complete-state equality includes the profile. A matching
  digest does not authorize resuming with a different profile.

The physical Freedom save families have their own explicit model/version
policies. Format65 does not migrate those families, add missions, or change the
Freedom starter route.

### Invalid and non-finite boundaries

For profile 1, the scalar helper returns positive infinity if either input is
infinite (including infinity paired with NaN), otherwise a quiet NaN for a NaN
input. Both signed zeros produce positive zero. Finite inputs are scaled to
avoid intermediate overflow/underflow; unrepresentable lengths return positive
infinity and subnormal results retain gradual underflow. Signaling NaNs are
classified from their encoding before any FP comparison and return the defined
quiet NaN or infinity result. NaN payloads, errno and
floating-point exception flags are outside this API's contract. Profile 0 uses
the host library's NaN/Inf semantics. Unknown scalar profiles return quiet NaN.

Coordinate entry points reject unknown profiles and non-finite coordinates with
`CoordinateError`. Planetary state validation rejects unknown profiles and
non-finite state; rejected advances leave the original state untouched. Save
validation rejects invalid or mismatched profiles before encoding or hydration.
The scalar helper's NaN/Inf handling never makes non-finite flight state valid.

## Historical reproduction and isolation

The new focused diagnostic runs the existing planetary descent/ascent fixture:

```sh
build/apsis-drift-tests --planetary-replay
```

With the then-current system math library and legacy host arithmetic, both GCC
and Clang produced:

| Execution | Final checksum |
| --- | --- |
| Direct 120 Hz simulation | `5619521665882378860` |
| Same commands scheduled through 30 FPS presentation | `5619521665882378860` |
| Same commands scheduled through 60 FPS presentation | `5619521665882378860` |
| Existing reference | `5033951390750856009` |

Before the production change above, the three runs had **equal complete states**,
not merely equal hashes. The old test mixed cadence independence and historical compatibility into one assertion,
so one numerical mismatch appeared to violate both. Those checks are now
separate; this table records the original failure, not profile-1 results.

Recompiling the planetary flight implementation from immediately before the
fixed-step enforcement change (`f6d2e8d^`) gives the same current mismatch.
Disabling runtime CPU math dispatch features also leaves it unchanged.

An isolated diagnostic shim loaded the math library from the official
[Debian glibc 2.41 package](https://packages.debian.org/trixie/amd64/libc6), without
installing it or replacing any system files. Substituting **only the two-argument
`hypot` function** restores the exact original reference under both compilers.
Individually substituting `sin`, `cos`, `sincos`, `asin`, `acos`, `sqrt`, `atan2`,
`fmod` or `remainder` does not. The application uses `std::hypot` for velocity
lengths/limits and horizontal position length before latitude reconstruction.
Tiny differences therefore enter the state that the checksum hashes bitwise.

With only that diagnostic substitution, the unchanged intersystem-planetfall,
intersystem-contract, planetfall and signal-collection acceptance suites all
pass as well. The longer origin-system, signal-run and onboarding suites were
not rerun under the substitution; their entire cause is not established here.

This establishes a math-library compatibility dependency for the measured
fixture, not a claim that every transcendental or every architecture is portable.
The diagnostic compares library implementations; it does not establish which
result is more mathematically accurate. No generator, save version or production
flight arithmetic was changed, and no preload is used by the native launcher.

## Implications

- Same-build replay and independence from presentation cadence work for this
  fixture. Historical cross-environment bitwise compatibility does not.
- Integer terrain generation is a separate contract; it must not be conflated
  with this floating-point flight result.
- The existing save stores state explicitly, but continuation of a replay can
  diverge in its last bits when the math environment changes. Whole-game
  persistence compatibility has not been proven by this test.
- A production solution needs an explicit, tested numerical contract: controlled
  math implementation/build settings and any necessary simulation-version
  transition. Quantizing hashes alone would hide the state difference.
- Do not ship a host-specific `LD_PRELOAD` workaround, weaken goldens, or silently
  rewrite existing saved/generated identities to get a green test run.

Local diagnostic sources, the isolated library and historical object are retained
under ignored `build-godot/math-probe/` and `build-godot/`. They are development
evidence, not new redistributable game dependencies.

## CI compiler and math-library evidence

The existing GCC/Clang build and native-core jobs retain small
`ctest-math-*` artifacts for seven days. Each records the tested commit,
run/attempt, job label, CTest step outcome and `LastTest.log` hash, configured
compiler/build options, representative vacuum-dynamics test binary hash, and
resolved libc/libm hashes and GNU build IDs when available. Missing builds,
tools and dependencies are reported as unavailable; a canceled runner may
produce no artifact. The existing tests and their failure results remain primary.
Artifact names include the attempt so a job retry retains earlier evidence.
On pull requests, `ctest-math-*` artifacts are absent when C++ jobs are
path-skipped and record a skipped CTest outcome for no-RtAudio PR builds;
dispatch and nightly runs retain full artifacts. Full CI runs nightly at
07:17 UTC and on manual dispatch; pushes to main do not trigger CI.

`tools/ctest_math_provenance.py` inspects the already-built executable without
running its tests. Loader dependency resolution does not establish which math
functions or IFUNC implementations executed. Configured flags are cache values,
not the complete options for every translation unit. These fingerprints help
compare CI environments; they do not themselves select an implementation or
establish historical/save-continuation compatibility. The selected issue
[#255](https://github.com/gobha-me/apsis-drift/issues/255) contract is specified
above.
