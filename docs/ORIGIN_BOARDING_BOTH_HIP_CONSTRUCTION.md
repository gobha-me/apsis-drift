# Conditional BOTH-hip root-height construction proof

The preserved original roll constraint supplies a useful lower bound on thigh descent. Combined with one radical restriction of the already proved ankle parameter component, it conditionally produces a connected root-height interval satisfying BOTH exact nominal all-caps inequalities as well as the existing ankle/reach/roll conditions. This is a sufficient construction subset, not an exclusion of every other height or branch. Actual source support, nonempty intersection, stored height, resources and original compiler acceptance remain UNKNOWN. This proof registers no literal arithmetic program, public API, finite Test roster, numerical query or new candidate.

The retained [hip-plane observation](ORIGIN_BOARDING_HIP_JOINT_PLANE_METHOD01.md#9-retained-implementation-and-first-observation-515) proves strict unowned interior for the one original Endpoint04 height. Its existence conclusion motivates a different source-derived construction; it does not authorize fitting the posture, changing the joint slab, radius, original capsule, body, source feet or policy. This proof preserves the original immutable chart and starts from its symbolic recipe, never an old constructor followed by correction.

This [#517](https://github.com/gobha-me/apsis-drift/issues/517) source/equation prerequisite follows merged [#515](https://github.com/gobha-me/apsis-drift/issues/515) at `f0505279c74412359bdac328934454bb430a1524`. The source and symbolic proof below remains conditional; exact arithmetic registration and implementation require a later child.

## 1. Same nominal source and frame

For each side s, retain the original root X/Z/yaw, independent sole yaw and source sole C_s. PORT has sigma=-1 and STARBOARD sigma=+1. The original graph constructs

    H_s=P+R_0(sigma_s*a_hip,0,0),
    A_s=C_s+(0,a_ankle,0),
    d_s=R_s^T(A_s-H_s)=(x_s,-h_s,z_s).

All constants denote their unchanged stored binary64 values lifted into exact-real expressions. The [original hip/ankle chart](../src/origin_boarding_planted_legs.cpp#L3430) and [proper yaw recipe](../src/origin_boarding_planted_legs.cpp#L3242) are the authority. A rational yaw rotation has an exact Y column and preserves Y; its other exact columns form a proper orthonormal frame. Independently rounded enclosure columns are not themselves assumed orthonormal.

Write a_s=C_s.Y+a_ankle, h_s=Y-a_s>0, w_s=-z_s and rho_s=sqrt(x_s²+h_s²). Both distinct nonzero origins a_s and both signed w_s remain present. With fixed source chart and root X/Z, x_s,w_s are independent of Y. Neither side can borrow the other's sign, yaw or sole-plane expression. The [ankle audit](ORIGIN_BOARDING_ANKLE_CONTRACT_AUDIT.md#genuine-chart-and-applicability) proves these nominal frame/link premises without a reporting midpoint inverse.

## 2. Roll factor and genuine thigh descent

Suppress s until the final intersection. The original roll margin is

    h*b-|x|,  b=2-sqrt(3)>0.

Its [ordered sector expression](../src/origin_boarding_planted_legs.cpp#L3534) uses the same local displacement and negative dY. A supported nonnegative margin implies the exact inequality |x|<=b*h. Squaring with h>0 gives

    h/rho=1/sqrt(1+(x/h)²) >= c_roll,
    c_roll=1/sqrt(1+b²),  0<c_roll<1.

The original positive-gamma knee expression has U=(-x,h,0)/rho and

    K-H=R_s(-F1*U-G1*ez),
    F1=alpha*rho+gamma*z,
    G1=gamma*rho-alpha*z.

The [actual expressions](../src/origin_boarding_planted_legs.cpp#L3480) and [link identity](ORIGIN_BOARDING_ANKLE_CONTRACT_AUDIT.md#positive-branch-link-and-shin-identities) give F1²+G1²=L1² and genuine u=(K-H)/L1. Yaw preserves WORLD Y, so on the conservative F1>0 branch

    s_down=-uY=(F1/L1)*(h/rho) >= c_roll*F1/L1.

This does not normalize an interval midpoint, use a report-only angle, or change Unit24. The [original Unit24 construction](../src/origin_boarding_planted_legs.cpp#L8297) later subtracts actual nominal knee minus hip and divides by the unchanged link length after authentic source/body gates.

## 3. All-caps threshold, signs and equality

Keep original radius r, axial slab limit L and pelvis half-height hy, requiring

    r>0;  0<hy<L;  r<=L<L1.

For genuine downward unit axis, the original all-caps extent is

    E=L*uY+r*sqrt(uX²+uZ²)
     =-L*s_down+r*sqrt(1-s_down²).

The [containment audit](ORIGIN_BOARDING_HIP_CONTAINMENT_AUDIT.md#all-caps-complement-and-equality) proves that exact E<=-hy excludes all outside-slab capsule points from the pelvis, including both original caps. Exact equality suffices because t>L and uY<0 produce a strict vertical inequality. The implemented [self02 hip expression](../src/origin_boarding_planted_legs.cpp#L4415) nevertheless requires supported uY.upper<0 and E.upper<(-hy).lower strictly. This construction targets strict inequality and preserves that policy.

Define f(s)=L*s-r*sqrt(1-s²) on0<=s<=1. It is continuous and strictly increasing, with f(0)=-r<hy and f(1)=L>hy. Its unique equality root satisfies L*s-hy>=0 before squaring. Expanding yields

    (L²+r²)*s²-2L*hy*s+(hy²-r²)=0,
    s_req=(L*hy+r*sqrt(L²+r²-hy²))/(L²+r²).

The denominator/radicand are positive. The plus root satisfies L*s_req-hy>0: the necessary radical comparison follows from (L²+r²)*(L²-hy²)>0. Writing D=L²+r², the range comparison also gives (D-L*hy)²-r²*(D-hy²)=D*(L-hy)²>0 with D-L*hy>0. Thus the positive plus root is strictly below1. Its quadratic identity, range and positive L*s_req-hy show that it satisfies the unsquared equation and is the unique root in(0,1); the other algebraic root cannot replace it without its missing sign test. If hy=L, only s=1 reaches equality and strict success is impossible for this sufficient criterion. If hy>L, the criterion cannot succeed. These are not general containment exclusions.

Set k=s_req/c_roll. Require0<k<1. Then F1/L1>k implies s_down>s_req and E<-hy. If k>=1, the shortcut is unavailable; actual h/rho may exceed c_roll, so the roll-only restriction can be conservative. It does not prove the full family impossible.

## 4. Directed radical restriction

Reuse the [proved ankle parameterization](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md#2-one-regular-directed-subbranch):

    G2=L2*tau; F2=L2*sqrt(1-tau²),
    G1=w-L2*tau; F1=sqrt(L1²-G1²)>0.

The existing open component has positive knee turn and radial monotonicity; F1>0 and positive knee alone do not imply G1>=0. Introduce g_cap=L1*sqrt(1-k²)>0 and the conservative restriction

    (w-g_cap)/L2 < tau < w/L2.

Its upper inequality explicitly earns G1>0, even for signed negative w. Its lower earns G1<g_cap. Therefore F1>L1*k and the strict all-caps conclusion follows. No sign of the current source w is assumed or evaluated.

This positive-F1/positive-G1 component also makes the two exact original hip-pitch margins positive: G1*cos20+F1*sin20 and F1*(sqrt3/2)+G1*.5, from [phase_hip_sector](../src/origin_boarding_planted_legs.cpp#L3786). Those are the current directed hip[-20,120] policy, not the older65-degree API. Supported original margins, knee/axial sectors and timing remain separate actual verification gates.

## 5. Universal outward/inward cut directions

Let W enclose the same exact signed w. Supported enclosures of s_req and c_roll, with positive denominators, permit publication of a finite singleton k_bar from the quotient upper endpoint, with0<k_bar<1. Thus k_bar>=k. Use original reciprocal-enclosure-then-multiply division; constants and singleton inputs do not remove outward rounding.

Enclose L1*sqrt(1-k_bar²) and retain a positive lower endpoint g_cut. Then0<g_cut<=L1*sqrt(1-k_bar²). Let base_p/base_q be the existing universal ankle/positive-knee cuts. Define prospective inward endpoints

    p=max(base_p, upper((W-point(g_cut))/L2)),
    q=min(base_q, lower(W/L2)),
    p<q.

For every w in W and every p<tau<q, the quotient directions imply0<w-L2*tau<g_cut. Consequently F1/L1>k_bar>=k. This is a universal statement about the same source uncertainty enclosure, not separate endpoint samples or an assumed positive W. An unsupported result, nonpositive g_cut, k_bar>=1 or p>=q leaves the construction unavailable.

Closed endpoints are legal value operands. For every w in W and p<=tau<=q, the new cuts give0<=G1<=g_cut and F1>=sqrt(L1²-g_cut²)>0. Base cuts retain |tau|<=1/2, both thigh radicand bounds and the closed positive-knee boundary. A straight-knee equality may remain at an endpoint; no phase derivative certificate is issued there. Interior tau retains strict knee positivity and the regular original coefficient domains. Supported root arguments must still be earned; any domain intersection must use the independently proved universal inequality, not clip an unsupported or actually negative value.

## 6. Radial image and actual root-height projection

For the same actual w, use

    r_w(tau)=sqrt(L1²-(w-L2*tau)²)+L2*sqrt(1-tau²).

It is continuous on the closed cuts and strictly increasing inside by the [positive-turn proof](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md#2-one-regular-directed-subbranch). Restricting the old component does not create another branch or require a generic root search. Supported endpoint evaluations enclose r_w(p),r_w(q). Retain u=R_p.upper, v=R_q.lower with0<=u<v. Every actual rho in(u,v) then has one strict interior tau, by continuity and monotonicity. It satisfies the original positive-gamma reconstruction, ankle condition and new hip descent restriction. Empty inward radial image is not all-height impossibility.

Reuse the [inward height projection](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md#5-inward-projection-onto-actual-root-height). For each side keep X enclosing signed x, A enclosing its own a_s and supported X2=SQUARE(X). The lower height uses the upper certified root of max(0,upper(point(u)²-point(X2.lower))) and the outward upper addition to A.upper. The upper height uses the lower root of lower(point(v)²-point(X2.upper)), requiring a positive radicand, and the outward lower addition to A.lower. Strict Y between these bounds gives h>0 and u<rho<v for the same actual source x,a_s.

Intersect both new height brackets with the preserved BOTH inward reach/roll brackets. A nonempty finite intersection conditionally gives a connected open height subset with BOTH nominal ankle/reach/roll and hip containment. BOTH sides use their own signed horizontal displacement and vertical origins; the common Y does not make them interchangeable. A later stored midpoint must actually satisfy both strict endpoint inequalities after rounding; no mathematical midpoint identity substitutes for stored membership.

## 7. Direct verification and completion boundary

The f(s) threshold used the exact unit identity only as a sufficient construction proof. A later constructor must directly recompute and verify the original supported transverse expression sqrt(uX²+uZ²), negative uY and strict original extent gap for BOTH actual generated nominal axes. It must not replace that expression by sqrt(1-uY²) on independently rounded intervals or manufacture orthogonality/normalization authority. All preserved original reach, ankle/roll, positive-shin, derivative, joint-sector and timing gates remain mandatory.

| Earned premise | Conditional conclusion |
|---|---|
| Authentic source charts/link identity and roll margin | Genuine descent factor bounded below by c_roll*F1/L1. |
| L>hy>0,r>0 and k_bar<1 | Supported threshold/radical domain can be attempted; no source feasibility is asserted. |
| Universal signedW cuts, p<q | Interior G1>0 and F1/L1>k, hence strict exact BOTH complement when applied separately. |
| Closed endpoint support and inward radial/height images | Actual interior Y reconstructs the same original positive-gamma branch. |
| Nonempty BOTH height intersection and stored membership | One prospective conditional source-bound height; original acceptance still unearned. |
| Missing support or empty component | Unavailable construction evidence; no policy relaxation or family impossibility. |

Source/symbolic review can close this conditional lemma before literal arithmetic registration. A future registration must freeze the one new immutable generated packet/current-call authority, charged primitives/guards/partial evidence, independent direct oracle and complete ownership/resources before implementation or evaluation. Old reports/Key/Context/Token cannot authorize a new Y. Construction/source scratch dies before a fresh unchanged full graph, body, contact/load and all SELF pairs. No success is inherited from the retained Endpoint04 cell.

Preserve creator8192/worker49152/helper4096/output16MiB, complete historical enrollment/graph32768, current/pending headers/requests/refusals/returns, original audits and separately nonzero world/process/presentation accounts. Actual layout and native error/allocator/cleanup bounds remain future gates. Other SELF regions, WORLD/material/HALO, continuous forward/reverse acquisition/load, route/seat and First Flight remain unearned. An explicit witness program or all-Y characterization is unnecessary to exhibit this conditional sufficient subset; neither is silently selected if it is empty.

This source/symbolic proof evaluates no source operands, coordinates, angles, derived numerical bounds or alternative candidate. No literal primitive/API/Test registration or implementation is authorized here. Ordinary publication checks on unchanged existing source do not select a new numerical construction.

The [Endpoint05 registration](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT05.md) and its [matched Test manifest](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT05_TEST_MANIFEST.md) specify the finite constructor and admission constraints in [#519](https://github.com/gobha-me/apsis-drift/issues/519). They evaluate no new posture and leave actual feasibility and original endpoint acceptance unknown.

The subsequent [knee-cut coverage audit](ORIGIN_BOARDING_KNEE_CUT_COVERAGE_AUDIT.md)
classifies the exact restricted scalar/common-enclosure intervals and retains
conditional actual descent dependence after Endpoint05's unavailable result.
It changes no registered constructor and selects no new candidate or program.
