# Numerical compatibility finding — 2026-09-18

The native frontend evaluation exposed a pre-existing **host math-library
dependency in authoritative floating-point flight state**. Do not fix this by
replacing reference checksums with the current machine's answers.

## Reproduction and isolation

The new focused diagnostic runs the existing planetary descent/ascent fixture:

```sh
build/apsis-drift-tests --planetary-replay
```

With the current system math library, both GCC and Clang produce:

| Execution | Final checksum |
| --- | --- |
| Direct 120 Hz simulation | `5619521665882378860` |
| Same commands scheduled through 30 FPS presentation | `5619521665882378860` |
| Same commands scheduled through 60 FPS presentation | `5619521665882378860` |
| Existing reference | `5033951390750856009` |

The three runs have **equal complete states**, not merely equal hashes. The old
test mixed cadence independence and historical compatibility into one assertion,
so one numerical mismatch appeared to violate both. Those checks are now
separate; the unchanged golden still fails.

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
