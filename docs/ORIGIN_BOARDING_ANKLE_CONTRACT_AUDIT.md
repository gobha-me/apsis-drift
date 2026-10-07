# Original BOTH-ankle contract audit

This source/equation audit completes the bounded prerequisite in [#503](https://github.com/gobha-me/apsis-drift/issues/503), after #501. It preserves the original compiler, policies and retained observations. It selects no endpoint, constructor, root-height range, numerical query or new runtime interface. Parent #462, #361, #352 and First Flight remain open.

The retained [SectorEvidence02 observation](ORIGIN_BOARDING_INTERMEDIATE_SECTOR_EVIDENCE02.md#retained-501-outcome) attributes the unchanged Endpoint03 program's PORT ordinal 4 to `ankle_pitch`, with a supported selected margin whose upper endpoint is strictly negative. The original result remains unresolved `joint_sector`, before body generation. This excludes that program's original ankle necessary predicate. It establishes neither collision nor infeasibility of all root heights or other postures. STARBOARD ankle and later qualifications remain unearned. The existing reach-and-BOTH-roll construction does not establish ankle acceptance.

## Genuine chart and applicability

All scalar literals below mean their original stored binary64 values lifted into exact-real expressions. Let `s=0` be PORT with `sigma_s=-1`, and `s=1` be STARBOARD with `sigma_s=+1`. The compiler constructs

```math
H_s=P+R_0(\sigma_s\,\text{stored } .14,0,0),\qquad
A_s=C_s+(0,\text{stored } .1,0),\qquad
d_s=R_s^T(A_s-H_s).
```

`P` and `C_s` come from the actual root/sole constant-term packets and their phase carriers; `C_s` also includes the declared swing hump. `R_0` and `R_s` are the actual root and independent sole yaw expressions. Torso lean does not replace the upright pelvis frame or the sole chart: [graph construction](../src/origin_boarding_planted_legs.cpp#L3394), [hip/ankle and local displacement](../src/origin_boarding_planted_legs.cpp#L3430), and [registered phase domain](ORIGIN_BOARDING_ROUTE_FOOT_PHASE01.md#exact-controls-and-domains).

For each original yaw carrier `k`, the exact underlying columns are `(c,0,-v)`, `(0,1,0)`, `(v,0,c)`, where `c=(1-k²)/(1+k²)` and `v=2k/(1+k²)`. The positive denominator and identity `c²+v²=1` give an orientation-preserving rotation about Y. Thus `phase_local`'s three column dot products implement the exact transpose/inverse of that same rotation. This proves the nominal chart's applicability; it does not assert that independently rounded interval columns are numerically orthonormal. The compiler retains outward value/first/second enclosures of the expressions: [rotation and local transform](../src/origin_boarding_planted_legs.cpp#L3239). No caller matrix, midpoint inverse, normalization or stored affine report supplies this premise.

Suppress the side subscript for the following identities, applied separately to both authentic charts. Write `d=(x,y,z)`, `rho²=x²+y²`, `D=rho²+z²`, and `rho=sqrt(rho²)`. Before the shin direction is available, the original leg requires supported value and derivative jets for `D/rho²`, `y.upper<0`, positive `rho².lower/D.lower`, sufficient original reach inclusion, supported `gamma²` with `gamma².lower>0`, and supported root/coefficient jets. Reach compares `D.upper` to the lower squared-sum bound and `D.lower` to the upper squared-difference bound. Equality of these compared endpoints is not itself refused; the subsequent strictly positive gamma domain remains mandatory. A straight/folded singularity does not acquire derivative completeness from a constant hold: [original branch/domain order](../src/origin_boarding_planted_legs.cpp#L3439) and [small-root jet](../src/origin_boarding_planted_legs.cpp#L2351).

## Positive-branch link and shin identities

Let `L1` and `L2` denote the fixed original stored thigh/shin lengths. Define the existing exact expressions

```math
\alpha=\frac{L_1^2-L_2^2+D}{2D},\qquad
\beta=1-\alpha,\qquad
\gamma=\sqrt{\frac{((L_1+L_2)^2-D)(D-(L_1-L_2)^2)}{4D^2}}>0,
```

```math
q=(xz/\rho,yz/\rho,-\rho),\qquad
K=H+R_s(\alpha d+\gamma q).
```

These are the actual [positive-branch coefficient/knee expressions](../src/origin_boarding_planted_legs.cpp#L3452); this audit introduces no alternate knee solution. Since `q·d=0` and `q·q=D`, the coefficient identity

```math
\gamma^2D=L_1^2-\alpha^2D=L_2^2-\beta^2D
```

proves `|K-H|²=L1²` and `|A-K|²=L2²` in exact reals under the proper-frame and positive-domain premises. The same identity follows by expanding the existing factored gamma numerator and substituting `2*alpha*D=L1²-L2²+D`; no new numerical residual is used as proof.

The compiler's [direction operands](../src/origin_boarding_planted_legs.cpp#L3478) are

```math
F_1=\alpha\rho+\gamma z,\quad G_1=\gamma\rho-\alpha z,\qquad
F_2=\beta\rho-\gamma z,\quad G_2=-(\beta z+\gamma\rho).
```

Consequently the sole-local thigh and shin vectors are

```math
R_s^T(K-H)=(xF_1/\rho,yF_1/\rho,-G_1),\qquad
R_s^T(A-K)=(xF_2/\rho,yF_2/\rho,-G_2).
```

Expanding the squared direction pairs cancels their cross terms:

```math
F_1^2+G_1^2=(\alpha^2+\gamma^2)D=L_1^2,\qquad
F_2^2+G_2^2=(\beta^2+\gamma^2)D=L_2^2.
```

Equivalently, `U=(-x,-y,0)/rho` and `ez=(0,0,1)` are exact orthonormal chart vectors: `d=-rho*U+z*ez`, `q=-z*U-rho*ez`, and the shin is `-F2*U-G2*ez`. This uses the genuine scalar chart and positive rho; independently rounded reported vectors are not substituted for these identities.

Also `F1*G2-G1*F2=-gamma*D<0`. For directions `theta_i=atan2(G_i,F_i)`, this gives `sin(theta1-theta2)=gamma*D/(L1*L2)>0`, the original positive knee turn, subject to the retained directed hip/knee sectors. It does not authorize an alternative angle wrap or branch.

Let `phi=atan2(x,-y)`. Because `y<0` and `rho>0`, `sin(phi)=x/rho` and `cos(phi)=-y/rho>0`. Each direction vector factors symbolically as

```math
(xF_i/\rho,yF_i/\rho,-G_i)
=R_z(\phi)R_x(\theta_i)(0,-L_i,0).
```

This is an exact-real explanation of the endpoint vectors, not a newly stored or reconstructed certified frame. With relative axial yaw equal to sole yaw minus root yaw, the factorization is consistent with the original root-relative leg order. PORT abduction is `-phi`, STARBOARD abduction is `+phi`; multiplying by their respective side signs gives the same `Rz(phi)`. Both sides derive ankle roll `-phi` and ankle pitch `-theta2`. There is no extra PORT/STARBOARD sign on `F2`, `G2` or the pitch cone. The ankle compensation applies inverse pitch followed by inverse roll, so

```math
R_sR_z(\phi)R_x(\theta_2)R_x(-\theta_2)R_z(-\phi)=R_s.
```

This retains the genuine flat sole frame and the original noncommuting rotation order. It matches [registered lateral frames and limits](ORIGIN_BOARDING_LATERAL_BODY02.md#registered-frames-and-limits) and the [phase pitch/roll convention](ORIGIN_BOARDING_ROUTE_FOOT_PHASE01.md#original-limits-directed-sectors-and-angular-rates); it does not introduce manual ankle controls or coupled pelvis pitch/abduction. The older lateral static policy keeps the flat boot at root yaw. The phase family extends that chart to independently authored sole yaw with derived relative hip axial yaw; its cancellation ends at `R_s`, not necessarily `R_0`. The corresponding original axial sector remains a separate obligation.

## Closed pitch cone and interval acceptance

The original exact ankle margin is

```math
\mu=\tfrac12F_2-\tfrac{\sqrt3}{2}|G_2|.
```

With `F2>0`, the principal shin direction lies strictly between `-pi/2` and `pi/2`. Dividing the nonnegative-margin inequality by the positive `F2` gives `|tan(theta2)|<=1/sqrt3`, equivalent on that chart to `|theta2|<=pi/6`. Conversely those directions give a nonnegative margin. Therefore the derived ankle pitch `-theta2` obeys the same closed ±30-degree cone. Both boundaries are included. The nonzero link identity means that a nonnegative exact margin cannot have `F2=0`; the compiler nevertheless requires an independently earned positive interval lower bound. Squaring an inequality without retaining its sign/chart premises cannot replace either gate.

The implemented value margin uses the original supported outward `sqrt3` bracket, the full interval absolute value of `G2`, and the literal ordered half-products/subtraction. It certifies the closed cone only if its supported **lower** endpoint is nonnegative. A supported **upper** endpoint below zero strictly excludes this necessary margin. `lower<0<=upper` is an inconclusive enclosure, despite the ordinary original refusal. An exact equality can also remain inconclusive when the enclosure straddles zero. Unsupported arithmetic supplies neither inclusion nor strict exclusion: [interval primitives](../src/origin_boarding_planted_legs.cpp#L58), [original constants](../src/origin_boarding_planted_legs.cpp#L3314), and [ankle margin/guard](../src/origin_boarding_planted_legs.cpp#L3540).

After all six sector margins pass, the separate `F2.value.lower<=0` test refuses the positive-shin domain. Its converse strict necessary violation is `F2.upper<=0`, including exact zero, rather than the pitch margin's `upper<0` rule. Positive F2 alone is not the ±30-degree cone; cone acceptance alone is not full derivative/timing admission. No epsilon, clamp, changed angle limit, square-root waiver or endpoint sample closes these gaps.

`pitch_derivatives` uses the proved constant link denominator `L2²` for the shin's first/second angular derivatives. The phase records its negation for ankle pitch and `-phi` for ankle roll, with physical duration scaling and first-derivative reversal. This derivative identity does not certify speed limits: the later ankle resultant speed includes both shin-pitch and roll rates. See [derivative identity](../src/origin_boarding_planted_legs.cpp#L523), [phase derivative reporting](../src/origin_boarding_planted_legs.cpp#L3498), and [later timing tests](../src/origin_boarding_planted_legs.cpp#L3564).

The legacy `report_angles` computes presentation degrees using midpoint sine/cosine bounds and `atan2`: [report-only angles](../src/origin_boarding_planted_legs.cpp#L337). These values are not the phase compiler's acceptance predicates or a replacement for its interval directions. The phase angular fields themselves contain derivative bounds, not certified angle magnitudes: [leg record](../include/apsis_drift/origin_boarding_route_foot_phase.hpp#L53).

## Finite premise/conclusion and availability table

|Row|Required premise and source behavior|Conclusion and remaining boundary|
|---|---|---|
|1. Both genuine charts|PORT/STARBOARD use their own sole yaw and hip offset signs; the exact rational yaw recipe is proper.|The same shin identities apply separately; rounded columns or another side's report cannot grant authority.|
|2. Regular positive branch|Supported jets, downward `y`, positive `rho²/D/gamma²`, original reach inclusion and positive gamma root.|Two exact nominal links and a fixed knee turn; singular or unsafe cases remain refused even on a hold.|
|3. Pitch/roll order|Authentic `(F2,G2)` and the original `Rz(phi)Rx(theta2)` factorization.|Inverse pitch then roll restores the sole frame; no added freedom or ankle sign change.|
|4. Closed pitch margin|Supported outward margin lower>=0 plus the later positive-F2 check.|Closed ±30-degree certificate; `lower<0<=upper` intervals remain unresolved and unsupported intervals unclassified.|
|5. Strict necessary exclusions|Authenticated supported margin upper<0, or separately supported F2 upper<=0 on its actual reached gate.|Failure of that program's necessary predicate; no collision or universal-posture conclusion.|
|6. Ordered refusal prefix|The six margin expressions are constructed before the loop, but each slot is copied and support/low checked in order: roll0, hip-lower1, hip-upper2, knee3, ankle4, axial5; F2 positivity follows all six.|An ankle4 refusal earns only prefix0..4. Axial5, positive-shin6 and the other side cannot be inferred from defaults or unvisited expression scratch.|
|7. Fresh attribution|Original side/work/condition and a supported reached prefix must belong to the same original call. The scalar refusal type has no support bit.|Only authenticated capture can earn selected-bound support/equality; generic nested doubles and default temporal metadata cannot substitute.|
|8. Later completeness|Each leg's sector flags follow all margins/F2; timing follows separately. PORT precedes STARBOARD; body and aggregate flags follow both legs.|Rates/nominal scratch are not speed, body, support, SELF, WORLD or route certificates. Body0 companions structurally stop even if both legs pass.|

The source order is explicit in [the original margin loop and positive-shin test](../src/origin_boarding_planted_legs.cpp#L3550) and [the original cell controller](../src/origin_boarding_planted_legs.cpp#L3715). `phase_refuse` returns ordinary unresolved for joint sectors and unsupported for unsupported arithmetic; it does not emit a strict-exclusion classification. Original `Bounds` alone retain no supported/evaluated flag: [refusal implementation](../src/origin_boarding_planted_legs.cpp#L3295) and [refusal record](../include/apsis_drift/origin_boarding_route_foot_phase.hpp#L106).

For #501, the fresh companion establishes that PORT's four predecessor margins pass and the selected ankle4 upper endpoint is negative; it does not earn the later F2 check or STARBOARD. Its capture verifies the actual original side/work, prefix and limiting-bound equality before classification: [SectorEvidence02 capture](../src/origin_boarding_intermediate_sector_evidence02.cpp#L278). The original private refusal's default `[0,0]` temporal fields do not change the authentic whole-hold `[0,1]` invocation or indicate a point query. The reviewed retained outcome remains unchanged.

## Preservation decision and root-height obligations

No source-contract contradiction is established. The original ±30-degree cone, positive branch, chart convention and inverse ankle order are consistent. Preserve this conservative contract, its restricted upright-pelvis phase domain, and every historical refusal. The broader anatomical sufficiency of the synthetic proxy is not established by this algebra; a separately justified model revision cannot be inferred from one failed program.

For a future fixed-foot/fixed-yaw/root-XZ family, exact yaw preserves Y, so changing only root height changes the local vertical displacement while the horizontal sole-local components retain their actual source-bound chart values. Nevertheless `rho`, `D`, `alpha`, `gamma`, `F2` and `G2` depend on that displacement, including its coupling with `dz`. This audit proves no monotonic ankle-feasibility interval or universal beneficial direction of root-height change. Any analytic restriction must derive from these same branch/link identities for **both** sides, retain all sign/denominator/radicand/equality premises, and remain compatible with the independent reach and roll checks. An empty conservative restriction is unavailable evidence, not an all-height impossibility proof. No new bound formula, midpoint, coordinate, finite family, operation count or candidate is selected here.

A subsequent constructor requires its own public registration: exact immutable source packet, stored scalar semantics, literal arithmetic and masks, fresh source/current authority, finite controls, and complete no-elision source/machine resources before evaluation. Original graph acceptance remains authoritative and must precede finite source/contact/load and all 105 SELF pair decisions, including full distal/connected-owner obligations. WORLD/material, bidirectional acquisition/load joins, continuous route and the separate pan-supported seated endpoint remain required. Preserve body lengths/shapes/masses, source planes and feet, joint limits, suit/pack state, finite hip/ankle owners, WORLD reservations and all prior observations. See [fixed lower-transfer policy](LOWER_TRANSFER_POLICY.md) and [ownership preservation audit](ORIGIN_BOARDING_HIP_POLICY_CONTRACT_AUDIT.md#8-preservation-decision-and-next-decomposition).

This document adds no runtime storage, numerical fixture or compiler geometry probe. Completion is the source-cited algebra and finite applicability table plus independent source/symbolic review; ordinary documentation/publication regression gates remain separate. It grants no actor, seating, save or First Flight qualification.
