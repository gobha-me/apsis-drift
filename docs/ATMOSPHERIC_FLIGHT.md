# Canonical bounded atmospheric flight

2026-09-30, #201, explicitly selected atmospheric-flight version 1. This C++
provider composes the existing [central gravity](CENTRAL_BODY_DYNAMICS.md),
[physical planet rotation](PLANET_ROTATION.md), immutable starter frame and
coupled thrust/torque RK4 kernel. It has no live consumer or gameplay-save
migration. The ordinary native shell remains docked; this does not complete
#291 or the Freedom flight/contact/walking journey.

`evaluate_atmospheric_flight` observes qualified physical planet-relative state;
`advance_atmospheric_flight` advances one atomic 120 Hz tick. Both require the
same explicitly owned catalog and matching physical rotation recipe as gravity
and orbital telemetry. Future sessions retain atmospheric version, central-body
version, physical rotation and assistance selection beside standalone rigid v3
state. There is no hidden filter, surface history, control state or render input.

## Air and shared space boundary

Air rotates with the existing generated planet pole and sidereal period:
`v_air_relative = v - cross(Omega,r)`. Coordinates remain body-relative,
nonrotating; no centrifugal/Coriolis force or competing gravity is added. This
is an isothermal, windless local atmosphere, not a climate/composition model.
Generated class selects scale height `H`: tenuous 6 km, temperate 8.5 km, dense
11 km. Generated pressure `P0` selects surface density `rho0 = 1.225*P0/1013.25`.
These authored approximations do not change any world generator or random stream.

For atmospheric bodies the finite edge is `H*log(rho0/1e-6)`. At altitude `h`,
`a=max(h,0)` and `t=clamp((edge-a)/H,0,1)`. Below the edge:

```
profile = exp(-a/H) * t*t*(3-2*t)
density = rho0 * profile
pressure = P0 * profile
```

The last scale height has a C1 fade: density and its derivative reach zero at
the edge. At/above it all aerodynamic forces, passive torques and control
surface authority are exactly zero. Airless bodies have edge/density/pressure
zero everywhere while retaining gravity. Subsurface samples use surface density
only; they imply no collision, survivable landing or interior pressure model.

The returned edge is the **same boundary supplied to orbital classification**.
The provider does not accept an independently chosen orbital air edge. Existing
pure telemetry can still be queried separately with an explicitly supplied
policy. Any future ORBIT HOLD integration must select this returned boundary.

## Force and control law

Dynamic pressure is `q=0.5*rho*|v_air_relative|^2`. Transform relative velocity
into body right/up/back axes using each RK4 stage's attitude. Drag is
`F_drag_i=-0.5*rho*speed*(Cd*A)_i*v_body_i`, consuming the craft's immutable
axis areas (starter 54, 80 and 3.84 m²). Its work against **air-relative** motion
is nonpositive. Rotating air can transfer planetary angular momentum to a craft;
inertial speed need not decrease when the craft initially lags the air.

The minimal authored lifting-body model uses 16 m² reference area and
`alpha=atan2(-v_body.y,-v_body.z)` for forward flow (`v_body.z<0`). Lift has
magnitude `q*16*clamp(2*alpha,-1,1)` along body-up projected perpendicular to
relative velocity. That projection must exceed `1e-12` length; otherwise lift
is zero. Reverse flow has no lift or weathercock restoring torque. Lift does
zero work against relative air, within floating-point tolerance. No detailed
stall, supersonic lift, CFD, wing geometry inference or compression model exists.

Forward-flow weathercock torque points along `cross(body_nose,air_direction)`,
with stiffness `q*16*2`, capped per pitch/yaw axis at 25% of registered thruster
torque authority. It restores the nose toward the air trajectory. Passive rate
damping is `-0.3*q/(q+250)*I*omega_relative`, using the craft rate relative to
planet spin transformed into body axes. Zero dynamic pressure has zero damping.

Pitch/yaw/roll fractions retain their vacuum sign and physical thruster meaning.
They additionally command modeled aerodynamic control torque with available
axis fraction `min((q*16*2*0.1)/registered_torque_i,0.35)`. Opposing surface
fractions cancel; opposing physical thrusters remain separate gross channels.
Assisted uses the existing actual capped rate stabilization; Advanced removes
that active stabilization while passive air effects remain. Aerodynamics never
becomes an unlimited assist or a new propellant channel.

`within_rated_envelope` explicitly requires altitude >= 0, generated surface
gravity within the craft rating, local pressure within its rating, air speed
<=350 m/s, |alpha|<=0.35 rad, body rates within individual registered ratings,
and forward flow (or speed <1 m/s). A finite outside-envelope state still
integrates with bounded coefficient/authority laws; the flag promises no
controllable approach outside those limits. It is not a speed limiter or damage
model. Simultaneous high angle/rate and very large forces can refuse numerical
integration rather than silently sanitize authoritative state.

## Composition, observations and accounting

Gravity, drag/lift and passive/surface torque are evaluated at **all four stages
of the existing coupled RK4**. Propulsion uses its actual capped body force and
torque channels. There is no second flight integrator. The same attitude stages
rotate both thrust and aerodynamic effects into the named nonrotating frame.
Above the boundary and on airless bodies, the extra force path is skipped and
ordinary central-body state/propulsion/gravity impulse results remain bit-exact.

The result carries initial/after air samples, post-step shared orbital telemetry,
canonical central actuation, and separate aerodynamic linear/angular impulses.
Central total linear impulse includes gravity, aerodynamics and propulsion once.
Propulsion linear/angular impulses are integrated directly from actual thrusters,
never obtained by subtracting large environmental terms. Later fuel billing
consumes only actual gross propulsion channels; gravity, lift, drag and surfaces
are environmental effects. Thermal presentation may consume pressure, density,
air speed, radial rate and angle without controlling physics.

All work occurs on a candidate. Owner/recipe/state/actuator/numerical errors,
invalid stages, clock overflow and refused post-step observation leave every
source bit unchanged. A reported envelope violation is distinct from refusal.

## Qualification

Physical home references at seeds 0, 42 and maximum u64 qualify a rated forward
approach, bounded signed assisted pitch, rotation phase-wrap continuation, the
last supported clock tick and maximum finite position.

Malformed owner/frame/body/craft/rotation/provider selections, all 13 non-finite
state scalars, invalid actuator fractions and clock/step bounds are tested before
trajectories. Fixed procedural reference seeds are airless 12, tenuous 4,
temperate 0 and dense 3, using each catalog's first planet. Density and speed
probes check monotonic drag/authority, independently rotated body signs, drag
work, lift orthogonality, restoring torque and explicit envelope failures.

Airless/above-edge trajectories require exact central-body state and impulse
parity with both assistance modes and rotating thrust. The independent scalar
radial oracle uses reverse flow at the rotating pole, axial Cd*A and eight
shorter RK4 substeps; a 10-second trace agrees within 0.001 m and 1e-6 m/s.
Passive roll decay, actual assisted stabilization, all signed control axes and
gross opposing physical channels have explicit fixtures.

Inbound/outbound edge crossings require continuous bounded integrated motion,
shared classification and exact observations/impulses after v3 hydration.
Mixed control/assistance traces hydrate during approach/ascent and reproduce
these new atmospheric-v1 state checksums in GCC and Clang:

| Reference | Checksum |
| --- | --- |
| Airless, seed 12 | 11156515336560805162 |
| Tenuous, seed 4 | 8356589538494115423 |
| Temperate, seed 0 | 1777814090012443372 |
| Dense, seed 3 | 1369238175830983921 |

Historical rigid, vacuum, attitude, central gravity and ORBIT HOLD goldens remain
unchanged. Actual native flight persistence, terrain/contact, thermal damage,
weather and fuel consumption remain their own work items.
