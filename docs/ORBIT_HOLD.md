# Explicit bounded orbit hold

2026-09-30, #325, controller version 1. This is an optional C++ control layer
over [canonical central-body dynamics](CENTRAL_BODY_DYNAMICS.md) and the shared
[orbital observation](ORBITAL_TELEMETRY.md). The
[saved C++ flight session](NATIVE_FLIGHT_SESSION.md) consumes its pure correction
plan. The [saved native controls](SAVED_NATIVE_FLIGHT.md) now expose an explicit
current-radius/current-angular-momentum-plane selector through that session;
the pure provider still requires a caller-selected target. Ordinary assisted neutral
spaceflight continues to coast;
hold acts only when the caller supplies an explicit target.

## Target and observable operating state

`OrbitHoldRequest{1, target}` selects a named physical planet, finite target
radius and unit plane normal on that body's nonrotating axes. The normal's sign
selects the orbital direction. The provider never infers a plane from a camera,
invented heading or zero angular momentum. An empty optional target disables
hold. Format18 retains the request/version, target, selected gravity and physical
rotation and runtime assistance; qualified atmosphere derives its space boundary.
There is no hidden integral, filter history, rail state or captured velocity.

The target must name the state's qualified planet, lie above the supplied space
boundary and have radius no greater than `1e15` metres. Its normal must be finite,
have squared norm within `1e-12` of one, and contain canonical zero components.
Both signs of a nonzero normal are valid different orbit directions.

| Status | Actual behavior |
| --- | --- |
| `disabled` | Empty target; ordinary central-body flight |
| `paused_advanced` | Assistance off; ordinary unassisted flight, target retained by caller |
| `paused_manual_translation` | Any direct positive/negative translation channel takes priority, including opposing firings |
| `unavailable_environment` | Current radius intersects the declared space boundary; hold contributes no thrust |
| `unavailable_geometry` | Current position projects too near the selected orbital plane's pole to define a tangent |
| `active` | Requested correction fits directional actuator authority |
| `saturated` | Requested correction exceeds authority; actual channels are capped |

Valid paused/unavailable states **continue integrating under gravity**. They do
not freeze the ship, clamp altitude, turn off commanded propulsion or teleport
to the target. Rotational stabilization retains the existing assistance behavior.
Invalid targets, versions, policies, owners and numerics refuse the whole step
without replacing any source bit. Invalid actuator inputs remain errors, even
when a manual input would pause hold.

## Version-one correction law

For target radius `Rt`, unit plane normal `n`, position `r`, velocity `v` and
the selected generated `mu`, project the current position onto the plane:

```
rp = r - n * dot(r,n)
u = rp / length(rp)
target position = Rt * u
target velocity = cross(n,u) * sqrt(mu/Rt)
feedforward acceleration = -mu * u / (Rt*Rt)
desired acceleration = feedforward
                     + 0.0004 * (target position - r)
                     + 0.04 * (target velocity - v)
requested thruster acceleration = desired acceleration - canonical gravity(r)
```

Position gain is `0.0004 s^-2`; velocity gain is `0.04 s^-1`. These values are
versioned controller behavior, not ship upgrades. A projected length no greater
than `1e-6` times current radius makes the geometry unavailable; there is no
arbitrary tangent fallback. The law tracks a circular reference in the explicitly
chosen plane with freely evolving phase. It is not an instantaneous orbit maker
or a promise to recover every powered/unpowered trajectory.

The feedforward describes the acceleration needed for the reference circular
motion. Subtracting the same canonical gravity determines only the thruster
correction. The gravity provider still evaluates actual gravity at every RK4
stage. At circular equilibrium the correction is only numerical residual;
at zero velocity, insufficient authority still permits gravitational descent.

Mass times requested acceleration is projected through the current attitude
into body axes. Each axis maps to positive or negative physical thruster
fractions, capped against the immutable craft's corresponding directional force
rating. The controller's `saturated` status reports the original demand exceeding
those ratings. The central actuation report receives already allocated fractions;
its own allocator flags retain their existing meaning.

These actual channels go through the **existing coupled RK4 thrust/torque kernel**.
Stage attitudes determine actual force direction while the ship rotates; the
controller does not inject a separate ideal world force or bypass torque/inertia.
There is no pose/velocity reset, speed clamp, gravity-free hover or rail capture.

## Report and atomic commit

`advance_orbit_hold_dynamics` returns the operating status, requested correction
acceleration, requested/applied body force, the complete central-body actuation
and the shared orbital observation **after** the completed tick.
`orbit_established` is exactly that observation's `STABLE` classification,
regardless of hold mode; it does not claim arrival at the requested reference.

Requested/applied hold force diagnostics are separate from manual input. During
a paused mode they are zero; the central actuation still reports the pilot's
actual propulsion. During active/saturated hold, central propulsion channels
and impulses contain the correction once. The later fuel owner charges actual
gross channels, not gravity, an ideal requested acceleration or an additional
assist diagnostic on top of the allocated channels.

Integration and post-step observation operate on a candidate. Only a complete,
validated result replaces the caller's state. Even failed post-step observation
leaves the source unchanged. No input/render/wall-clock cadence enters the law.

## Qualification

`orbit-hold-contract` checks invalid target radius/plane/body/version, invalid
policy/gravity/rotation owner, malformed actuator/state scalars and clock/step
limits before trajectories. Disabled, Advanced, manual translation and valid
unavailable cases require exact parity with ordinary central-body flight.
Attitude/torque results require exact parity with the existing attitude kernel.

The seed-42 physical origin fixture qualifies positive/negative plane directions
at circular equilibrium over **300 simulated seconds**, within **0.01 m** of
analytic phase/radius and **0.01 N** peak numerical correction. A separate
insufficient-authority fixture begins stationary: requested tangential force
exceeds real channels, saturation remains visible, velocity changes only through
bounded integration, and the ship descends rather than being captured.

A **900-second** perturbation starts 2 km radially displaced, 200 m out of the
plane, with 20 m/s radial motion, a 1% tangential speed deficit and 5 m/s plane
motion. The declared acceptance bounds are **1 m** radius error, **0.01 m/s**
radial rate, **0.1 m** plane departure and eccentricity below **1e-5**, with
clear bound classification and recorded nonzero propulsion.
GCC and Clang measured approximately **0.000814 m**, **-0.0000152 m/s**,
**0.000127 m** and **2.16e-9** respectively. This fixture used no saturated
ticks; its accumulated norm of net thruster impulse was about **740,444 N·s**.
That last measurement is neither a gross propellant budget nor a fuel conversion.

Mixed Advanced/manual/rotation traces retain target and all selected recipes,
hydrate standalone rigid v3 state and require identical subsequent state,
correction, impulses and observations. The new explicit hold-v1 replay checksum
is **11305337061246386878**, agreed by GCC and Clang. Earlier central-body,
vacuum, attitude and rigid projection goldens are unchanged.

Actual native flight saves/controls, physical contact and landing remain work
in #238/#291/#245. This bounded controller alone
does not complete the Freedom journey or adopt the old unsaved lab as a save.

[Canonical atmospheric flight](ATMOSPHERIC_FLIGHT.md) now composes rotating air
and aerodynamic forces through the same gravity/kernel. Live Godot flight
adoption remains separate work; the saved C++ session always composes the
correction through that atmospheric kernel, including crossing ticks.
