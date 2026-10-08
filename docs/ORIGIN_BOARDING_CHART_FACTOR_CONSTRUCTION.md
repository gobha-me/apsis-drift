# Conditional same-domain chart-factor construction

Issue [#527](https://github.com/gobha-me/apsis-drift/issues/527), after the
[knee-cut coverage audit](ORIGIN_BOARDING_KNEE_CUT_COVERAGE_AUDIT.md).
This source/equation proof derives a conditional BOTH-height subset using the
actual chart factor. It first fixes an independent ankle/reach/roll domain,
earns factor bounds there, and intersects both later hip-restricted images
with that **same domain**. No actual interval, improved factor or usable height
is earned here. No source operand, sign, derived bound, posture or candidate
was evaluated; no literal arithmetic program, control, API or Test roster is
selected.

## 1. Authentic operands and independent base

For each side s retain the [original chart](../src/origin_boarding_planted_legs.cpp#L3430):

```math
d_s=(x_s,-h_s,z_s),\quad w_s=-z_s,\quad
a_s=C_s.Y+\text{stored ankle offset},\quad
h_s=Y-a_s>0,\quad \rho_s=\sqrt{x_s^2+h_s^2}.
```

The fixed original root X/Z/yaw, separate sole yaw and signed hip offsets make
x_s,w_s,a_s independent of Y. Keep the distinct PORT and STARBOARD origins and
all source dependencies. Exact proper yaw preserves WORLD Y and norms;
independently rounded frame columns do not provide orthogonality authority.
Stored binary64 source constants retain their exact lifted meaning.

Let supported finite X_s,W_s,A_s enclose those authentic x_s,w_s,a_s, and keep
the original positive link lengths L1,L2. First derive only the
[original BOTH-ankle subset](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md#2-one-regular-directed-subbranch),
before any hip threshold or G1 cap. Its regular positive-F1/positive-knee
component has

```math
G_1=w-L_2\tau,\quad F_1=\sqrt{L_1^2-G_1^2},\quad
r_w(\tau)=F_1+L_2\sqrt{1-\tau^2},
```

with `|tau|<1/2`, `-L1<G1<L1` and `tau<w/(L1+L2)`.
It does not yet require G1>0. The existing proof establishes continuity at
legal closed endpoints, strict radial increase inside, and reconstruction of
the same original positive-gamma solution.

Conditionally earn each side's universal base cuts p0_s<q0_s with the
[existing supported inward directions](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md#4-supported-inward-radial-endpoint-construction):
the lower cut uses the upper quotient endpoint; each upper cut uses its lower
quotient endpoint. Supported closed radial endpoint evaluations then give
`u0_s=R(p0_s).upper`, `v0_s=R(q0_s).lower`, with `0<=u0_s<v0_s`.
These are premises, not performed evaluations or selected operation rows.

With supported X2_s enclosing x_s squared, the
[inward height projection](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md#5-inward-projection-onto-actual-root-height)
and independent reach/roll bounds have the following directions. Every square,
subtraction, root, quotient and addition below must retain its supported whole
enclosure; point inputs do not make rounded arithmetic exact.

| Bound | Required inward direction |
|---|---|
| Ankle lower | A_s.upper plus the upper root of `max(0, upper(u0_s^2-X2_s.lower))`; retain the outward sum's upper endpoint. |
| Ankle upper | A_s.lower plus the lower root of `lower(v0_s^2-X2_s.upper)`; require that lower radicand positive and retain the sum's lower endpoint. |
| Reach lower | A_s.upper plus the upper root of `max(0, upper(m.upper-C.lower))`; retain the sum's upper endpoint. |
| Reach upper | A_s.lower plus the lower root of `lower(M.lower-C.upper)`; require positivity and retain the sum's lower endpoint. |
| Roll lower | A_s.upper plus the upper endpoint of supported `ABS(X_s)/b`; require b.lower>0 and retain the sum's upper endpoint. |

Here C encloses x_s squared plus z_s squared, m the original squared link
difference, M the squared link sum, and b encloses the exact `2-sqrt(3)`.
The max-zero lower projections use an independently proved nonnegative squared
height; unsupported arithmetic cannot be repaired by clipping. All root and
division domains remain explicit prerequisites.

Let ell0 be the maximum of both sides' earned ankle/reach/roll lower bounds,
and u0 the minimum of their ankle/reach upper bounds. Require finite ell0<u0
and fix

```math
I_0=(\ell_0,u_0).
```

Every Y in this conditional base lies on both authentic regular ankle images,
has h_s>0, and satisfies the exact nominal reach/roll conditions. The base
depends on no hip factor, threshold, restricted cap, later image or selected
height. This reuses source identities; it does not call Endpoint04/05 or import
their Y, Key or acceptance. Actual base support and nonemptiness remain unknown.
The broader original hip/knee/axial/timing predicates still require direct
verification; this is not a claim that the full original graph accepts I0.

## 2. Uniform height and lateral premises on that same base

For each side conditionally earn finite H_s and Xmax_s satisfying

```math
0<H_s\le\ell_0-A_s.\mathrm{upper},\qquad
Xmax_s\ge |x_s|,\qquad Xmax_s\ge0.
```

A supported subtraction of point(ell0) and point(A_s.upper), retaining its
lower endpoint, supplies a sufficient H_s **only if that endpoint is positive**.
A supported ABS(X_s), retaining its upper endpoint, supplies Xmax_s. These
directions bind the same original source enclosures used by I0. No source sign
or numerical bound is assumed. Merely knowing h_s>0 for each Y in an open
interval does not give a positive uniform H_s; failure of this stronger premise
leaves the chart-floor argument unavailable.

For every Y in I0,

```math
h_s=Y-a_s>\ell_0-A_s.\mathrm{upper}\ge H_s>0.
```

No later hip-restricted lower endpoint may replace ell0 in this proof. If the
cap itself needed that later factor, doing so would be circular. No artificial
base truncation, trial height or fallback program is chosen here.

## 3. Directed chart-factor bound

For positive h, the exact ratio `h/sqrt(x^2+h^2)` is nondecreasing with h and
nonincreasing with |x|. Thus the same H_s appears in numerator and denominator:

```math
\frac{h_s}{\rho_s}\ge c_{chart,s}
=\frac{H_s}{\sqrt{Xmax_s^2+H_s^2}},\qquad 0<c_{chart,s}\le1.
```

Xmax_s=0 is included and gives exact c_chart,s=1. There is no strict-less-than-one
premise for the factor itself. A future supported enclosure of the displayed
denominator must earn a positive division domain. Its upper endpoint with a
supported quotient's lower endpoint gives a conservative factor bound; using
an actual or differently approximated height only in the denominator would
not establish this inequality. All square/root/quotient support remains
conditional; no primitive sequence is frozen here.

The [original roll proof](ORIGIN_BOARDING_BOTH_HIP_CONSTRUCTION.md#2-roll-factor-and-genuine-thigh-descent)
also supplies, on I0,

```math
c_{roll}=\frac{1}{\sqrt{1+(2-\sqrt3)^2}}.
```

Supported lower endpoints for these two factors can be combined by their
maximum to obtain a finite c_s with
`0<c_s<=max(c_roll,c_chart,s)<=h_s/rho_s<=1` throughout I0.
Both bounds must have their own earned support. Taking their maximum is sound
because their domain is the same. It does not establish strict improvement
over the exact roll factor or over another program's rounded bound.

The [authentic original knee expression](../src/origin_boarding_planted_legs.cpp#L3468)
gives the genuine nominal unit axis u=(K-H)/L1 and

```math
s_{down}=-u_Y=\frac{F_1}{L_1}\frac{h_s}{\rho_s}.
```

This exact identity, rather than a reporting angle or normalized midpoint,
connects the chart factor to the original WORLD-axis descent.

## 4. Conditional threshold and universal restricted component

Keep the unchanged original capsule radius r, axial slab length L and pelvis
half-height hy, with the existing threshold premises `r>0`, `0<hy<L` and
`r<=L<L1`. The
[unsquared plus-root proof](ORIGIN_BOARDING_BOTH_HIP_CONSTRUCTION.md#3-all-caps-threshold-signs-and-equality)
earns the unique t=s_req in (0,1):

```math
t=\frac{L\,hy+r\sqrt{L^2+r^2-hy^2}}{L^2+r^2}.
```

Conditionally retain a finite upper quotient k_s with
`0<t/c_s<=k_s<1`, and a supported positive lower cap
`0<g_s<=L1*sqrt(1-k_s^2)<L1`.
For Y in I0 whose authentic auxiliary value additionally satisfies
`0<G1=w_s-L2*tau<g_s`, positive F1 gives

```math
\frac{F_1}{L_1}>k_s,\qquad
s_{down}>c_s k_s\ge t.
```

Consequently the exact all-caps extent
`-L*s_down+r*sqrt(1-s_down^2)` is strictly below -hy.
Each side retains its own c_s, k_s and g_s. Neither can borrow the other side's
stronger factor. The threshold and cap are conditional proof operands; no
actual value or program has been selected.

For W_s=[wl_s,wh_s], intersect the original universal ankle/positive-turn cuts
with the upper endpoint of `(W_s-point(g_s))/L2` as a lower cut and the lower
endpoint of `W_s/L2` as an upper cut. The
[exact common-enclosure theorem](ORIGIN_BOARDING_KNEE_CUT_COVERAGE_AUDIT.md#3-one-common-interval-for-a-signed-enclosure)
characterizes a positive-width common strict component by all four conditions:

```math
wl_s>-L_2/2,\quad wh_s<g_s+L_2/2,\quad
wh_s<g_s+L_2wl_s/(L_1+L_2),\quad wh_s-wl_s<g_s.
```

These concern one interval valid for every enclosed w, with signed w retained.
The future supported inward p_s/q_s must independently earn p_s<q_s;
exact compatibility removes neither enclosure width nor rounding loss.
The G1>0 restriction is still a sufficient construction subset, not the full
original hip policy. Missing support or an empty common interval does not
exclude the authentic source or every original branch.

## 5. New images and the indispensable base intersection

At p_s and q_s, the proved closed cuts give `0<=G1<=g_s<L1`, `|tau|<=1/2`
and the closed positive-knee bound. Roots are legal exact value operands;
straight-knee equality may occur, and no endpoint derivative authority follows.
Supported radial enclosures conditionally give
`u_s=R(p_s).upper`, `v_s=R(q_s).lower`, with `0<=u_s<v_s`.
Continuity and strict interior radial increase then place every actual rho
in (u_s,v_s) on one strict interior original positive-gamma solution.

Use the same supported inward ankle-height directions from section 1, with
u_s,v_s in place of u0_s,v0_s, to obtain separate finite height images
`I_s=(ell_s,upper_s)`. Keep X_s, X2_s and A_s bound to the same source chain.
The upper-height radicand must still have a supported positive lower endpoint.

These endpoints and images may extend outside I0. Their radical/image evidence
does **not** grant the chart-factor or hip guarantee there. The final set is

```math
I_* = I_0\cap I_{PORT}\cap I_{STAR},\qquad
\ell_* = \max(\ell_0,\ell_{PORT},\ell_{STAR}),\qquad
u_* = \min(u_0,upper_{PORT},upper_{STAR}).
```

If all endpoints are supported finite and ell*<u*, every Y in I* lies in the
independent base where both factors were earned and in both restricted radial
images. The chain is therefore noncircular:
`base -> same-base factors -> side caps -> side images -> intersection with base`.
It proves the conditional exact BOTH ankle/reach/roll and strict all-caps subset.
Individual side nonemptiness gives no common Y. Actual nonemptiness and
usefulness remain unknown; no source correlation is silently discarded as
authority or replaced by a sampled posture.

## 6. Completion, refusal and remaining gates

| Premise | Conditional conclusion | Unavailable or separate obligation |
|---|---|---|
| Authentic chart and supported base cuts/radial/reach/roll bounds | One independent I0 on both regular branches. | Base support, legal roots and ell0<u0 are not evaluated. |
| Same I0, lower height floor H_s>0 and lateral upper bound | Positive chart-factor lower bound on I0. | Pointwise h>0 is insufficient; either floor may fail. |
| Supported same-domain chart and roll factors | Their lower-bound maximum is safe. | Strict improvement is not guaranteed. |
| Supported threshold quotient k_s<1 and positive cap g_s | Interior restricted G1 gives strict exact descent/extent. | Quotient/radical support or cap positivity may fail. |
| Signed universal p_s<q_s and legal closed radial endpoints | Unique original strict interior radial preimage. | Exact/common-enclosure/rounding refusals remain distinct; no endpoint derivatives. |
| Supported new height images and I0 intersection with both sides | Every Y in nonempty I* has the conditional exact BOTH subset. | Finite inward images or common intersection may be unavailable. |
| A later stored height strictly in I*, with direct original checks | A future registered constructor may attempt fresh authority. | No stored height, direct acceptance, Key or full graph is earned here. |

The exact unit identity is a theorem premise only. Runtime must retain
[direct transverse evaluation](../src/origin_boarding_planted_legs.cpp#L4415)
`sqrt(uX^2+uZ^2)`, supported whole records, negative uY upper bound and strict
extent below the pelvis bottom. The supported gap record has no additional
positive-gap predicate. Original reach/roll/ankle, hip/knee/axial sectors,
derivative domains, workspace and timing remain mandatory. Stored membership
cannot be inferred from an ideal midpoint.

This conditional same-domain proof is closed without claiming an actual source
interval or selecting a numerical recipe. Literal arithmetic/schema and a
matched independent finite Test registration require a separate child before
implementation, complete both-compiler ownership/resource admission and a new
once-only observation. Numerical-worker ceilings and separately nonzero
world/process/presentation accounts are preserved; no new layout or resource
fit is claimed. Original source packets, geometry, policies, Endpoint05 and
every frozen observation remain unchanged.

Full graph/body/contact/load/SELF, WORLD/material/HALO, moving forward/reverse
acquisition, route/seat, actor/save integration and First Flight remain separate
and unqualified. Parents #462/#361/#352 stay open. No all-height impossibility
theorem or additional witness program is needed to close this proof child.

The subsequent [Endpoint06 registration](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT06.md)
and [matched independent Test](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT06_TEST_MANIFEST.md)
specify one literal base-first program under this conditional proof. Actual
implementation, admission and numerical observations remain separate gates.
