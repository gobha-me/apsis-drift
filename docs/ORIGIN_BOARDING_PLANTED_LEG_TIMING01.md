# Bounded timing for planted leg recipe01

Registered for #426 on 2026-10-05 before timing implementation or fixture
evaluation. Publication depends on #425 merge. The original registration is
preserved in the commit history; this document describes the selected cut.

## Fixed recipe and clock

Use the [exact planted-leg recipe](ORIGIN_BOARDING_PLANTED_LEGS01.md) unchanged:
rootX=.32*t²*(3-2*t), rootY=.72, rootZ=-.16; hip X offsets -.14/+.14;
port ankle X=.02/Z=-.35 on plane0; starboard ankle X=.34/Z=-.65 on plane-.16;
nominal links .47285/.47478; exact ankle/boot height expressions .1/.05;
upright pelvis, yaw0 and fixed forward knee branch. The old closure evaluator,
types and evaluation order remain unchanged. This work does not fit a ship.

The original clock is seconds=12*t. Divide first parameter derivatives by12
and second derivatives by144. Reverse negates physical first bounds as
[-upper,-lower], preserving second derivatives and ascending spatial coverage.
Subintervals retain the original clock; elapsed time is12*abs(last-first).
Point requests describe instantaneous derivatives rather than stationary holds.
The endpoints have zero root velocity but nonzero root acceleration; stationary
C2 joins and supported pauses are not established by these results.

## Public result and ownership

`assess_origin_boarding_planted_legs_timing(first,last)` accepts only finite
closed endpoints in [0,1], including reverse and point requests. It returns a
separately versioned diagnostic owning the unchanged closure result, a separate
bounded timing cover and the first owned refusal. No caller duration, geometry,
angle override, expression tree or supplied success flag is accepted.

Each timing leaf owns hip/knee/ankle/boot-center velocity and acceleration
bounds, signed coordinate angular first/second derivatives and physical
resultant hip/knee/ankle speed bounds for both legs. Units are m/s, m/s²,
rad/s and rad/s². Coordinate angle second derivatives are not complete physical
angular-acceleration vectors. Elapsed time metadata does not retime the curve.

Complete timing requires complete closure plus a gap-free closed timing cover
whose every leaf passes derivative domains, root limits and both legs' physical
joint-speed limits. A timing refusal preserves existing closure evidence without
granting whole-request derivative or timing flags. Self, load, full-body COM,
world, route, actor, seat, save, dynamics and free-foot swing remain unqualified.

## Analytic derivative construction

Use purpose-specific local triples {value,first,second} on the fixed expressions.
For a product, (ab)'=a'b+ab' and (ab)''=a''b+2a'b'+ab''. For reciprocal,
(1/a)'=-a'/a² and (1/a)''=2a'²/a³-a''/a². For y=sqrt(a),
y'=a'/(2y) and y''=a''/(2y)-a'²/(4y³). Every operation uses finite outward
bounds; complete leaf denominators must be proven away from zero.

Propagate through d, rho², D, alpha, factored gamma², gamma, q and K from the
registered closure. Do not differentiate rounded report joints or inverse-angle
reports. Root derivatives are .32*6*t*(1-t) and .32*(6-12*t); the first has a
fixed critical point at t=.5 and the second is affine. Every planted ankle/boot
coordinate and the common .72 placement have exact zero derivatives.

For the registered down/forward components F/G and constant bone length L,
exact link identities give theta'=(F*G'-G*F')/L² and
 theta''=(F*G''-G*F'')/L². Hip/shin retain the existing certified pitch sectors.
Knee is hip minus shin; flat ankle pitch is minus shin. Roll phi=atan2(dx,-dy)
uses its variable rho² denominator and quotient second derivative. Hip-abduction
coordinates are port -phi/star +phi; flat ankle roll is -phi on both sides.

## Frozen policy and bounds

The [lower transfer policy](LOWER_TRANSFER_POLICY.md) supplies root speed
<=.25m/s, root acceleration <=.10m/s² and joint angular speed <=30degrees/s.
This cut certifies the root and constrained legs only; it cannot qualify
full-body COM/load acceleration. Planted soles are stationary by construction.
Report knee point and coordinate angular accelerations without inventing new
limits or a free-foot swing policy.

Joint angular speed means the physical resultant relative rotation. Orthogonal
Rz(phi)*Rx(theta) axes give hip speed sqrt(phi_dot²+theta1_dot²), knee speed
abs(theta1_dot-theta2_dot) and flat ankle speed sqrt(phi_dot²+theta2_dot²).
Bound these against certified pi/6 rad/s; individual component caps do not
suffice. This choice was frozen before implementation or timing evaluation.

Retain binary64 nearest rounding, actual nearest-addition probes, gradual
underflow and preserved subnormal input admission. Unsupported arithmetic,
overflow, nonfinite bounds or uncertain derivative domains refuse. Inverse
square-root derivatives require rho, D and gamma² bounded strictly positive.
Straight reach may be derivative-singular; do not loosen old closure acceptance
to manufacture a boundary example or guess a removable derivative limit.

Timing subdivision has independent depth12/nodes8191/leaves4096 ceilings, with
left-first shared endpoints and a final closed-cover check. The owned closure
result keeps its separate unchanged budget. Storage is bounded to these two
leaf vectors and the fixed local expression graph. There is no hidden retry,
capacity enlargement, generic expression framework or duration/fixture tuning.

Private adversaries accept world hip values within +/-8m and per-parameter
first/second component magnitudes <=4096. Ankle X/Z and plane stay within +/-8m,
links in [1e-6,4]m, side0/1 and the forward branch remain mandatory. Private
geometry, derivative and speed-comparison seams do not become public authority.

## Independent checks and observed results

Tests independently differentiate unfactored sphere-intersection expressions
with higher precision, reconstruct segment/plane derivatives and cross-check
knee rates with a cosine-angle derivative away from singularities. Samples
corroborate complete interval enclosures; they never certify a curve.

Check original-clock subinterval/point/reverse behavior, structural planted
zeros, root critical extrema, exact differentiated link identities, physical
resultant norms and an adversary whose two individually acceptable components
exceed the resultant speed limit. Domain, nextafter/nonfinite, environment,
capacity and truthful-prefix controls remain mandatory. Preserve all existing
closure and native boarding/boot/self/world/walk/save/flight regressions.

The frozen timing fixture passes full/reverse [0,1] requests with 20 leaves,
39 examined nodes and maximum depth 5. Halves have 10 leaves/19 nodes/depth 4;
[.25,.5] has 7 leaves/13 nodes/depth 3; [.125,.875] has 16 leaves/31 nodes/depth 4.
All five registered point requests qualify with one leaf. Both compilers pass
the final independent timing contract's 86,985 checks and the unchanged
closure contract's 18,829 checks. No curve, clock, branch, dimension, limit or
workspace was changed after evaluation.

The first builds found two unnecessary copies in test loops; references fixed
them before execution. Initial execution found one private pure-lateral input
refusing a zero knee rate: widened subtraction introduced an artificial
subnormal interval. Reusing existing equal-singleton subtraction independently
for value/first/second preserves exact cancellation without zeroing genuine
derivatives or changing the old evaluator. Independent review confirms the
correction, and all original fixture outcomes remain unchanged.

Full native builds pass on GCC and Clang, each with 53/53 tests (21.64s and
21.41s respectively). Pinned format20, all 143 tidy20 translation units and
the existing 31 justified suppression directives pass. No whole-body clearance, source-backed load transfer,
seated endpoint, actor action, save phase, rendered capture or First Flight
completion is granted by this timing cut.

Publication CI GCC13 additionally diagnosed copies in two std::pair test loops.
Using references changes no production arithmetic or fixture outcome. The
repaired full native suites pass53/53 on both local compilers (GCC22.39s,
Clang21.14s); remote GCC13 CI is the compatibility check for that diagnostic.
