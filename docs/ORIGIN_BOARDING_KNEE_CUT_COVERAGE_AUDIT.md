# Selected knee-cut coverage audit

Issue [#524](https://github.com/gobha-me/apsis-drift/issues/524), 2026-10-08 UTC,
after #522 / PR #523 and #521 / PR #525. This source/equation audit classifies
the exact restricted cuts behind Endpoint05 and retains the original descent
dependence. All theorems below have explicit generic premises. No source
coordinate, signed operand, derived bound, sample, posture or alternative
control was evaluated. No new candidate, primitive program, API, implementation
or numerical observation is selected.

The [retained Endpoint05 observation](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT05.md#11-retained-first-observation)
stops at PORT G15 after O082 with `no_tau_interval`, before earning Y, a Key,
an original phase call or geometry audits. Its supported attached `[0.5,0.5]`
record describes q. The lower endpoint and dominating cut inputs were not
exported. That observation remains immutable; actual-source feasibility and
the broader original branch remain unknown.

## 1. Authentic chart and selected domain

Keep each side's original sole, root X/Z/yaw, independent sole yaw and stored
lengths. The [original leg chart](../src/origin_boarding_planted_legs.cpp#L3430)
and [BOTH-ankle proof](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md#1-authentic-operands-and-preserved-source-chart)
give, in exact real arithmetic,

```math
d_s=(x_s,-h_s,z_s),\quad w_s=-z_s,\quad
h_s=Y-a_s>0,\quad \rho_s=\sqrt{x_s^2+h_s^2}.
```

Each side retains its own signed w, x and vertical origin a. Proper original
yaw preserves WORLD Y and norms. These are identities of the authenticated
exact expressions; rounded enclosure columns supply no extra orthogonality.
Stored binary64 constants keep their exact lifted meaning.

For one side suppress s, assume finite L1,L2>0, and write S=L1+L2. The
[selected positive-thigh-cosine branch](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md#2-one-regular-directed-subbranch)
has

```math
G_2=L_2\tau,\quad F_2=L_2\sqrt{1-\tau^2},\quad
G_1=w-L_2\tau,\quad F_1=\sqrt{L_1^2-G_1^2},\quad
\rho=r_w(\tau)=F_1+F_2.
```

The strict ankle subcone is `-1/2<tau<1/2`. Its regular F1>0, positive-knee
component also requires `-L1<G1<L1` and `tau<w/S`. Endpoint05 adds `0<G1<g`
for a finite fixed cap with `0<g<L1`. This g stands for a conditionally earned
cap; no game value is evaluated or chosen here. The
[hip proof](ORIGIN_BOARDING_BOTH_HIP_CONSTRUCTION.md#4-directed-radical-restriction)
uses this restriction to obtain a sufficient uniform descent guarantee.
The original compiler retains its broader allowed hip sector and all of its
own supported reach, derivative, joint-sector and timing checks.

## 2. Exact scalar compatibility

Combining these strict selected-domain inequalities gives

```math
A(w)=\max\left(-\tfrac12,\frac{w-L_1}{L_2},\frac{w-g}{L_2}\right),
\qquad
B(w)=\min\left(\tfrac12,\frac{w+L_1}{L_2},\frac{w}{S},\frac{w}{L_2}\right).
```

Since g<L1 and L2>0, the new lower cut dominates the thigh lower cut. The
G1>0 upper cut is below the thigh upper cut. Therefore exactly

```math
A(w)=\max\left(-\tfrac12,\frac{w-g}{L_2}\right),\qquad
B(w)=\min\left(\tfrac12,\frac{w}{S},\frac{w}{L_2}\right).
```

The strict restricted component exists precisely when A(w)<B(w). Each lower
entry must be below every upper entry. The constant comparison is automatic;
the two lower-slab comparisons reduce to `w>-L2/2`, which implies `w>-S/2`.
The other three comparisons reduce to `w<g+L2/2`, `w<g*S/L1` and `g>0`.
Consequently the complete equivalence on the stated domain is

```math
A(w)<B(w)\quad\Longleftrightarrow\quad
-\frac{L_2}{2}<w<\min\left(g+\frac{L_2}{2},\frac{gS}{L_1}\right).
```

For w<0, B=w/L2 and the lower restriction is the only nonautomatic condition.
At w=0 the component exists under the standing premises. For w>0 both upper
restrictions remain: their relative order depends on g versus L1/2.
Equality at either limiting compatibility boundary can leave a touching
closed set, but earns no positive-width strict interval. This equivalence
classifies the restricted branch for a scalar w; it evaluates no actual w.

## 3. One common interval for a signed enclosure

Let finite `wl<=wh` define W=[wl,wh], retaining the same fixed g and lengths.
The exact common inward cuts are

```math
A_W=\max\left(-\tfrac12,\frac{w_h-g}{L_2}\right),\qquad
B_W=\min\left(\tfrac12,\frac{w_l}{S},\frac{w_l}{L_2}\right).
```

Their meaning is **one positive-width open tau interval valid for every
w in W**. The weaker pointwise statement `for every w, some interval exists`
allows different intervals and has a different coverage boundary.

The same pairwise comparison proof gives the following complete equivalence:

```math
A_W<B_W\quad\Longleftrightarrow\quad
\begin{cases}
w_l>-L_2/2,\\
w_h<g+L_2/2,\\
w_h<g+L_2w_l/S,\\
w_h-w_l<g.
\end{cases}
```

The third inequality is the positive-turn upper comparison, and the fourth
is the G1>0 upper comparison. For wl<0, the lower and width conditions imply
both other upper comparisons: `wh<g+wl<g+L2*wl/S` and `wh<g<g+L2/2`.
For wl>=0, the lower condition is automatic and the turn comparison implies
the width comparison; the independent `wh<g+L2/2` condition remains. At wl=0
the turn and width comparisons coincide. Setting wl=wh recovers section 2.

Even exact failure of this common-enclosure intersection does not establish
failure for the one authentic source value enclosed by W. Enclosure width
and discarded correlations can already lose coverage before outward
primitive rounding introduces further loss. Actual computed p/q must also
retain every supported endpoint direction and guard in the
[registered operation table](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT05.md#4-literal343-operation-rows)
and [readiness guards](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT05.md#5-literal50-readiness-rows-and-interleave).
G15's q attachment alone cannot identify which exact or rounded loss occurred.

## 4. Closed values and regular interiors

When A_W<B_W, every A_W<tau<B_W and every w in W satisfy
`|tau|<1/2`, `0<G1<g<L1` and `tau<w/S`. Thus F1,F2 are positive and the
original selected positive-gamma reconstruction has a regular interior.
The [positive-turn proof](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md#3-same-original-positive-gamma-solution)
retains the original lengths, determinant sign and square-root branch.

At the closed endpoints the inequalities become non-strict. With g<L1,
both root arguments remain positive; G1=0 or g, a shin-slab boundary, or the
straight-turn boundary `tau=w/S` may occur. These endpoints are legal exact
value operands under the stated premises. Their availability supplies no
strict interior membership, original phase derivative certificate or strict
hip certificate. In particular, the straight-turn boundary cannot lend an
inverse radial derivative to a later computation. The actual stored Y must
still be strictly inside an earned common height interval and pass direct
original checks.

The original closed ankle policy continues to allow its own legal pitch
boundaries. This open construction subset remains conservative. F1=0,
`rho=|x|`, h=0 or missing supported domains invalidate the corresponding
regularity arguments; continuity of a value supplies no missing denominator.

## 5. Retained descent dependence

The [original knee/leg expressions](../src/origin_boarding_planted_legs.cpp#L3468)
use `U=(-x,h,0)/rho` and

```math
K-H=R_s(-F_1U-G_1e_z),\qquad
F_1=\alpha\rho+\gamma z,\quad G_1=\gamma\rho-\alpha z.
```

For the authentic exact nominal unit axis `u=(K-H)/L1`, yaw preserves Y, so

```math
s_\mathrm{down}=-u_Y=\frac{F_1}{L_1}\frac{h}{\rho},\qquad
\frac{h}{\rho}=\sqrt{1-\frac{x^2}{\rho^2}}.
```

Fix the same x,w on the restricted regular interior and further require
`rho>|x|`. The [radial proof](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md#2-one-regular-directed-subbranch)
gives rho'>0. Direct differentiation, with every denominator positive, yields

```math
F_1'=\frac{L_2G_1}{F_1}>0,\quad
\rho'=L_2\left(\frac{G_1}{F_1}-\frac{\tau}{\sqrt{1-\tau^2}}\right)>0,
```

```math
v=\frac{h}{\rho}>0,\quad
v'=\frac{x^2\rho'}{\rho^3v}\ge0,\quad
s_\mathrm{down}'=\frac{F_1'v+F_1v'}{L_1}>0.
```

This covers x=0, where v=1 is constant and F1 still strictly increases.
For x!=0 both factors increase. On the same connected regular radial image,
`Y=a+sqrt(rho^2-x^2)` increases with rho; its composition with the radial
inverse therefore gives increasing exact descent with Y. These derivative
claims retain the regular interior and fixed authentic chart. They provide
no derivative authority at a boundary or a monotonicity theorem for every
original branch or the full original joint predicate.

The [original hip expression](../src/origin_boarding_planted_legs.cpp#L4415)
directly computes transverse `sqrt(uX^2+uZ^2)`. For the authenticated exact
unit identity this equals `sqrt(1-s_down^2)` in a theorem. If ell>0 is the
original axial slab length and b>0 the original capsule radius, the exact
all-caps extent is `E(s)=-ell*s+b*sqrt(1-s^2)`. It strictly decreases on
0<s<1 and is continuous at 1. Greater exact descent improves this sufficient
inequality on the proved domain.

Runtime verification must retain the direct transverse expression and its
supported whole enclosure. A rounded unit-bound identity, normalized
midpoint or reporting angle cannot replace it. Production certifies supported
records, a negative-axis upper bound and strict extent below the pelvis
bottom; the gap record remains supported without an extra positive-gap
predicate, as recorded by [#521](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT04.md#12-test-contract-alignment).
Exact equality retains the [containment theorem's meaning](ORIGIN_BOARDING_HIP_CONTAINMENT_AUDIT.md#all-caps-complement-and-equality);
the implemented strict predicate is preserved.

## 6. Conditional chart-factor information

If an independently earned argument on this same domain supplies
`h>=h_min>0` and `|x|<=X_max` with X_max>=0, monotonicity in positive h and
absolute x gives

```math
\frac{h}{\rho}=\frac{1}{\sqrt{1+(x/h)^2}}
\ge\frac{h_\min}{\sqrt{X_\max^2+h_\min^2}}.
```

If the original roll bound also holds on that domain, both lower bounds hold
and their maximum is a valid exact lower factor. This retains a possible
source of chart information that the uniform roll factor omits. The audit
earns no h_min, X_max, supported enclosure, improved threshold or cut. Those
premises require their own source-bound derivation; a guessed height, hidden
G15 operand or sampled posture cannot provide them. No new construction is
chosen here.

PORT and STAR retain separate `x_s,w_s,a_s` and uncertainty dependencies.
Their individually regular images must intersect for one common Y satisfying
both `h_s>0` and every original ankle/reach/roll/hip condition. Separate
nonempty side intervals supply no common-Y conclusion. Exact monotonicity
likewise supplies no finite outward image or stored-packet acceptance.

## 7. Completion and next obligations

| Premise or retained evidence | Earned conclusion | Remaining obligation |
|---|---|---|
| Finite positive lengths, 0<g<L1, scalar signed w | Section 2 is necessary and sufficient for the strict restricted tau component. | Actual w/g support and signs remain unevaluated; broader branches remain unknown. |
| Ordered finite W and the same fixed g | Section 3 characterizes one common positive-width interval for all enclosed w. | Pointwise/source feasibility and lost dependencies remain distinct. |
| Strict selected interior and rho>|x| | Exact descent increases with tau and Y on the same regular image. | Supported direct WORLD-axis and original phase checks remain mandatory. |
| Separately earned h_min/X_max on that domain | The conditional factor inequality is proved. | Its source-bound premises, usefulness and outward recipe remain unearned. |
| Two individually valid side images | Each retains its own origin and constraints. | A common-Y intersection, actual stored membership and fresh authority remain required. |
| Retained G15/O082 refusal and attached q | Endpoint05 did not earn its selected interval or a generated height. | p, dominating cuts and the cause of coverage loss remain unknown. |

The generic restricted-cut and descent lemmas are closed conditionally.
The next useful proof obligation is a source-bound same-domain chart-factor
argument and BOTH common-height intersection, with complete supported
enclosure directions. Whether this supplies a usable constructor remains
unknown. If pursued, that derivation and then any literal arithmetic/schema,
matched independent Test, current-call authority, complete ownership/resource
admission and new once-only first observation require separately scoped
children. This audit selects none of those programs or control values.

Existing constructor/source/geometry/joint/slab/owner policies and every
frozen observation stay unchanged. Full original phase, contact/load, all
SELF pairs, WORLD/material/HALO, continuous reversible route/seat, actor
movement, save/dynamics and First Flight acceptance remain separate.
Parents #462, #361 and #352 remain open. No all-height impossibility theorem
or additional witness program is required to close this bounded audit.

The subsequent [same-domain chart-factor proof](ORIGIN_BOARDING_CHART_FACTOR_CONSTRUCTION.md)
derives that conditional base/factor/BOTH-intersection implication. It evaluates
no source bound and selects no arithmetic program or candidate.
