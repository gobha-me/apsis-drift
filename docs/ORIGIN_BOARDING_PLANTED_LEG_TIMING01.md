# Bounded timing for planted leg recipe01

Registered for #426 on 2026-10-05 before timing implementation or fixture evaluation.
Publication depends on #425 merge. The fixed #425 constants, branch, original
12-second clock and completed kinematic records remain unchanged.

#361 needs movement timing as well as exact contact. #425 establishes the fixed, source-free mixed-height paired-leg closure and joint limits; its recorded 12-second duration is not a speed or acceleration certificate. Implement bounded derivatives of that same recipe before fitting a source-backed route.

Plan:
- Add a separately named/versioned source-free timing query for the exact #425 recipe. Public input remains finite closed parameter endpoints in [0,1], including reverse and point requests; no caller geometry, duration fitting, angle overrides or supplied certificate flags. Preserve #425 diagnostics and their recorded outcomes.
- Freeze the same root/hip/ankle/plane/link constants and positive knee branch. Use the original clock seconds=12*t; first derivatives scale by 1/12 and second derivatives by 1/144. A subinterval retains that clock. Reverse changes physical first-derivative signs, preserving second derivatives and ascending spatial coverage.
- Bound value/first/second derivatives of the fixed analytic leg expressions with purpose-specific local interval triples. Derivative square-root/reciprocal domains must be proven strictly positive over every leaf. No generic expression framework, finite-difference production authority or derivative of rounded reports.
- Derive hip/shin angular rates from (F*G'-G*F')/L^2 and angular second derivatives from (F*G''-G*F'')/L^2 using the exact link identity. Knee is hip minus shin; flat ankle pitch is minus shin. Derived roll uses its variable rho^2 denominator and existing side signs. Keep all current position/sector prerequisites. For the orthogonal Rz/Rx axes, certify hip speed sqrt(roll_rate^2+hip_pitch_rate^2), knee speed abs(hip_pitch_rate-shin_pitch_rate), and flat ankle speed sqrt(roll_rate^2+shin_pitch_rate^2); coordinate-rate reports alone cannot grant joint-speed qualification.
- Freeze the existing applicable policy limits: root speed <=.25m/s, root acceleration <=.10m/s^2 and physical resultant relative leg-joint angular speed <=30degrees/s. Both planted soles are constant, with exact zero derivatives. Report point/angular second derivatives without inventing joint angular-acceleration or knee-point limits. These root/leg certificates do not qualify full-body COM/load acceleration or a free-foot swing.
- Retain binary64 nearest/gradual-underflow arithmetic admission and depth12/nodes8191/leaves4096 ceilings, with a fixed straight-line expression graph and owned leaf evidence. Complete results require all derivative domains, applicable limits and a gap-free closed cover. Preserve closure evidence distinctly when derivative timing refuses; exact straight reach may be closure-valid but derivative-singular. No capacity enlargement or duration/fixture tuning after refusal.

Acceptance:
- Observe the frozen full/subinterval/reverse/point outcomes without changing recipe or clock. All successful timing bounds enclose actual analytic derivatives over their complete leaves; samples corroborate but cannot certify.
- Independently reconstruct derivatives using higher precision and unfactored analytic forms; cross-check knee rates with a separate cosine-angle derivative away from singularities. Verify exact zero planted-foot derivatives, root critical-point extrema, clock conversion and reverse signs.
- Exercise derivative reach/rho/gamma singularity, nonfinite/overflow/unsupported arithmetic, angular-rate threshold/nextafter boundaries, incomplete subdivision and every capacity. Distinguish closure, derivative and timing refusal without false complete/route flags.
- Full GCC/Clang native builds/tests, pinned format20/tidy20 and current boarding/boot/self/world/walk/save/flight regressions.

This is the missing timing seam for the active #361 transfer. Whole-body expression/self ownership, finite load, source/crop sweeps, swing phases, pan-supported seating, actor/save integration and First Flight remain open. No asset/master/capture/material-proof replay, old-source mutation or new gameplay gate. Publication depends on #425 merge.

## Numeric workspace and API registration

Append a separately named/versioned timing API to the existing planted-leg
module. Preserve the original evaluator and evaluation order. The timing result
owns unchanged closure evidence plus a separately bounded ascending timing
cover; closure acceptance survives derivative refusal. Complete timing requires
both complete closure and gap-free derivative/limit evidence for every leaf.

Private adversaries use world hip value/first/second derivative vectors, with
values within +/-8m and per-parameter first/second component magnitude <=4096.
Ankle X/Z and plane remain within +/-8m; each link is in [1e-6,4]m; the forward
branch stays mandatory. These private inputs are never public route geometry.
Unsupported finite operations, arithmetic environments or derivative domains
refuse. Freeze independent timing subdivision depth12/nodes8191/leaves4096;
the owned old closure diagnostic retains its unchanged separate budget. No
expression graph or unbounded scratch/catalog allocations are introduced.

Use physical resultant relative joint angular speed: for the orthogonal
Rz(phi)*Rx(theta) axes, the hip squared speed is phi_dot^2+theta1_dot^2,
knee squared speed is (theta1_dot-theta2_dot)^2 and flat ankle squared speed
is phi_dot^2+theta2_dot^2. Bound these against the certified exact pi/6 rad/s
threshold. Hip-abduction coordinates are port -phi/star +phi; local flat ankle
roll is -phi on both sides. Coordinate second derivatives are reported as such,
not as complete angular-acceleration vectors. No angular-acceleration policy
threshold or full-body dynamics claim is introduced.

The original clock is seconds=12*t. Physical first derivatives divide by12,
second derivatives by144; reverse negates first bounds, preserving seconds and
second derivatives. Point queries describe instantaneous curve derivatives.
Elapsed subinterval time is12*abs(last-first), and does not retime the curve.
No timing outcome is predicted or selected before evaluation.

## Source-only derivative construction plan

# Fixed planted-leg recipe: bounded derivative/timing next step

Source-only planning, 2026-10-05. No new issue/API/code, fixture query, build,
tracked change, geometry source read or prior proof replay was performed.

## Existing scope and timing

`docs/LOWER_TRANSFER_POLICY.md` registers root speed .25 m/s, swing-sole speed
.35 m/s, joint angular speed 30 degrees/s and surrogate root/load acceleration
.10 m/s². It does not register an angular-acceleration limit. GitHub #361 keeps
continuous whole-body clearance, support/load transfer and route admission open.
`docs/ORIGIN_BOARDING_PLANTED_LEGS01.md` and
`src/origin_boarding_planted_legs.cpp` define only the fixed paired-leg recipe,
its complete closure/angle bounds and duration12 s. Keep those claims intact.

Use normalized parameter t and the existing fixed clock s=12t. Full forward
play has dt/ds=1/12; reverse play has -1/12. Derivatives with respect to seconds
are first derivatives divided by12, and second derivatives divided by144.
Subintervals retain this original clock, with elapsed duration
12*abs(last-first); do not silently traverse every subinterval in12 s or
compress it into another duration. A point request is an instantaneous bound
on the original curve, not a zero-speed pause. No caller duration fitting is
needed for this cut.

The root expression and its exact derivatives are:

```
x  = .32*t²*(3-2t)
x' = .32*6*t*(1-t)
x''= .32*(6-12t)
```

Prime denotes differentiation with respect to t. Y/Z and the common .72 Y
placement are constant. Both planted ankles and boot centers are constant, so
their first/second derivatives are exactly zero. This grants no free-foot swing
capability. The analytic root maxima can be bounded directly using monotonic
pieces and the t=.5 critical point; evaluating samples is unnecessary.

## Small purpose-specific derivative graph

Extend the existing bounded interval evaluation of the same fixed expressions
with value/first/second derivative triples, locally named for this recipe.
Do not build a public expression walker, automatic-differentiation framework,
editable node registry or alternate kinematics. Retain the current algebraic
identities, bone lengths, derived plane, positive gamma branch and joint sectors.

For interval triples, addition is componentwise. For multiplication,
(ab)'=a'b+ab' and (ab)''=a''b+2a'b'+ab''. For reciprocal,
(1/a)'=-a'/a² and (1/a)''=2a'²/a³-a''/a². For y=sqrt(a),
y'=a'/(2y) and y''=a''/(2y)-a'²/(4y³). Every operation uses the existing
outward finite interval discipline. Derivative denominators must be proved
away from zero over the entire leaf, never by its midpoint.

Carry these triples through d=A-H, rho²=dx²+dy², D=rho²+dz²,
alpha=(L1²-L2²+D)/(2D), the existing factored gamma², gamma, q and
K=H+alpha*d+gamma*q. Report knee point velocity/acceleration bounds and retain
unchanged hip/ankle/boot point identities. Do not differentiate rounded reported
joints, sampled inverse angles or independently normalized stored frames.

Require positive lower bounds for rho, D and gamma² when invoking their
inverse/square-root derivative formulas. Closure allows exact straight reach;
this derivative cut may conservatively refuse it. A vanishing radicand can
have a finite derivative after correlated cancellation, but no unregistered
limit or guessed zero is introduced to make that case pass. Preserve closure
acceptance separately from derivative refusal.

## Angular rates without inverse-angle permission

Keep #425's down/forward components F1,G1,F2,G2 and certified pitch quadrants.
For theta=atan2(G,F), the constant bone-length identity gives:

```
theta'  = (F*G' - G*F') / L²
theta'' = (F*G''- G*F'') / L²
```

This uses F²+G²=L² by the registered analytic construction. It does not assume
that independently rounded sine/cosine reports lie on a unit circle. Compute
hip and shin angular derivative bounds this way. Knee rates are hip minus shin;
flat ankle pitch rates are minus shin. Branch/sector certification remains
mandatory so the reported physical angle is continuous and correctly oriented.

For derived roll phi=atan2(dx,-dy), let F=-dy, G=dx and R=F²+G²=rho².
Then B=F*G'-G*F', phi'=B/R and
phi''=(F*G''-G*F'')/R-B*R'/R². Use the port/starboard sign convention of
#425 for hip abduction and the opposite physical roll for the flat ankle;
absolute speed limits are unchanged. R is variable, unlike the bone-length
denominators. No rounded angle difference or acos differentiation near straight
knees is used as production authority.

Convert rates to seconds before comparisons. The 30-degrees/s bound is pi/6
radians/s; prove abs(rate).upper <= the lower enclosure of that exact threshold
using the existing certified pi bracket. Degree reports use bounded conversion
and are secondary. Report angular second derivatives, but do not invent an
angular-acceleration policy threshold. These leg rates do not establish actual
whole-body rotation or kinetic/force balance.

## Complete bounded certificates and refusal

Retain binary64 round-to-nearest, gradual-underflow and preserved-subnormal
environment checks before authority-bearing arithmetic. Unsupported products,
roots, denominators, overflow, nonfinite bounds, singular derivatives or
uncertain comparisons refuse. Keep depth12/nodes8191/leaves4096 as prospective
ceilings; do not enlarge them after evaluating the frozen recipe. Additional
named expression storage/work bounds must be declared before implementation.

Reuse left-first subdivision over the original requested interval, with the
same exact endpoint-sharing and final gap-free cover check. An accepted leaf
needs both legs' closure/angle certificates and derivative/timing certificates.
Return distinct closure-complete, derivative-complete and applicable timing
flags, with the first side/interval/domain or limit refusal and honest counts.
An accepted prefix never grants a complete result. Reverse queries retain the
same ascending spatial cover; physical first derivatives change sign and
second derivatives do not. Leaf bounds may subdivide further for derivative
conditioning; no pose or duration changes follow from a refusal.

Root speed/acceleration may be compared to the policy limits for this fixed
root curve alone. Planted sole speed is zero by construction. Joint speed
certification concerns these two constrained legs only. Full-body COM/load
acceleration remains false: the recipe contains neither all15 mass trajectories
nor their consistent constrained self/world ownership. No load, self, source
sweep, route, actor, seated or save-phase admission follows.

The cubic has zero root velocity at t=0/1 but nonzero endpoint second derivative.
Extension by stationary holds is C1, generally not C2. Do not claim a smooth
acceleration join or a supported pause from endpoint timing alone; later route
composition must explicitly declare its join/one-sided acceleration semantics.

## Meaningful independent controls before acceptance

* Verify value/closure enclosures and exact planted identities remain those of
  #425; every returned derivative uses the same fixed branch and placement.
* Corroborate point bounds independently at endpoints and interior parameters,
  using a test-only higher-precision reconstruction with the unfactored
  gamma²=L1²/D-alpha² and independently differentiated formulas. These points
  test bounds; they do not replace complete leaf certification.
* Cross-check knee rates with an independent test-only cosine-angle derivative
  away from straight/folded singularities; check hip/shin sectors and roll signs.
* Check full/subinterval/reverse covers, seconds conversion, point requests,
  fixed-duration ownership, first-derivative sign reversal and unchanged second
  derivatives. Test root critical-point bounds and exactly zero planted-foot
  derivatives directly.
* Exercise private synthetic reach/rho/gamma singularities, tiny denominators,
  overflow/nonfinite inputs, directed rounding/FTZ/DAZ denial, angular-rate
  threshold and nextafter boundaries, unsplittable intervals and every capacity.
* Retain honest partial refusal and all false full-body/load/world/route flags.
  Endpoint finite differences may corroborate reports but never certify timing.

GCC/Clang publication checks and pinned format/tidy follow a separately
registered implementation. This plan makes no assertion that the frozen
12-second recipe passes all future timing certificates.
