# Orbit means the same thing in both control modes

2026-09-19; approved pilot amendment, #272. This supersedes the native lab's
older advice to disable assist before orbital coasting. It does not redefine
orbit as geostationary positioning or introduce capture rails.

## Player contract

An established orbit is a bound, clear path around the planet. With propulsion
idle, both assist and manual must allow orbital coasting. Turning the nose is
not itself a translational burn. In space, assist stabilizes rotation; manual
allows released-axis rotational coasting. Direct propulsion commands in either
mode still change the trajectory. The current numerical model is approximate,
not a promise of infinite exact conservation.

All orbit readouts share the authoritative C++ `bound_orbit` / `clear_orbit`
classification, not the assist switch or the optional reference circle:

- **NOT IN ORBIT:** the current path is not both bound and clear.
- **ORBIT ESTABLISHED:** the current path is bound and its lowest predicted
  altitude is above the atmosphere boundary, or 20 km datum altitude if that
  is higher. The guide says “Coast when ready.”
- **ORBIT AT RISK:** that condition was previously established during this
  practice session and is no longer met. The reason identifies a low path or
  escape path, not an invisible capture/release threshold.

The remembered prior-orbit flag is UI history only; it cannot change motion,
forces or simulation time. Reset and practice relocation clear that history.
Clearing optional guidance does not clear it. Critical label transitions force
a NAV refresh rather than waiting for the normal 10 Hz instrument update.
“Horizon speed” means travel along the local horizon, not sideways relative to
the ship's nose. Current coast remains green; the optional ideal reference is
blue and is never evidence that the actual ship achieved orbit.

## Versioned native physics change

Native startup now selects **thrust-lab-3**: angular model 2 plus translation
policy 2. `enable_orbit_practice()` initializes the same surveyed surface start
and then explicitly selects that policy. Existing `enable_thrust_flight()`
(lab 1) and `enable_surface_practice()` (lab 2) retain their old semantics and
checksums. The new policy adds a checksum tag; historical fixture values are
not relabeled or overwritten. Supported saved career/generation formats are
unchanged; this remains an unsaved native practice session.

Policy 2 removes automatic gravity cancellation and lateral/vertical velocity
damping in space. It does not remove gravity, clamp velocity, inject orbital
speed or suppress a requested main/retro/translation burn. Angular assistance
still uses bounded physical torque through the existing native attitude kernel.

Atmospheric support smoothly fades using a clamped smoothstep in raw density:
zero at `1e-6 kg/m³`, full at `min(sea-level density, 0.001 kg/m³)`. There is no
velocity/orbit-dependent switch. The same weight tapers aerodynamic density
for policy 2, regardless of assist setting. At or beyond the lab's atmosphere
edge, effective drag is exactly zero; there is no orientation-dependent
exponential density tail silently braking a craft labeled as in space.
This fade is explicit game tuning, not a validated physical atmosphere model.

Raw density remains environment telemetry. `effective_air_density` reports the
force model's density, and dynamic pressure uses the effective value.
`translation_assist_weight` is the available automatic-support envelope,
independent of the assist toggle; actual force still requires assist ON.
The propulsion panel reads “ON / ROTATION” where this weight is zero.

Airless bodies have no automatic hover support under this policy, at any
altitude. A future explicit assisted landing action may support the craft with
available thrusters, but must not masquerade as ordinary orbital assistance.
The existing floor guard remains a test safeguard, not landing or collision.

## Verification and boundaries

The new C++ contract covers malformed inputs, transactional policy selection,
smooth boundary behavior, direct thrust, airless freefall and exact agreement
of assisted/manual vacuum translation while their angular states differ.
It checks both a 600-second coast and a complete orbit just above the safe
boundary, with explicit numerical drift and periapsis margins. Legacy initial
and mixed-input checksums remain asserted separately.

The new native bridge contract consumes ordinary descriptor-generated seed-42
and airless seed-4 snapshots. It covers model identity, reset/relocation,
30/60/144 Hz scheduling, zero automatic vacuum translation/drag, commanded
burns, atmospheric boundaries and read-only guidance. Presentation tests cover
identical labels across modes, prior-orbit history, reset, invalid telemetry
and contrast with misleading reference cues. Native GPU and actual controller
feel are separate verification layers, not inferred from headless tests.

Canonical vacuum-provider adoption must preserve this player contract when
replacing the lab translation path; the provider's older combined-assist
option must not simply be wired through and reintroduce orbital braking.
Fuel, terrain contact, physical damage, automatic orbit insertion and assisted
landing are not implemented by this change.

### Recorded local evidence

- GCC and Clang builds and all nine targeted native contracts passed.
- A complete 495,864-tick orbit (4,132.19 seconds) retained exact assisted/manual
  translation agreement: maximum radial error 35.65 m and minimum predicted
  periapsis margin 4,928.7 m above the safe boundary.
- Both compiled bridges passed the new headless contract with identical
  30/60/144 Hz cadence checksum `lab3:6497804308686322030`.
- Status and main/bridge integration suites passed. The new physics test passed
  AddressSanitizer/UndefinedBehaviorSanitizer with the test/header code
  instrumented; linked support libraries were not rebuilt with sanitizers.
- Native GPU controller smoke and five 1920×1080 visual review captures passed.
  These captures use explicit practice relocations, not a player-flown orbit.
- Pinned formatting and suppression checks passed. Clang-tidy 20 is a hosted CI
  gate, unavailable locally. These focused results do not claim the known
  historical host-math full-suite discrepancy (#255) has been resolved.

Pilot acceptance remains separate: establish a clear orbit, release propulsion,
toggle assist and verify the labels and hands-off motion are understandable.
