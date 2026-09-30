# Canonical orbital observations

2026-09-30, #323, telemetry recipe version 1. This pure C++ query consumes the
selected [central-body gravity](CENTRAL_BODY_DYNAMICS.md) and explicitly physical
[planet rotation](PLANET_ROTATION.md), at the state's authoritative integer tick.
It neither advances the clock nor normalizes, corrects or replaces state.
There is no live cockpit consumer, orbit-hold controller or flight-save adoption
in this slice of #238/#291.

`evaluate_orbital_telemetry(context, state, rotation, policy, dynamics)` accepts
the same physical-owner-only planet-relative nonrotating state as the gravity
provider. The physical rotation must name that same planet and rederive against
the same owner; equal numeric IDs cannot substitute another recipe family.
Malformed state, wrong owners/recipes, unsupported versions and unsafe arithmetic
refuse before an observation is returned.

## Three different motion measurements

For body-relative nonrotating position `r`, velocity `v`, generated `mu`, and
the selected planet's angular velocity `Omega`:

```
inertial speed = length(v)
surface-relative velocity = v - cross(Omega, r)
surface-relative speed = length(surface-relative velocity)
radial rate = dot(v, r / length(r))
h = cross(r, v)
specific energy E = dot(v,v)/2 - mu/length(r)
eccentricity e = length(cross(v,h)/mu - r/length(r))
periapsis radius = (dot(h,h)/mu) / (1+e)
bound geometric apoapsis radius = (dot(h,h)/mu) / (1-e), for e < 1 and nonzero h
radial bound apoapsis fallback = -mu/E - periapsis radius
```

Radial rate is signed climb/descent. Zero radial rate is instantaneous; a
stationary craft has zero radial rate while gravity causes descent. Zero
surface-relative velocity ordinarily means supported hovering. Neither
measurement alone proves an unpowered orbit. Quaternion/camera orientation
does not enter the orbital elements or surface velocity; the shared physical
rotation pole determines the rotating reference.

Both geometric apsides use the same angular-momentum invariant, avoiding an
energy-derived apoapsis that rounds below periapsis on a circular orbit. A
radial bound trajectory with zero angular momentum or an eccentricity rounded to `e == 1` uses its
energy-derived finite outer turning point rather than dividing zero by zero.

Element outputs are **radii from the center**, not altitude. Subtract the
generated reference radius when presenting altitude. `apoapsis_radius_metres`
is optional, with no infinity or invented clamp: it is absent on unbound or
near-parabolic trajectories and when the bound apoapsis exceeds the declared
`2e15` metre element limit. A bound orbit can therefore retain `bound = true`
and its semantic classification while having no reportable apoapsis radius.

Near-parabolic energy uses the declared numerical band
`64 * epsilon_double * (dot(v,v)/2 + mu/length(r))`. Only energy below the negative
band qualifies as bound. `near_parabolic` reports the band explicitly; marginal
trajectories use escape semantics unless they intersect the body in their future.
This avoids unstable enormous apoapsis values from a rounded near-zero divisor.
The measured signed energy remains observable and is not changed to zero.

## Explicit classification policy

`OrbitalTelemetryRecipe{1, space_boundary_altitude_metres}` is a required query
argument. It is a classification policy supplied by the future selected
atmosphere owner, not density generation or aerodynamic force. The boundary
must be finite, nonnegative canonical zero, and no greater than `1e15` metres.
Zero is a valid explicit vacuum policy. Test fixtures use 100 km; this does not
declare the generated world's atmosphere height. Changing the policy does not
alter gravity, orbital elements, velocity or state.

The ordered classifications are:

| Classification | Meaning |
| --- | --- |
| `IMPACT` | Already at/below the reference surface, or a below-surface periapsis lies in the future: a bound trajectory revisits it; an inbound unbound trajectory reaches it |
| `ESCAPE` | Unbound or marginal, with no future surface intersection |
| `DECAYING` | Bound, above the surface, but periapsis intersects the declared space boundary; a later atmosphere provider determines actual dissipation |
| `STABLE` | Bound and periapsis clears the declared space boundary |

An outbound hyperbolic trajectory can have a below-surface **past** periapsis;
that geometric extension does not predict an impending collision. Conversely,
an inbound hyperbola whose periapsis intersects the surface is `IMPACT` even
though its energy is positive. This is osculating central-body classification,
not a contact event, future thrust promise or lifetime orbit guarantee.

## Strict synchronous qualification

The query derives `synchronous_radius_metres = cbrt(mu / dot(Omega,Omega))`.
Its `synchronous_orbit` flag uses a strict surface-stationary contract: bound,
clear, equatorial, prograde, circular motion at that radius, with nonzero
inertial orbital speed matching the planet's spin. Relative tolerance is `1e-6`
for radius, eccentricity, equatorial departure, radial rate and residual
surface-relative speed. Rate tolerances scale against local circular speed.

A retrograde circular orbit, inclined period match, eccentric orbit, stationary
craft at the synchronous radius or supported surface-stationary craft at another
radius fails this flag. The flag does not mean that an orbit-hold controller is
active or authorize any correction force. Body rotation and gravitational
authority remain their existing independent C++ providers.

## Qualification

`orbital-telemetry-contract` runs malformed state/owner/recipe/policy and maximum
finite bounds before physics fixtures. Circular prograde/retrograde and
independently constructed eccentric fixtures compare energy, momentum, elements
and classification against analytic results. Radial fall, zero-speed/zero-radial
ambiguity, marginal escape, inbound impact versus outbound escape, and excessive
bound apoapsis have explicit fixtures.

At origin/procedural seeds 0, 42 and maximum u64, synchronous surface velocity
agrees with an independent rotation matrix and fixed/nonrotating C++ handoff.
Phase-wrap and maximum supported ticks are included. A standalone v3 hydrate
with the same explicitly selected gravity/rotation/policy reproduces query
values exactly and leaves the source projection unchanged. Existing state,
gravity, vacuum, attitude and legacy checksums remain separate regression gates
under GCC and Clang.

Explicit bounded `ORBIT HOLD`, atmospheric force composition, cockpit adoption
and actual native flight persistence remain outstanding work in #238/#291.
