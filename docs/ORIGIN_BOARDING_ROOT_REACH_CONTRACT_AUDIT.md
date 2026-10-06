# Root reach contract audit

Issue [#491](https://github.com/gobha-me/apsis-drift/issues/491), 2026-10-06. This is a finite source/equation audit after #488 / PR #490 merged at `39fae1eb8b546d77c4dbbb77d3005d30300df772`. It introduces no production change, candidate, coordinate, input family, angular freedom, load change or numerical observation. No creator, phase, solver, verifier, oracle, compiler probe, subdivision or replay is part of the audit. Publication regression checks are separate.

The original phase kernel applies to the registered constant root/sole packet. This audit finds no concrete contradiction between that packet family and its source contract. Request validity and source admission do not promise reach acceptance: the kernel provides a sufficient interval certificate for a constrained, nonsingular positive-knee branch. The preserved #488 refusal establishes that this certificate was not earned. It establishes neither exact unreachability nor downstream contact/SELF qualification.

## Immutable packet and constant semantics

The packet construction chain is explicit:

1. The original Step01 constructor initializes equal root endpoint packets, equal held sole packets and fixed yaw controls before selecting its phase. Its hold branch fixes torso and duration: [original constructor](../src/origin_boarding_route_intermediate_step.cpp#L246), lines246–292.
2. Step02 phase4 selects original phase3 through `phase - 1`; its modifications apply only to phases0/1. It returns the held original packet: [Step02 constructor](../src/origin_boarding_route_intermediate_step02.cpp#L244), lines244–267.
3. Support02 changes only both reaction entries to the same registered dyadic value: [Support02 request](../src/origin_boarding_intermediate_pause_support02.cpp#L479), lines479–485.
4. Endpoint01 changes the existing root.Y term list in **both** root endpoints and returns the packet. It preserves every other field: [Endpoint01 request](../src/origin_boarding_intermediate_endpoint01.cpp#L510), lines510–520. The immutable candidate definition and independent authored-descent rationale remain in [Endpoint01 registration](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT01.md). This audit selects no replacement.

A coordinate is the exact-real sum of its active stored binary64 terms, not a rounded reporting coordinate. `phase_constant` validates the active count, finite bounded terms and inactive zeros; outward additions enclose the sum. The bounded exact sum-sign check establishes the workspace before intersecting with its independently proved range: [constant implementation](../src/origin_boarding_planted_legs.cpp#L3109), lines3109–3161. This can produce a non-singleton enclosure of one exact constant.

For term packets with identical counts and term arrays, `phase_carrier` returns `timing_constant(start)` directly. Equal scalar endpoints have the same shortcut. It does not compute a varying interpolation and then try to cancel its interval dependencies: [carrier implementation](../src/origin_boarding_planted_legs.cpp#L3163), lines3163–3183. `timing_constant` retains the value enclosure and sets both parameter derivatives to exact supported zero: [jet primitives](../src/origin_boarding_planted_legs.cpp#L450), lines450–483.

The graph still computes its registered quintic and hump enclosures. The quintic's value and derivative enclosures have independently justified polynomial ranges; exact endpoint-point derivative zeros are a separate shortcut: [quintic](../src/origin_boarding_planted_legs.cpp#L2370), lines2370–2389. The hump is the original C2 polynomial with its value and derivative enclosures: [hump](../src/origin_boarding_planted_legs.cpp#L3185), lines3185–3207. In the held packet, the swing amplitude is exact zero. `timing_multiply` multiplies every applicable value/derivative term by zero, and `multiply` has the exact supported-zero shortcut. Thus a possibly wide hump enclosure does not introduce actual sole motion. These shortcuts require supported operands; they do not bypass unsupported arithmetic.

Consequently the held root, soles, yaw, torso and reaction expressions are constant over the whole local hold, with zero parameter derivatives. Later division by the unchanged positive duration preserves the zero physical derivatives; it does not turn interval value widths into physical motion: [physical evidence](../src/origin_boarding_planted_legs.cpp#L3258), lines3258–3279. Finite outward widths remain from constant summation, rational rotation and interval products. Those arithmetic dependencies are different from a moving curve. No point/subrange was evaluated to reach this conclusion.

## Nominal frame and two-link geometry

The request validator accepts finite bounded constant packets, yaw/torso carriers, swing amplitude, reaction cues and duration. It does not test reach: [request validation](../src/origin_boarding_planted_legs.cpp#L3330), lines3330–3370. There is no caller-provided pelvis lean, knee branch, link length or frame matrix. The declared domain is upright pelvis with the original derived abduction/ankle roll and retained joint limits; it does not promise that every domain-valid root is feasible. See [phase scope](ORIGIN_BOARDING_ROUTE_FOOT_PHASE01.md), sections “Exact controls and domains” and “Phase and exact target graph.”

The graph constructs the root and sole yaw rotations from the original rational tangent-half-angle family. With exact-real scalar `k`,

\[
c=(1-k^2)/(1+k^2),\qquad s=2k/(1+k^2),\qquad c^2+s^2=1.
\]

Its columns therefore describe a proper nominal rotation; the positive denominator follows from the exact expression. Interval columns enclose this rotation and its jets. Stored interval endpoints or reporting midpoints are not themselves asserted orthogonal, and no rounded matrix inverse is used as authority: [rotation and local transform](../src/origin_boarding_planted_legs.cpp#L3229), lines3229–3256; [graph construction](../src/origin_boarding_planted_legs.cpp#L3394), lines3394–3424.

For side `i`, let `P` be the exact root expression, `R0` the nominal root yaw, `C_i` the sole expression and `Rb_i` the nominal sole yaw. The code constructs

\[
H_i=P+R_0(\text{original signed hip offset},0,0),\qquad
A_i=C_i+(0,\text{original ankle offset},0),\qquad
d_i=R_{b_i}^{T}(A_i-H_i).
\]

`phase_local` takes dot products with the nominal frame columns. Transpose is the inverse because of the **compiled rational family**, not because rounded columns happened to pass an orthogonality comparison. Hence the exact squared distance `D = d_i·d_i` is also `|A_i-H_i|²`. The interval evaluation may lose dependencies between repeated column/components; it still encloses the same exact nominal distance. Root translation changes the genuine hip location relative to fixed feet; torso lean affects the later trunk, not this hip construction: [leg construction](../src/origin_boarding_planted_legs.cpp#L3426), lines3426–3440.

The original forward/down branch requires `dY.upper < 0`; the original derivative domain requires strictly positive lower bounds for `rho² = dx²+dy²` and `D`. Supported value, first and second jets are checked before either predicate. These checks remain separate from reach: [ordered leg guards](../src/origin_boarding_planted_legs.cpp#L3441), lines3441–3446.

Within the accepted domains, the two-link construction uses the unchanged stored positive lengths `L1` and `L2`, exact-real branch coefficients

\[
\alpha=(L_1^2-L_2^2+D)/(2D),\qquad
\gamma^2=((L_1+L_2)^2-D)(D-(L_1-L_2)^2)/(4D^2),
\]

and the positive root of `gamma²`. Its perpendicular vector is
`q=(dx*dz/rho, dy*dz/rho, -rho)`, satisfying `q·d=0` and `q·q=D`. The resulting knee `H + Rb*(alpha*d+gamma*q)` has the original two nominal link lengths by these symbolic identities. The identity is conditional on the genuine construction/domains; a rounded link-length residual is not acceptance authority: [coefficient and knee code](../src/origin_boarding_planted_legs.cpp#L3447), lines3447–3478. No alternate knee branch, fitted ankle or new normalization is introduced.

## Reach inclusion, arithmetic and equality

The interval type is supported only for finite ordered endpoints. `add`, `subtract`, `multiply` and `square` propagate unsupported inputs, preserve selected exact zero/one/equal-singleton identities, and otherwise expand rounded bounds outward. `square` uses zero as its lower bound when its input crosses zero: [original interval primitives](../src/origin_boarding_planted_legs.cpp#L49), lines49–107. Arithmetic support is conditional on the original binary64/nearest/subnormal environment and finite resulting bounds, not on arbitrary finite input alone: [environment](../src/origin_boarding_planted_legs.cpp#L27), lines27–47. This audit calls none of those runtime checks.

The phase path forms `maximum = square(add(L1,L2))` and `minimum = square(subtract(L1,L2))` from the immutable original constants, then requires

\[
D_{upper}\le maximum_{lower},\qquad
D_{lower}\ge minimum_{upper}.
\]

Both comparisons are inward-safe for the desired reach range: they establish containment using the lower bound on the exact maximum and upper bound on the exact minimum. The implementation refuses ordinary `reach` when either inclusion fails: [phase reach guard](../src/origin_boarding_planted_legs.cpp#L3447), lines3447–3451. The earlier `timing_supported(D)` guard is independently present. This is the actual phase path; the older generic `evaluate_leg` reach/unsupported combination at lines236–240 is a different function and cannot classify #488.

Failure of that inclusion is not its strict geometric converse. Given supported enclosures of the same exact expressions, a sufficient **strict exclusion** would instead require

\[
D_{lower}>maximum_{upper}\quad\text{or}\quad
D_{upper}<minimum_{lower}.
\]

The interval can meet neither inclusion nor strict exclusion. Outward widths can make inclusion fail near a boundary even if the exact value is in range; the retained record does not establish that this happened. The kernel does not publish a strict-exclusion classifier on this path, and the saved log contains neither failed disjunct nor the original numeric intervals. No exact unreachability claim follows.

The mathematical two-sphere reach range is closed, including equality at squared sum/difference. The implemented inclusion comparisons likewise do not reject equality **of their compared interval endpoints** merely because it is equality. Exact geometric equality and equality of those rounded bounds are different facts. Even if reach inclusion passes, full phase acceptance requires `gamma².lower > 0`; a straight/folded zero-height intersection is singular for the selected derivative construction. Supported square-root jets also require a strictly positive input lower bound. Thus closed geometric reach alone does not authorize a degenerate timing certificate: [gamma guard](../src/origin_boarding_planted_legs.cpp#L3452), lines3452–3469; [root jet](../src/origin_boarding_planted_legs.cpp#L2351), lines2351–2360. No singular factorization, equality tolerance or finite derivative inferred from a stationary sample is selected here.

Joint sectors, physical timing, both legs and full body are later obligations. In particular legal input carrier ranges are not the actual hip/knee/ankle/roll/axial/torso certificate: [leg sectors](../src/origin_boarding_planted_legs.cpp#L3543), lines3543–3564; [whole cell completion](../src/origin_boarding_planted_legs.cpp#L3754), lines3754–3774. This audit preserves all of them.

## Evidence and refusal attribution

The private original cell clears its output and refusal, validates domains/caps/environment, charges the graph, then visits PORT before STARBOARD and charges each leg before its evaluation. It sets aggregate completion flags only after both legs, body and remaining predicates pass: [private cell](../src/origin_boarding_planted_legs.cpp#L3715), lines3715–3774.

`phase_refuse` sets the original condition and copies the supplied interval endpoints only when that interval is supported. It returns `unsupported` only for `unsupported_arithmetic`; reach and the other ordinary predicates return `unresolved`: [original refusal](../src/origin_boarding_planted_legs.cpp#L3295), lines3295–3300. The public type `BoardingRouteFootPhaseRefusal` stores no supported/evaluated bit on its scalar bound. Defaults cannot be read as numeric proof merely because they are finite: [refusal type](../include/apsis_drift/origin_boarding_route_foot_phase.hpp#L106), lines106–111.

The old public cover fills original `first`, `last`, `depth` and `predicate_condition=condition` after invoking the private cell. It may then replace the terminal cover condition with a capacity/unsplittable condition while retaining that predicate: [public cover](../src/origin_boarding_route_foot_phase.cpp#L121), lines121–158. The private cell alone does not fill `predicate_condition`; its default `none` does not negate its actual condition. It also leaves refusal `first`, `last` and `depth` at their defaults after `reason = {}`. Only the cell output gets the actual interval before evaluation (`planted:3719–3723`). Endpoint01's real invocation is the fixed local `[0,1]` in producer lines546–548; nested default refusal endpoints `[0,0]` are not evidence of a point call or altered interval.

Endpoint01 invokes that private cell exactly once, copies its refusal into the nested `phase` record and reports `phase_prerequisite` before P251 or a fresh current token can be earned. Capacity/unsupported remain distinct hard states. The new generic limiting bound stays unearned rather than treating an old default as supported numeric evidence: [Endpoint01 bridge](../src/origin_boarding_intermediate_endpoint01.cpp#L538), lines538–584. The new outer side/edge/axis sentinels are not the nested original leg attribution. For a fresh authenticated `phase_leg` call that actually returned `reach`, with a valid original side and the prior supported `D/rho`, forward-branch and positive-distance guards established, the original two-double `limiting_bound` may be interpreted as the supported enclosure of **`D.value`**: this exact reach branch passed `D.value` to `phase_refuse`, which copied its endpoints. This is squared-distance value evidence only. It retains no derivative bounds, minimum/maximum thresholds, failed-disjunct tag or strict-exclusion certificate. The scalar type alone, default endpoints, an unrelated predicate or cover cause, or the new generic wrapper bound grants none of that interpretation. The saved text does not serialize the nested original bound endpoints.

## Finite applicability and evidence table

|Audit row|Source conclusion|What it establishes / what remains unearned|
|---|---|---|
|1. Packet identity|Both held endpoint term packets follow the existing pure constructor chain; Endpoint01 updates both root.Y packets.|A permitted fixed recipe, not a reachable pose or caller-minted source authority.|
|2. Constant/zero-hump jets|Exact equal-packet shortcuts and supported zero multiplication preserve stationary semantics.|No interpolation-induced motion; constant interval arithmetic can still have width.|
|3. Frame and displacement|Hip and ankle are genuine nominal graph expressions; sole-frame transpose is justified by the analytic proper rotation.|`D` encloses exact hip-to-ankle distance; no stored-affine/midpoint inverse or extra freedom.|
|4. Arithmetic/domain order|Supported `D/rho` and forward/positive-distance guards precede reach; lengths are fixed original constants.|Reach classification is not an unsupported-arithmetic label on the #488 path. It grants no later derivative/sector/body flags.|
|5. Reach boundary|Original comparison is sufficient interval inclusion; strict exclusion is a separate, unimplemented conclusion.|Ordinary refusal is unresolved containment. The retained failed disjunct and numeric margin are unknown.|
|6. Equality/singularity|Closed geometric reach and interval endpoint equality differ; the next positive `gamma²`/root-jet guard remains mandatory.|No singular timing/closure extension or relaxed knee/ankle policy is warranted.|
|7. Attribution|Private condition/side differ from public-cover predicate metadata and the new generic supported bound.|PORT reach is identified; outer bound/sentinels do not certify a numeric limit or collision.|
|8. Current authority|Private graph acceptance precedes P251, token issuance, finite pressure and complete SELF.|Source admission/request validity cannot substitute for any current/body/support/SELF/route qualification.|

The preserved #488 FIRST therefore remains ordinary unresolved PORT reach: one graph, one leg, zero body/sectors/timing; source64 admitted, D01–04 charged, projection/contact/unit/SELF and independent body audits NOT_RUN. Its two retained logs are byte-identical, SHA-256 `6fadb33c421b81cbe7c5dc3239a82a92a6183f47c1989526afa7359d7db4d321`; the frozen code is `c55c510d1e71700984d2c1983cf53ce7df42e2d3`. The immutable outcome and receipt details remain in [Endpoint01 retained FIRST](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT01.md#retained-first-result). No observation is replaced by this audit.

## Decision and next prerequisite

Preserve the original body, source planes, feet, owner policy, load cues, lengths, positive-knee branch, freedoms, limits, packet and failures. No source-contract defect is demonstrated by the refusal. The constant root-only packet is representable without a new kinematic freedom; the existing restricted kernel may still decline its reach or any later certificate. The audit does not assert that all postures are impossible.

The smallest follow-up is a separately registered **source-bound reach evidence diagnostic for the unchanged #488 candidate**, before selecting another endpoint family. It may expose or recompute the actually earned original `D`, the same original minimum/maximum outward thresholds and the actual failed inclusion disjunct. It can distinguish sufficient inclusion, strict exclusion and unresolved enclosure without changing the geometry or retrying later body/contact/SELF. The existing private refusal alone has no tagged threshold/disjunct evidence, so a new typed diagnostic must register how it earns those fields; this audit does not authorize an API change or another call. Such a child must fix the same authentic packet/issuer, comparison/degeneracy policy, finite charge order, partial/support masks, numerical roster and complete original graph/resource accounting before observation. It must preserve unsupported versus ordinary semantics and every old receipt. Results remain unknown: in particular this audit predicts neither the failed branch nor strict exclusion. No coordinate, new family or diagnostic budget is selected here.

Any discovered future implementation contradiction requires a separate versioned correction justified by source proof and retained regression evidence. Any subsequent posture or continuous route remains a separately registered engine prerequisite with original contact/load/all-pair SELF checks, WORLD/material exclusion, acquisition/reverse joins and seating/actor gates still required. This document grants none of those qualifications and does not change the open parents #462, #361, #352 or First Flight.
