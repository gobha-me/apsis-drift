# Pelvis/thigh reservation and finite hip-owner contract audit

Contract decision for [#487](https://github.com/gobha-me/apsis-drift/issues/487), following [#485](https://github.com/gobha-me/apsis-drift/issues/485). This source-only audit introduces no runtime API, geometry, candidate pose or qualification. All existing body/owner/source/pose inputs and observations are preserved.

The audit selects **preservation of the current conservative synthetic policy**, followed by a separately registered intermediate endpoint prerequisite. The sources give a deliberate finite adjacent-owner restriction and no independent rationale requiring a larger owner or smaller pelvis. This is a game-policy decision supported by documented intent, not a claim of anatomical fidelity or universal posture feasibility.

## Scope and evidence boundary

Read only existing documents, static C++ definitions, issue bodies/dated scope and saved FIRST records. Creator, phase, solver, raw math, verifier, oracle, executable, compiler, capture and geometry-query calls: **zero**. The saved #485 summary binds frozen head `b6f4c284acf7b8b815f695c89678226407a6458d`, identical compiler logs SHA `040946335bdc9562c13426fb20a6003c0db3f023fd50a65fe409318c5ba95db4`, 4,057 bytes, 3,380 checks and zero failures. `build-native/issue485-validation/root-retained-first-summary.json` and `first-gcc.log` are immutable evidence, not replay instructions.

Parent [#361](https://github.com/gobha-me/apsis-drift/issues/361) freezes a synthetic articulated proxy before fitting and forbids adaptive body shrinking. Parent [#352](https://github.com/gobha-me/apsis-drift/issues/352) requires source-bound support, reach, clearance and an occupied seat; presentation landmarks do not prove those properties. Parent [#462](https://github.com/gobha-me/apsis-drift/issues/462) keeps intermediate movement separate from the final pan-only seated endpoint. Its 2026-10-06 comments at 15:52, 17:07 and 19:11 UTC respectively record nominal support, unresolved combined SELF and the first analytic exclusions. #485 adds verified original-pair witnesses; it does not revise the body policy.

## 1. Current policy, pair, region and chart

The body part enum puts pelvis first and port_thigh fourth (`include/apsis_drift/origin_boarding_body.hpp:66`). Lexicographic pair2 is therefore pelvis/port_thigh. Recipe03 creates each hip region from the **original full thigh capsule**, its hip and knee joints and `kBoardingSelfHipLengthMetres`, then sorts regions lexicographically (`src/origin_boarding_self_model03.cpp:755`, `:763`, `:788`). The resulting port-hip region is region1. The shared constant is explicitly frozen in `include/apsis_drift/origin_boarding_self_model02.hpp:10`.

The region is capsule K intersected with `(B−A)·(x−A) <= L*sqrt((B−A)·(B−A))`, with radius<=L<original axis length. Its flat axial front excludes distal material; it is not a pair-wide exception or a short capsule with a distal ball (`include/apsis_drift/origin_boarding_self_model03.hpp:8`; `docs/ORIGIN_BOARDING_SELF_MODEL03.md:24`). Membership checks both the full capsule and plane (`src/origin_boarding_self_model03.cpp:358`). Complete common interior and its limiting closure must be owned; a certified strict common-interior point outside the region is a refusal (`docs/ORIGIN_BOARDING_SELF_MODEL03.md:40`).

For the current exact-expression diagnostic, h=(.24,.12,.18), local H=(−.14,0,0), r=.105 and L are original constants (`src/origin_boarding_route_intermediate_hip_diagnostic02.cpp:557`). The genuine current nominal ROOT frame is a proper rational/composed rotation; the chart encloses D=ROOTᵀ(K−Hworld), then u=D/.47285 by directed interval division (`:574` through `:595`; `docs/ORIGIN_BOARDING_ROUTE_INTERMEDIATE_HIP_DIAGNOSTIC01.md:84`). This exact nominal provenance differs from the older stored binary64 affine solids, whose columns cannot simply be declared orthogonal (`docs/ORIGIN_BOARDING_SELF_MODEL02.md:29`). Neither reporting midpoints nor old successful reports supply chart authority.

**Finding:** the reported pair/region is correctly a finite owned-adjacency obligation on the original full solids. The new witness concerns the current compiled expression policy, not an unregistered reinterpretation of old affine columns.

## 2. Dimensions and synthetic rationale

`docs/LOWER_TRANSFER_POLICY.md:24` freezes pelvis full dimensions .48×.36×.24m, thigh joint length .47285m and radius .105m. Joint lengths reuse retained fixture proportions at stature1.93m; the canonical hip centers are X=±.14m (`:36`). `docs/ORIGIN_BOARDING_BODY.md:16` and `:18` define pelvis half(.24,.12,.18), actual pelvis-relative hip offsets and full endpoint-ball capsules. The exact planted part factory retains those same pelvis halves and thigh radius (`src/origin_boarding_planted_legs.cpp:887`).

The finite hip extent `0x1.bb0cd605d7512p-3` has an explicit synthetic rationale: the pelvis sagittal diagonal, shared with the waist extent (`docs/ORIGIN_BOARDING_SELF_MODEL02.md:55` through `:70`). Recipe03 changes the geometric region construction to eliminate rounded-prefix ambiguity while preserving L (`docs/ORIGIN_BOARDING_SELF_MODEL03.md:10`, `:24`); Recipe04 binds the unchanged recipe to body policy02 (`docs/ORIGIN_BOARDING_SELF_MODEL04.md:3` through `:13`).

**Finding:** the constants have declared fixture/game-policy provenance. The particular pelvis dimensions and thigh radius are frozen synthetic declarations; these sources do not supply an anatomical derivation for those numbers. Links have an explicit stature-proportion derivation, while hip offsets retain the canonical fixture layout and the owner has the explicit sagittal-diagonal rationale. Missing provenance is anatomical or percentile validation and a guarantee that every sector-valid articulation fits this owner; the policy explicitly excludes human percentile coverage/material strength (`docs/LOWER_TRANSFER_POLICY.md:3`). Neither missing premise is required to operate the declared conservative game policy. No source provides an independently required replacement extent, body dimension or capsule shape. Four witnessed poses do not create such a rationale.

## 3. WORLD, SELF, owner, suit and mass meanings

The ordinary .64m standing envelope is a broad walking reservation, not a solid anatomical trunk (`docs/LOWER_TRANSFER_POLICY.md:13`). Articulated WORLD reservations are full original boxes/capsules. SELF deliberately replaces only trunk and helmet boxes with ellipsoids; pelvis remains a box and thighs remain full Euclidean capsules (`docs/ORIGIN_BOARDING_SELF_MODEL02.md:29`). The exact expression part bindings and mass points remain fixed (`docs/ORIGIN_BOARDING_PLANTED_BODY01.md:35`). A finite adjacent owner permits a specifically bounded common intersection; it is neither world clearance nor a shrink of either solid.

Suit-on/pack-detached is an explicit synthetic geometry prerequisite (`docs/LOWER_TRANSFER_POLICY.md:8`), not an independently measured deformable suit mesh. The declared contact skin applies only to selected external seat faces and does not relax rigid cores, source collision or internal SELF (`:74`). Integer weights and point/midpoint COM are a game surrogate, not densities reconstructed from reservation volumes (`docs/ORIGIN_BOARDING_BODY.md:31`; `docs/LOWER_TRANSFER_POLICY.md:81`). The pelvis mass144 and thigh120 are retained at their original center/midpoint (`docs/ORIGIN_BOARDING_PLANTED_BODY01.md:37`).

**Finding:** shared dimensions are intentional and do not collapse these authorities. Changing SELF ownership cannot silently change WORLD reservations, foot contacts, seat skin, masses or suit state.

## 4. Permitted articulation and contact domain

Joint-sector limits are necessary kinematic restrictions; finite connected neighborhoods and nonadjacent exclusion are separate mandatory restrictions (`docs/LOWER_TRANSFER_POLICY.md:49`). Recipe03 requires all105 pair decisions, including complete ownership or absence of common positive interior for connected pairs (`docs/ORIGIN_BOARDING_SELF_MODEL03.md:48`). Its arbitrary-pose certificate coverage is explicitly incomplete (`:74`; `include/apsis_drift/origin_boarding_self_model03.hpp:60`). A legal joint angle, source contact, finite load or nominal linkage is not a SELF acceptance certificate.

The older Body01 model review identifies intrinsic shoulder/helmet and hip/trunk conflicts and motivates an independently registered self version, rather than forgiving them (`docs/ORIGIN_BOARDING_BODY.md:35` through `:39`). Those historical reasons justify the actual Model02/Recipe03 changes; they do not establish that every later pelvis/thigh overlap is intended. Current finite owners deliberately retain root-joint contact and restrict distal material.

**Finding:** preserve the conjunction of joint sectors AND complete SELF. The current sources support intended finite adjacent interaction, but provide no rule allowing all material overlap whenever parts share a joint. No new model revision is necessary merely because a selected legal-sector pose fails that conjunction.

## 5. Exact retained cases and their limits

The six CaseIds, point ties and phase metadata are frozen in `docs/ORIGIN_BOARDING_ROUTE_INTERMEDIATE_HIP_DIAGNOSTIC02.md:24`. Actual saved results are:

|Case|Original global/local and phase|Retained original-pair conclusion|
|---|---|---|
|0|g0, phase0/local0|Dual exclusion of strict unowned interior.|
|1|g.25, preceding phase1/local1|Verified strict common interior outside original hip owner.|
|2|g.5, preceding phase2/local1|Verified strict common interior outside original hip owner.|
|3|g.75, preceding phase3/local1|Verified strict common interior outside original hip owner.|
|4|g1, phase4/local1, stationary hold|Verified strict common interior outside original hip owner.|
|5|g[145/1024,146/1024], phase1/local[17/128,9/64]|Dual exclusion throughout the selected closed interval.|

At stationary point1 the saved axial gap is `[.0082907649331997162,.008290764933205937]` and radial gap `[.007724744352739045,.0077247443527397033]`. These are verifier inequalities, not a displacement request or clinical penetration depth. The generator is untrusted; full-original same-interval membership establishes the witness (`docs/ORIGIN_BOARDING_ROUTE_INTERMEDIATE_HIP_DIAGNOSTIC01.md:90` through `:96`). No unqueried pose, interval between points, other pair, complete SELF or physical anatomy inference follows. Case5 resolves an earlier proof-family gap; it cannot negate the four separate point witnesses.

**Finding:** the unchanged endpoint now fails the current finite owner policy. Changing only the approach curve, timing or subdivision cannot qualify that same endpoint.

## 6. Authored source constraints versus selected C/E and feet

The original immutable factory constructs C/E root term lists and U/H/I/star sole term lists separately (`src/origin_boarding_route_intermediate_step.cpp:246` through `:269`). E is a chosen root term packet; I chooses a sole center and exact decoded intermediate source plane. Step02 introduces P using original U.X/Y with star.Z, retaining other old packets (`src/origin_boarding_route_intermediate_step02.cpp:244` through `:267`). Five-phase maps and preceding-phase point ties are selected in `docs/ORIGIN_BOARDING_ROUTE_INTERMEDIATE_STEP02.md:11` through `:29`.

Necessary authored constraints are the immutable admitted surfaces, identities, plane expressions, eligible finite footprints and strict material/crop semantics. Intermediate provenance is inventory735, admitted HALOobject10 evaluated faces182/183 with keys{halo,0,662}/{halo,0,663}; full inventory and cropped-catalog indices are distinct (`docs/ORIGIN_BOARDING_INTERMEDIATE_PAUSE_SUPPORT01.md:23`). Original upper/transition floors and genuine star partition remain separately bound. Surface existence does not require the particular E root, I center or root yaw selected by Step02, and does not grant any arbitrary point on that surface support.

Later support code authenticates unchanged unloaded shares0 and the separately registered ramp0→1/16/hold1/16 (`src/origin_boarding_route_intermediate_support01.cpp:484`, `:548`). The original Step02 .25-load candidate table is historical; it is not the current lighter-load packet. These load revisions never promise SELF clearance. Source shapes and contact eligibility are not the cause established by the internal pair witness.

**Finding:** E/I is a rejected candidate intermediate body configuration. It is not an authored mandatory occupied-seat pose. A future endpoint may change a declared body placement while preserving authored source/contact constraints, but each changed placement must earn its own genuine geometry and support evidence.

## 7. Intermediate, seated and WORLD obligations

The intermediate prerequisite is an unloaded foot acquisition followed by finite nonzero loading and a supported pause, reversing back to the original unloaded C endpoint. #462 explicitly separates final pan-only occupancy. #361 additionally requires acquisition of source-selected pan support and a specified seated endpoint. Eligibility of pan patch30 and the selected back/head/elbow contacts is narrowly defined (`docs/LOWER_TRANSFER_POLICY.md:68`); floor-supported intermediate load is not pan-only occupied-seat support.

Prior seat findings concern different selected body families and current source constraints. Static self-clear/source-clear points still retain negative pelvis-only pan load evidence; they prove neither global posture impossibility nor a qualified endpoint (`docs/LOWER_TRANSFER_FEASIBILITY.md:27` through `:40`). Full source WORLD boxes and material/HALO predicates stay strict regardless of a SELF policy decision (`docs/ORIGIN_BOARDING_SELF_MODEL02.md:42`; `docs/ORIGIN_BOARDING_SOURCE_ENDPOINT_SELF01.md:14`).

**Finding:** no self owner change can waive seat/source interference, and no seat skin or hardware art change can erase an internal current-pair witness. Full intermediate SELF, subsequent all-source WORLD/material, pan-only seated support, continuous acquisition/reverse route and actor/save integration remain distinct open gates.

## 8. Preservation decision and next decomposition

**Affirm the existing conservative synthetic pelvis/thigh SELF policy for the next investigation.** It has documented finite-joint intent and frozen rationale. This audit finds no contradictory source requirement demanding a different owner or self solid. Lack of anatomical validation limits interpretation but does not invalidate the selected game contract. Preserve all existing versions, original body/owner/source/asset/pose packets, masses, selected load law and historical positive/negative evidence. No slab extension, box reduction or witness-shaped exemption is selected.

The next necessary numerical child should be a **static source-bound intermediate endpoint prerequisite under the original body and finite owners**, preceding another trajectory. A narrowly permissible initial input family is: change only the constant root endpoint's X/Y/Z term packet relative to the already admitted, held I/star feet; hold original pelvis orientation policy, root yaw, folded arms, torso endpoint, positive knee branch, flat sole frames, radii/dimensions/owners/masses, source surfaces and 1/16 load split fixed. No new abduction, pelvis lean, alternate knee branch, foot placement, share optimization, seat contact or geometry dimension enters this family. The existing policy02 restriction that nonzero abduction requires an upright pelvis remains mandatory; coupled pitch/abduction and unregistered axial twist, manual ankle, neck or wrist controls are not admitted. Root placement may change the existing derived hip abduction or ankle roll; those quantities must pass the unchanged original checks, rather than remain numerically equal to the rejected pose. This identifies available controls, not numeric candidate values, direction, interval, grid or an expected outcome. Existing compiler support for a specific new packet and its physical rationale must be reviewed before registration; an unsupported packet must not be admitted by a reporting-pose adapter.

A subsequent exact registration must select one finite candidate packet or bounded finite family independently of the four witness coordinates, with literal CaseIds/roster and all caps frozen before any evaluation. Public input should be a genuine source provider plus an immutable selected candidate identifier; caller poses, successful old reports and positive flags cannot mint current-body authority. Each candidate requires a fresh original graph and body/context, exact source planes/finite foot contact, reach/sectors/branch/timing where applicable, all105 original SELF pairs (including all91 strict nonadjacent obligations and full distal caps), current COM and full symbolic nominal finite load. Missing, unsupported or ordinary negative evidence refuses. Higher-precision corroboration is independent, and outcomes remain unknown. Whole-source WORLD/material and all obstacle/crop obligations must then qualify before that endpoint is treated as an acquisition target; no positive SELF/load endpoint alone grants a route.

If that restricted family is unsupported or fails, preserve the result and stop; another control family requires a new independently reasoned registration. Do not enlarge the owner or silently introduce more controls. A later model revision remains possible only if new intended-interaction rationale, independent of fitting, establishes exactly which self/owner semantics need revision and strict nonadjacent/distal controls. It must remain separately versioned, preserve WORLD and historical negatives, and cannot be inferred from failure of one endpoint family.

This source audit creates no runtime storage burden or new resource forecast. Any later implementation keeps the whole original graph proof and selected 8KiB creator/48KiB co-live/16MiB output ceilings, with complete actual two-compiler measurements before FIRST. Parent #462/#361/#352 and First Flight remain open.

## Independent source review

A separate source reviewer checked all eight rows against the unchanged definitions, source-bound packets and retained results. The review found no contract blocker in preserving the policy and decomposing the narrow endpoint prerequisite. It expressly made no existence, anatomical or runtime qualification claim and performed no numerical, API or compiler calls. Repository build/CI validation is separate from this zero-query audit.
