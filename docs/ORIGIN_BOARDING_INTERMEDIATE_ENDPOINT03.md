# Registered intermediate endpoint03: source-bound reach and both roll constraints

This is the selected pre-implementation registration for [issue #499](https://github.com/gobha-me/apsis-drift/issues/499). One fresh version3 program constructs one root height from inward reach and both original roll restrictions. All other source packets, body dimensions, nominal links, joint limits, finite ownership and WORLD policies remain unchanged. The original full compiler remains authoritative; later route, seat, actor and First Flight obligations remain open.

The independently reviewed method and matched finite manifest below are selected together: 78 operations, 21 construction guards, 25 lowerable limits and 93 literal test labels. These include two conditionally reached operation-word boundary controls and three sequential A01-only audits. There is one genuine creator, one default FIRST per compiler, no raw/inspector calls and no substitute candidate or query for a skipped control.

Actual layouts, complete source/physical resources and runtime outcomes are UNKNOWN. The resource tables are forecasts under the existing creator8192, scratch49152, output16777216 and shared-output16384 ceilings. Held source review, both-compiler compile-only measurement and complete resource admission precede the immutable binary/source freeze and once-only FIRST. Observed expectations may be added only after preserving that original result.

For Endpoint03, the current matched test interval inventory controls: 1296+1728+1152+1024+512+2480=8192 bytes, with no unused margin. Core section9's 4736+3456 wording is historical reference only. The 2480-byte helper pool is a bounded source forecast requiring later actual lexical and current/pending corroboration; it is not a measured object sum.

The following four held payloads are preserved literally. Their original “draft,” “proposal” and remaining-gate wording records their status at preparation; this selection cover registers their exact method and roster without claiming implementation, measured admission or runtime success. Relative validation filenames inside those payloads are provenance labels, not portable build dependencies. Both JSON inventories are included here in full.

Paired independent review SHA256: `06b67d5a0eea306ac6e06822e3606b7c880cc430df3d3bd498001fd08fd8084c`. Root source/forecast corroboration SHA256: `0519d7e3ba5426746a2f5983a8b85853f2aa7c389266238e751cd4b97627a70f`. The review requires the exact interval-accounting clarification above.

## Selected exact typed method

Held payload SHA256: `9349774b07e6ccf2689b29ec0f21578b78463639a51066ba5b8e1f50bdfeb3ac`.

# Endpoint03: exact reach-and-both-roll typed method proposal v1

SOURCE-ONLY proposal for issue499, based on merged fd940d46a2cd7898d0110d897a70e1c9c91cede0. No code, compiler, creator, phase, API, numerical coordinate/angle/derived-bound calculation, raw fixture, solver, grid or oracle evaluation is performed or authorized. Existing #495/#497 FIRST and every old source/helper/policy remain preserved. Actual new layouts, physical resource stages and outcomes are UNKNOWN. Independent paired method/manifest review and public registration must precede implementation; actual compile-only admission must precede FIRST.

## 1. One immutable purpose and typed interface

New public prefix `BoardingIntermediateEndpoint03`, version3. `Candidate:uint8_t {root_y_reach_roll_slice=0}`. Public `assess_origin_boarding_intermediate_endpoint03(const OriginBoardingIntermediatePauseSupport&,Candidate)->Expected`; `Expected=std::expected<Diagnostic,std::string>`. Private `detail::intermediate_endpoint03_bounded(provider,Candidate,Limits={})` returns that same Expected. No scalar Y/X/Z, range/reverse, report, old key/token, numerical fixture or alternative candidate is an input. Exactly one fixed forward closed[0,1] two-second hold.

Only rootY is generated. Keep original root X/Z term packets, both I/star feet and decoded planes, rational root yaw-half.125, flat sole yaw-half0, torso-half−.30, zero humps, folded arms, constant PORT1/16/STAR15/16 share, original lengths/knee branch, body/masses/finite owners and full-source/WORLD policy. The final stored binary64 Y is lifted to exact real as the new nominal coefficient; BOTH root endpoints receive `{Y,+0,+0}`count1. It is neither an exact square root nor a reporting midpoint nor the old root-Y sum. No second program, correction from the observed negative margin, fitting or fallback height.

New State ordinals: not_run0, accepted1, unresolved2, witness_refused3, capacity4, unsupported5. Condition has exactly the old Endpoint02 names/ordinals: none0, invalid_candidate1, invalid_limits2, invalid_binding3, output_capacity4, source_capacity5, source_identity6, phase_prerequisite7, projection_capacity8, projection_identity9, definition_capacity10, sole_plane11, source_rectangle12, empty_intersection13, denominator14, sole_extrema_capacity15, source_coordinate_capacity16, intersection_capacity17, midpoint_capacity18, allocation_capacity19, pressure_capacity20, edge_capacity21, symbolic_equilibrium22, sole_disk23, source_disk24, self_body_capacity25, self_body_identity26, unit_axis_capacity27, unit_axis_identity28, self_pair_capacity29, self_axis_capacity30, self_signed_capacity31, self_owner_capacity32, self_hip_capacity33, self_region_identity34, unresolved_self_pair35, unsupported_arithmetic36, incomplete_endpoint37, slice_unavailable38, slice_identity39, slice_guard_capacity40, slice_operation_capacity41.

New SelfStage:uint8_t is not_run0/source1/phase2/projection3/definition4/sole_extrema5/source_coordinates6/intersection7/midpoint8/allocation9/pressure_candidate10/disk11/body12/unit_axes13/pair14/owner15/hip16/separation17/complete18/slice_guard19/slice_operation20. New Certificate:uint8_t retains not_run0, convex_support_plane1, cap_partner_support2, original_axis_box_support3, half_ray_angle_bound4, shoulder_split5, original_capsule_slab_complement6. New SliceCondition:uint8_t: none0, identity1, no_positive_upper2, empty_inward_interval3, candidate_domain4, midpoint_unavailable5, verification_inconclusive6, guard_capacity7, operation_capacity8, unsupported_arithmetic9, no_positive_roll_factor10. No construction failure asserts collision, strict all-Y exclusion or complete regular-branch admission.

## 2. Schema, constants and authority

Fresh names: Diagnostic, Cell, Refusal, Counters, SliceEvidence, Expected and private Limits, ProgramKey, AdmissionContext, CurrentToken under the Endpoint03 prefix. Diagnostic/Cell/Refusal/Counters retain the exact field order/defaults and mathematical evidence fields of [Endpoint02 public header](../include/apsis_drift/origin_boarding_intermediate_endpoint02.hpp#L128), with every Endpoint02-specific enum/cell/counter/refusal name replaced by the corresponding NEW type; version becomes3/Candidate0. Mathematical original PhaseCell/parts/sites/owners/hip records remain original value types. No authority type is aliased. The only value-layout change is SliceEvidence below. No persistent lateral, ABS, b, roll-height, direct-distance/margin scratch or second Slice copy is stored in Cell. Runtime kinematic/support/self/complete flags remain actually earned; no static SELF qualification boolean. The same downstream staticfalse world/material/halo/route/seat/actor/save/dynamics/strength/friction/first_flight fields remain unearned.

SliceEvidence exact field order: `array<uint64_t,2> operation_attempted{},operation_written{}; uint32_t guard_attempted{},guard_written{}; BoardingFootSiteScalarBounds limiting_bound; double y{},lo{},hi{}; uint8_t operation{255},guard{255},side{255}; SliceCondition condition{}; bool arithmetic_supported{},complete{}; uint8_t preflight_guards{},zero_mask{};`. Target96 bytes. Counters retain phase_calls, original phase counters and the19 old outer uint64 fields, including construction_guards/operations; target200. Proposed maximum Expected1600, Cell11032, Limits200, Refusal184, Slice96, Key16, Context40, Token40, validatorExpected40. Actual symbols must corroborate or stop admission. Maximum creator8192/scratch49152/output16777216/shared stdout+stderr16384 stay fixed.

Public pure constexpr `boarding_intermediate_endpoint03_required_output_bytes()` returns `2*sizeof(Expected)+sizeof(Cell)`, computed after complete types. No retained required-size member. Diagnostic.output_capacity_bytes is actual `2*sizeof(Expected)+actual_vector_capacity*sizeof(Cell)`. Early refusal owns headers only. All outputs must use actual sizes, not forecast constants.

ProgramKey contains storedY/version3/Candidate0; private constructor friend ONLY new bounded, with read-only getters. Context privately constructed ONLY new bounded, binding owner/Data/parts/owned immutable Request/private Key; five pointer fields, deleted copy/move/all assignments, read-only getters. Token privately constructed ONLY new current_cell after one actual accepted original call AND current P251, binding context/owner/current cell/Request/Data; five pointers, synchronous copies only, never returned as capability. Consumers validate all anchors before dereferencing. No original friendship/version/type is relaxed; no old report, key/token, copied successful flags or construction mask can issue new authority.

## 3. Preflight, source and one fresh call

FP-FIRST is one fixed counted guard per API call, even unexpected errors; no lowerable FP field. External test totals count1 for every actual call; structured Slice.preflight_guards=1. Exact preflight order: original exported FP helper; Candidate0; each raised Limits field in order; TWO owning headers budget. Exact unexpected strings respectively: `intermediate endpoint03 unsupported floating point`, `intermediate endpoint03 invalid candidate`, `intermediate endpoint03 invalid limits`, `intermediate endpoint03 output headers`. Unsafe path performs no source/request/vector/Diagnostic allocation beyond the required bounded string error representation. String objects already live in Expected alternatives; one transferred bounded error buffer remains inside the named error allowance, not two copied heaps.

After safe preflight create the current Expected, Request680 and Refusal184; check REQUIRED one-slot room BEFORE S64. Structured output_capacity shortfall has zero source/construction/phase work and headers-only actual bytes. This separately named11520 source forecast includes Request/Refusal; preflight10656 does not claim those absent slots cover it.

S64 reuse is a new private noinline source adapter using a local EMPTY Endpoint02 Diagnostic, old Endpoint02 Limits and Refusal, calling ONLY unchanged `detail::intermediate_endpoint02_source_enroll` ([Endpoint01 CPP](../src/origin_boarding_intermediate_endpoint01.cpp#L1043)). It may not call the old complete constructor or any old public assessment. Preserve ALL64 actual seed rows, full15 bindings/14 regions/family tuples and the actual old S18 seed factory; source_enrolled is actual fresh helper-success AND old.source_enrolled, never mask alone. Old seed S16/version and seed-Y rows remain authentication of the immutable original seed, not new version3/Y. New G01/G03/G20/G21 separately bind the new program. Copy parts/source work/mask from this same local successful source. Forward failure field-by-field BY REFERENCE from the local full old184 reason into the distinct new184 scratch/owning first refusal; no aggregate oldRefusal report/return is consumed, no truncation. Original full metadata/predicate/side/source/SELF fields stay literal. The adapter's old EMPTY carrier/old L/R/catalog/factory locals die at adapter return BEFORE new construction, Key, graph/body/audits. Current seed Request and newly copied full parts stay owned.

Construction is ONE new noinline by-reference bool adapter in a new END-only planted suffix. It reuses the original primitive definitions and symbolic recipe, never `intermediate_endpoint02_construct` followed by a corrected Y. All78/21 records and actual new packet validation must complete before new bounded privately issues Key, then assigns reporting_elapsed_seconds=2 and constructs Context. Clock remains0 through every earlier error/refusal; later failures retain the requested-hold cue without original timing promotion.

New producer D01–03 are charged revalidations after construction/key, before graph: same owner/provider/Data/parts/request/key; original FP; new Candidate/version3/generated packet with exact duration/share/S64/G/O readiness. No second request. Reserve1 once; catch allocation failure as output_capacity, release abnormal capacity before reporting headers-only, emplace/reset one flat Cell. D prefix is actual uint32 bits0..2, no full Cell temporary preallocation. Increment phase_calls immediately before ONE unchanged `boarding_route_foot_phase_cell(request,0,1,false,originalCaps,work,cell.phase,originalReason)` with original1/2/1/3/6. D04 checks actual result immediately afterward, BEFORE projection; retain original reason/capacity/unsupported state honestly. No retry, body0 shortcut or presumed acceptance. All original numerical domains/guards/branches are unchanged.

## 4. Primitive semantics and genuine geometry

Use unchanged [planted scalar primitives](../src/origin_boarding_planted_legs.cpp#L56), `phase_constant`, `self_root`/exact squared-product sign, and `transfer_small_root` at :2351. Interval DIV is specifically the existing outward reciprocal interval plus MUL, NOT singleton RN(1/L). The later unit24 remains its separate original direct endpoint DIV protocol. CONST owns original ordered term adds/exact sum-domain checks as one explicitly charged compound; SMALL_ROOT owns original sqrt endpoint proposals plus exact residual/sign correction protocol, at most four attempts per endpoint/eight correction checks. No alternate root oracle, tolerance, inverse angle, widened scalar or bisection. SMALL_ROOT requires supported0<=input<=16384; exact-zero shortcut still earns its charged row.

Each operation charges BEFORE operand field reads/construction/helper call, sets attempted, verifies supported finite ordered result and publishes written/current limiting output. POINT/selected endpoint/literal selection, primitive local/argument/pending outputs, finite checks and same primitive zero/one branches are bounded constituents of that row. RN midpoint SUB/MUL/ADD are three named rows and individually finite-checked. Static authenticated original term domain[-8,8], yaw.125, hip.14, ankle.1, links<=1 and certified-root domain bound all constructor operands; no overflow/NaN is used to get a candidate. Any actual unsupported primitive/result stops hard. Guards charge BEFORE tuple reads; true readiness earns written, false readiness leaves written clear. No count is inferred from an attempted upcoming cursor.

Exact-real nominal root yaw preservesY/norms; held sole yaw0 makes its frame WORLD identity. Hip is root+Rroot*(signed.14,0,0), ankle is sole+(0,.1,0). Let x_s,z_s be the same nominal horizontal ankle−hip components, c_s=x_s²+z_s², a_s=soleY+.1. Exact nominal squared distance is c_s+(Y−a_s)². Let b=2−sqrt3. On down branch Y>a_s, original necessary roll is `(Y−a_s)*b−ABS(x_s)>=0`. This is the same exact-real predicate as original [phase_leg roll](../src/origin_boarding_planted_legs.cpp#L3533), under authenticated nominal proper-yaw/flat-sole premises; rounded stored affine columns are not asserted orthogonal. Independent interval recipes can differ in width. Both sides are mandatory.

Retain BOTH full supported signed lateral enclosures x_port/x_star from O20/O30, BEFORE the active-leg scratch is overwritten. O56/O59 retain their full ABS enclosures. O54/O55 retain certified sqrt3 and whole b. Selected quotient/height outputs reuse active-leg slots only after previous reach consumers finish. Direct nominal reach and roll consume the SAME retained x/ABS/a/b source provenance. No rounded midpoint lateral value, nominal point divided by a rounded reciprocal or reporting angle enters authority.

## 5. Exact operations O01–O78

One lowerable construction_operations ceiling78. Labels are one-based, operation cursor0..77; side0PORT/1STAR/255common. Before EVERY record reset only Slice.limiting_bound, set upcoming cursor/side/Stage20; preserve already earned y/lo/hi/zero bits. Capacity sets upcoming cursor but neither bit. Successful charge sets attempted; supported write sets written. Bit location is word=(row−1)/64, bit=(row−1)%64. Complete operation masks are `{UINT64_MAX,0x0000000000003fff}`; word1 upper50bits always0. Never shift by64 or treat word0 alone as completion.

|Rows|Literal expression and destination|
|---|---|
|O01,O02|CONST original rootX then rootZ, endpoint0 term order.|
|O03–O05|CONST PORT soleX,Y,Z.|
|O06–O08|CONST STAR soleX,Y,Z.|
|O09|SQUARE point(root yaw k).|
|O10|ADD point1+O09.|
|O11|SUB point1−O09.|
|O12|DIV O11/O10 →cos.|
|O13|MUL point2*pointk.|
|O14|DIV O13/O10 →sin.|
|O15|NEG O14 →negative_sin.|
|O16,O17|PORT MUL point(−.14)*cos then point(−.14)*negative_sin.|
|O18,O19|PORT ADD rootX+O16, rootZ+O17 →hipX/hipZ.|
|O20,O21|PORT SUB soleX−hipX →RETAINED x_port; soleZ−hipZ →active z_port.|
|O22,O23|PORT SQUARE retained x_port, then z_port.|
|O24|PORT ADD O22+O23 →retained c_port.|
|O25|PORT ADD soleY+point.1 →retained a_port.|
|O26,O27|STAR MUL point(+.14)*cos then point(+.14)*negative_sin.|
|O28,O29|STAR ADD rootX+O26, rootZ+O27.|
|O30,O31|STAR SUB soleX−hipX →RETAINED x_star; soleZ−hipZ →active z_star.|
|O32,O33|STAR SQUARE retained x_star, then z_star.|
|O34|STAR ADD O32+O33 →retained c_star.|
|O35|STAR ADD soleY+point.1 →retained a_star.|
|O36,O37|ADD pointL1+pointL2, SQUARE →retained M.|
|O38,O39|SUB pointL1−pointL2, SQUARE →retained m.|
|O40|PORT SUB point(m.high)−point(c_port.low) →v.|
|O41|PORT MAX_ZERO: v.high<=0 writes canonical+0, otherwise preserves v.high bits, point(t). Publish zero_mask bit0 only after supported write.|
|O42|PORT SMALL_ROOT O41.|
|O43|PORT ADD point(a_port.high)+point(O42.high); retain reachlo_port=result.high.|
|O44|PORT SUB point(M.low)−point(c_port.high) →w; then G07.|
|O45|PORT SMALL_ROOT point(w.low), only after G07.|
|O46|PORT ADD point(a_port.low)+point(O45.low); retain hi_port=result.low.|
|O47|STAR SUB point(m.high)−point(c_star.low) →v.|
|O48|STAR MAX_ZERO as O41, zero_mask bit1.|
|O49|STAR SMALL_ROOT O48.|
|O50|STAR ADD point(a_star.high)+point(O49.high); retain reachlo_star=result.high.|
|O51|STAR SUB point(M.low)−point(c_star.high) →w; then G08.|
|O52|STAR SMALL_ROOT point(w.low), only after G08.|
|O53|STAR ADD point(a_star.low)+point(O52.low); retain hi_star=result.low.|
|O54|SMALL_ROOT point3 →retained sqrt3, SAME original certified recipe.|
|O55|SUB point2−WHOLE O54 →retained b; then G09.|
|O56|PORT ABS WHOLE retained x_port →retained abs_port.|
|O57|PORT DIV WHOLE abs_port/WHOLE b →active quotient_port.|
|O58|PORT ADD point(a_port.high)+point(O57.high); retain rolllo_port=result.high.|
|O59|STAR ABS WHOLE retained x_star →retained abs_star.|
|O60|STAR DIV WHOLE abs_star/WHOLE b.|
|O61|STAR ADD point(a_star.high)+point(O60.high); retain rolllo_star=result.high.|
|O62|MAX reachlo_port,reachlo_star (PORT first on equality) →accumulated lower.|
|O63|MAX accumulated lower,rolllo_port (accumulated first on equality).|
|O64|MAX accumulated lower,rolllo_star (accumulated first on equality) →publish final slice.lo.|
|O65|MIN hi_port,hi_star (PORT first on equality) →publish slice.hi; then G10.|
|O66|RN finite SUB hi−lo.|
|O67|RN finite MUL O66*.5.|
|O68|RN finite ADD lo+O67 →ONLY storedY; publish slice.y; then G11–G13.|
|O69|PORT SUB point(Y)−WHOLE a_port →retained active proof height.|
|O70|PORT SQUARE O69.|
|O71|PORT ADD WHOLE c_port+O70 →direct D; then G14,G15.|
|O72|PORT MUL WHOLE O69*WHOLE b →active roll product.|
|O73|PORT SUB O72−WHOLE retained abs_port →direct roll margin; then G16.|
|O74|STAR SUB point(Y)−WHOLE a_star.|
|O75|STAR SQUARE O74.|
|O76|STAR ADD WHOLE c_star+O75 →direct D; then G17,G18.|
|O77|STAR MUL WHOLE O74*WHOLE b.|
|O78|STAR SUB O77−WHOLE retained abs_star →direct roll margin; then G19,G20,G21.|

MAX/MIN are named charged finite scalar selection records, preserving selected binary64 operand bits on equality. All generated unused packet terms/zero threshold outputs are canonical+0. The max-zero test uses the supported upper bound, never rounds or guesses the exact radicand sign. Its inactive branch is an inward proof only; a positive uncertain upper bound can conservatively lose feasibility. O64 publishes lo only when written; O65 publishes hi; O68 publishes Y. Zero bits are meaningful only after O41/O48 writes. No later row clears them.

## 6. Guards G01–G21 and exact interleaving

One construction_guards ceiling21. Cursor0..20/side0,1,255; bit(label−1) in uint32; complete mask0x001fffff, upper11bits0. Set upcoming Stage19/cursor/side and clear current limiting bound BEFORE charge; preserve selected scalar evidence. Failed capacity sets no bit. Charged readiness false remains unwritten; successful full compound true writes its bit. Arithmetic/support failures hard unsupported, structural identity failures ordinary slice_identity, finite sufficient-check failure ordinary slice_unavailable with specified SliceCondition. Each bound below is attached only after actual support/finite/order evidence.

|Guard|Placement, ordered tuple and mapping|
|---|---|
|G01|After actual S64: same current owner/Data/parts/Request; valid source; source_enrolled from actual successful helper, count64/full source mask. False slice_identity/identity.|
|G02|Original FP environment after all source factories, before O01. False hard unsupported.|
|G03|NEW version3/Candidate0 and immutable seed template: rootX/Z/I/star/yaws/torso/zero humps/folded family/share/duration/held endpoints. False slice_identity/identity.|
|G04|Eight selected coordinate packet counts1..3, finite terms abs<=8, canonical unused zeros BEFORE CONST. Numerical/domain failure hard unsupported; false immutable tuple identity slice_identity.|
|G05|Original compiler version/links/source S63 identity; finite positive unchanged L1=.47285/L2=.47478 <=1. Nonfinite hard unsupported; wrong identity slice_identity.|
|G06|Original S64 proper rational yaw/torso-folded provenance; Y-preserving root/hip and exact sole yaw0; no pelvis pitch/abduction or stored-column orthogonality claim. False slice_identity. Then O01–O44.|
|G07|After O44: supported finite w, w.high<=16384, then w.low>0. Invalid/support/domain hard unsupported; finite low<=0 ordinary no_positive_upper. Then O45–O51.|
|G08|After O51: identical STAR check. Then O52–O55.|
|G09|After O55: attach actual whole b; supported finite ordered first, then b.low>0. Invalid hard unsupported; finite nonpositive low ordinary no_positive_roll_factor. No DIV before success. Then O56–O65.|
|G10|After O65: both stored selected bounds finite, then lo<hi. Nonfinite hard unsupported; false order ordinary empty_inward_interval. Then O66–O68.|
|G11|After O68: actual stored finiteY, then abs(Y)<=8. Nonfinite hard unsupported; finite domain failure ordinary candidate_domain.|
|G12|Actual stored Y>lo. False ordinary midpoint_unavailable.|
|G13|Actual stored Y<hi. False ordinary midpoint_unavailable. Then O69–O71.|
|G14|PORT actual stored Y>a_port.high, with supported a attached. False ordinary verification_inconclusive.|
|G15|PORT supported direct D; first D.low>m.high, then D.high<M.low. Invalid hard unsupported; finite false ordinary verification_inconclusive. Then O72–O73.|
|G16|PORT actual supported direct roll margin; finite ordered first, then margin.low>=0. Invalid hard unsupported; finite false ordinary verification_inconclusive. Then O74–O76.|
|G17|STAR Y>a_star.high, same G14 rule.|
|G18|STAR direct-D inclusion, same G15 predicate order/rule. Then O77–O78.|
|G19|STAR direct roll margin, same G16 rule.|
|G20|After both direct verifications: check immutable seed fields BEFORE assignment; write ONLY both rootY packets `{Y,+0,+0}`count1; verify exact generated packets/all remaining static fields. False slice_identity. No factory or candidate recomputation.|
|G21|Charged compound: original environment precheck; actual generated packet structure/domains; unchanged original request validator ONCE; original environment postcheck. Numeric/FP/validator inability hard unsupported, structural mismatch slice_identity. Complete only after final postcheck.|

G21 exact suborder: environment helper false stops before validator; check counts/canonical-unused/static held fields/generatedY equality (structural failure ordinary), original finite domains (numerical failure hard); call `boarding_route_foot_phase_request_valid` once and keep its actual current/pending expected<void,string> returns; any unexpected under prior predicates is hard arithmetic/support inability, not reach evidence parsed from an error string; final original environment helper must succeed. Old validator's unsafe-FP sum shortcut cannot issue the new Key. All original bounded validator term adds/sign checks/environment/error locals belong to this ONE charged compound and are explicitly source-accounted.

Global execution order is source64; G01–06; O01–44/G07/O45–51/G08/O52–55/G09/O56–65/G10/O66–68/G11–13/O69–71/G14–15/O72–73/G16/O74–76/G17–18/O77–78/G19–21; private Key/clock/context; D01–03; allocation/reset; ONE original compiler/D04; P251; D05–31; B63/unit24; pairs/owners/hip/planes. No extra guard is silently inserted or combined count inferred. construction_guards0 stops G01 before any operation; construction_operations0 stops O01 after G01–06. Original source failure prevents all construction. Original graph/body/work caps are reached only after construction/key.

## 7. Proof, completion and stops

Existing inward reach formulas use supported c/a/m/M: lower uses outer ADD.high of `a.high+sqrt_up(max(0,m.high−c.low))`; upper uses outer ADD.low of `a.low+sqrt_down(M.low−c.high)`. Roll lower uses outer ADD.high of `a.high+quotient.high`, where quotient encloses WHOLE ABS(x)/WHOLE supported positive b. Hence strict storedY above BOTH conservative roll lowers preserves the exact-real necessary roll inequalities under the authenticated chart. The explicit G14–19 full nominal interval checks can still conservatively refuse, and do not replace the original compiler's wider full-frame evaluation or any later sector/timing/body/support/SELF guard.

Complete construction requires all78 written operations, all21 written guards, full true source enrollment, exact generated Request and actual G21 final postcheck. No mask alone grants authority. Slice.complete means construction only; arithmetic_supported startsfalse, earns only after an actual supported operation write, stays partial through ordinary/capacity stops, and clears on actual constructor FP/numeric/root/validator failure. After complete construction, an unrelated later unsupported graph/pressure/SELF failure clears corresponding aggregate arithmetic but preserves earned complete Slice arithmetic/masks/Y. Slice condition stays none on successful constructor, not a downstream phase cause. Final Slice cursors are operation77/guard20/side255; G21 clears its current limiting bound and does not invent a final transcript bound. Clock2 only follows actual Key issuance; earlier failures retain clock0.

New first_refusal retains first actual ordinary full184 metadata; terminal stop fields retain actual later capacity/unsupported and cannot be downgraded. Each upcoming scalar/guard/pair/axis record resets its own scratch, not already accepted certificates or selected Slice values. Original phase reason is copied intact; default two-double bound, nested [0,0]/depth0 or predicate0 is not supported evidence or a different invocation. Actual forward[0,1] remains explicit. Unsupported original result retains unsupported; capacity retains capacity. Generic limiting support is never inferred from an unsupported/default original reason.

Original accepted current protocol is identical to immutable [Endpoint01 sections5–9](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT01.md#5-exact-producer-order-and-current-projection-p251), as actually applied to [Endpoint02](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT02.md#2-source-first-ownership-and-private-authority), with all owner/request/key/Candidate/version identities rebound to NEW version3/types/actual fresh call and all generated rootY comparisons using the new Key. No inherited preflight order overrides this draft. Specifically D01–03 before graph/D04 before P; P251 lazy identity53+11 carriers*18; P11 canonical factory only after charge; D05–31 source-bound1/16 COM allocation and all16 edges; B63 current values/frame/regions/relative18; unit24 direct original WORLD K−H/K−A SUB then exact-link endpointDIV; both hip/ankle proofs consume that SAME unit axis; owner14/pair105/hip2 and original grouped finite signed axes, exact sentinels/partial masks/reset order. Preserve ankle uY>=0/flat sole cancellation, actual pelvis-to-trunk waist support, full hip caps/finite slab and all91 nonadjacent/distal negative obligations. No body/pressure/SELF before accepted original graph/full P; no successful diagnostic body0 treated accepted.

Full endpoint accepted only if genuine original current completion, finite nominal support and complete SELF are earned. WORLD/material/HALO/route/reverse joins/pan-supported seat/occupied mechanisms/actor/dynamics/save/FirstFlight remain separate staticfalse. Parents462/361/352 stay open. Empty conservative slice/failed stored membership/direct verification is ordinary unavailable, not all-Y impossibility. No alternate program or changed limits/owners/body policy follows.

## 8. Literal Limits/work order

All25 fields are lowered only, checked in this order against defaults:

|Index|Field|Default|
|---:|---|---:|
|0|phase.graphs|1|
|1|phase.legs|2|
|2|phase.bodies|1|
|3|phase.sectors|3|
|4|phase.timing|6|
|5|source_guards|64|
|6|projection_guards|256 (actual251)|
|7|definition_guards|31|
|8|sole_extrema|4|
|9|source_coordinates|8|
|10|intersection_operations|4|
|11|midpoint_operations|4|
|12|allocation_operations|6|
|13|pressure_candidates|2|
|14|disk_edges|16|
|15|self_body_guards|63|
|16|self_pairs|105|
|17|self_axes|1470|
|18|self_signed_trials|2940|
|19|self_owners|14|
|20|self_hip_complements|2|
|21|unit_axis_operations|24|
|22|output_bytes|16777216|
|23|construction_guards|21|
|24|construction_operations|78|

Limits.phase uses the existing five fields, unused original phase caps defaults untouched. Work records successful charges/one phase invocation; no default/last-failed cursor becomes a count. Fixed preflight1 is Slice metadata, not another lowerable field. Output exact replay uses REQUIRED room, never headers-only actual-owned bytes. A structured FIRST unavailable/source/phase/support/SELF refusal is a valid exact replay baseline. An unexpected FIRST disables later structured-control baseline without inventing a substitute. No unregistered source inspector/factory/pose/oracle call is permitted.

Future matched manifest selects one genuine source creator, ONE FIRST program, conditional actual-reached zero/used−1/exact controls, isolated raised fields, six unsafe regimes, invalid enum/empty or moved source/lifetime consistency as expressly indexed there. RAW0, no reverse/range/grid or secondY. Scalar hashing reads masks/cursors/conditions normally but y/lo/hi/zero only after actual O68/O64/O65/O41/O48 written bits; successful source ID0..14 hash needs actual source_enrolled/count64/fullmask, not mask alone. Any proposed word-crossing64/65 controls need explicit roster selection and genuine reached-prefix gating. No extra calls are implicit in this method draft.

A01-only independent nominal reach/roll corroboration is sequential AFTER producer, gated by actual complete Slice/all78/all21/source proof; it uses the retained storedY and locally reconstructed immutable literal packet only, no original generator/phase/helper call or new authority. It independently encloses the same nominal proper-yaw/chart expressions, not a body pose/oracle on an incomplete original graph. Maxone such audit, bounded utility2048. Accepted original fields then permit maxone full body-pose audit and105 pair/14owner/2hip interval-certificate audits, also A01-only and sequential. The full body/audit requirement cannot be weakened to make a resource gate pass. Exact test arithmetic/LD resource inventory must be separately reviewed in the matched manifest before registration; no production raw seam is selected.

## 9. Named source forecasts and measurement gates

The sibling `endpoint03-reach-roll-typed-source-forecast-v1.json` (SHA256 `0dc42f8c884f4c31706e28d1a7486dd4d8accfe7654a626a90f970d93dbc18f3`) is part of this proposed method. All maps are mechanically summed resource integers, not numerical coordinate/bound evaluations. Every actual layout/physical chain remains UNKNOWN. Selected target E1600/TWO3200, Cell11032 (conservative cap vs historical10944), FOUR L200=800, R184, Ctx/Token40/40, current/pendingKey32, current/pendingRequest1368, originalCaps/Reason/Work176 and caller2048 are external unless explicitly named inside a header. Work/Slice growth is inside both headers, never counted twice. No Slice copy or constructor cache survives graph/audits.

|Stage|Source forecast|Ceiling|
|---|---:|---:|
|creator|7000|8192|
|preflight|10656|49152|
|required room refusal|11520|49152|
|source enrollment|33208|49152|
|construction|15016|49152|
|enrollment+disjoint construction overcharge|35768|49152|
|reset|34250|49152|
|original graph|48040|49152|
|postgraph canonical|30728|49152|
|pressure/SELF|29192|49152|
|A01 construction audit|25656|49152|
|A01 body audit|48600|49152|
|A01 interval audit|31800|49152|
|owned capacity1 output target|14232|16777216|

Source enrollment retains ENTIRE historical Endpoint02 bound28880 without replacing it by24680. Add distinct newheaders3200/L800/R184/Ctx40/Token40/twoKey32/control32. Inside the intact28880, old TWO1536 header reservation covers lexical oldEMPTY1528/current-pending old owning alternatives; FOUR old200 limits and old184 current reason remain reserved. That baseline itself contains ENTIRE inner24680 including original oldTWO1440 headers/FOUR184 limits/arena/caller/Request/factory/catalog/tuple/error chain. No old allowance is reduced due to a shorter actual path. The old carrier is only genuine pregraph source scratch; no old key/constructor or old report is called. Its fields are copied by reference to current new owners, no aggregate forwarding return. All old carriers/old limits/source catalogs/pending factories die at source adapter return, before the new constructor/Key/graph. Conservatively coadding FULL new construction helper2560 yields35768 even though phases are disjoint; it does not imply old scratch survives in graph.

Construction2560 has named2192/UNUSED368: original arrays input192/yaw144/c-a96/threshold48/active leg192/selected doubles48/proof96; primitive argument/local/pending slots192; constant sign arrays96; certified-root interval/scratch/terms/scalars176; product/reference/closure/cursor slots160; TWO G21 current/pending validatorExpected40=80; library/environment/error512; ADD retained two signed lateral intervals48, two full ABS intervals48, sqrt3/b intervals48 and two roll-height endpoints16. Full active-leg/proof storage explicitly owns quotient/height/direct-roll intermediates after prior consumers finish; no two simultaneously needed operands alias an overwritten output. Old2032/16 is historical only; the helper reservation actually grows to2560. A by-reference bool result gives no aggregate Slice return or elision discount. Current/pending local primitive/validator returns are named even if compiler elides them.

Factory7680 names EIGHT optional688 returns5504 + Request680 + six constants576 + step96 + refs/error512 =7368/UNUSED312. Entire source catalogs two full3840/two converted1920 and tuple2048 remain old28880. Tuple2048 names three region arrays672/two part records128/descriptors256/refs-error512/sort480. Pressure6144 names3192/UNUSED2952 including FOUR128 corner return slots512 and full edge/current/pending returns. SELF6144 names5160/UNUSED984 including relative1296/whole region224/FOUR solids192 return slots768/SIX points432/24Intervals576/two squared scratch112/eightVec192/THREEowners168/THREEhip+THREEhalfRay336/extraIntervals192/doubles192/refs160/error512. B44 region factory remains separately staged1440 (THREE region arrays672+tuple/ref512+error256); no graph-resident extra catalog. Original owner/hip/signed-axis helper chains still need actual new measurements.

Construction audit2048 names1632/UNUSED416: sixteen LD bounds512/two LD vectors192/twelve LD scalars192/sixteen double slots128/refs-closures-cursors96/library-error512. Current/pending independent literal Request1368 lives in outer audit map, no inspector. It dies before body audit's separately reconstructed Requests; no extra body oracle or original query. Body map retains TWO8400 Oracles16800/fullCell11032/headers3200/arena4096/L800/caller2048/Requests1368/utility8192/scalar-error1024/inactiveKeyContext40=48600. Interval follows after body Request and both Oracles die, keeps its own8192 utility and conservatively inactiveRequest1368. Detailed body7904+UNUSED288 and interval4736+UNUSED3456 remain prior selected local inventories, to be independently corroborated by Tests. New independent arithmetic/helper objects must fit their named stage before registration and actual measurement.

Creator preserves full original7000 reservation, including arena4096/current-pending source evidence1040/port376/two creatorExpected80/constructorLimits96/aliases32/caller1024/init-error256. Graph retains ENTIRE original32768 proof PLUS newTWO3200/arena4096/full Cell delta3296/FOURL800/caller2048/originalCaps72/Reason64/pendingoptional688/currentRequest680/R184/Ctx40/Token40/Key32/control32=48040. Original Work40 stays inside the full original proof, and is separately retained as176 only in other stage maps. Reset retains TWO full11032 Cells plus axis initializer current/pending210 and all outer owners. No old proof/slack/NRVO is borrowed. Body headroom552 is only forecast margin and cannot hide an omitted owner, request, global, library frame or helper.

Physical gates require both strict GCC/Clang Release objects/layout symbols and complete saved disassembly/duplicate-preserving stack records, including true maxima across actual PIC/native test variants and included-TU measurements. Source bounds and physical chains are independent. Inventory all fresh bounded/source adapters/constructor specializations/CONST/validator/certified-root/product-sign/primitive alternatives; whole original graph leg/rotate/scale/timing chain; pressure/SELF/canonical/owner/hip/half-ray/solid returns; all arena/heap/globals/caller/pendingExpected/Limits/Requests/keys/context/token/initializer/current error objects; destruction/standard-library/libm leaves. Do not replace a machine frame by a source helper pool, count a dead old carrier as authority, or omit a project helper inside a library leaf. A failed actual gate stops before FIRST with exact stage, never raises ceilings or fits geometry.

## 10. Immutable file boundary and next gates

Baseline pins1182 files, SHA bb33cd0615c6bf2b9ef26452770ffcf2ae2d995d2fe122c7e045decdfa2b36ef. Proposed implementation later: new `include/apsis_drift/origin_boarding_intermediate_endpoint03.hpp`, new internal header/producer/test with same stem; END-only typed source/canonical adapters after complete current Endpoint01 CPP48734 bytes SHA c654ba25e45149966eeee82abb339864f4e52a147bc3a5f4a685b441f2cc8dc4; new typed construction/SELF after planted330161 bytes SHA38d435846977eeeac95766584d03e2b06c159d7ad3b4847465e61cc8346a6eb2; new pressure after boot247916 bytes SHA2b5df5e8034e1063ac40af520eec2f551f92370aceafb7ccffd6e8a80cead865. Every original prefix/header/function/save seed/body/source policy/result stays byte-identical. No old class gains friendship or accepted-version change. Narrow CMake/strictFP/test registration and public method/doc index follow reviewed publication only.

Finite manifest, independent exact mathematical/source/schema review and public registration remain required before coding. Full actual source/physical admission and held source/binary freeze precede ONE retained FIRST per compiler. Outcomes remain UNKNOWN; expectations may follow retained observations only. Full ordinary dual compiler suites, pinned format/tidy20, native compatibility, reviews and exact-head CI are publication gates. No motion redesign, altered body/owner/link/joint policy, art/release or parent closure is selected.

## Selected matched finite test manifest

Held payload SHA256: `902a74b384a3603b62c4bd8c7530b17559bded02430a02e5586719a7265229af`.

# Endpoint03 reach-and-both-roll — matched finite test/resource draft v1

SOURCE-ONLY DRAFT, outcome and actual layout/resources UNKNOWN. Issue499 is selected; this file does not authorize coding, compilation, queries or FIRST. It binds Core's exact typed method `endpoint03-reach-roll-exact-typed-method-v1.md` SHA9349774b07e6ccf2689b29ec0f21578b78463639a51066ba5b8e1f50bdfeb3ac and its source forecast `endpoint03-reach-roll-typed-source-forecast-v1.json` SHA0dc42f8c884f4c31706e28d1a7486dd4d8accfe7654a626a90f970d93dbc18f3. Root agreed the two word-boundary controls subject to the literal table; proposed93 labels below still require final paired review/selection/public registration. No coordinate, angle, derived geometric bound, candidate outcome or fixture is evaluated or predicted. Existing FIRST/source/binaries/receipts remain immutable.

## Surface, ownership and one FIRST

One genuine existing provider creator, one arena4096, one default public FIRST of new version3/Candidate `root_y_reach_roll_slice=0`, fixed forward whole hold. Direct public/private calls own their Expected in the assessment stage. No owning query wrapper, returned Summary, copied full report, old constructor/assessment/report replay, scalar or range/reverse input, second program/corrected height, raw seam, inspector, grid or body retry. Summary is filled by reference. Creator Expected and constructor locals die before A01. Only genuine creator failure suppresses all93 labels; a structured A01 S64/construction/original/support/SELF refusal remains a baseline.

A01 prints source, construction words/guards/cursors/partial flags, original reason and terminal cause, downstream availability and owned bytes BEFORE any generic numerical assertions. No positive outcome, roll sign, branch, root coefficient or body completion is mandatory before retained FIRST. Each actual API call receives one fixed FP-FIRST check; external totals include it for unexpected errors too. Calls are serial; retain no archive of reports or previous source creators. TWO owning header reservations remain explicit even when return elision occurs.

## Literal25-cap controls

A02–26 set exactly one field to zero. Except output-zero, run only if structured A01 successfully charged the field at least once. A27–51 set exactly one field to literal default+1 and always run after genuine creator success. A52–76 set exactly one field to actual A01 successful used count-minus-one only if used>1; used0 is unreached and used1 duplicates the registered zero control, hence honest SKIP. Output one-less uses the pure constexpr REQUIRED one-cell budget-minus-one whenever structured A01 exists, not actual-owned bytes-minus-one. All other fields remain default. No replacement call fills a skip.

|Index|Field|Default|Zero|Raised|Reached one-less|
|---:|---|---:|---|---|---|
|0|`phase.graphs`|1|A02|A27|A52|
|1|`phase.legs`|2|A03|A28|A53|
|2|`phase.bodies`|1|A04|A29|A54|
|3|`phase.sectors`|3|A05|A30|A55|
|4|`phase.timing`|6|A06|A31|A56|
|5|`source_guards`|64|A07|A32|A57|
|6|`projection_guards`|256|A08|A33|A58|
|7|`definition_guards`|31|A09|A34|A59|
|8|`sole_extrema`|4|A10|A35|A60|
|9|`source_coordinates`|8|A11|A36|A61|
|10|`intersection_operations`|4|A12|A37|A62|
|11|`midpoint_operations`|4|A13|A38|A63|
|12|`allocation_operations`|6|A14|A39|A64|
|13|`pressure_candidates`|2|A15|A40|A65|
|14|`disk_edges`|16|A16|A41|A66|
|15|`self_body_guards`|63|A17|A42|A67|
|16|`self_pairs`|105|A18|A43|A68|
|17|`self_axes`|1470|A19|A44|A69|
|18|`self_signed_trials`|2940|A20|A45|A70|
|19|`self_owners`|14|A21|A46|A71|
|20|`self_hip_complements`|2|A22|A47|A72|
|21|`unit_axis_operations`|24|A23|A48|A73|
|22|`output_bytes`|16777216|A24|A49|A74|
|23|`construction_guards`|21|A25|A50|A75|
|24|`construction_operations`|78|A26|A51|A76|

A77: exact-used replay, only if A01 returned a structured Expected, including ordinary SliceUnavailable or hard original/support/SELF refusal. Set every nonoutput field to actual successful charges, output to REQUIRED. Compare genuine semantic hash, source owner separately, first/terminal causes, masks/cursors, partial flags and actual owned bytes. An upcoming failed cursor is not a used count. No structured Expected means SKIP, not a replacement call.

A78–83: FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO, FTZ, DAZ, then x87/SSE rounding-unit disagreement. Save/restore full fenv flags, rounding and MXCSR with no trap changes. Unsupported actual hardware regimes SKIP without substitutes. FP-FIRST unsafe exact error wins before candidate/caps/header/source/constructor. A84: literal Candidate255, default caps, safe invalid-candidate error.

A85: copy genuine alias, move alias to saved public handle, assess now-empty alias once while original remains valid, restore alias. Source S01 invalid-binding is structured and no construction/graph is earned. A86: move original to valid moved-to handle, assess it once, then restore original. A87: copy survivor; move original into scoped discarded handle; destroy discarded BEFORE assessment; original is genuinely empty and creator Expected already dead; assess survivor, then restore original. A88: assess restored original. Genuine owner/copy/move operations only; no default provider, fake Data/Context/Key/Token, cached factory or second arena. Valid lifecycle calls compare exact semantics only when structured A01 exists.

A89: source_guards0+construction_guards0, only if A01 source count>0; source capacity wins and construction/Key/graph are NOT_RUN. A90: construction_guards0+construction_operations0, only if A01 guard count>0; failed G01 charge wins, no attempted bit/O01/Key/graph. A91: construction_operations0+phase.graphs0, only if A01 operation count>0; actual G01–G06 precede failed O01 charge, no Key/clock/phase. This does not independently exercise graph-zero.

A92: construction_operations64, all other fields default. Dispatch only if structured A01 operation count>64, actual source enrollment/count64/fullmask, O64 written and G09 written are retained as a bounded Summary availability flag. No numerical value is stored in Summary. Expect successful O01–O64, word0 attempted/written UINT64_MAX and word1 both zero; successful G01–G09 mask0x1ff/count9; upcoming O65 is zero-based operation cursor64, bit(word1,0) remains clear, Slice operation_capacity with outer slice_operation_capacity/Stage slice_operation. O64 final lo is earned; hi/O65, G10 interval, Y/O68, Key/clock/phase/D/P/support/SELF are NOT_RUN. No late scalar default may be read.

A93: construction_operations65, all other fields default. Dispatch only if structured A01 successful operations>65, actual source enrollment/count64/fullmask, O65 written and actual G10 written; capture this bounded availability flag before discarding A01. Complete O01–O65 gives word0 UINT64_MAX and word1 exactly1, guards G01–G10 attempted/written0x3ff/count10. Genuine G10 lies AFTER O65 and BEFORE O66; it must be independently written, not inferred from a count. Failed upcoming O66 is zero-based cursor65/word1 bit1 clear, same hard operation-capacity cause. lo/hi and their strict interval relation are earned; Y/O68/Key/clock/phase/later stages remain NOT_RUN. No boundary substitute if the dispatch gate fails.

Decomposition93 = FIRST1 +zero25 +raised25 +one-less25 +exact1 +FP6 +invalid1 +lifetime4 +mixed3 +word-boundary2. Every label is visited once with a monotonic next-label counter; actual consumers+skips=93. RAW0/inspectors0 remain literal telemetry and source-verified absence, not silently discarded counters.

## Preflight, masks and authentic earning

Exact unsafe/candidate/raised/header errors are `intermediate endpoint03 unsupported floating point`, `intermediate endpoint03 invalid candidate`, `intermediate endpoint03 invalid limits`, `intermediate endpoint03 output headers`. Order is fixed FP-FIRST, Candidate, raised fields in table order, TWO headers. Safe REQUIRED-minus-one is structured output_capacity before source and construction; actual ownership is headers-only. Required budget and actual owned capacity are independently checked with actual sizeof after later object admission. Structured Slice.preflight_guards=1; every actual call increments external fixed-preflight1.

Charge before borrowing operands; successful charge increments work and attempted, usable supported publication sets written. Failed charge sets upcoming cursor without bit/count; readiness false remains attempted/unwritten. Partial written masks must never authenticate complete construction. Exact global interleave is S64; G01–06; O01–44/G07/O45–51/G08/O52–55/G09/O56–65/G10/O66–68/G11–13/O69–71/G14–15/O72–73/G16/O74–76/G17–18/O77–78/G19–21. The matched table's compound semantics and no extra operations are retained.

Operation labels are one-based O01–O78; stored cursor=row-minus-one, range0..77/sentinel255. Word=(row-minus-one)/64, bit=(row-minus-one)%64. Ordered complete attempted AND written words are {UINT64_MAX,0x3fff}; word1 upper50 bits always zero. Guards G01–G21 map to bits0..20, complete0x1fffff, upper11 bits zero, cursor0..20/sentinel255. No shift by64, old O64 completion predicate or count-only arithmetic support. Compare explicit words in fixed order; do not hash padding or copied unearned numeric fields. A successful scalar shortcut still earns its specific row. Complete cursors operation77/guard20/side255 only after genuine G21 final environment postcheck; current limiting scratch is unearned after that reset.

Read/hash actual lo only after O64 written (word0bit63), hi after O65 written (word1bit0), Y after O68 written (word1bit3), zero branch bit0 after O41 write and bit1 after O48 write. Selected bounds survive a later record scratch reset; limiting_bound is read only when current supported publication belongs to its reached failing row. PORT-first reachlo/hi ties, accumulated-first roll-lower MAX ties and canonical positive-zero rules remain the method's exact source conventions, not synthetic evaluated branches. No row index from Endpoint02 is reused for a new meaning.

Hash actual returned source/part bindings only after source_enrolled from actual successful helper plus count64/fullmask. Source64 authenticates the old seed only. Generated packet/version identity requires actual G20/G21 and the private new Key; never borrowed old flag/report/key/token. Hash actual state/first+terminal metadata/work/masks/cursors/arithmetic/complete flags and stored clock; owner/Data pointer identity is a separate check. Hash body/contact/unit/owner/hip/pair scalar evidence only under its actual publication support and authentic original premises, never default current-body geometry. Original temporal refusal defaults do not alter the actual fixed forward whole call.

Clock remains0 through any construction/G21 failure; successful actual private Key earns2 before D01–03/reserve/phase. It reports requested duration, not timing/body acceptance. Slice arithmetic starts false and earns supported writes; actual constructor arithmetic/FP failure clears it, ordinary/capacity preserves earlier supported arithmetic. Complete Slice arithmetic remains earned if an unrelated later original/support/SELF unsupported result clears aggregate flags. Original capacity/unsupported is terminal despite an earlier ordinary guard; first ordinary refusal and terminal cause remain separate. No complete construction or mask grants original-body, support or SELF acceptance.

## A01-only sequential independent audits

Three noinline stages run AFTER the A01 producer returns and generic evidence accounting; none runs on A02–93. No producer query, original constructor, classifier/raw seam, factory/inspector, alternate Y, original body retry or sampled grid occurs inside an audit.

1. Construction audit, max1: gated by actual successful source enrollment/count64/fullmask, Slice.complete, both78 written words/all21 written guards and finite supported stored lo/Y/hi. Exact binary64 lo<Y<hi is checked without allowance. Reconstruct one immutable literal Request locally using only actual written storedY and original held packet recipe; it is untrusted test arithmetic and issues no authority. Compute nominal proper-yaw rational chart directly in long double from original source terms, authentic lateral hip/sole offsets and exact stored links. For each of two sides corroborate Y above authentic ankle offset, direct squared distance between original min/max link squares, and same-chart original roll expression with certified-policy coefficient corroborated independently in long double. Do NOT reconstruct inward lower bounds, rerun SMALL_ROOT/certified-root/generator, recover a reporting angle or search a second point. Exactly two sides, one stored value, no body pose. Existing scalar corroboration allowance is 2e-12L*max(1,abs(value)); it belongs only to long-double comparison arithmetic and never relaxes any producer guard/direct-margin lower>=0 or exact stored membership. Any tolerance must be visibly confined to the audit; no sampled ideal frame replaces the authenticated chart. Save/restore exception state. End all current/pending literal Requests and chart operands before the body stage.

2. Full body audit, max1: only one retained genuinely complete accepted original current phase and completed Slice can expose body/COM/trunk/arms/physical jets. Independent Oracle uses the actual stored coefficient, unchanged held foot/yaw/torso/share/mass/body literals and original physical duration. No positive pose expected. Contact/pressure expressions require their own genuine projection/supported masks. TWO current/pending8400 Oracles and reconstructed Requests are fully charged; both die before the next stage.

3. Interval SELF audit: only original accepted current phase and actual fresh B63 can expose its records; Unit24 gates relevant same axes. Audit at most105 actually accepted selected pair records,14 actually attempted usable owner records and2 actually attempted usable hip records. Per-record supported publication/premise tuple gates every scalar read, irrespective of eventual final SELF state. Original24-byte supported recovery intervals and exact original reporter convention recover the actual selected axis/sign in grouped order; independent long-double support checks full original solids with that fixed vector. Convex ordinal must be below actual grouped count; not_run never accepts. Whole caps, finite owner slab cuts, pelvis/waist proper-frame support, folded-arm families and ankle cancellation premises remain exact. Failed sufficient owner remains failed after complement success. Unit corroboration divides original-current endpoint bounds by exact stored lengths, not a recreated producer reciprocal normalization. No extra body pose, pair search or phase/certificate replay. Per-kind local counters independently bound105/14/2, combined<=121.

A partial original refusal leaves these later body/SELF audits honestly zero. A construction-only success can permit only stage1. None certifies WORLD/material/HALO/route/pan/seat/actor/dynamics/save/FirstFlight or every possible movement. Ordinary unavailable conservative intersection is not exact-real all-height impossibility.

## Checked finite counts and compact metadata

26 uint32 work counters in exact order: phase.graphs, phase.legs, phase.bodies, phase.sectors, phase.timing, source_guards, projection_guards, definition_guards, sole_extrema, source_coordinates, intersection_operations, midpoint_operations, allocation_operations, pressure_candidates, disk_edges, self_body_guards, self_pairs, self_axes, self_signed_trials, self_owners, self_hip_complements, unit_axis_operations, construction_guards, construction_operations, phase_calls, fixed_preflight. Output owns byte metadata and is not a charged work counter. Guard narrowing and checked accumulation BEFORE storing; aggregate field ceiling is93*its default, phase_calls<=93, fixed_preflight=actual consumers<=93. Original graphs are not API invocations, no DFS multiplication. Masks/upcoming cursors never synthesize work.

Seven explicit uint16 roster/audit counters, each checked before increment/store: creators<=1; consumers<=93; skips<=93; next_slot<=94; construction_audits<=1; body_poses<=1; interval_records<=121. This keeps104 work bytes plus14 bounded roster bytes, target120 after alignment; actual layout UNKNOWN, gate Totals<=128. Separate globals checks:uint64 and failures:uint32 add12, target132 total; retain the full selected global140 reservation. RAW0 and inspectors0 are fixed source/roster absence and explicitly printed. Per-kind105/14/2 loop counters remain local to the8192 interval utility. Compression cannot drop a counter or mask off overflow.

ONE Summary target120/gate<=200 has ONE24 used uint32 array96, hash8, actual-owned bytes8, structured availability and two earned word-boundary flags, bounded padding; no Y/lo/hi, full masks, report archive, second count array, required-size member or returned owning Summary. Capture by reference; boundary flags require the actual written/source predicates specified above. REQUIRED is pure constexpr, no API/inspector query. Monotonic slot validation needs no93-bit archives. Shared stdout+stderr16384 bounds SOURCE, compact FIRST, short SKIPs, three audit counts and final counters; never print all pair geometry/transcripts. Check both stream states/truncation and restore rdbufs.

## Complete source forecast inventories; no resource admission

All new sizes/physical paths remain UNKNOWN. The companion JSON copies every Core component map and verifies integer sums; this is storage arithmetic only, no coordinate or geometric evaluation. Source creator7000/preflight10656/required-room11520/enrollment33208/construction15016/enrollment+disjoint-helper35768/reset34250/graph48040/canonical30728/pressure_SELF29192/construction-audit25656/body48600/interval31800/output14232 are forecast targets under8192/49152/16MiB. Source maps and physical measurements are separate evidence.

Caller creator selected1024: Summary120+streams176+global reservation140+native/boot Expected80+provider optional24+saved stream pointers/bytes24+references32+current/pending errors160 =756 named, UNUSED268. No endpoint report/audit fixture survives into creator. All original current/pending creator returns, source evidence/port/limits/initialization/error and4096 arena remain separately in the complete old7000 map.

Graph selected2048 named1388/UNUSED660: Summary120+streams176+global140+native/boot80+provider optional24+four aliases64+saved stream slots24+references/arguments96+slot/field/candidate/cursor/status112+saved environments64+current/pending errors/check args256+post-return work array192+Hash8+TWO16-byte prefix-mask current/pending arrays32. FOUR Limits200=800 and TWO Expected1600=3200 are OUTSIDE that caller pool; current/pending Request1368, Refusal184, Key32, Context40/Token40, old original caps/reason/work, fullCell/heap and arena are independently additive. These are targets with explicit unused margin, not invented physical stack frames. No pool slack replaces whole original32768 graph.

Enrollment keeps ENTIRE oldEndpoint02 source28880, including its oldEMPTY headers/FOUR oldLimits/refusal, full inner source24680 catalogs/factories/tuple/error chain, then distinct new headers3200/L800/R184/Ctx40/Token40/Key32/control32. Old carrier lives through genuine source helper and dies at adapter return BEFORE new construction/Key/graph. Never invoke its old constructor to correct Y. Current generated Request and copied source parts remain genuine owners. Full disjoint new construction helper2560 is conservatively added to enrollment35768, not hidden in original graph or reduced old source allowance.

New construction helper2560 names2192+UNUSED368: input192/yaw144/c-a96/threshold48/active-leg192/selected-double48/proof96/primitive current-local-pending192/sign arrays96/certified-root scratch176/refs-product-cursors160/G21 TWO expected<void,string>80/library-environment-error512/retained signed lateral48/retained fullABS48/sqrt3-and-b48/roll-height16. Every operand has a named owner through its consumers; by-reference bool gives no aggregate Slice return. Full old factory7680/tuple2048/pressure6144/SELF6144/B44region1440 remain separate complete maps, including all solid/owner/hip/half-ray returns and authenticated region materialization.

Construction audit2048 names1632+UNUSED416: sixteen LD bounds512 (operand/expression/return slots), TWO LD vectors192, twelve LD scalars192 including nested products/scales, sixteen double slots128, refs/closures/cursors96, independent library/error512. Current/pending reconstructed Request allowance1368 is external, not borrowed from those512 LD-bound bytes. Fill chart destinations by reference where possible; any returned bounds/vector helper has its current/pending slots inside the named bounds/vectors above. No full Oracle or original PhaseCell copy. Scalar/loop control may reuse slots only after earlier consumers die; final implementation must itemize actual lexical ownership. Construction audit complete-Slice gate is not a phase/body acceptance gate.

Body utility8192 keeps original named7904+UNUSED288: outer Jets/root points768; side-loop points/Jets/scalars2144; twelve Point argument/local/pending slots1728; twelve Jet slots576; FOUR Matrix argument/local/pending slots1728; loops96; ExceptionState16; norm/root returns96; angle/trig/reciprocal returns96; check labels/references128; independent library/error512; current/pending Y args16. TWO8400 Oracles, fullCell11032, headers3200, arena4096, FOURlimits800, caller2048, reconstructedRequests1368, scalar/errors1024 and inactiveKey/Context40 are external, giving48600. No additional persistent chart/roll arrays survive here. Do not subtract old unused/header/global pools because of early observed stopping.

Interval utility8192 independently names relative54 supported original Intervals1296; LD point operand/local/pending slots1728; frame slots1152; two full solid descriptions1024; pending proof returns512; owner/hip/scalar/axis/refs/error/library helper allowance2480. Original regions are authenticated before scalar corroboration. Retain fullCell/headers/arena/limits/caller/errors separately. TWO Oracles and body Requests are dead; conservative inactive1368 Request reservation remains in31800 map. No allocator/archive/query/extra body pose in this stage.

Before FIRST Root must measure both GCC/Clang strictPIC and native units/layouts/duplicate-preserving .su and disassembly: full creator main/create/public/make/init/auth/quad/edge/primitive/library chain with arena; new source and old source/factory/catalog descendants; all78 row CONST/root/validator/sign/primitive branches; reset full pendingCell/initializers; new current_cell plus full original cell/leg/rotate/scale/timing graph; canonical eight-optional factory; pressure/SELF owner/hip/half-ray/separation/copy returns; A01 chart helper/current-pending bounds and library; body driver/oracle/out-of-line yaw/libm; interval owner/hip/selected-axis helpers; direct postcheck/hash/print/check/stream; destruction/error paths. Add actual heapCell, arena, mutable globals, pendingExpected/Request/Oracle and FOURlimits wherever co-live. Source bounds cannot be replaced with optimizer saving or a library reserve standing in for a project frame. A missing .su match is not zero; preserve duplicate variants/true main ancestry and included-TU normal/PIC alternatives. Any actual excess closes query gate without changing geometry, audits or ceilings.

## Hold and next gates

This draft changes no tracked source or frozen result. Exact paired review/public registration and Root final roster selection precede code. Actual layouts/source+physical admission follow held formatted code, then Root's exclusive immutable source/binary/resource freeze and once-FIRST per compiler. Observed regressions are separately authorized only after retention; no predicted geometry expectations here. All original suites/policies/prefixes and both compiler publication/native/pinned-format/tidy gates remain mandatory.

## Complete producer source forecasts

Held payload SHA256: `0dc42f8c884f4c31706e28d1a7486dd4d8accfe7654a626a90f970d93dbc18f3`.

```json
{
  "status": "SOURCE_ONLY_TYPED_PROPOSAL_NOT_ADMISSION",
  "issue": 499,
  "numerical_executions": 0,
  "compiler_calls": 0,
  "actual_new_layouts": "UNKNOWN",
  "actual_new_physical_stages": "UNKNOWN",
  "outcome": "UNKNOWN",
  "selected_global_ceilings": {
    "creator": 8192,
    "scratch": 49152,
    "output": 16777216,
    "shared_process_output": 16384
  },
  "layout_targets": {
    "expected": 1600,
    "cell": 11032,
    "limits": 200,
    "refusal": 184,
    "slice": 96,
    "work": 200,
    "key": 16,
    "context": 40,
    "token": 40,
    "validator_expected": 40
  },
  "source_stage_maps": {
    "creator": {
      "components": {
        "arena": 4096,
        "current_pending_constructor_evidence": 1040,
        "port": 376,
        "two_creator_expected": 80,
        "constructor_limits": 96,
        "native_boot_aliases": 32,
        "selected_caller": 1024,
        "initialization_error": 256
      },
      "bytes": 7000,
      "ceiling": 8192
    },
    "preflight": {
      "components": {
        "two_expected": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "library_error": 512
      },
      "bytes": 10656,
      "ceiling": 49152
    },
    "required_room_refusal": {
      "components": {
        "two_expected": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "library_error": 512,
        "current_request": 680,
        "current_refusal": 184
      },
      "bytes": 11520,
      "ceiling": 49152
    },
    "enrollment": {
      "components": {
        "entire_old_enrollment": 28880,
        "new_two_headers": 3200,
        "new_four_limits": 800,
        "new_refusal": 184,
        "context": 40,
        "token": 40,
        "two_keys": 32,
        "new_control": 32
      },
      "bytes": 33208,
      "ceiling": 49152
    },
    "construction": {
      "components": {
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "new_refusal": 184,
        "two_keys": 32,
        "context": 40,
        "old_caps_reason_work": 176,
        "construction_helper": 2560,
        "independent_error": 512
      },
      "bytes": 15016,
      "ceiling": 49152
    },
    "enrollment_plus_disjoint_construction_overcharge": {
      "components": {
        "entire_old_enrollment": 28880,
        "new_two_headers": 3200,
        "new_four_limits": 800,
        "new_refusal": 184,
        "context": 40,
        "token": 40,
        "two_keys": 32,
        "new_control": 32,
        "disjoint_construction_pool": 2560
      },
      "bytes": 35768,
      "ceiling": 49152
    },
    "reset": {
      "components": {
        "two_full_cells": 22064,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "refusal": 184,
        "context": 40,
        "two_keys": 32,
        "old_caps_reason_work": 176,
        "two_axis_initializer_arrays": 210,
        "control": 32
      },
      "bytes": 34250,
      "ceiling": 49152
    },
    "graph": {
      "components": {
        "entire_original_proof": 32768,
        "two_headers": 3200,
        "arena": 4096,
        "cell_delta": 3296,
        "four_limits": 800,
        "caller": 2048,
        "old_limits": 72,
        "old_reason": 64,
        "pending_optional_request": 688,
        "current_request": 680,
        "new_refusal": 184,
        "context": 40,
        "token": 40,
        "two_keys": 32,
        "control": 32
      },
      "bytes": 48040,
      "ceiling": 49152
    },
    "canonical": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "new_refusal": 184,
        "context_token": 80,
        "two_keys": 32,
        "old_caps_reason_work": 176,
        "control": 32,
        "factory_pool": 7680
      },
      "bytes": 30728,
      "ceiling": 49152
    },
    "pressure_SELF": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "new_refusal": 184,
        "context_token": 80,
        "two_keys": 32,
        "old_caps_reason_work": 176,
        "control": 32,
        "helper_pool": 6144
      },
      "bytes": 29192,
      "ceiling": 49152
    },
    "body_audit": {
      "components": {
        "two_oracles": 16800,
        "full_cell": 11032,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "body_utility": 8192,
        "scalar_error": 1024,
        "inactive_key_context_overcharge": 40
      },
      "bytes": 48600,
      "ceiling": 49152
    },
    "interval_audit": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "inactive_request_overcharge": 1368,
        "scalar_error": 1024,
        "interval_utility": 8192,
        "inactive_key_context_overcharge": 40
      },
      "bytes": 31800,
      "ceiling": 49152
    },
    "output": {
      "components": {
        "two_headers": 3200,
        "capacity1_cell": 11032
      },
      "bytes": 14232,
      "ceiling": 16777216
    },
    "construction_audit": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "inactive_request_overcharge": 1368,
        "scalar_error": 1024,
        "inactive_key_context_overcharge": 40,
        "construction_audit_utility": 2048
      },
      "bytes": 25656,
      "ceiling": 49152
    }
  },
  "source_pools": {
    "request_factory7680": {
      "selected": 7680,
      "named": {
        "eight_optional_requests": 5504,
        "current_request": 680,
        "six_point_constants": 576,
        "step_control": 96,
        "references_error_control": 512
      },
      "named_total": 7368,
      "unused_margin": 312
    },
    "source_tuple2048": {
      "selected": 2048,
      "named": {
        "three_region_arrays": 672,
        "two_part_records": 128,
        "tuple_predicate_descriptors": 256,
        "references_refusal_error_control": 512,
        "sorting_factory": 480
      },
      "named_total": 2048,
      "unused_margin": 0
    },
    "SELF6144": {
      "selected": 6144,
      "named": {
        "relative_points": 1296,
        "regions": 224,
        "four_solid_current_local_pending_returns": 768,
        "six_point_slots": 432,
        "24_intervals": 576,
        "two_square_scratch": 112,
        "eight_vec_slots": 192,
        "three_owner_returns": 168,
        "three_hip_math_three_half_ray": 336,
        "extra_argument_return_intervals": 192,
        "24_doubles": 192,
        "references_closures_cursors": 160,
        "library_error": 512
      },
      "named_total": 5160,
      "unused_margin": 984
    },
    "pressure6144": {
      "selected": 6144,
      "named": {
        "three_edge_returns": 240,
        "four_corner_return_slots": 512,
        "decoded_vec": 96,
        "three_bounds_vectors": 144,
        "three_body_point_conversions": 216,
        "16_bounds": 384,
        "six_edge_points": 192,
        "32_boot_intervals": 512,
        "16_doubles": 128,
        "references_closures_cursors": 256,
        "error_cleanup_reset_returns": 512
      },
      "named_total": 3192,
      "unused_margin": 2952
    },
    "construction2560": {
      "selected": 2560,
      "named": {
        "input_intervals": 192,
        "yaw_intervals": 144,
        "c_a_intervals": 96,
        "threshold_intervals": 48,
        "active_leg_intervals": 192,
        "selected_double_array": 48,
        "final_proof_intervals": 96,
        "primitive_argument_local_pending_expression_intervals": 192,
        "phase_constant_sign_arrays": 96,
        "certified_small_root_intervals_scratch_terms_scalars": 176,
        "product_endpoints_references_closures_cursors": 160,
        "library_environment_error": 512,
        "retained_same_chart_lateral_intervals": 48,
        "retained_full_ABS_lateral_intervals": 48,
        "certified_sqrt3_and_b_intervals": 48,
        "selected_two_roll_height_endpoints": 16,
        "current_pending_G21_expected_void_string_forecast": 80
      },
      "named_total": 2192,
      "unused_margin": 368
    },
    "construction_audit2048": {
      "selected": 2048,
      "named": {
        "sixteen_LD_bounds": 512,
        "two_LD_vectors": 192,
        "twelve_LD_scalars": 192,
        "sixteen_double_scalar_slots": 128,
        "references_closures_cursors": 96,
        "library_error": 512
      },
      "named_total": 1632,
      "unused_margin": 416
    }
  },
  "entire_preserved_old_enrollment28880_components": {
    "entire_old_enrollment": 24680,
    "new_two_headers": 3072,
    "new_four_limits": 800,
    "new_refusal": 184,
    "context": 40,
    "token": 40,
    "two_keys": 32,
    "new_control": 32
  },
  "old_inner24680_components": {
    "old_two_headers": 2880,
    "arena": 4096,
    "old_four_limits": 736,
    "caller": 2048,
    "current_request": 680,
    "eight_optional_returns": 5504,
    "six_point_constants": 576,
    "original_step_control": 96,
    "two_full_body_catalogs": 3840,
    "two_converted_phase_catalogs": 1920,
    "source_tuple_pool": 2048,
    "error": 256
  },
  "historical_reference": {
    "path": "build-native/issue495-validation/endpoint02-producer-prequery-resource-receipt-v2.json",
    "sha256": "90e6e5544d13f2d1e9ac11e3c70e7ab25517de9b6cdd2e1c6e219f7e345e1e32"
  },
  "baseline_reference": {
    "path": "build-native/issue499-validation/baseline-source-pins.json",
    "sha256": "bb33cd0615c6bf2b9ef26452770ffcf2ae2d995d2fe122c7e045decdfa2b36ef"
  },
  "notes": [
    "All maps are source forecasts, not actual resource admission.",
    "Entire historical28880 enrollment is additive; no helper subset replaces it.",
    "Old EMPTY Endpoint02 carrier1528 is covered by the old TWO1536 header reservation inside that entire28880, with old FOUR200 limits/old184 scratch intact.",
    "New source scratch184 forwards field-by-field by reference; no aggregate old refusal return or old successful report is consumed.",
    "Old EMPTY carrier dies after source adapter return, before NEW construction/Key/graph; combined source+construction is deliberately overcharged by the full2560 helper.",
    "Constructor scratch is not retained in graph or audits.",
    "Foot/body/owner/WORLD policies and old byte prefixes remain unchanged."
  ]
}
```

## Complete matched test source forecasts

Held payload SHA256: `6c3176e877561137ee4542288bca95e274741cbf7788b4a588084546b79ea3b3`.

```json
{
  "status": "SOURCE_ONLY_MATCHED_DRAFT_NOT_REGISTRATION_OR_ADMISSION",
  "issue": 499,
  "numerical_executions": 0,
  "compiler_calls": 0,
  "actual_new_layouts": "UNKNOWN",
  "actual_physical_chains": "UNKNOWN",
  "outcome": "UNKNOWN",
  "method": {
    "path": "build-native/issue499-validation/endpoint03-reach-roll-exact-typed-method-v1.md",
    "sha256": "9349774b07e6ccf2689b29ec0f21578b78463639a51066ba5b8e1f50bdfeb3ac"
  },
  "core_forecast": {
    "path": "build-native/issue499-validation/endpoint03-reach-roll-typed-source-forecast-v1.json",
    "sha256": "0dc42f8c884f4c31706e28d1a7486dd4d8accfe7654a626a90f970d93dbc18f3"
  },
  "proposed_roster": {
    "labels": 93,
    "FIRST": 1,
    "zero": 25,
    "raised": 25,
    "reached_one_less": 25,
    "exact": 1,
    "FP": 6,
    "invalid_candidate": 1,
    "lifetime": 4,
    "mixed": 3,
    "word_boundary": 2,
    "creator_max": 1,
    "RAW": 0,
    "inspectors": 0,
    "construction_audit_max": 1,
    "body_pose_max": 1,
    "pair_audit_max": 105,
    "owner_audit_max": 14,
    "hip_audit_max": 2
  },
  "fields": [
    {
      "index": 0,
      "field": "phase.graphs",
      "default": 1,
      "zero_label": 2,
      "raised_label": 27,
      "one_less_label": 52
    },
    {
      "index": 1,
      "field": "phase.legs",
      "default": 2,
      "zero_label": 3,
      "raised_label": 28,
      "one_less_label": 53
    },
    {
      "index": 2,
      "field": "phase.bodies",
      "default": 1,
      "zero_label": 4,
      "raised_label": 29,
      "one_less_label": 54
    },
    {
      "index": 3,
      "field": "phase.sectors",
      "default": 3,
      "zero_label": 5,
      "raised_label": 30,
      "one_less_label": 55
    },
    {
      "index": 4,
      "field": "phase.timing",
      "default": 6,
      "zero_label": 6,
      "raised_label": 31,
      "one_less_label": 56
    },
    {
      "index": 5,
      "field": "source_guards",
      "default": 64,
      "zero_label": 7,
      "raised_label": 32,
      "one_less_label": 57
    },
    {
      "index": 6,
      "field": "projection_guards",
      "default": 256,
      "zero_label": 8,
      "raised_label": 33,
      "one_less_label": 58
    },
    {
      "index": 7,
      "field": "definition_guards",
      "default": 31,
      "zero_label": 9,
      "raised_label": 34,
      "one_less_label": 59
    },
    {
      "index": 8,
      "field": "sole_extrema",
      "default": 4,
      "zero_label": 10,
      "raised_label": 35,
      "one_less_label": 60
    },
    {
      "index": 9,
      "field": "source_coordinates",
      "default": 8,
      "zero_label": 11,
      "raised_label": 36,
      "one_less_label": 61
    },
    {
      "index": 10,
      "field": "intersection_operations",
      "default": 4,
      "zero_label": 12,
      "raised_label": 37,
      "one_less_label": 62
    },
    {
      "index": 11,
      "field": "midpoint_operations",
      "default": 4,
      "zero_label": 13,
      "raised_label": 38,
      "one_less_label": 63
    },
    {
      "index": 12,
      "field": "allocation_operations",
      "default": 6,
      "zero_label": 14,
      "raised_label": 39,
      "one_less_label": 64
    },
    {
      "index": 13,
      "field": "pressure_candidates",
      "default": 2,
      "zero_label": 15,
      "raised_label": 40,
      "one_less_label": 65
    },
    {
      "index": 14,
      "field": "disk_edges",
      "default": 16,
      "zero_label": 16,
      "raised_label": 41,
      "one_less_label": 66
    },
    {
      "index": 15,
      "field": "self_body_guards",
      "default": 63,
      "zero_label": 17,
      "raised_label": 42,
      "one_less_label": 67
    },
    {
      "index": 16,
      "field": "self_pairs",
      "default": 105,
      "zero_label": 18,
      "raised_label": 43,
      "one_less_label": 68
    },
    {
      "index": 17,
      "field": "self_axes",
      "default": 1470,
      "zero_label": 19,
      "raised_label": 44,
      "one_less_label": 69
    },
    {
      "index": 18,
      "field": "self_signed_trials",
      "default": 2940,
      "zero_label": 20,
      "raised_label": 45,
      "one_less_label": 70
    },
    {
      "index": 19,
      "field": "self_owners",
      "default": 14,
      "zero_label": 21,
      "raised_label": 46,
      "one_less_label": 71
    },
    {
      "index": 20,
      "field": "self_hip_complements",
      "default": 2,
      "zero_label": 22,
      "raised_label": 47,
      "one_less_label": 72
    },
    {
      "index": 21,
      "field": "unit_axis_operations",
      "default": 24,
      "zero_label": 23,
      "raised_label": 48,
      "one_less_label": 73
    },
    {
      "index": 22,
      "field": "output_bytes",
      "default": 16777216,
      "zero_label": 24,
      "raised_label": 49,
      "one_less_label": 74
    },
    {
      "index": 23,
      "field": "construction_guards",
      "default": 21,
      "zero_label": 25,
      "raised_label": 50,
      "one_less_label": 75
    },
    {
      "index": 24,
      "field": "construction_operations",
      "default": 78,
      "zero_label": 26,
      "raised_label": 51,
      "one_less_label": 76
    }
  ],
  "checked_uint32_work_order": [
    "phase.graphs",
    "phase.legs",
    "phase.bodies",
    "phase.sectors",
    "phase.timing",
    "source_guards",
    "projection_guards",
    "definition_guards",
    "sole_extrema",
    "source_coordinates",
    "intersection_operations",
    "midpoint_operations",
    "allocation_operations",
    "pressure_candidates",
    "disk_edges",
    "self_body_guards",
    "self_pairs",
    "self_axes",
    "self_signed_trials",
    "self_owners",
    "self_hip_complements",
    "unit_axis_operations",
    "construction_guards",
    "construction_operations",
    "phase_calls",
    "fixed_preflight"
  ],
  "aggregate_work_ceilings": {
    "phase.graphs": 93,
    "phase.legs": 186,
    "phase.bodies": 93,
    "phase.sectors": 279,
    "phase.timing": 558,
    "source_guards": 5952,
    "projection_guards": 23808,
    "definition_guards": 2883,
    "sole_extrema": 372,
    "source_coordinates": 744,
    "intersection_operations": 372,
    "midpoint_operations": 372,
    "allocation_operations": 558,
    "pressure_candidates": 186,
    "disk_edges": 1488,
    "self_body_guards": 5859,
    "self_pairs": 9765,
    "self_axes": 136710,
    "self_signed_trials": 273420,
    "self_owners": 1302,
    "self_hip_complements": 186,
    "unit_axis_operations": 2232,
    "construction_guards": 1953,
    "construction_operations": 7254,
    "phase_calls": 93,
    "fixed_preflight": 93
  },
  "bounded_uint16_roster_fields": {
    "creators": 1,
    "consumers": 93,
    "skips": 93,
    "next_slot": 94,
    "construction_audits": 1,
    "body_poses": 1,
    "interval_records": 121
  },
  "payload_forecasts": {
    "Summary": 120,
    "Totals": 120,
    "mutable_globals": 132,
    "global_selected_reservation": 140,
    "StreamCounter": 88,
    "Environment": 16
  },
  "source_test_pools": {
    "creator1024": {
      "objects": {
        "Summary_reservation": 120,
        "two_StreamCounter": 176,
        "mutable_globals_reservation": 140,
        "native_boot_Expected": 80,
        "provider_optional": 24,
        "saved_stream_pointers_and_bytes": 24,
        "references": 32,
        "current_pending_errors": 160
      },
      "named": 756,
      "unused": 268,
      "selected": 1024
    },
    "graph2048": {
      "objects": {
        "Summary_reservation": 120,
        "two_StreamCounter": 176,
        "mutable_globals_reservation": 140,
        "native_boot_Expected": 80,
        "provider_optional": 24,
        "four_alias_slots": 64,
        "saved_stream_pointers_and_bytes": 24,
        "references_arguments": 96,
        "slot_field_candidate_cursor_status": 112,
        "saved_environment_slots": 64,
        "current_pending_errors_check_arguments": 256,
        "post_return_work_array": 192,
        "Hash": 8,
        "two_word_prefix_current_pending_arrays": 32
      },
      "named": 1388,
      "unused": 660,
      "selected": 2048
    },
    "body8192": {
      "objects": {
        "outer_Jets_and_root_points": 768,
        "side_loop_points_Jets_scalars": 2144,
        "twelve_Point_slots": 1728,
        "twelve_Jet_slots": 576,
        "four_Matrix_slots": 1728,
        "loop_indices_control": 96,
        "ExceptionState": 16,
        "norm_root_scalar_returns": 96,
        "angle_trig_reciprocal_returns": 96,
        "check_arguments_labels_references": 128,
        "library_error_leaf": 512,
        "current_pending_y_arguments": 16
      },
      "named": 7904,
      "unused": 288,
      "selected": 8192
    },
    "interval8192": {
      "objects": {
        "relative_54_original_supported_intervals": 1296,
        "LD_points_operands_local_pending": 1728,
        "LD_frames_operands_local_pending": 1152,
        "two_solid_description_allowance": 1024,
        "pending_proof_returns": 512,
        "scalar_owner_hip_error_helpers_including_library512": 2480
      },
      "named": 8192,
      "unused": 0,
      "selected": 8192
    },
    "construction_audit2048": {
      "objects": {
        "sixteen_LD_bounds": 512,
        "two_LD_vectors": 192,
        "twelve_LD_scalars": 192,
        "sixteen_double_scalar_slots": 128,
        "references_closures_cursors": 96,
        "library_error": 512
      },
      "named": 1632,
      "unused": 416,
      "selected": 2048
    }
  },
  "source_stage_maps": {
    "creator": {
      "components": {
        "arena": 4096,
        "current_pending_constructor_evidence": 1040,
        "port": 376,
        "two_creator_expected": 80,
        "constructor_limits": 96,
        "native_boot_aliases": 32,
        "selected_caller": 1024,
        "initialization_error": 256
      },
      "bytes": 7000,
      "ceiling": 8192
    },
    "preflight": {
      "components": {
        "two_expected": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "library_error": 512
      },
      "bytes": 10656,
      "ceiling": 49152
    },
    "required_room_refusal": {
      "components": {
        "two_expected": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "library_error": 512,
        "current_request": 680,
        "current_refusal": 184
      },
      "bytes": 11520,
      "ceiling": 49152
    },
    "enrollment": {
      "components": {
        "entire_old_enrollment": 28880,
        "new_two_headers": 3200,
        "new_four_limits": 800,
        "new_refusal": 184,
        "context": 40,
        "token": 40,
        "two_keys": 32,
        "new_control": 32
      },
      "bytes": 33208,
      "ceiling": 49152
    },
    "construction": {
      "components": {
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "new_refusal": 184,
        "two_keys": 32,
        "context": 40,
        "old_caps_reason_work": 176,
        "construction_helper": 2560,
        "independent_error": 512
      },
      "bytes": 15016,
      "ceiling": 49152
    },
    "enrollment_plus_disjoint_construction_overcharge": {
      "components": {
        "entire_old_enrollment": 28880,
        "new_two_headers": 3200,
        "new_four_limits": 800,
        "new_refusal": 184,
        "context": 40,
        "token": 40,
        "two_keys": 32,
        "new_control": 32,
        "disjoint_construction_pool": 2560
      },
      "bytes": 35768,
      "ceiling": 49152
    },
    "reset": {
      "components": {
        "two_full_cells": 22064,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "refusal": 184,
        "context": 40,
        "two_keys": 32,
        "old_caps_reason_work": 176,
        "two_axis_initializer_arrays": 210,
        "control": 32
      },
      "bytes": 34250,
      "ceiling": 49152
    },
    "graph": {
      "components": {
        "entire_original_proof": 32768,
        "two_headers": 3200,
        "arena": 4096,
        "cell_delta": 3296,
        "four_limits": 800,
        "caller": 2048,
        "old_limits": 72,
        "old_reason": 64,
        "pending_optional_request": 688,
        "current_request": 680,
        "new_refusal": 184,
        "context": 40,
        "token": 40,
        "two_keys": 32,
        "control": 32
      },
      "bytes": 48040,
      "ceiling": 49152
    },
    "canonical": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "new_refusal": 184,
        "context_token": 80,
        "two_keys": 32,
        "old_caps_reason_work": 176,
        "control": 32,
        "factory_pool": 7680
      },
      "bytes": 30728,
      "ceiling": 49152
    },
    "pressure_SELF": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "new_refusal": 184,
        "context_token": 80,
        "two_keys": 32,
        "old_caps_reason_work": 176,
        "control": 32,
        "helper_pool": 6144
      },
      "bytes": 29192,
      "ceiling": 49152
    },
    "body_audit": {
      "components": {
        "two_oracles": 16800,
        "full_cell": 11032,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "body_utility": 8192,
        "scalar_error": 1024,
        "inactive_key_context_overcharge": 40
      },
      "bytes": 48600,
      "ceiling": 49152
    },
    "interval_audit": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "inactive_request_overcharge": 1368,
        "scalar_error": 1024,
        "interval_utility": 8192,
        "inactive_key_context_overcharge": 40
      },
      "bytes": 31800,
      "ceiling": 49152
    },
    "output": {
      "components": {
        "two_headers": 3200,
        "capacity1_cell": 11032
      },
      "bytes": 14232,
      "ceiling": 16777216
    },
    "construction_audit": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3200,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "inactive_request_overcharge": 1368,
        "scalar_error": 1024,
        "inactive_key_context_overcharge": 40,
        "construction_audit_utility": 2048
      },
      "bytes": 25656,
      "ceiling": 49152
    }
  },
  "producer_source_pools": {
    "request_factory7680": {
      "selected": 7680,
      "named": {
        "eight_optional_requests": 5504,
        "current_request": 680,
        "six_point_constants": 576,
        "step_control": 96,
        "references_error_control": 512
      },
      "named_total": 7368,
      "unused_margin": 312
    },
    "source_tuple2048": {
      "selected": 2048,
      "named": {
        "three_region_arrays": 672,
        "two_part_records": 128,
        "tuple_predicate_descriptors": 256,
        "references_refusal_error_control": 512,
        "sorting_factory": 480
      },
      "named_total": 2048,
      "unused_margin": 0
    },
    "SELF6144": {
      "selected": 6144,
      "named": {
        "relative_points": 1296,
        "regions": 224,
        "four_solid_current_local_pending_returns": 768,
        "six_point_slots": 432,
        "24_intervals": 576,
        "two_square_scratch": 112,
        "eight_vec_slots": 192,
        "three_owner_returns": 168,
        "three_hip_math_three_half_ray": 336,
        "extra_argument_return_intervals": 192,
        "24_doubles": 192,
        "references_closures_cursors": 160,
        "library_error": 512
      },
      "named_total": 5160,
      "unused_margin": 984
    },
    "pressure6144": {
      "selected": 6144,
      "named": {
        "three_edge_returns": 240,
        "four_corner_return_slots": 512,
        "decoded_vec": 96,
        "three_bounds_vectors": 144,
        "three_body_point_conversions": 216,
        "16_bounds": 384,
        "six_edge_points": 192,
        "32_boot_intervals": 512,
        "16_doubles": 128,
        "references_closures_cursors": 256,
        "error_cleanup_reset_returns": 512
      },
      "named_total": 3192,
      "unused_margin": 2952
    },
    "construction2560": {
      "selected": 2560,
      "named": {
        "input_intervals": 192,
        "yaw_intervals": 144,
        "c_a_intervals": 96,
        "threshold_intervals": 48,
        "active_leg_intervals": 192,
        "selected_double_array": 48,
        "final_proof_intervals": 96,
        "primitive_argument_local_pending_expression_intervals": 192,
        "phase_constant_sign_arrays": 96,
        "certified_small_root_intervals_scratch_terms_scalars": 176,
        "product_endpoints_references_closures_cursors": 160,
        "library_environment_error": 512,
        "retained_same_chart_lateral_intervals": 48,
        "retained_full_ABS_lateral_intervals": 48,
        "certified_sqrt3_and_b_intervals": 48,
        "selected_two_roll_height_endpoints": 16,
        "current_pending_G21_expected_void_string_forecast": 80
      },
      "named_total": 2192,
      "unused_margin": 368
    },
    "construction_audit2048": {
      "selected": 2048,
      "named": {
        "sixteen_LD_bounds": 512,
        "two_LD_vectors": 192,
        "twelve_LD_scalars": 192,
        "sixteen_double_scalar_slots": 128,
        "references_closures_cursors": 96,
        "library_error": 512
      },
      "named_total": 1632,
      "unused_margin": 416
    }
  },
  "physical_stage_measurement_required": [
    "creator_prearena",
    "creator_with_arena",
    "preflight_required_room",
    "source_enrollment",
    "construction",
    "reset",
    "original_graph_and_FIRST",
    "canonical",
    "pressure_SELF",
    "B44_region",
    "construction_audit",
    "body_audit",
    "interval_audit",
    "direct_post_checks_print_hash",
    "teardown"
  ],
  "remaining_gates": [
    "Root roster selection",
    "independent exact paired review",
    "public registration commit and push",
    "implementation headers/source HOLD",
    "Root pinned format",
    "Root GCC Clang PIC native compile-only/layout SU disassembly",
    "complete actual source and physical resource admission",
    "Root immutable source binary resource freeze",
    "Root exclusive once-FIRST each compiler",
    "retained outcomes before any observed regressions",
    "normal dual compiler publication suites"
  ],
  "manifest": {
    "path": "build-native/issue499-validation/endpoint03-reach-roll-test-manifest-resource-draft-v1.md",
    "sha256": "902a74b384a3603b62c4bd8c7530b17559bded02430a02e5586719a7265229af"
  }
}
```


## Retained first observation — 2026-10-07

The one registered Endpoint03 program completed its source-bound reach-and-roll construction and independent nominal construction audit. Its fresh call to the unchanged original compiler remained unresolved at the PORT joint-sector check, before body construction. This establishes a completed necessary construction, with endpoint acceptance still unearned.

The original source commit is `cdf1db31c89b99fae0c51040f034f577df21e796`. The independently reviewed final freeze is `58b12b5312ef8aaea71dd058b33479ea9a2f76d0eff739f7a8d2bbc13a36da46`, covering 1,187 source files, two immutable binaries, 77 receipts and 954 artifacts. It retains the complete original graph and source-enrollment reservations and the reviewed historical input aliases. GCC and Clang each ran once, in that order, after all resource, normal-link and freeze gates passed.

Both original processes exited zero and produced identical 2,510-byte logs with SHA256 `961288368752ba9d3456fb39db6131ae85304a2811987e2ecae18fb1b3f988d2`. The logs, original test source, binaries, freeze, marker and source recovery bundle remain retained. Subsequent ordinary regression runs do not replace this observation.

| Earned surface | Original observation |
|---|---|
| Genuine source | Admitted and complete; arena 4,096 bytes; creator work `21,1,2,2` |
| Construction | All 78 operations and 21 guards completed and supported |
| Operation masks | Both attempted and written words are `18446744073709551615,16383` |
| Guard masks and final cursors | Attempted/written `2097151`; cursors `77,20,255` |
| Stored midpoint | `0.5288999213015908`, strictly inside `[0.49306122159857413,0.56473862100460748]` |
| Fresh program continuation | Requested clock 2 seconds; one original phase call; one retained cell/capacity 1; owned output 14,048 bytes |
| Original phase work | Graphs 1, legs 1, bodies 0, sectors 1, timing 0 |
| Original refusal | Joint-sector condition ordinal 6, PORT side 0; predicate ordinal 0 remains an unattributed default |
| Endpoint state | Unresolved, incomplete; definition mask 15 |
| Independent construction audit | One audit, using the supported complete slice |
| Body and finite support | Body, contact, unit, owner, pair and SELF continuations not reached |

The original nested refusal has no generic support flag for its limiting doubles. Their output remains `nested_bound=NOT_INTERPRETED`; the default predicate value does not identify a particular sector margin. This observation supplies neither a supported negative margin nor a collision or all-posture impossibility result. A source-only reached-sector attribution contract is the next diagnostic prerequisite for this same program.

The fixed 93-slot roster used one genuine creator, 56 consumers and 37 documented skips; RAW and inspector counts remained zero. The construction audit count was one, with zero body poses and zero interval-record audits. Both compilers reported 3,772 checks and zero failures. Skips followed the registered reached-work conditions; no substitute query or alternate candidate was used.

The original aggregate work record, in the registered 26-field order, is:

```text
8,7,0,6,0,1216,0,35,0,0,0,0,0,0,0,0,0,0,0,0,0,0,279,1064,9,56
```

Independent retained-outcome review: `9eb97ee4a569aa1c8c217e6b5766b65ca5de80e6874a75a8e9c5509bcf836c4c`. WORLD, the boarding route, loaded seat-pan support, actor movement and First Flight remain subsequent obligations; parent issues #462, #361 and #352 remain open.

Publication validation completed locally after the reviewed six-check Test-only regression addition. Both full Release builds passed all 95 CTest contracts. Ten normal objects matched their strict measured objects byte-for-byte under the registered FP flags. Both ordinary Endpoint03 runs reported 3,778 checks and zero failures; all retained printed source/A01 rows and the aggregate were unchanged. The original once-only observation remains the 3,772-check record above.

Pinned format20 and full lint20 passed, including 213 unique files from 216 selected compilation entries and the suppression-policy self-tests. All five fresh Godot 4.7.2 compatibility contracts passed: native walking, native save, saved flight, native start staging and Wayfarer operation. These isolated headless contracts do not qualify visible/hardware/audio play or the unresolved boarding route.

The post-FIRST Test resource delta was independently reviewed: all eight duplicate-preserving stack-row multisets and measured layouts remained equal to the originals; all 17 complete physical stage maps retained their bounds, with peak 46,796 bytes against 49,152. The full original graph/source reservations and no-elision returns remain charged. The public registration prefix, old source prefixes, historical first observations and the complete Endpoint03 first-run freeze remain unchanged. Local validation receipt: `cdd73cb5eabf29dc1def81e7d8b82f15c91c599743aa105b5ae953d22ed11a9c`. Final independent publication review and the eight exact-head CI jobs remain required before merge.
