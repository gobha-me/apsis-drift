# Exact planted leg closure recipe01

Registered for #425 on 2026-10-05 before implementation or fixture evaluation.
This source-free expression recipe follows the mixed-plane seam under #361.

#361 needs continuous mixed-height load transfer. #420/#422 now provide static contact and surface checkpoints, but rounded Body02 forward joints do not impose the exact identity between two boot bottoms and different source planes. Switching anchors without that identity can move the entire body discontinuously. Deliver the missing constrained kinematics before ship fitting.

Plan:
- Add one separately versioned, purpose-specific paired planted-leg expression recipe. Preserve nominal .47285/.47478m bones, original hip offsets, boot dimensions and existing hip/knee/ankle/abduction limits. Keep Body02/self04 and their static records untouched. No generic IK, expression engine or caller-created route/geometry authority.
- Define hip/ankle/boot coordinates as exact-real expressions with bounded outward evaluation. A planted ankle uses its plane +100mm and a boot center its plane +50mm, with halfheight50mm. Retain those expressions and one common stance placement, so both boot bottoms meet their distinct planes algebraically. Reporting points never supply that identity.
- Use the analytic two-link sphere intersection in the leg's vertical sagittal plane, deriving that plane from hip/ankle X/Y with a fixed forward knee branch. Preserve both bone lengths algebraically; validate domain, reach, plane and actual joint limits with complete bounds. Do not lerp joint points, snap feet, silently normalize a stored legacy frame or increase limits.
- Freeze one source-free mixed-plane lateral-transfer fixture before evaluation: rootX=.32*t²*(3-2*t), rootY=.72, rootZ=-.16; hip X offsets -.14/+.14; port planted ankle X .02/Z-.35 at plane0; starboard X .34/Z-.65 at plane-.16. Y is the exact plane+boot-height expression. t is in[0,1], duration12s. Fixed branch and recipe constants cannot be tuned after refusal. This is a constrained leg observation, not an inherited supported whole-body route.
- Provide complete interval/subinterval/reverse diagnostics with bounded subdivision and explicit capacity/domain refusals. Compare angle limits using sound bounds/sector inequalities, rather than rounded inverse-angle reports. Register numeric workspace/depth/node limits before code. Continuous plane/link identities and interval angle evidence must cover the complete requested interval, with genuine leaf coverage.
- If derivative evidence is included, bound it over the interval and keep it distinct from root duration arithmetic. Do not claim timing/acceleration or full trajectory readiness from samples. A first narrow closure/limit delivery may leave dynamics clearly deferred.

Acceptance:
- The frozen mixed-plane closure preserves both exact plane identities and both fixed link lengths throughout the interval where domain/limits certify. An unresolved interval is reported honestly, without fixture changes.
- Independent synthetic controls cover exact source differences, common placement, report rounding, fixed knee branch, reach/singularity, invalid/nonfinite parameters, nextafter bounds, every enforced joint limit and exhausted subdivision. Independent point reconstruction corroborates enclosures; samples are not the interval certificate.
- Full/subinterval/reverse coverage has no missing leaf, hidden endpoint, partial-success flag or history-dependent result. Source-free private numeric adversaries may not become public geometry admission.
- Full GCC/Clang native builds/tests, pinned format20/tidy20 and current boot/self/world/save/walk/flight regressions.

This only delivers source-free exact planted-leg kinematics. Whole-body expression/self ownership, finite load, source/crop sweeps, free-foot swing, pan-supported seating, actor/save integration and First Flight remain open under #361/#352. No asset, master/capture/material-proof replay, source-floor expansion, contact epsilon or gameplay teleport. Publication depends on #422 merge.


## Numeric and ownership registration

The public query accepts only finite parameter interval endpoints in [0,1].
Reverse endpoints request the same closed geometric interval in reverse;
reporting direction is retained. No caller geometry, joint-angle override,
source table, supplied diagnostic or editable identity flag is admitted.
The fixed recipe uses the forward knee branch in each derived vertical leg
plane. Its plane normal is an exact normalized expression of hip/ankle X/Y;
it never normalizes an existing stored Body02 or source frame.

Register maximum subdivision depth12, maximum8191 examined nodes and maximum4096
retained leaves. Every accepted leaf owns its interval bounds and certificates;
only a complete gap-free cover can grant the source-free interval result.
Unresolved domain, arithmetic, reach, branch or joint limits and exhausted
capacity retain explicit refusal without a partial-success flag. A midpoint
that cannot strictly subdivide also refuses. Private numeric adversaries can
exercise smaller budgets or synthetic degeneracy without public admission.

Keep outward arithmetic bounded and finite. Prove the paired planted plane
and link identities from the analytic expressions; rounded report endpoints
can have discrepancies and do not replace those expressions. Fixed limits are
hip [-20,65], knee [0,135], flat ankle pitch/roll +/-30/+/-15 and hip abduction
+/-35 degrees with upright pelvis. Abduction is derived from the leg plane;
flat-roll15 is the tighter bound. Report inverse angles separately from limit
certificates. Root curve monotonicity follows 6*t*(1-t)>=0; enclosing endpoints
may tighten that fixed polynomial without sample-based permission.

The12-second duration is recorded for the fixture. This first delivery does
not certify joint speed, acceleration, load, self, world surfaces or a gait.
Those require the actual complete-body expression and trajectory model.
No source-free result grants route, actor, seat, saved-phase or source-volume
qualification. Body02/self04 and their existing static checkpoints remain
unchanged and separately versioned.

## Compiled expressions and certificates

For hip H and planted ankle A, let d=A-H, D=d dot d and
rho=sqrt(dx²+dy²). The registered domain requires rho>0, D>0 and dy<0.
Its derived upward leg-plane direction is U=(-dx,-dy,0)/rho, with normal
N=(Uy,-Ux,0). These are new expression directions, not repaired legacy frames.
Then q=N cross d satisfies q dot d=0, q dot q=D and qz=-rho exactly.

```
alpha = (L1²-L2²+D)/(2D)
gamma² = (((L1+L2)²-D)*(D-(L1-L2)²))/(4D²)
K = H + alpha*d + gamma*q
```

The positive square-root branch is fixed. Reach certificates establish the
nonnegative factor domains before evaluating gamma. The equivalent identity
gamma²=(L1²-alpha²*D)/D gives |K-H|²=L1² and |A-K|²=L2².
The knee is derived from this expression; rounded points do not establish link
lengths. Canonical point bounds subtract the same .72 placement from Y.
Ankle terms `{plane,.1,-.72}` and boot-center terms `{plane,.05,-.72}`
retain their exact plane expressions. Because .1 is exactly twice the stored
.05, the flat boot bottom equals its own plane after the common placement.

Down/forward components of the linked thigh and shin supply the hip, knee
and flat-ankle sectors. Positive downward components fix the relevant pitch
quadrants. Hip sectors use bounded sine/cosine constants for -20/65 degrees;
knee cosine must be at least -sqrt(2)/2; flat ankle uses the +/-30-degree
sector. Derived lateral roll uses |dx|<=(-dy)*(2-sqrt(3)), the tighter
15-degree flat-roll limit, also satisfying 35-degree hip abduction. There is
no independent shin plane or flat-boot override.

Arithmetic bounds widen each finite operation outward. Square-root endpoints
are checked with bounded exact product-residual signs. Trigonometric constants
use the registered adjacent-binary64 pi bracket, fixed Taylor polynomials and
bounded remainders. Certification requires binary64 round-to-nearest with
gradual underflow and preserved subnormal inputs; unsupported arithmetic
environments refuse rather than grant a complete result. Inverse-angle reports
describe the accepted leaf midpoint and never grant a joint-limit certificate.

The fixed root polynomial is monotone on [0,1]. Outward endpoint evaluation
therefore encloses its complete interval without sampling. Each accepted leaf
holds both legs' bounds and certificates. Depth-first subdivision visits the
left interval before the right; a final gap-free closed-cover check is required
for every complete summary flag. A retained prefix and first refusal remain
diagnostic evidence only. Reverse queries retain the same ascending cover.

## Implementation results

The first frozen-fixture execution passed with both GCC and Clang: full and
reverse [0,1] requests have 20 accepted leaves, 39 examined nodes and maximum
depth 5. Each half has 10 leaves/19 nodes/depth 4; [.125,.875] has
16 leaves/31 nodes/depth 4. Every registered point query has one accepted leaf.
The first independent contract run passed 18,825 checks on each compiler,
including arithmetic-environment refusals. No fixture constant, link length,
dimension, branch or joint limit was changed after evaluation.

The final contract adds assertions preserving the observed complete outcomes:
18,829 checks pass with each compiler. Full native builds, including the Godot
bridge, and all 52 native tests pass with GCC (23.14s) and Clang (21.81s).
Pinned format20, the 31-directive suppression policy and full tidy20 over
142 translation units pass. The initial compile-only test-helper declaration typo was corrected
before either fixture execution. The first tidy pass flagged a no-effect move
of a trivially copied leaf; removing it preserves the same evidence and the
full native tests pass again. No rendered capture or owner playtest is claimed
by this source-free implementation.

Bounded timing follows in #426. The recorded 12-second duration alone does not
grant dynamics or speed/acceleration qualification.
