# Canonical central-body dynamics

2026-09-30, #321, bounded provider version 1. This is one implementation slice
of [#238](https://github.com/gobha-me/apsis-drift/issues/238), using the physical
[rigid state](RIGID_BODY_STATE.md), [planet-relative handoffs](RIGID_FRAME_HANDOFF.md)
and existing [vacuum thrust/torque kernel](VACUUM_DYNAMICS.md). It has no live
consumer yet. Ordinary Freedom save17 remains docked-only; the unsaved native
flight study retains its declared lab recipes.

## Selected world and model

`evaluate_central_body_gravity(context, state, recipe)` is a pure query.
`advance_central_body_dynamics(context, state, intent, recipe, step)` advances
one atomic `kSimulationStep` (120 Hz) tick and returns actuation telemetry.
The only recipe is `CentralBodyDynamicsRecipe{1}`. Unknown versions refuse;
future persistent sessions must retain this selection and runtime assistance
alongside state. Standalone rigid v3 JSON contains state and catalog ownership,
not a dynamics recipe or last input.

The context must explicitly own a valid `PhysicalLocalSystem`; unwrapping its
embedded catalog cannot authorize this provider. State must use
`planet_relative_inertial`, name a planet in that exact catalog, and satisfy
the canonical identity, clock and numerical bounds. System, station-relative
and rotating planet-fixed state must be transformed explicitly by their owner
before this provider can consume it.

The generated planet radius `R` and surface gravity `g` are authoritative:

```
R  = radius_km * 1000
g  = surface_gravity_milli_g * 9.80665 / 1000
mu = (g * R) * R
a  = -mu * r / (dot(r,r) * sqrt(dot(r,r)))
```

`r` is the position relative to the named planet, on system-aligned nonrotating
axes. The bounded local model neglects external/tidal fields and common motion
of the body origin. It does not treat the catalog's accelerating translated
frame as exact force-free system-inertial coordinates or perform patched-conic
interbody transfers. There are no centrifugal or Coriolis terms in these
nonrotating coordinates. The selected body still moves according to its C++
ephemeris when projecting the result through a same-tick coordinate handoff.

The declared singularity bound is **one metre from the center**, inclusive.
This is a numerical domain bound, not collision, touchdown, an interior-density
model or permission to survive below the surface. A later contact owner must
handle surface interception; this provider never clamps altitude, resets pose
or creates support thrust. Position components retain the canonical maximum
of `1e15` metres; non-finite/excessive intermediate positions and singular
stages/results refuse before state replacement.

## Shared integration and propulsion truth

Gravity is evaluated at **all four stages of the existing coupled RK4 kernel**.
Each stage rotates body thrust using that stage's attitude, then composes its
linear acceleration with gravity at that stage's position. Quaternion and
world angular momentum evolve through the same torque/inertia implementation
as canonical vacuum and attitude-only consumers. A single completed candidate
is normalized/canonicalized and committed; rejected steps leave every source
bit and tick untouched.

Runtime assistance affects bounded rotational stabilization only. With neutral
translation, both assistance settings have zero applied translational channels:
gravity bends a coasting trajectory and a stationary craft descends. There is
no automatic gravity compensation, velocity braking, orbital capture or orbit
hold. Actual manual positive/negative firing channels and torque allocation
retain the vacuum provider's semantics, including opposing gross firings.

`CentralBodyActuation` separates:

- `propulsion`: actual body force/torque channels, assistance deltas and
  integrated propulsion impulses. Existing `world_*` impulse fields here use
  the named body's nonrotating owning frame.
- `initial_gravity`: selected planet, generated `R`, `g`, `mu`, initial distance
  and initial acceleration; this initial sample is not a constant acceleration
  to apply for the whole tick.
- `gravity_impulse_newton_seconds`: mass times the RK-weighted stage gravity.
- `total_linear_impulse_newton_seconds`: RK-weighted composed acceleration
  times mass, corresponding to the actual velocity change up to rounding.

Propulsion impulse is calculated directly from stage thrust, rather than by
subtracting two large environmental/total impulses. Tiny thruster contributions
therefore remain observable even when subtraction would cancel them. Gravity
never occupies a fuel/actuator channel; later propulsion accounting consumes
actual gross firings, not environmental acceleration or an assist diagnostic
charged a second time.

The old vacuum v1, explicit coasting v2 and attitude-only callers select no
gravity. Their numerical order, projection versions and existing checksum/replay
goldens remain unchanged. Ordinary vacuum still refuses planet-relative state.

## Refusal and qualification

Unsupported recipe, invalid physical owner, absent/unknown body, unsupported
frame, invalid state and initial singular radius have explicit central-body
errors. Fixed-step, tick, craft, input and integration failures preserve the
underlying `VacuumDynamicsError` detail in `dynamics_failure`; a singular RK
stage/result or unsafe arithmetic refuses through that path atomically.

`central-body-dynamics-contract` tests invalid owners, forged generated
radius/gravity, frame/identity/version/clock limits, every non-finite/signed-zero
state component and invalid actuator channel, singular initial/stage positions,
and excessive result positions before trajectory checks. Law probes cover
every planet in origin/procedural catalogs at seeds 0, 42 and maximum u64.

The seed-42 physical body's prograde and retrograde circular fixtures integrate
**900 simulated seconds** without pose resets, velocity clips or neutral thrust.
They agree with analytic phase within **0.01 m** and **1e-5 m/s**; radius stays
within **0.01 m**, specific energy within **1e-4 m²/s²**, and specific angular
momentum components within **2 m²/s** of their initial values. Radial fall,
eccentric, suborbital and escape-energy fixtures run **120 seconds** against an
independent translation RK4 oracle, within **0.001 m** and **1e-6 m/s**, retaining
the same specific-energy bound. Assistance on/off produces identical neutral
translation. These are declared bounded fixtures, not an indefinite error claim.

Six thrust directions, gross opposing channels, tiny force reporting, mixed spin
and assisted attitude are checked separately. A ten-second rotating-thrust
fixture uses analytic principal-axis rotation in its independent force oracle;
attitude/torque results must exactly match the existing attitude-only provider.
Interrupted circular/fall/eccentric/escape and mixed thrust/assist traces hydrate
standalone v3 state with the same recipe and subsequent inputs, then require
exact state bits, actuation and checksums at every continuation tick in GCC and
Clang builds.

The separate [orbital observation provider](ORBITAL_TELEMETRY.md) in #323 adds
orbital/surface motion measurements, classification and synchronous fixtures
without controlling flight. Explicit bounded `ORBIT HOLD`, atmospheric
consumption of this gravity law, actual flight saves and native presentation
remain follow-ups in #238/#291. These foundations do not close those parents.
