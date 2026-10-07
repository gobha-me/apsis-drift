# Conditional BOTH-ankle root-height construction

This source/symbolic derivation for [#505](https://github.com/gobha-me/apsis-drift/issues/505) follows the [original ankle contract audit](ORIGIN_BOARDING_ANKLE_CONTRACT_AUDIT.md). It proves one conservative connected subset satisfying BOTH nominal ankle, reach and roll conditions, **conditional on the explicitly stated supported interval gates and a nonempty intersection**. No source operands, derived bounds, candidate heights or angle values were evaluated. Nonemptiness for the current source, actual layouts/resources and original compiler outcomes remain unknown. Exact arithmetic registration, implementation and FIRST are separate gates.

The original compiler remains unchanged, including its broader allowed hip sector. The construction selects the regular `F1>0` subbranch to obtain a provably monotone projection; this is a conservative restriction of one new program, not a new original policy or a claim that all other branches are invalid. No old constructor is called and corrected. Original source packets, fixed lengths/body/owners/joint limits and all retained observations remain preserved. Parents #462/#361/#352 and First Flight remain open.

## 1. Authentic operands and preserved source chart

For each side `s`, keep the actual original fixed sole `C_s`, root X/Z, root yaw, independent sole yaw, torso, folded arms, duration and reaction cues. PORT uses `sigma=-1`; STARBOARD uses `sigma=+1`. With proper exact rational yaw expressions,

```math
H_s=P+R_0(\sigma_s\,\text{stored } .14,0,0),\quad
A_s=C_s+(0,\text{stored } .1,0),\quad
d_s=R_s^T(A_s-H_s)=(x_s,y_s,z_s).
```

Yaw preserves Y, hence `y_s=a_s-Y`, where `a_s=C_s.Y+stored .1`. The fixed horizontal components `x_s,z_s` are independent of the constructed root Y, but include the rotated hip offset and the actual sole chart. Write `h_s=Y-a_s>0`, `w_s=-z_s`, and `rho_s=sqrt(x_s²+h_s²)`. **Retain signed `z_s`**, not its magnitude or a reporting midpoint. Neither `a_s` nor `w_s` is silently zeroed, shared across sides or replaced by another source plane.

These premises come from [the original graph](../src/origin_boarding_planted_legs.cpp#L3394), [leg chart](../src/origin_boarding_planted_legs.cpp#L3430) and [proper yaw construction](../src/origin_boarding_planted_legs.cpp#L3239). Rounded interval columns are not orthonormality authority. Source enrollment authenticates the immutable seed before any new packet; a new generated Y needs its own fresh identity/validator/current-call boundary.

The held seed has separate PORT/STARBOARD decoded sole-plane expressions, root coordinate term packets and source-selected feet; [original seed rows](../src/origin_boarding_intermediate_endpoint01.cpp#L207) preserve those stored terms and [held yaw/duration rows](../src/origin_boarding_intermediate_endpoint01.cpp#L254) preserve the exact carriers. Existing [canonical generation](../src/origin_boarding_intermediate_endpoint01.cpp#L1140) changes only both root-Y packets. These source definitions establish provenance, not evaluated signs or feasibility. The present proof keeps `w` signed and works with either sign under its conditional domain gates.

## 2. One regular directed subbranch

Suppress `s` until the two-side intersection. Let `L1,L2>0` be the original stored thigh/shin lengths. From the [proved original link identities](ORIGIN_BOARDING_ANKLE_CONTRACT_AUDIT.md#positive-branch-link-and-shin-identities), the sole-local thigh/shin directions satisfy

```math
F_1^2+G_1^2=L_1^2,\qquad F_2^2+G_2^2=L_2^2,\qquad
F_1+F_2=\rho,\quad G_1+G_2=w.
```

Parameterize the closed shin cone by `tau=sin(theta2)`, with `-1/2<=tau<=1/2` and positive shin cosine. This is an auxiliary symbolic parameter, not the phase-time parameter or a new caller control. Choose the positive thigh cosine subbranch:

```math
G_2=L_2\tau,\quad F_2=L_2\sqrt{1-\tau^2},\quad
G_1=w-L_2\tau,\quad F_1=\sqrt{L_1^2-(w-L_2\tau)^2}>0,
```

```math
r_w(\tau)=F_1+F_2
=\sqrt{L_1^2-(w-L_2\tau)^2}+L_2\sqrt{1-\tau^2}.
```

On this subbranch, `theta1=asin((w-L2*tau)/L1)` and `theta2=asin(tau)` both lie in the principal interval `(-pi/2,pi/2)`. Thus the original positive knee turn `theta1>theta2` is equivalent to

```math
w>(L_1+L_2)\tau.
```

This equivalence uses monotonic sine on that directed interval, not a squared inequality with lost signs. Strict thigh regularity is `-L1<w-L2*tau<L1`. Therefore the open auxiliary interval

```math
A(w)=\max\left(-\tfrac12,\frac{w-L_1}{L_2}\right),\qquad
B(w)=\min\left(\tfrac12,\frac{w+L_1}{L_2},\frac{w}{L_1+L_2}\right)
```

gives a regular ankle/positive-turn branch for every `A(w)<tau<B(w)`, if `A(w)<B(w)`. Using an open subcone conservatively omits the legal closed pitch boundaries. It does not alter their original acceptance rule.

For an interior tau, `theta1` and `theta2` have positive cosines and `0<theta1-theta2<pi`. Differentiate the exact radial function:

```math
\frac{dr_w}{d\tau}
=L_2\left(\frac{w-L_2\tau}{F_1}
            -\frac{\tau}{\sqrt{1-\tau^2}}\right)
=L_2\bigl(\tan\theta_1-\tan\theta_2\bigr)>0.
```

This **proves**, rather than assumes, strict radial monotonicity on the selected domain. It does not assert monotonicity of the full original ankle predicate in Y or on its other branches. Endpoint roots may vanish; the function remains continuous at finite endpoints satisfying the closed radicand premises. No derivative is evaluated at such an endpoint.

## 3. Same original positive-gamma solution

For an interior auxiliary value, the resulting two vectors have exact original lengths and sum to `d=-rho*U+z*ez`, with `U=(-x,-y,0)/rho`. Their directed determinant gives

```math
F_2G_1-F_1G_2=L_1L_2\sin(\theta_1-\theta_2)>0.
```

Because `0<theta1-theta2<pi`, the two-link distance lies strictly between the squared length difference and squared sum. In the orthogonal basis `d,q`, where `q=-z*U-rho*ez`, the thigh coefficient along `d` is the original `alpha=(L1²-L2²+D)/(2D)` from its dot product. Its coefficient along `q` is the positive value `(F2*G1-F1*G2)/D`. Squaring this coefficient gives exactly the original factored `gamma²`; positivity selects the original positive square root uniquely. Thus the constructed direction is the same nominal solution as [original alpha/gamma/knee/shin expressions](../src/origin_boarding_planted_legs.cpp#L3452), not another sphere-intersection branch.

On this selected solution `F2>0` and `|tau|<1/2`, so the exact original ankle margin is positive; the closed ±30-degree limit follows. Exact linkage and this argument do not remove any original supported `rho²/D/gamma²/F2` lower-bound or derivative gate. They also do not replace the original hip/axial/knee/timing predicates with symbolic certificates.

## 4. Supported inward radial endpoint construction

The following are proof identities for a prospective registration, not a selected operation table or performed computation. Use the existing supported outward primitives; keep their whole enclosures, and require finite ordered outputs and positive denominators before division/root use. In particular, interval division uses its original reciprocal enclosure followed by multiplication. The original [interval primitives](../src/origin_boarding_planted_legs.cpp#L58) and [certified small-root helper](../src/origin_boarding_planted_legs.cpp#L2351) are sufficient in kind; their exact future applicability/work/storage gates still need registration.

Let `W` enclose the exact signed `w`, and use supported original length point inputs and outward length sums. Select finite stored auxiliary endpoints `p,q` obeying

```math
p=\max\left(-\tfrac12,\operatorname{upper}\frac{W-L_1}{L_2}\right),\qquad
q=\min\left(\tfrac12,\operatorname{lower}\frac{W+L_1}{L_2},
                            \operatorname{lower}\frac{W}{L_1+L_2}\right),
\qquad p<q.
```

Then for the same actual `w`, `A(w)<=p<q<=B(w)`. Every auxiliary value strictly between `p,q` satisfies all strict premises. The endpoint thigh radicands are nonnegative for **every** value enclosed by `W`: each endpoint lies between the appropriate closed length bounds. Also `1-p²` and `1-q²` are positive since both lie in `[-1/2,1/2]`.

Evaluate the expressions `r_W(p),r_W(q)` outward to get supported intervals `R_p,R_q`. Ordinary dependency width is retained. If a supported outward thigh-radicand lower endpoint crosses below zero, intersect that interval with the independently proved nonnegative domain **only after** the endpoint inequalities above are earned; preserve its upper endpoint. This is sound enclosure intersection, not guessing a sign or manufacturing a zero-root result. Unsupported/nonfinite arithmetic cannot be repaired by clipping; a negative upper endpoint cannot be promoted. The future recipe must explicitly register these checks and the certified root domain. Endpoint roots are value-only computations and do not waive the original strict interior derivative domains.

Set `u=R_p.upper`, `v=R_q.lower` and require supported finite `0<=u<v`. For any exact `rho` strictly between `u,v`,

```math
r_w(p)\le u<\rho<v\le r_w(q).
```

Continuity and strict interior monotonicity give a unique `tau` strictly between `p,q` with `rho=r_w(tau)`. No root search, inverse trigonometric computation, auxiliary trial stance or sampling is needed. Correlations between W and the other chart operands can make this universal interval conservative; they cannot make it unsafe. Failure to obtain `u<v` is unavailable evidence, not exclusion of the broader branch or all postures.

## 5. Inward projection onto actual root height

For each side retain supported intervals `X` for the original signed x, `A` for the exact nonzero vertical origin a, and `X2=SQUARE(X)`. The nominal relation is `rho²=x²+(Y-a)²`, on `Y>a` only. Given the radial bracket `(u,v)` above, form a conservative height bracket by the following endpoint directions:

|Bound|Supported outward expression and inward endpoint choice|Reason|
|---|---|---|
|Ankle lower|Compute `SQUARE(point(u))−point(X2.lower)`; use its upper endpoint, max with0, a certified point square root's upper endpoint, then outward `point(A.upper)+point(root.upper)` and retain upper.|Strict Y above this ensures positive height and `rho>u` for the same exact x/a. The max-zero branch is justified by a nonnegative squared height, not an assumed radicand sign.|
|Ankle upper|Compute `SQUARE(point(v))−point(X2.upper)`; require its lower endpoint strictly positive; take the certified point root's lower endpoint, then outward `point(A.lower)+point(root.lower)` and retain lower.|Strict Y below this ensures `rho<v`; a nonpositive conservative radicand leaves no certified upper height.|

Each supported point-square enclosure and addition must actually be performed; their mathematical singleton inputs do not license silently treating rounded results as exact. This projection uses both distinct source origins and the full lateral enclosure; it never subtracts a reporting root from an unbound sole.

Intersect these ankle bounds with the independent original inward reach/roll bounds for that side. With `C` a supported enclosure of `x²+z²`, `m` the original squared-difference enclosure, `M` the original squared-sum enclosure, and `b` a supported enclosure of `2-sqrt3` with `b.lower>0`:

- The reach lower uses the outer upper addition of `A.upper` and the certified upper root of `max(0, upper(point(m.upper)−point(C.lower)))`.
- The reach upper uses the outer lower addition of `A.lower` and the certified lower root of `lower(point(M.lower)−point(C.upper))`, after that radicand is strictly positive.
- The roll lower uses the outer upper addition of `A.upper` and the upper endpoint of the original full supported `ABS(X)/b` division.

These are [the existing nominal inward identities](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT03.md#7-proof-completion-and-stops), reused from the authentic symbolic recipe, not by invoking the old constructor or importing its selected Y. Their strict interior ensures exact regular reach, downward height and the original roll inequality. Direct nominal interval checks can still conservatively refuse, and the original full-frame compiler may enclose the same expressions differently.

Let `lo` be the maximum of both sides' ankle, reach and roll lower bounds, and `hi` the minimum of both sides' ankle and reach upper bounds. Require all contributing bounds earned, finite and `lo<hi`. The open set `(lo,hi)` is one connected conservative BOTH-ankle/reach/roll root-height subset. This is an intersection of proved subsets, not an assertion that the complete feasible set is connected. Source nonemptiness has not been evaluated.

## 6. Stored candidate, fresh authority and remaining gates

Only a later exact registration may freeze one finite checked midpoint operation sequence, tie/zero behavior and a fresh purpose-specific program. It must check the actual stored binary64 result is finite, in the original workspace and strictly between the earned lo/hi. No tolerance, rounded ideal midpoint, alternative component or second height can replace a failed check.

Before issuing new authority, directly verify BOTH supported nominal `rho²/D`, original downward/positive-gamma branch, selected positive `F1`, positive `F2`, ankle cone and roll predicates on the same signed chart operands at that stored height. Retain the original root/coefficient arithmetic recipe and full support/denominator checks; do not infer direct interval acceptance from a symbolic or inward endpoint proof. Revalidate the exact generated root-Y packet and all immutable held fields with the original environment/validator compound. Unavailable conservative bounds, failed stored membership and finite inconclusive predicates remain ordinary unavailability; unsupported arithmetic and capacity remain distinct hard stops. Their precise typed mapping is to be registered before code.

Even successful construction proves only its nominal necessary subset. A fresh original accepted full graph remains the sole prerequisite for body/current projection, finite source contact and COM/load support, and all105 original SELF decisions. Original hip/axial/knee/timing, derivative and workspace gates remain unmodified. WORLD/material, reversible acquisition/load joins, continuous movement and separate pan-supported occupancy remain later obligations. No inherited refusal or old private capability issues a new current token.

## 7. Finite proof and completion table

|Premise|Proved conclusion|Unknown or separately required|
|---|---|---|
|Actual proper yaw, signed source x/z and distinct a for both sides|Root height changes only the vertical component of each genuine chart.|No rounded-frame orthogonality, common-zero origin or source support from target coordinates.|
|`F1>0`, auxiliary tau in its open length/cone/positive-turn domain|Strictly increasing radial map and one connected image; same positive-gamma solution.|The original compiler's broader F1<=0 domain is not classified or relaxed.|
|Supported universal p/q endpoint inequalities and radial enclosures|Every rho strictly between u/v has a regular shin-cone preimage.|The actual supported gates and nonempty radial interval have not been evaluated.|
|Correct inward root-height endpoints plus both sides' reach/roll intersection|Every Y strictly between lo/hi satisfies the exact nominal necessary predicates.|No source-specific interval nonemptiness, monotonicity of all original sectors or old compiler acceptance.|
|Actual stored membership and direct supported checks, then fresh original full graph|A future constructor can attempt a genuine unchanged-policy endpoint.|No implementation/registration/measurement/FIRST is granted by this derivation.|

The algebraic branch/projection lemma is closed conditionally; there is no remaining assumed monotonicity or unproved choice between square-root branches in this subset. Source-specific support/nonemptiness and representation/resource feasibility are expressly unresolved. The documentation-only [Endpoint04 arithmetic registration draft](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT04.md) freezes one supported packet/primitive/control program from this proof for separate matched review; actual implementation, resource admission and observation remain later gates. An empty inward intersection, unsupported endpoint root or later refusal cannot establish all-Y/posture impossibility or justify body/joint/owner/source changes.

That registration must specify ordered charged primitives/guards, partial multiword masks/cursors, FP-FIRST errors, packet/Key/context/token provenance, one retained candidate, meaningful finite tests and required versus owned output. Preserve the full historical enrollment and whole32768 graph proof; add all distinct new/old preparation carriers, current/pending headers/Limits/Requests/refusals, auxiliary/radial proof slots, validator and primitive returns, globals, allocator/library/error/destructor paths without elision or slack borrowing. Preparation and construction scratch must die before the fresh graph/body/audit stages. The near-limit original body audit makes compact persistent evidence a real obligation; no new layout or helper budget is assumed to fit. Retain 8192 creator/49152 whole-live/16MiB output limits and both-compiler actual admission before FIRST.

This derivation changes no production source, header, friend, Test, pose or policy and adds no runtime storage. Independent source/symbolic review precedes exact arithmetic registration; implementation and numerical observation require separate authorization. The finite body/owner/WORLD policies, source planes/feet and all earlier FIRST records remain immutable.
