# Intermediate endpoint04: exact BOTH-ankle, reach and roll program

This is the documentation-only arithmetic registration draft for [issue #505](https://github.com/gobha-me/apsis-drift/issues/505), following the reviewed [BOTH-ankle construction proof](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md). One source-bound version4 program projects a conservative connected positive-knee component onto rootY, stores one midpoint and directly checks BOTH nominal ankle/reach/roll predicates. Every original joint, link, body, mass, owner, source and WORLD policy remains unchanged. F1>0 is this constructor's conservative subbranch, not a replacement of the original compiler's broader policy.

No coordinate, derived geometric bound, angle, candidate, API, FP experiment, compiler, fixture, oracle or runtime has been evaluated. Source-specific support/nonemptiness, actual layouts, complete source/physical resources and outcomes remain UNKNOWN. This draft grants no implementation or FIRST. Exact paired method/manifest review and public registration precede coding; held source plus both-compiler compile-only resource admission precede a frozen once-only FIRST. Issue505 must complete arithmetic registration in addition to the proof; later implementation/observation is a separate prerequisite.

## 1. Immutable input and exact-real meaning

The only public input is the genuine OriginBoardingIntermediatePauseSupport provider and one BoardingIntermediateEndpoint04Candidate:uint8_t {root_y_both_ankle_reach_roll_slice=0}. Public function: assess_origin_boarding_intermediate_endpoint04(const OriginBoardingIntermediatePauseSupport&,Candidate)->Expected. Expected is std::expected<BoardingIntermediateEndpoint04Diagnostic,std::string>. Private detail::intermediate_endpoint04_bounded(provider,Candidate,Limits={}) returns the same Expected. No caller Y, auxiliary cut, numerical bounds, interval/reverse control, old report, key/token, raw/classifier seam, inspector or alternative candidate is admitted.

Keep original ordered rootX/Z packets; both held I/star feet and authenticated planes; rational root-yaw half-angle .125, flat sole-yaw half-angle0, torso-half-angle−.30; zero humps, folded arms, original L1/L2, PORT1/16 and STAR15/16 share; exact two-second whole forward[0,1] hold. Only BOTH rootY packets become {Y,+0,+0}count1 after direct verification. Y is the one stored binary64 midpoint lifted to an exact-real coefficient, not an exact radical or reporting angle. There is no old constructor followed by a correction, fitted branch, grid, fallback cut or second midpoint.

Source-authenticated proper rational yaw gives hip=root+Rroot*(sigma*.14,0,0) and ankle=sole+(0,.1,0), sigmaPORT=−1/STAR=+1. Held sole yaw0 means its exact-real frame is WORLD identity. Use signed x,z, a=soleY+.1, w=−z, h=Y−a>0, rho²=x²+h² and D=rho²+z². Proper exact-real rotation preserves Y/norms; rounded interval columns are never assumed orthonormal. These are the original [phase_leg chart and positive branch](../src/origin_boarding_planted_legs.cpp#L3430), not the static body reporting frame.

For auxiliary tau, F2=L2*sqrt(1−tau²), G1=w−L2*tau, F1=sqrt(L1²−G1²), and r(tau)=F1+F2. Universal closed cuts p/q stay inside the proved endpoint value-domain, p<q and q<=w/(L1+L2). Interior tau is strictly inside the positive-knee component, yielding the same original positive gamma and regular shin cone. The conservative radial interval is (rho_p.upper,rho_q.lower), not endpoint derivative admission. The proof's signed origins and inverse shin/sole rotation order are preserved. Direct supported checks at actual Y are mandatory; symbolic inclusion never overrides an interval refusal.

## 2. Exact schema and private issuance

Fresh public names use BoardingIntermediateEndpoint04 for Diagnostic, Cell, Refusal, Counters, SliceEvidence, Expected, State, Condition, SelfStage, Certificate and SliceCondition; version4. All enums below have uint8_t underlying type.

State ordinals: not_run0,accepted1,unresolved2,witness_refused3,capacity4,unsupported5. Condition ordinals: none0,invalid_candidate1,invalid_limits2,invalid_binding3,output_capacity4,source_capacity5,source_identity6,phase_prerequisite7,projection_capacity8,projection_identity9,definition_capacity10,sole_plane11,source_rectangle12,empty_intersection13,denominator14,sole_extrema_capacity15,source_coordinate_capacity16,intersection_capacity17,midpoint_capacity18,allocation_capacity19,pressure_capacity20,edge_capacity21,symbolic_equilibrium22,sole_disk23,source_disk24,self_body_capacity25,self_body_identity26,unit_axis_capacity27,unit_axis_identity28,self_pair_capacity29,self_axis_capacity30,self_signed_capacity31,self_owner_capacity32,self_hip_capacity33,self_region_identity34,unresolved_self_pair35,unsupported_arithmetic36,incomplete_endpoint37,slice_unavailable38,slice_identity39,slice_guard_capacity40,slice_operation_capacity41.

SelfStage: not_run0,source1,phase2,projection3,definition4,sole_extrema5,source_coordinates6,intersection7,midpoint8,allocation9,pressure_candidate10,disk11,body12,unit_axes13,pair14,owner15,hip16,separation17,complete18,slice_guard19,slice_operation20. Certificate: not_run0,convex_support_plane1,cap_partner_support2,original_axis_box_support3,half_ray_angle_bound4,shoulder_split5,original_capsule_slab_complement6. SliceCondition: none0,identity1,no_positive_upper2,empty_inward_interval3,candidate_domain4,midpoint_unavailable5,verification_inconclusive6,guard_capacity7,operation_capacity8,unsupported_arithmetic9,no_positive_roll_factor10,no_tau_interval11,endpoint_domain_unavailable12,no_radial_interval13.

Diagnostic, Cell, Refusal and Counters use the exact field order/defaults of the immutable [Endpoint03 public header](../include/apsis_drift/origin_boarding_intermediate_endpoint03.hpp): replace every Endpoint03-specific type/name with its fresh Endpoint04 counterpart, version3 with4 and Candidate with the new enumerator. Original PhaseCell/parts/sites/owners/hip mathematical value types remain unchanged. Refusal is the FULL184-byte target: both Condition fields, bounds, original PhaseRefusal, optional side/edge/axis/source_key, source_name, self_pair65535, operation/self_region/self_axis/self_sign255, SelfStage and source_edge. No compact truncation or source reason alias. Counters retain phase_calls/original five phase counters and all19 outer uint64 fields, target200. Work/Slice growth is inside EACH owning header, never again counted as a separate persistent copy.

Only SliceEvidence changes its exact field order:

    std::array<std::uint64_t,4> operation_attempted{}, operation_written{};
    std::uint64_t guard_attempted{}, guard_written{};
    BoardingFootSiteScalarBounds limiting_bound;
    double y{}, lo{}, hi{};
    std::uint8_t operation{255}, guard{255}, side{255};
    BoardingIntermediateEndpoint04SliceCondition condition{};
    bool arithmetic_supported{}, complete{};
    std::uint8_t preflight_guards{}, zero_mask{};

No persistent p/q/radial/cut/direct scratch, chart array, extra Slice in Cell, stored Request or audit Oracle. Scalar availability uses literal written bits. Staticfalse world/material/halo/route/seat/actor/save/dynamics/strength/friction/first_flight qualifications retain old names/defaults; runtime kinematic/support/SELF/complete fields are independently earned.

Selected layout ceilings, NOT measurements: Expected1664, Cell11032, Limits200, Refusal184, Slice144, Work200, Key16, Context40, Token40, original validatorExpected40; Test Summary120/Totals120/global140. Creator8192 and numerical-worker live49152 bytes (48KiB) bound registered working storage; output16777216 and shared stdout+stderr16384 remain unchanged. These are not total native-game or whole-world-startup memory limits. Actual sizes must corroborate all ceilings or stop admission; no margin is silently borrowed.

New private BoardingIntermediateEndpoint04ProgramKey(double y,Candidate) constructor is friend ONLY new bounded; fields Y/version4/Candidate0, read-only getters. AdmissionContext privately constructed ONLY new bounded from (const Diagnostic&,const BoardingRouteFootPhaseRequest&,const ProgramKey&); five pointer anchors owner/Data/parts/owned Request/Key; deleted copy/move/all assignments; read-only getters. CurrentToken privately constructed ONLY new current_cell after ONE accepted original graph and complete P251, from (const Context&,const Diagnostic&,const Cell&,const Request&); anchors context/owner/cell/Request/Data; synchronous copies only, never returned as capability. Continuations validate actual anchors before dereference. No old issuer/type/version/friendship changes.

Private concrete helper suffixes under detail::intermediate_endpoint04_ (D/C/R/L/X/T/Q are fresh Diagnostic/Cell/Refusal/Limits/Context/Token and original Request) are: source_enroll(D&,Q&,const L&,R&)->bool; construct(D&,Q&,const L&,R&)->bool; canonical(const ProgramKey&)->optional<Q>; template_valid(const Q&,bool generated,double y)->bool; current_cell(const X&,D&,C&,const L&,R&)->State; pressure_bridge(const T&,D&,C&,const L&,R&)->State; self_bridge(const T&,D&,C&,const L&,R&)->State; refuse(D&,R&,Condition)->bool; charge(D&,R&,uint64_t&,size_t,Condition)->bool; definition_charge(D&,C&,const L&,R&)->bool. No helper accepts caller geometry reports or returns a construction aggregate. Arithmetic value primitives stay original definitions; capability types are never aliases.

## 3. FP-FIRST, output and source lifetime

One fixed counted preflight guard per actual API call, including unexpected returns. Structured Slice.preflight_guards=1; external finite Test totals count1 even without a report. No lowerable FP field. Literal order: original exported FP helper; Candidate0; raised Limits in field order; TWO Expected headers budget. Exact unexpected strings: "intermediate endpoint04 unsupported floating point", "intermediate endpoint04 invalid candidate", "intermediate endpoint04 invalid limits", "intermediate endpoint04 output headers". FP-first wins unsafe mixed inputs. Unsafe path makes no Diagnostic/source/Request/vector allocation beyond required bounded string-error representation. String objects are already in Expected alternatives; a single transferred error buffer<=57 bytes inclNUL is named in error reserves, not two copied heaps.

After safe preflight materialize current Expected, current Request and distinct full R; REQUIRED one-slot room is checked BEFORE S64. Public pure constexpr boarding_intermediate_endpoint04_required_output_bytes() returns 2*sizeof(Expected)+sizeof(Cell), after complete types. Diagnostic.output_capacity_bytes is actual 2*sizeof(Expected)+actual_vector_capacity*sizeof(Cell). Structured room/source/construction refusals can own only headers. Exact replay uses REQUIRED input room, never headers-only owned bytes. Allocate the one slot only after successful constructor/Key/D01–03. Catch allocation failure as output_capacity; release abnormal capacity before headers-only reporting. No slot is claimed before actual reserve.

New END-only source adapter owns lexical EMPTY Endpoint03 Diagnostic, old Limits/Refusal and unchanged source-enrollment factory/catalog wrappers. Call ONLY unchanged intermediate_endpoint03_source_enroll once, not its constructor/bounded/public endpoint or old private canonical key. Preserve ENTIRE Endpoint03 enrollment33208, nested Endpoint02 enrollment28880 and inner24680. Actual helper success AND old.source_enrolled are required; count64/fullmask alone is not source_enrolled. Copy all15 bindings, source work/mask and lossless full refusal fields BY REFERENCE into fresh owners. S16/S18/version/seedY authenticate original seed; new G03/G36/G37 authenticate version4/generatedY. Current fresh Request is the seed later updated by this constructor.

The old EMPTY carrier, ALL old L/R/factory/catalog/current-pending wrappers and source forwarding scopes die at source-adapter return BEFORE new construction. The constructor uses no old report, Key or constructor. Constructor chart/cut/endpoint certificate/direct scratch and validator returns die at its return BEFORE new Key/Context/reset/graph/body/any audit. Persistent Slice/parts/work remain only inside headers; fresh generated Request and actual refusal scratch remain exterior. Separate source maps conservatively coadd full historical source and full4096 constructor pool; they do not keep source scratch live in graph.

Actual complete226/37/source/generation/final-environment success permits bounded to privately issue Key, THEN reporting_elapsed_seconds=2 and Context. Clock remains0 earlier. Later clock2 is only the requested-hold cue, not original timing/route promotion.

## 4. Primitive, branch and charged publication protocol

Use unchanged [scalar primitives](../src/origin_boarding_planted_legs.cpp#L58), original support/finite/order rules and [transfer_small_root/self_root](../src/origin_boarding_planted_legs.cpp#L2351). DIV is outward reciprocal interval then original MUL, never RN(1/L) singleton normalization. CONST uses original ordered packet term adds/compound exact sum-domain validation. SMALL_ROOT owns original endpoint sqrt proposals and exact squared-product residual corrections, maximum four attempts per endpoint/eight corrections, supported domain[0,16384], including charged exact-zero shortcut. No alternate root oracle, trig, tolerance, generic solver or unbounded repair. Root value-domain includes exact0 at closed endpoints; later original strict derivative domains remain unchanged.

POINT_UPPER/LOWER are charged supported finite singleton publication. MAX/MIN are finite scalar selections; first operand wins equality and preserves its bits throughout the table. MAX_ZERO uses supported upper<=0 to write canonical+0, else retains upper bits. INTERSECT_NONNEG is legal ONLY after same-side universal endpoint certificate and supported upper>=0: lower<=0 becomes canonical+0, else lower bits survive; upper always unchanged. This intersects an enclosure of a proved nonnegative exact quantity, never clips unsupported data or guesses a negative exact quantity positive. That exact interval result feeds SMALL_ROOT.

For each operation set UPCOMING stage20/cursor/side and clear only row-local Slice.limiting_bound BEFORE capacity charge; preserve earlier y/lo/hi/zero bits. Successful charge increments count/attempted, THEN reads tuple/helper operands through deferred lambda. A supported finite ordered write earns written and current limiting.supported; invalid result is hard unsupported/attempted-unwritten. Capacity stops before field reads/count/attempted. No precomputed expression or by-value uncharged tuple crosses charge. Midpoint RN SUB/MUL/ADD are separate finite-checked rows under earned FP environment.

Guards set UPCOMING stage19/cursor/side, clear only current scratch, then charge/read. Successful charge sets attempted; readiness true writes, finite false readiness leaves written clear. CHECK-readiness is not false-boolean publication. Compound rows stop at first failed predicate and attach only its actually supported scalar bound; identity/FP checks invent no bound. Arithmetic/support/nonfinite/root/validator inability is hard unsupported; finite sufficient-check failure is ordinary slice_unavailable with literal SliceCondition; structural failure is slice_identity/identity. First ordinary full184 refusal persists; actual later terminal capacity/unsupported remains separate and is never downgraded. Default zeros are unsupported, not evidence.

Cursors operation0..225/guard0..36/side0PORT,1STAR,255common; sentinels255. Labels one-based; bit=row−1, operation word/bit quotient/remainder64. Complete words {UINT64_MAX,UINT64_MAX,UINT64_MAX,0x00000003ffffffff}; guard uint64 mask0x0000001fffffffff, upper27bits0. No shift64 or unused-bit writes. Complete cursors remain225/36/255, not reset255. G37 leaves current limiting scratch defaultunsupported. Slice.lo only O154 written, hi O157, y O160; scalar consumers/hash require those bits.

zero_mask: bits0/1 reachMAX_ZERO PORT/STAR O89/O141; bits2/3 PORT p/q thigh clips O63/O74; bits4/5 STAR p/q clips O115/O126; bits6/7 ankleMAX_ZERO PORT/STAR O81/O133. Bit1 means actual canonical-zero lower branch, bit0 means retained positive operand; meaningful ONLY if that operation written. <=0 canonicalizes even signed−0, nonzero retained bits unchanged. No extra branch evaluation or fabricated default choice.

## 5. Literal operation table O001–O226

Lowerable construction_operations ceiling226. Common side255; PORT0/STAR1. Active destinations reuse only after last consumer. Full BOTH X/Z/X²/Z²/C/A/W/ABS, link thresholds, cut certificates and radial endpoints have distinct retained storage through final consumers. Each side endpoint p/q uses WHOLE signed W and singleton stored tau. Interleave guards from section6 at their exact placements.

|Row|Side|Charged expression/publication|
|---|---|---|
|O001|common|CONST rootX|
|O002|common|CONST rootZ|
|O003|common|CONST PORT soleX|
|O004|common|CONST PORT soleY|
|O005|common|CONST PORT soleZ|
|O006|common|CONST STAR soleX|
|O007|common|CONST STAR soleY|
|O008|common|CONST STAR soleZ|
|O009|common|SQUARE point(rootYawHalf)|
|O010|common|ADD point1+O09|
|O011|common|SUB point1−O09|
|O012|common|DIV O11/O10 →cos|
|O013|common|MUL point2*point(rootYawHalf)|
|O014|common|DIV O13/O10 →sin|
|O015|common|NEG O14 →negative_sin|
|O016|PORT|MUL point(−.14)*cos →offsetX|
|O017|PORT|MUL point(−.14)*negative_sin →offsetZ|
|O018|PORT|ADD rootX+offsetX →hipX|
|O019|PORT|ADD rootZ+offsetZ →hipZ|
|O020|PORT|SUB soleX−hipX →retained X_PORT|
|O021|PORT|SUB soleZ−hipZ →retained Z_PORT|
|O022|PORT|SQUARE X_PORT →retained X2_PORT|
|O023|PORT|SQUARE Z_PORT →retained Z2_PORT|
|O024|PORT|ADD X2_PORT+Z2_PORT →retained C_PORT|
|O025|PORT|ADD soleY+point.1 →retained A_PORT|
|O026|STAR|MUL point(+.14)*cos →offsetX|
|O027|STAR|MUL point(+.14)*negative_sin →offsetZ|
|O028|STAR|ADD rootX+offsetX →hipX|
|O029|STAR|ADD rootZ+offsetZ →hipZ|
|O030|STAR|SUB soleX−hipX →retained X_STAR|
|O031|STAR|SUB soleZ−hipZ →retained Z_STAR|
|O032|STAR|SQUARE X_STAR →retained X2_STAR|
|O033|STAR|SQUARE Z_STAR →retained Z2_STAR|
|O034|STAR|ADD X2_STAR+Z2_STAR →retained C_STAR|
|O035|STAR|ADD soleY+point.1 →retained A_STAR|
|O036|common|ADD pointL1+pointL2 →retained Lsum|
|O037|common|SQUARE Lsum →M|
|O038|common|SUB pointL1−pointL2 →Ldiff|
|O039|common|SQUARE Ldiff →m|
|O040|common|SQUARE pointL1 →L1sq|
|O041|common|SQUARE pointL2 →L2sq|
|O042|common|SUB L1sq−L2sq →Delta|
|O043|common|SMALL_ROOT point3 →sqrt3|
|O044|common|DIV sqrt3/point2 →sqrt3half|
|O045|common|SUB point2−sqrt3 →b|
|O046|PORT|NEG Z_PORT →W|
|O047|PORT|SUB W−pointL1 →cutlowNum|
|O048|PORT|DIV cutlowNum/pointL2 →cutlow|
|O049|PORT|ADD W+pointL1 →cuthiNum|
|O050|PORT|DIV cuthiNum/pointL2 →cuthi|
|O051|PORT|DIV W/Lsum →cutturn|
|O052|PORT|MAX(point−.5,point(cutlow.high)) →p (first on equality)|
|O053|PORT|MIN(point+.5,point(cuthi.low)) →q0 (first on equality)|
|O054|PORT|MIN(q0,point(cutturn.low)) →q (first on equality)|
|O055|PORT|SQUARE point(p) →tau2|
|O056|PORT|SUB point1−tau2 →cosRad|
|O057|PORT|SMALL_ROOT WHOLE cosRad →cosShin|
|O058|PORT|MUL pointL2*cosShin →F2endpoint|
|O059|PORT|MUL pointL2*point(p) →tauL2|
|O060|PORT|SUB W−tauL2 →G1endpoint|
|O061|PORT|SQUARE G1endpoint →g1sq|
|O062|PORT|SUB L1sq−g1sq →thighRad|
|O063|PORT|INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 2|
|O064|PORT|SMALL_ROOT WHOLE clip →F1endpoint|
|O065|PORT|ADD F1endpoint+F2endpoint →rho_p|
|O066|PORT|SQUARE point(q) →tau2|
|O067|PORT|SUB point1−tau2 →cosRad|
|O068|PORT|SMALL_ROOT WHOLE cosRad →cosShin|
|O069|PORT|MUL pointL2*cosShin →F2endpoint|
|O070|PORT|MUL pointL2*point(q) →tauL2|
|O071|PORT|SUB W−tauL2 →G1endpoint|
|O072|PORT|SQUARE G1endpoint →g1sq|
|O073|PORT|SUB L1sq−g1sq →thighRad|
|O074|PORT|INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 3|
|O075|PORT|SMALL_ROOT WHOLE clip →F1endpoint|
|O076|PORT|ADD F1endpoint+F2endpoint →rho_q|
|O077|PORT|POINT_UPPER rho_p →u|
|O078|PORT|POINT_LOWER rho_q →v|
|O079|PORT|SQUARE pointu →u2|
|O080|PORT|SUB u2−point(X2_PORT.low) →ankleLowerRad|
|O081|PORT|MAX_ZERO ankleLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 6|
|O082|PORT|SMALL_ROOT point(selected) →ankleLowerRoot|
|O083|PORT|ADD point(A_PORT.high)+point(ankleLowerRoot.high) →ankleLower (retain.high)|
|O084|PORT|SQUARE pointv →v2|
|O085|PORT|SUB v2−point(X2_PORT.high) →ankleUpperRad|
|O086|PORT|SMALL_ROOT point(ankleUpperRad.low) →ankleUpperRoot|
|O087|PORT|ADD point(A_PORT.low)+point(ankleUpperRoot.low) →ankleUpper (retain.low)|
|O088|PORT|SUB point(m.high)−point(C_PORT.low) →reachLowerRad|
|O089|PORT|MAX_ZERO reachLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 0|
|O090|PORT|SMALL_ROOT point(selected) →reachLowerRoot|
|O091|PORT|ADD point(A_PORT.high)+point(reachLowerRoot.high) →reachLower (retain.high)|
|O092|PORT|SUB point(M.low)−point(C_PORT.high) →reachUpperRad|
|O093|PORT|SMALL_ROOT point(reachUpperRad.low) →reachUpperRoot|
|O094|PORT|ADD point(A_PORT.low)+point(reachUpperRoot.low) →reachUpper (retain.low)|
|O095|PORT|ABS WHOLE X_PORT →retained absX|
|O096|PORT|DIV WHOLE absX/WHOLE b →rollQuotient|
|O097|PORT|ADD point(A_PORT.high)+point(rollQuotient.high) →rollLower (retain.high)|
|O098|STAR|NEG Z_STAR →W|
|O099|STAR|SUB W−pointL1 →cutlowNum|
|O100|STAR|DIV cutlowNum/pointL2 →cutlow|
|O101|STAR|ADD W+pointL1 →cuthiNum|
|O102|STAR|DIV cuthiNum/pointL2 →cuthi|
|O103|STAR|DIV W/Lsum →cutturn|
|O104|STAR|MAX(point−.5,point(cutlow.high)) →p (first on equality)|
|O105|STAR|MIN(point+.5,point(cuthi.low)) →q0 (first on equality)|
|O106|STAR|MIN(q0,point(cutturn.low)) →q (first on equality)|
|O107|STAR|SQUARE point(p) →tau2|
|O108|STAR|SUB point1−tau2 →cosRad|
|O109|STAR|SMALL_ROOT WHOLE cosRad →cosShin|
|O110|STAR|MUL pointL2*cosShin →F2endpoint|
|O111|STAR|MUL pointL2*point(p) →tauL2|
|O112|STAR|SUB W−tauL2 →G1endpoint|
|O113|STAR|SQUARE G1endpoint →g1sq|
|O114|STAR|SUB L1sq−g1sq →thighRad|
|O115|STAR|INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 4|
|O116|STAR|SMALL_ROOT WHOLE clip →F1endpoint|
|O117|STAR|ADD F1endpoint+F2endpoint →rho_p|
|O118|STAR|SQUARE point(q) →tau2|
|O119|STAR|SUB point1−tau2 →cosRad|
|O120|STAR|SMALL_ROOT WHOLE cosRad →cosShin|
|O121|STAR|MUL pointL2*cosShin →F2endpoint|
|O122|STAR|MUL pointL2*point(q) →tauL2|
|O123|STAR|SUB W−tauL2 →G1endpoint|
|O124|STAR|SQUARE G1endpoint →g1sq|
|O125|STAR|SUB L1sq−g1sq →thighRad|
|O126|STAR|INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 5|
|O127|STAR|SMALL_ROOT WHOLE clip →F1endpoint|
|O128|STAR|ADD F1endpoint+F2endpoint →rho_q|
|O129|STAR|POINT_UPPER rho_p →u|
|O130|STAR|POINT_LOWER rho_q →v|
|O131|STAR|SQUARE pointu →u2|
|O132|STAR|SUB u2−point(X2_STAR.low) →ankleLowerRad|
|O133|STAR|MAX_ZERO ankleLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 7|
|O134|STAR|SMALL_ROOT point(selected) →ankleLowerRoot|
|O135|STAR|ADD point(A_STAR.high)+point(ankleLowerRoot.high) →ankleLower (retain.high)|
|O136|STAR|SQUARE pointv →v2|
|O137|STAR|SUB v2−point(X2_STAR.high) →ankleUpperRad|
|O138|STAR|SMALL_ROOT point(ankleUpperRad.low) →ankleUpperRoot|
|O139|STAR|ADD point(A_STAR.low)+point(ankleUpperRoot.low) →ankleUpper (retain.low)|
|O140|STAR|SUB point(m.high)−point(C_STAR.low) →reachLowerRad|
|O141|STAR|MAX_ZERO reachLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 1|
|O142|STAR|SMALL_ROOT point(selected) →reachLowerRoot|
|O143|STAR|ADD point(A_STAR.high)+point(reachLowerRoot.high) →reachLower (retain.high)|
|O144|STAR|SUB point(M.low)−point(C_STAR.high) →reachUpperRad|
|O145|STAR|SMALL_ROOT point(reachUpperRad.low) →reachUpperRoot|
|O146|STAR|ADD point(A_STAR.low)+point(reachUpperRoot.low) →reachUpper (retain.low)|
|O147|STAR|ABS WHOLE X_STAR →retained absX|
|O148|STAR|DIV WHOLE absX/WHOLE b →rollQuotient|
|O149|STAR|ADD point(A_STAR.high)+point(rollQuotient.high) →rollLower (retain.high)|
|O150|common|MAX ankleLower_PORT,ankleLower_STAR →lower; first operand on equality|
|O151|common|MAX lower,reachLower_PORT →lower; first operand on equality|
|O152|common|MAX lower,reachLower_STAR →lower; first operand on equality|
|O153|common|MAX lower,rollLower_PORT →lower; first operand on equality|
|O154|common|MAX lower,rollLower_STAR →publish slice.lo; first operand on equality|
|O155|common|MIN ankleUpper_PORT,ankleUpper_STAR →upper; first operand on equality|
|O156|common|MIN upper,reachUpper_PORT →upper; first operand on equality|
|O157|common|MIN upper,reachUpper_STAR →publish slice.hi; first operand on equality|
|O158|common|RN finite SUB hi−lo →width|
|O159|common|RN finite MUL width*.5 →halfWidth|
|O160|common|RN finite ADD lo+halfWidth →ONE storedY; publish slice.y|
|O161|PORT|SUB pointY−WHOLE A_PORT →h|
|O162|PORT|SQUARE h →h2|
|O163|PORT|ADD WHOLE X2_PORT+h2 →rho2|
|O164|PORT|ADD rho2+WHOLE Z2_PORT →D|
|O165|PORT|SMALL_ROOT WHOLE rho2 →rho|
|O166|PORT|ADD WHOLE Delta+D →alphaNum|
|O167|PORT|MUL point2*D →alphaDen|
|O168|PORT|DIV alphaNum/alphaDen →alpha|
|O169|PORT|SUB M−D →outer|
|O170|PORT|SUB D−m →inner|
|O171|PORT|MUL outer*inner →gammaNum|
|O172|PORT|SQUARE D →D2|
|O173|PORT|MUL point4*D2 →gammaDen|
|O174|PORT|DIV gammaNum/gammaDen →gamma2|
|O175|PORT|SMALL_ROOT WHOLE gamma2 →gamma|
|O176|PORT|SUB point1−alpha →beta|
|O177|PORT|MUL alpha*rho →alphaRho|
|O178|PORT|MUL gamma*WHOLE Z_PORT →gammaZ|
|O179|PORT|ADD alphaRho+gammaZ →F1|
|O180|PORT|MUL gamma*rho →gammaRho|
|O181|PORT|MUL alpha*WHOLE Z_PORT →alphaZ|
|O182|PORT|SUB gammaRho−alphaZ →G1|
|O183|PORT|MUL beta*rho →betaRho|
|O184|PORT|SUB betaRho−gammaZ →F2|
|O185|PORT|MUL beta*WHOLE Z_PORT →betaZ|
|O186|PORT|ADD betaZ+gammaRho →G2negative|
|O187|PORT|NEG G2negative →G2|
|O188|PORT|ABS WHOLE G2 →absG2|
|O189|PORT|MUL F2*point.5 →halfF2|
|O190|PORT|MUL absG2*WHOLE sqrt3half →pitchTerm|
|O191|PORT|SUB halfF2−pitchTerm →ankleMargin|
|O192|PORT|MUL h*WHOLE b →rollProduct|
|O193|PORT|SUB rollProduct−WHOLE absX →rollMargin|
|O194|STAR|SUB pointY−WHOLE A_STAR →h|
|O195|STAR|SQUARE h →h2|
|O196|STAR|ADD WHOLE X2_STAR+h2 →rho2|
|O197|STAR|ADD rho2+WHOLE Z2_STAR →D|
|O198|STAR|SMALL_ROOT WHOLE rho2 →rho|
|O199|STAR|ADD WHOLE Delta+D →alphaNum|
|O200|STAR|MUL point2*D →alphaDen|
|O201|STAR|DIV alphaNum/alphaDen →alpha|
|O202|STAR|SUB M−D →outer|
|O203|STAR|SUB D−m →inner|
|O204|STAR|MUL outer*inner →gammaNum|
|O205|STAR|SQUARE D →D2|
|O206|STAR|MUL point4*D2 →gammaDen|
|O207|STAR|DIV gammaNum/gammaDen →gamma2|
|O208|STAR|SMALL_ROOT WHOLE gamma2 →gamma|
|O209|STAR|SUB point1−alpha →beta|
|O210|STAR|MUL alpha*rho →alphaRho|
|O211|STAR|MUL gamma*WHOLE Z_STAR →gammaZ|
|O212|STAR|ADD alphaRho+gammaZ →F1|
|O213|STAR|MUL gamma*rho →gammaRho|
|O214|STAR|MUL alpha*WHOLE Z_STAR →alphaZ|
|O215|STAR|SUB gammaRho−alphaZ →G1|
|O216|STAR|MUL beta*rho →betaRho|
|O217|STAR|SUB betaRho−gammaZ →F2|
|O218|STAR|MUL beta*WHOLE Z_STAR →betaZ|
|O219|STAR|ADD betaZ+gammaRho →G2negative|
|O220|STAR|NEG G2negative →G2|
|O221|STAR|ABS WHOLE G2 →absG2|
|O222|STAR|MUL F2*point.5 →halfF2|
|O223|STAR|MUL absG2*WHOLE sqrt3half →pitchTerm|
|O224|STAR|SUB halfF2−pitchTerm →ankleMargin|
|O225|STAR|MUL h*WHOLE b →rollProduct|
|O226|STAR|SUB rollProduct−WHOLE absX →rollMargin|

## 6. Literal readiness table G01–G37

G01–06 precede O001. Each later row runs after its named operation BEFORE next operation; shared placement uses ascending guard numbers. After O226 execute G35,G36,G37. G36 checks seed before ONLY two packet assignments. G37 environment/structure/numerics/ONCE-validator/postenvironment is one bounded charged compound.

|Row|Side|After operation|Ordered readiness predicate and finite-failure SliceCondition|
|---|---|---|---|
|G01|common|before O001|same actual owner/Data/parts/Request; valid provider; actual source helper success, count64/fullmask|
|G02|common|before O001|original FP helper|
|G03|common|before O001|new version4/Candidate0, immutable seed template (no generatedY yet)|
|G04|common|before O001|original coordinate term/count/canonical-unused/abs8 domains|
|G05|common|before O001|original compiler/link identities and finite positive fixed L1/L2<=1|
|G06|common|before O001|S64 proper rational yaw/flat held soles and preserved seed tuple|
|G07|common|O045|supported finite b then b.low>0 (no_positive_roll_factor)|
|G08|PORT|O054|finite singleton p/q, −.5<=p<q<=.5; p>=cutlow.high, q<=cuthi.low, q<=cutturn.low; save endpoint-domain certificate (no_tau_interval)|
|G09|PORT|O056|earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for p|
|G10|PORT|O062|earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for p|
|G11|PORT|O067|earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for q|
|G12|PORT|O073|earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for q|
|G13|PORT|O078|supported finite singleton u/v, 0<=u<v (no_radial_interval)|
|G14|PORT|O085|supported ankleUpperRad.low>0 (no_positive_upper)|
|G15|PORT|O092|supported reachUpperRad.low>0 (no_positive_upper)|
|G16|STAR|O106|finite singleton p/q, −.5<=p<q<=.5; p>=cutlow.high, q<=cuthi.low, q<=cutturn.low; save endpoint-domain certificate (no_tau_interval)|
|G17|STAR|O108|earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for p|
|G18|STAR|O114|earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for p|
|G19|STAR|O119|earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for q|
|G20|STAR|O125|earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for q|
|G21|STAR|O130|supported finite singleton u/v, 0<=u<v (no_radial_interval)|
|G22|STAR|O137|supported ankleUpperRad.low>0 (no_positive_upper)|
|G23|STAR|O144|supported reachUpperRad.low>0 (no_positive_upper)|
|G24|common|O157|earned finite lo/hi then lo<hi (empty_inward_interval)|
|G25|common|O160|finite storedY then abs(Y)<=8 (candidate_domain)|
|G26|common|O160|storedY>lo (midpoint_unavailable)|
|G27|common|O160|storedY<hi (midpoint_unavailable)|
|G28|PORT|O164|supported finite h/rho2/D; h.low>0, rho2.low>0, D.low>m.high, D.high<M.low in order (verification_inconclusive)|
|G29|PORT|O174|supported finite gamma2.low>0 (verification_inconclusive)|
|G30|PORT|O187|supported finite F1/F2/G1/G2; F1.low>0 then F2.low>0 (verification_inconclusive)|
|G31|PORT|O193|supported finite ankle/roll margins; ankleMargin.low>=0 then rollMargin.low>=0 (verification_inconclusive)|
|G32|STAR|O197|supported finite h/rho2/D; h.low>0, rho2.low>0, D.low>m.high, D.high<M.low in order (verification_inconclusive)|
|G33|STAR|O207|supported finite gamma2.low>0 (verification_inconclusive)|
|G34|STAR|O220|supported finite F1/F2/G1/G2; F1.low>0 then F2.low>0 (verification_inconclusive)|
|G35|STAR|O226|supported finite ankle/roll margins; ankleMargin.low>=0 then rollMargin.low>=0 (verification_inconclusive)|
|G36|common|O226|check immutable seed BEFORE writing only both rootY={Y,+0,+0}count1; exact generated packet/static tuple after assignment (identity)|
|G37|common|O226|original FP precheck; generated packet structural checks then numerical domains; current+pending original validator Expected ONCE; FP postcheck (identity/unsupported)|

G01/G03/G05/G06 identity failure is ordinary slice_identity/identity; G02 FP failure is hard unsupported. G04 first structural counts/canonical unused/static seed tuple, then finite terms abs<=8/original sum-domain prerequisites: structural failure identity, arithmetic/domain failure unsupported. G05 finite/positive link domain failure unsupported, altered original version/length identity slice_identity. G07 unsupported/nonfinite/order hard, finite b.lower<=0 no_positive_roll_factor. G08/G16 finite ordered cut inequalities in table order give no_tau_interval; invalid support hard. Success retains same-side endpoint certificate in constructor controls, not new authority. G09/11/17/19 finite cosRad.lower<=0 and G10/12/18/20 finite thighRad.upper<0 give endpoint_domain_unavailable. Unsupported/nonfinite data remain hard. Closed endpoint thighRad=0 is allowed by clip/root after universal certificate; no endpoint derivative is evaluated.

G13/G21 finite singleton u/v, then u>=0, then u<v; finite false no_radial_interval. G14/G15/G22/G23 supported finite upper radicand.lower>0; finite false no_positive_upper, unsupported hard. Every SMALL_ROOT itself enforces supported domain before sqrt reads with no extra uncharged readiness. G24 finite lo/hi then lo<hi gives empty_inward_interval. G25 nonfinite hard, finite absY>8 candidate_domain. G26/G27 failed strict membership midpoint_unavailable; rounded midpoint at endpoint is not moved/retried.

G28/G32 supported h/rho²/D, then h.lower>0, rho².lower>0, D.lower>m.upper, D.upper<M.lower in that order. G29/G33 gamma².lower>0 BEFORE positive root. G30/G34 supported finite F1/F2/G1/G2, then F1.lower>0, F2.lower>0. G31/G35 supported ankle/roll, then ankle.lower>=0, roll.lower>=0. Finite sufficient failure verification_inconclusive, not exact theorem/policy contradiction. First failed supported h/rho²/D/gamma²/F1/F2/ankle/roll supplies current bound; no later read. Closed ankle/roll equality allowed, interior gamma/rho/down/F1/F2 strict. Inward proof never waives these direct interval predicates.

For a finite failed cut/p/q/radial/height/stored-membership guard, attach the first failed predicate's point or interval bound as follows: G08/G16 first p if lower range/cutlow fails, else q for upper/order/cuthi/cutturn; G09/11/17/19 cosRad; G10/12/18/20 thighRad; G13/G21 u for u<0, otherwise v for u>=v; G14/15/22/23 upper radicand; G24 hi for lo>=hi; G25 Y; G26/G27 Y. Each support/finite/order check precedes attachment. Current scratch is reset each row and need not remain as a transcript of a previously earned interval; selected lo/hi/Y/zero bits always persist. Ineligible/unattempted values are never read for attachment.

G36 rechecks seed BEFORE writing ONLY BOTH rootY={Y,+0,+0}count1, then exact generated/static packet comparison. Identity failure grants no Key even if local Request assignment occurred. G37 suborder: original FP precheck (false hard/no validator); actual generated counts/canonical unused/held fields/version/Y identity (ordinaryidentity); original finite numeric/sum domains (hard); unchanged boarding_route_foot_phase_request_valid ONCE, preserving current/pending expected<void,string> (unexpected under prerequisites hard unsupported, no error-string parsing); original FP postcheck (hard). The old validator's unsafe-environment sum shortcut cannot issue authority. Its bounded checks are explicitly constituents of ONE charged compound; no hidden added operation/guard count.

## 7. Proof, partial evidence and original continuation

Cuts implement p=max(−.5,upper((W−L1)/L2)), q=min(.5,lower((W+L1)/L2),lower(W/(L1+L2))) with full signed W and outward DIV. Closed universal endpoint inequalities prove actual endpoint thigh radicands>=0 even when the rounded enclosure.lower is negative; this is the sole clipping justification. u>=rho_p(w), v<=rho_q(w) for ALL w in W; u<v defines an open radial subset. Empty/uncertain cuts are ordinary unavailability, not all-rootY exclusion. Lower ankle height uses upper u, X².lower, root.upper, A.upper and outer ADD.upper; upper uses lower v, X².upper, positive radicand.lower, root.lower, A.lower and outer ADD.lower. Original independent reach/roll bounds use the same directed choices and BOTH distinct origins.

Complete constructor requires actual source_enrolled/count64/full source mask, all226 supported written operations, all37 true written readiness rows, exact generated Request and G37 final FP success. Slice.arithmetic_supported startsfalse, earns after actual supported operation write, survives ordinary/capacity stops, clears on constructor FP/numeric/root/validator failure. Slice.complete is construction only. Later unrelated original graph/pressure/SELF unsupported clears corresponding aggregate arithmetic but preserves complete Slice arithmetic/masks/Y/lo/hi. No original body/timing/support/arithmetic flag is promoted from the new certificate. Slice condition staysnone after successful constructor; downstream phase causes are separate.

D01–03 are actual charged revalidations AFTER Key BEFORE original graph: same owner/Data/parts/Request/Key; original FP; version4/Candidate/generated packet/share/duration/full readiness. No second factory. Reset one owned flat Cell only after reservation, accounting current/pending Cell and initializer scopes. phase_calls increments immediately before ONE unchanged boarding_route_foot_phase_cell(request,0,1,false,caps,work,cell.phase,reason) with fixed original1/2/1/3/6, independently lowered by five original fields. No structuralbody0, retry or expectation the constructor makes other original sectors/timing pass. D04 checks actual result immediately BEFORE projection. Original unsupported/capacity/unresolved retained honestly; default nested bounds/[0,0]/depth0/predicate0 earn no support or different call-range interpretation.

After genuinely accepted original graph, select the unchanged literal current protocol [Endpoint01 sections5–9](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT01.md#5-exact-producer-order-and-current-projection-p251) and [Endpoint03 section7](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT03.md#7-proof-completion-and-stops), fresh owner/Data/parts/Request/Key/Candidate/version4 bindings only. P01 charged actual Context/call1/cellslot identity; P251=53 identity rows+11carriers*18 scalar rows; full canonical factory only at charged P11. D01–03 before graph/D04 before P/D05–31 in typed boot. Repeated checks are actual charged reads, not retroactive masks.

D05–31 preserves original source rectangles/sole planes/closed intersection/midpoint, source-bound PORT1/16/STAR15/16 COM allocation and all16 disk edges. D31 may set intermediate accepted to reach SELF; public complete remainsfalse. B63 actual current body/frame/region/relative18 and Unit24 preserve original WORLD K−H/K−A SUB3 then exact-link endpointDIV3. The same genuinely normalized axis feeds original owner/complement; no singleton reciprocal/midpoint normalization. Preserve14 owners/105pairs (all91 nonadjacent/distal negatives),1470axes/2940signed/2hips; pelvis/trunk waist support, finite hip caps/slab/full distal cap; ankle uY>=0 and bootC−A=−.05WORLDY/exact sole.Y=WORLDY cancellation; original family applicability. Every record sets UPCOMING actual stage BEFORE charge (pair14); reset only row scratch, retain accepted certificates. Source64 UINT64_MAX, D31 lower31bits, B63 lower63bits, Unit24 lower24bits and cursor255/pair65535 follow the literal original partial/reset rules.

Only own current_cell issues synchronous new Token after actual graph/P. Boot/SELF cannot construct Context/Token. Original mathematical helpers may be reused behind new END-only typed adapters; no old capability/friend/header changes. Accepted endpoint requires actual original current/projection, finite nominal contact/load and complete SELF. WORLD/material/HALO/route/reverse acquisition/pan seat/occupied actor/dynamics/save/FirstFlight remain separate; parents462/361/352 stayopen. Unavailable connected subset/later refusal does not relax policy or classify all postures.

## 8. Exact Limits and finite controls

Limits starts original BoardingIntermediatePausePhaseLimits (five uint64 fields1/2/1/3/6), followed by20 size_t fields below. All25 independently lowered; any raised field rejects before source in this order. Default validation Limits is one of FOUR separate L slots. Work is successful charge counts/one actual phase invocation, never failed upcoming cursor or default readiness.

|Index|Field|Default|
|---:|---|---:|
|0|phase.graphs|1|
|1|phase.legs|2|
|2|phase.bodies|1|
|3|phase.sectors|3|
|4|phase.timing|6|
|5|source_guards|64|
|6|projection_guards|256|
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
|23|construction_guards|37|
|24|construction_operations|226|

Selected finite purposes: one genuine creator, ONE default public A01 FIRST per compiler,97 literal labels, RAW0/inspector0 and no substitute after SKIP. A01 has no success/refusal expectation. Base A02–26 lower one field0; A27–51 raise default+1; A52–76 actual used−1; A77 all exact-used and REQUIRED output. Workzero needs structured FIRST actualfield>0; work used−1 needs>1 (used1 duplicate0 and used0 unreached SKIP). Output0 always dispatches after creator; output used−1 is REQUIRED−1 with a structured baseline. Any structured FIRST ordinary/capacity/unsupported source/construction/original/support/SELF report is exact-replay baseline; unexpected disables dependent baseline controls, not independent raised/FP/identity calls. A77 compares earned semantic hash/full first+terminal causes/partial masks/cursors/availability/clock and actual owned bytes. Provider ownership checked separately; no cached report/capability or owning result wrapper.

A78–83: saved/restored FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO,FTZbit15,DAZbit6,MXCSRnonnearest (control & ~(3U<<13)) | (1U<<13) with otherwise normal fenv. Restore flags/control/rounding without changing traps; unsupported regimes SKIP/no substitute. A84 Candidate255. A85 copyprovider/movealiastosaved/assess genuinely empty alias once/restore, while originalvalid. A86 moveoriginaltomoved-to/assessvalid/restore. A87 copysurvivor/moveoriginaltoscopeddiscarded/destroydiscarded BEFORE assessing survivor with original genuinely empty/restore. A88 restored original reuse. Creator Expected/pending creator results die before FIRST; no hidden owner or second factory.

A89 source0+G0 requires FIRSTsource reached; sourcecapacity prevents constructor. A90 G0+O0 requires FIRSTguard reached; G01capacity preventsO. A91 O0+phase.graphs0 requires FIRSToperation reached; G01–06 then O01capacity, noKey/clock/phase. New boundaries require successful source witness and structured FIRST operationused>cap plus ALL prior interleaved guards genuinely written; absent premise SKIP.

|Label|Operation cap|Failed upcoming row/cursor|Last prior required guard|Distinct publication consequence|
|---|---:|---|---|---|
|A92|64|O065 /64|G10 bit9|PORT endpoint F1root O064written; word1clear/rho_pNOT_RUN.|
|A93|65|O066 /65|G10 bit9|rho_p O065written/word1bit0; qNOT_RUN.|
|A94|128|O129 /128|G20 bit19|STAR rho_q O128written; word2clear/uNOT_RUN.|
|A95|129|O130 /129|G20 bit19|STAR u O129written/word2bit0; v/G21NOT_RUN.|
|A96|192|O193 /192|G30 bit29|PORT roll product O192written; word3clear/rollmargin G31NOT_RUN.|
|A97|193|O194 /193|G31 bit30|PORT roll margin AND G31truewritten, word3bit0; STAR directcheckNOT_RUN.|

The finite ceiling is97consumers/APIcalls/fixedFP; creator1, RAW0/inspector0; at mostone construction audit, one body audit and105pair+14owner+2hip interval checks, A01only. Checked accumulation before narrowing:26 uint32 successful-work totals (fivephase+19outer+phase_calls+fixedFP),7 independently bounded uint16 creator/consumer/skip/next_slot/construction/body/interval counters (1/97/97/98/1/1/121). TargetTotals120; separate checksuint64/failuresuint32; globals target140. Summary120 byreference; actualassessment owns Expected, no report or large word/certificate archives. One monotonic labelcursor accounts dispatch/skip, not dual97-bit storage.

Hash y onlyO160/loO154/hiO157 and zero bits only at their eight actual written rows; four operationwords/guardmasks/cursors/condition/availability/clock. Original IDs0..14 ONLY after actual source_enrolled ANDcount64/fullmask, no catalog/type/family/inspector query or fullmaskalone. Body/contact/load/owner/hip reads require their actual masks/support. Default original nested reason/limiting doubles never gain generic support.

A01-only construction corroboration recomputes direct original properyaw/chart, signed rho²/D, positivegamma/F1/F2/G1/G2 and BOTH ankle/roll predicates at actual storedY, from locally reconstructed immutable literals. It does not recycle inward endpoints/p/q/radial cuts as oracle, and calls no generator/inspector/phase/source/solver/RAW. Actual storedlo<Y<hi is binary64 strictcomparison with no tolerance. Independent LD allowance remains existing2e-12*max(1,abs(value)), corroboration-only; never relaxes producer/original guards. Maxone audit after source+complete226/37 supportedSlice. Its current/pending reconstructedRequests and3072utility die BEFORE body. Actual accepted original phase then permits at mostone unchanged full body/15mass/pose audit with current/pending TWO8400 Oracles and Requests; no accepted-pose assumption. These scopes die BEFORE original interval audits105pairs/14owners/2hips, conditional on actual supported per-record applicability. No body/SELF audit is weakened to pass resources. The [matched97 manifest](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT04_TEST_MANIFEST.md) and [exact audit/control inventory](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT04_TEST_INVENTORY.json) are selected with this table/schema for paired publication review; no arithmetic fixture seam.

## 9. Complete additive source forecasts and actual gates

### Effective ownership scope

The user-directed scope amendment retains the registered numerical worker and bounded source-creator limits while removing a total native-game or whole-world-startup limit. Every existing forecast, component, helper pool, layout target and historical binding below is unchanged. The historical `whole_live` key now denotes numerical-worker working storage, not total process memory. Earlier failed whole-world gates remain preserved evidence under their former scope; this amendment does not retroactively relabel them as passing measurements.

Numerical working storage includes the candidate-born provider, source catalog and arena; source enrollment, canonical guards and factories; constructor, original graph, support and SELF work; target-owned outputs; all current and pending headers, requests, refusals, limits, cells, capabilities and returned values; actual active caller frames, errors, allocations, cleanup and independent audits. Retain the numerical scalar cache and its guard as separately charged storage. Existing conservative native prerequisite metadata charges remain retained rather than reclaimed as margin. Complete source and machine proofs remain independent and permit no elision or borrowed slack.

Original native world/contact and boot prerequisite preparation are a separate, nonzero accounting scope: cold loader frames and temporary owners, joined source text, parsing and duplicate-check storage, canonical recipe and decoder caches, published support/cabin/lower/stowed geometry, pose and shared ownership, errors and destruction. Record their actual capacities, payloads and simultaneous lifetimes separately, with unknown complete loader peaks stated as unknown; no new total-world upper cap or fit claim is introduced. Immutable application-owned world inputs borrowed by the numerical worker are reported in this separate scope, not counted again as candidate-born scratch. This follows the native ownership split in [Godot adoption](GODOT_ADOPTION.md); world source and public ownership remain authoritative.

Separation does not make a selected call free. Any loader, decoder or authentication path actually reentered during a numerical stage must be identified from source and saved call edges; its active numerical-stage temporaries, returns, errors, caller frames and cleanup remain charged. Persistent borrowed world owners are distinguished from those fresh working owners. Cold numerical helper initialization remains covered without assuming a warm static state. Existing authentic prerequisite setup runs as originally specified, with no added warm-up, source recapture, hidden factory, extra creator or alternate program. Later WORLD and route qualification remains separately unearned.

All byte sums below are integer storage/control forecasts only. Actual sizes, lexical compiler lowering, complete physical paths and outcomes remain UNKNOWN. Source and machine proofs independent. FULL historical33208/nested28880/24680 and32768graph reservations stay intact; no subset/slack/NRVO/optimized-return credit. phase_scalar_limits BSS144+guard8=152 is independent of Testglobals140 and coadded to EVERY live stage, not pure output ownership.

TWOExpected1664=3328; FOURLimits200=800 includes actual validationdefault/current/caller/pending. Full current/pendingRefusal184=368 is exterior to header first_refusal. Request680+pendingoptional688=1368, current/pendingKey16=32, Context40, Token40reservation and originalCaps72/Reason64/Work40 remain distinct. Some maps conservatively retain inactive Request/authority/Token; labeled overcharges, not fabricated actual objects. Old EMPTY/currentpending headers/refusals/catalog/factory wrappers remain wholly reserved in source only. No new scratch survives into body margin.

Selected forecast payload endpoint04-exact-source-forecast-v2.json SHA256 6419f0767819a90a93d594a4e492899e67436434ca5cd00ddda5bc902abf18b1. Complete maps are embedded below; validation filename is a provenance label, not a portable build dependency.

|Stage|Forecast bytes|Ceiling|Headroom|
|---|---:|---:|---:|
|creator|7152|8192|1040|
|preflight|11000|49152|38152|
|required_room_refusal|12048|49152|37104|
|source_enrollment|39400|49152|9752|
|source_plus_disjoint_construction_overcharge|43496|49152|5656|
|construction|17080|49152|32072|
|reset|34746|49152|14406|
|original_graph|48536|49152|616|
|canonical|31224|49152|17928|
|pressure_SELF|29688|49152|19464|
|region_factory|24984|49152|24168|
|construction_audit|26960|49152|22192|
|body_audit|48880|49152|272|
|interval_audit|32080|49152|17072|
|owned_capacity1_output|14360|16777216|16762856|

The body delta is exactly TWOheader3328−3200=128 plus independently added static152; all other selected body component changes are0. New Slice growth is inside BOTH headers, no retained extra certificate or mask archive. Old prep Expected/carriers/L/R/factory scopes, new constructor4096 and independent chart3072/Requests all die before body; body owns separately named Request/Oracle slots. The new current/pending Expected reservation stays LIVE and counted3328, never elided. Actual Test lexical verification must corroborate that no extra Refusal, Key, Context, work array or pending factory copy remains beyond these selected reservations. Body48880 leaves272 bytes under49152, a forecast margin not storage for omitted owners. FullCell/TWOOracles/Requests/headers/fourLimits/caller/scalarerror/utility/static152 remain co-live. Constructor auditutility dies before body; both body Oracles/Requests die before interval. Combined43496 overcharges sequential whole source+4096constructor, never keeps old carrier into Key. Graph48536 retains full32768 and Cell delta11032−7736=3296 plus exterior owners. Actual returned originalgraph4096/projectprimitive256 reservations must be explicitly covered in separate machine proof; sourcepool is not a frame cap.

Constructor4096 names3728+UNUSED368: inputs192/yaw144/BOTHcharts384/linkthresholds216/BOTHparameter-radial288/heightcuts80/activeendpoint432/activedirect432(current arrays conservatively simultaneous)/primitive argument-local-pending192/CONSTsign96/certifiedsmallroot176/references-closures-cursor-controls256/TWOvalidatorExpected80/environment-library-error512/scalarselections64/full row-reset Refusal initializer184. The row-reset fullRefusal initializer is distinct from exterior current/pendingR368 and header first_refusal; it lives through assignment full-expression and dies BEFORE following row charge. One initializer per sequential helper is reserved without elision credit. These are inherited existing objects explicitly named inside their unchanged helper pools, not new growth or a transfer from source/graph/caller/header slack. Each reused direct slot waits its lastconsumer; sourcebound BOTH X/Z/X²/Z²/C/A/W/ABS and thresholds are never overwritten. Endpoint certificate/branch/controls are explicitly inside256. G37 returnobjects are separate from bounded string/error allowance. Byreferencebool creates no aggregate Slice return. Every local/caller/pending helper alternative remains reserved even if optimized away.

Factory7680 names EIGHT optional688 returns5504+Request680+sixconstants576+step96+refs-error512=7368+312. Tuple2048 names threefullregions672+twoparts128+descriptors256+refs-error512+sort480. Pressure6144 names3376+2768, including four128 corner returnslots512 and full row-reset Refusal initializer184. SELF6144 names5344+800, including local/caller/pending solids/points/owner/hip/half-ray and full row-reset Refusal initializer184: exact map below. B44regionfactory1440 is sequential, full672+512+256. Body8192 retains7904+288. Interval8192 full1296+1728+1152+1024+512+2480 has nounused. Inherited registrations bind the detailed original names; these are retained reservations, not newly fabricated objects.

Test creator756+268 under1024 and graph1871+177 under2048 remain FULL selected callerpools. Graph includes initializer peak136/currentpending192workarrays/environment/error/hash/ref/stream/global metadata. New certificate has no Summary wordarchive, so hashing reads actual ownedExpected inassessment. Tests must independently itemize actual lexical changes; any extra object can close272body headroom. Constructor audit3072 names2624+448: twelveLDbounds384/sixLDvectors576/48LDscalars768/32doubles256/refs-closures-cursors128/library-error512, with Request1368 separately exterior. This is proposed sequential audit storage for matched review, not measured source layout. Complete component maps are authoritative forecasts; UNKNOWN objects never count0. The public companion inventory is the exact independent Test lexical plan and finite structured controls, with shared maps referenced once and historical author hashes explicitly separated from current publication bindings.

```json
{
  "status": "EXACT_TYPED_REGISTRATION_DRAFT_NOT_IMPLEMENTATION_OR_ADMISSION",
  "issue": 505,
  "coordinate_bound_angle_evaluations": 0,
  "compiler_API_runtime_queries": 0,
  "actual_new_layouts": "UNKNOWN",
  "actual_complete_source_and_physical_admission": "UNKNOWN",
  "outcome": "UNKNOWN",
  "selected_global_ceilings": {
    "creator": 8192,
    "whole_live": 49152,
    "output": 16777216,
    "shared_stdout_stderr": 16384
  },
  "layout_targets": {
    "expected": 1664,
    "cell": 11032,
    "limits": 200,
    "refusal": 184,
    "slice": 144,
    "work": 200,
    "key": 16,
    "context": 40,
    "token": 40,
    "validator_expected": 40,
    "test_summary": 120,
    "test_totals": 120,
    "test_globals": 140
  },
  "source_stage_maps": {
    "creator": {
      "components": {
        "entire_old_creator": 7000,
        "phase_scalar_limits_global": 152
      },
      "bytes": 7152,
      "ceiling": 8192,
      "headroom": 1040
    },
    "preflight": {
      "components": {
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "library_error": 512,
        "control": 64,
        "phase_scalar_limits_global": 152
      },
      "bytes": 11000,
      "ceiling": 49152,
      "headroom": 38152
    },
    "required_room_refusal": {
      "components": {
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "library_error": 512,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "current_request": 680,
        "current_pending_refusal": 368
      },
      "bytes": 12048,
      "ceiling": 49152,
      "headroom": 37104
    },
    "source_enrollment": {
      "components": {
        "full_old_endpoint03_enrollment": 33208,
        "new_two_headers": 3328,
        "new_four_limits": 800,
        "new_current_pending_refusal": 368,
        "new_context": 40,
        "new_token_reservation": 40,
        "new_current_pending_key": 32,
        "new_control": 64,
        "new_exterior_current_pending_request_overcharge": 1368,
        "phase_scalar_limits_global": 152
      },
      "bytes": 39400,
      "ceiling": 49152,
      "headroom": 9752
    },
    "source_plus_disjoint_construction_overcharge": {
      "components": {
        "full_old_endpoint03_enrollment": 33208,
        "new_two_headers": 3328,
        "new_four_limits": 800,
        "new_current_pending_refusal": 368,
        "new_context": 40,
        "new_token_reservation": 40,
        "new_current_pending_key": 32,
        "new_control": 64,
        "new_exterior_current_pending_request_overcharge": 1368,
        "phase_scalar_limits_global": 152,
        "disjoint_full_constructor_pool": 4096
      },
      "bytes": 43496,
      "ceiling": 49152,
      "headroom": 5656
    },
    "construction": {
      "components": {
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 368,
        "current_pending_key": 32,
        "context": 40,
        "old_caps_reason_work": 176,
        "constructor_pool": 4096,
        "independent_error": 512,
        "outer_control": 64,
        "phase_scalar_limits_global": 152
      },
      "bytes": 17080,
      "ceiling": 49152,
      "headroom": 32072
    },
    "reset": {
      "components": {
        "two_full_cells": 22064,
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 368,
        "context": 40,
        "current_pending_key": 32,
        "old_caps_reason_work": 176,
        "two_initializer_arrays": 210,
        "control": 64,
        "phase_scalar_limits_global": 152
      },
      "bytes": 34746,
      "ceiling": 49152,
      "headroom": 14406
    },
    "original_graph": {
      "components": {
        "entire_original_graph": 32768,
        "two_headers": 3328,
        "arena": 4096,
        "cell_delta": 3296,
        "four_limits": 800,
        "caller": 2048,
        "old_caps": 72,
        "old_reason": 64,
        "pending_optional_request": 688,
        "current_request": 680,
        "current_pending_refusal": 368,
        "context": 40,
        "token_reservation": 40,
        "current_pending_key": 32,
        "control": 64,
        "phase_scalar_limits_global": 152
      },
      "bytes": 48536,
      "ceiling": 49152,
      "headroom": 616
    },
    "canonical": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 368,
        "context_token": 80,
        "current_pending_key": 32,
        "old_caps_reason_work": 176,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "factory_pool": 7680
      },
      "bytes": 31224,
      "ceiling": 49152,
      "headroom": 17928
    },
    "pressure_SELF": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 368,
        "context_token": 80,
        "current_pending_key": 32,
        "old_caps_reason_work": 176,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "helper_pool": 6144
      },
      "bytes": 29688,
      "ceiling": 49152,
      "headroom": 19464
    },
    "region_factory": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 368,
        "context_token": 80,
        "current_pending_key": 32,
        "old_caps_reason_work": 176,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "region_factory": 1440
      },
      "bytes": 24984,
      "ceiling": 49152,
      "headroom": 24168
    },
    "construction_audit": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "scalar_error": 1024,
        "inactive_key_context_overcharge": 40,
        "phase_scalar_limits_global": 152,
        "construction_audit_utility": 3072
      },
      "bytes": 26960,
      "ceiling": 49152,
      "headroom": 22192
    },
    "body_audit": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "scalar_error": 1024,
        "inactive_key_context_overcharge": 40,
        "phase_scalar_limits_global": 152,
        "two_oracles": 16800,
        "body_utility": 8192
      },
      "bytes": 48880,
      "ceiling": 49152,
      "headroom": 272
    },
    "interval_audit": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "scalar_error": 1024,
        "inactive_key_context_overcharge": 40,
        "phase_scalar_limits_global": 152,
        "interval_utility": 8192
      },
      "bytes": 32080,
      "ceiling": 49152,
      "headroom": 17072
    },
    "owned_capacity1_output": {
      "components": {
        "two_headers": 3328,
        "capacity1_cell": 11032
      },
      "bytes": 14360,
      "ceiling": 16777216,
      "headroom": 16762856
    }
  },
  "source_pools": {
    "constructor": {
      "named_components": {
        "eight_input_intervals": 192,
        "six_yaw_intervals": 144,
        "two_side_chart_records_16_intervals": 384,
        "nine_link_threshold_intervals": 216,
        "twelve_parameter_radial_intervals": 288,
        "ten_height_cut_doubles": 80,
        "eighteen_endpoint_active_intervals": 432,
        "eighteen_direct_active_intervals": 432,
        "eight_primitive_argument_local_pending_intervals": 192,
        "constant_sign_arrays": 96,
        "certified_small_root_interval_terms_scratch_scalars": 176,
        "references_closures_cursor_controls": 256,
        "two_validator_expected": 80,
        "library_environment_error": 512,
        "eight_scalar_selection_doubles": 64,
        "row_reset_full_refusal_initializer": 184
      },
      "named_bytes": 3728,
      "unused_bytes": 368,
      "reservation_bytes": 4096
    },
    "factory": {
      "named_components": {
        "eight_optional_request_returns": 5504,
        "request": 680,
        "six_constants": 576,
        "step": 96,
        "references_error": 512
      },
      "named_bytes": 7368,
      "unused_bytes": 312,
      "reservation_bytes": 7680
    },
    "tuple": {
      "named_components": {
        "three_region_arrays": 672,
        "two_part_records": 128,
        "descriptors": 256,
        "references_error": 512,
        "sort": 480
      },
      "named_bytes": 2048,
      "unused_bytes": 0,
      "reservation_bytes": 2048
    },
    "pressure": {
      "named_components": {
        "entire_preserved_pressure_named_inventory": 3192,
        "row_reset_full_refusal_initializer": 184
      },
      "named_bytes": 3376,
      "unused_bytes": 2768,
      "reservation_bytes": 6144
    },
    "SELF": {
      "named_components": {
        "relative_points": 1296,
        "whole_region": 224,
        "four_solid_local_caller_pending_returns": 768,
        "six_points": 432,
        "twentyfour_intervals": 576,
        "two_squared_scratch": 112,
        "eight_vectors": 192,
        "three_owner_returns": 168,
        "three_hip_and_three_half_ray_returns": 336,
        "extra_intervals": 192,
        "doubles": 192,
        "references": 160,
        "error": 512,
        "row_reset_full_refusal_initializer": 184
      },
      "named_bytes": 5344,
      "unused_bytes": 800,
      "reservation_bytes": 6144
    },
    "region_factory": {
      "named_components": {
        "three_region_arrays": 672,
        "tuple_references": 512,
        "error": 256
      },
      "named_bytes": 1440,
      "unused_bytes": 0,
      "reservation_bytes": 1440
    },
    "construction_audit": {
      "named_components": {
        "twelve_LD_bounds": 384,
        "six_LD_vectors": 576,
        "fortyeight_LD_scalars": 768,
        "thirtytwo_double_slots": 256,
        "references_closures_cursors": 128,
        "library_error": 512
      },
      "named_bytes": 2624,
      "unused_bytes": 448,
      "reservation_bytes": 3072
    },
    "body_audit": {
      "named_components": {
        "preserved_body_named_inventory": 7904
      },
      "named_bytes": 7904,
      "unused_bytes": 288,
      "reservation_bytes": 8192
    },
    "interval_audit": {
      "named_components": {
        "solids": 1296,
        "points": 1728,
        "bounds": 1152,
        "scalar_support": 1024,
        "library_error": 512,
        "selected_helper_returns_arguments": 2480
      },
      "named_bytes": 8192,
      "unused_bytes": 0,
      "reservation_bytes": 8192
    },
    "creator_caller": {
      "named_components": {
        "summary": 120,
        "two_streams": 176,
        "mutable_globals_target": 140,
        "boot_native_expected": 80,
        "provider": 24,
        "saved_buffers_bytes": 24,
        "references": 32,
        "errors": 160
      },
      "named_bytes": 756,
      "unused_bytes": 268,
      "reservation_bytes": 1024
    },
    "graph_caller": {
      "named_components": {
        "entire_preserved_endpoint03_named_caller": 1871
      },
      "named_bytes": 1871,
      "unused_bytes": 177,
      "reservation_bytes": 2048
    }
  },
  "lowerable_limits": [
    {
      "index": 0,
      "field": "phase.graphs",
      "default": 1
    },
    {
      "index": 1,
      "field": "phase.legs",
      "default": 2
    },
    {
      "index": 2,
      "field": "phase.bodies",
      "default": 1
    },
    {
      "index": 3,
      "field": "phase.sectors",
      "default": 3
    },
    {
      "index": 4,
      "field": "phase.timing",
      "default": 6
    },
    {
      "index": 5,
      "field": "source_guards",
      "default": 64
    },
    {
      "index": 6,
      "field": "projection_guards",
      "default": 256
    },
    {
      "index": 7,
      "field": "definition_guards",
      "default": 31
    },
    {
      "index": 8,
      "field": "sole_extrema",
      "default": 4
    },
    {
      "index": 9,
      "field": "source_coordinates",
      "default": 8
    },
    {
      "index": 10,
      "field": "intersection_operations",
      "default": 4
    },
    {
      "index": 11,
      "field": "midpoint_operations",
      "default": 4
    },
    {
      "index": 12,
      "field": "allocation_operations",
      "default": 6
    },
    {
      "index": 13,
      "field": "pressure_candidates",
      "default": 2
    },
    {
      "index": 14,
      "field": "disk_edges",
      "default": 16
    },
    {
      "index": 15,
      "field": "self_body_guards",
      "default": 63
    },
    {
      "index": 16,
      "field": "self_pairs",
      "default": 105
    },
    {
      "index": 17,
      "field": "self_axes",
      "default": 1470
    },
    {
      "index": 18,
      "field": "self_signed_trials",
      "default": 2940
    },
    {
      "index": 19,
      "field": "self_owners",
      "default": 14
    },
    {
      "index": 20,
      "field": "self_hip_complements",
      "default": 2
    },
    {
      "index": 21,
      "field": "unit_axis_operations",
      "default": 24
    },
    {
      "index": 22,
      "field": "output_bytes",
      "default": 16777216
    },
    {
      "index": 23,
      "field": "construction_guards",
      "default": 37
    },
    {
      "index": 24,
      "field": "construction_operations",
      "default": 226
    }
  ],
  "original_floor_nesting": {
    "full_endpoint03_enrollment": 33208,
    "contains_full_endpoint02_enrollment": 28880,
    "contains_full_endpoint01_enrollment": 24680,
    "full_original_graph": 32768
  },
  "inherited_text_identities": {
    "src/origin_boarding_intermediate_endpoint01.cpp": {
      "bytes": 51455,
      "sha256": "22720a7224d08c8009329feb84cabb31abb76c7d32d296ed6f2fc2fe205d6f76"
    },
    "src/origin_boarding_planted_legs.cpp": {
      "bytes": 374618,
      "sha256": "6b401090e85e91d4c7a8585abf38d6e3b28c90d8ed750e77be6ae513b462f8cf"
    },
    "src/origin_boarding_boot_support.cpp": {
      "bytes": 264868,
      "sha256": "4695bfbbeae5b100cd6e34924f643c325cd0ac9447b4391516aceceba58d88ee"
    },
    "include/apsis_drift/origin_boarding_intermediate_endpoint03.hpp": {
      "bytes": 8632,
      "sha256": "e7ac33e3e7060251ee96b39925b4f95ca4bcc1334c9127eec58a67f1d21bab7f"
    },
    "src/origin_boarding_intermediate_endpoint03_internal.hpp": {
      "bytes": 8167,
      "sha256": "b26f0bf2749574706025fd1b2803f4acb85473a9c21488adbc4d9ace21ae2165"
    },
    "docs/ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT03.md": {
      "bytes": 100642,
      "sha256": "703edeef45226d957a8f473f2a166e11e1f12f6c28f3809089ac8b52b5a92dcc"
    },
    "docs/ORIGIN_BOARDING_INTERMEDIATE_SECTOR_EVIDENCE02.md": {
      "bytes": 91490,
      "sha256": "9588a888b0abef4505c5303fc758941ce54cf6f3b328b611fa4d2514b0c217a5"
    },
    "docs/ORIGIN_BOARDING_ANKLE_CONTRACT_AUDIT.md": {
      "bytes": 16364,
      "sha256": "05832e5c832fc3227bd794a12e752216f8762cb0bbd5e477f607e0f330da59b1"
    },
    "docs/ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md": {
      "bytes": 17537,
      "sha256": "6112a7b8c91432c1a3c1ce4e52a78e18bb19a2ba635095b6d976b89ffaa91c01",
      "binding": "HELD_PROOF_BEFORE_ALLOWED_NAVIGATIONAL_ENDING_APPEND; held-symbolic-proof-before-registration.md preserves exact bytes"
    }
  },
  "physical_stage_maps": "UNKNOWN; future complete actual compile-only ancestry required; source reservations are never machine frame bounds",
  "finite_work_total_bounds": [
    {
      "field": "phase.graphs",
      "maximum": 97
    },
    {
      "field": "phase.legs",
      "maximum": 194
    },
    {
      "field": "phase.bodies",
      "maximum": 97
    },
    {
      "field": "phase.sectors",
      "maximum": 291
    },
    {
      "field": "phase.timing",
      "maximum": 582
    },
    {
      "field": "source_guards",
      "maximum": 6208
    },
    {
      "field": "projection_guards",
      "maximum": 24832
    },
    {
      "field": "definition_guards",
      "maximum": 3007
    },
    {
      "field": "sole_extrema",
      "maximum": 388
    },
    {
      "field": "source_coordinates",
      "maximum": 776
    },
    {
      "field": "intersection_operations",
      "maximum": 388
    },
    {
      "field": "midpoint_operations",
      "maximum": 388
    },
    {
      "field": "allocation_operations",
      "maximum": 582
    },
    {
      "field": "pressure_candidates",
      "maximum": 194
    },
    {
      "field": "disk_edges",
      "maximum": 1552
    },
    {
      "field": "self_body_guards",
      "maximum": 6111
    },
    {
      "field": "self_pairs",
      "maximum": 10185
    },
    {
      "field": "self_axes",
      "maximum": 142590
    },
    {
      "field": "self_signed_trials",
      "maximum": 285180
    },
    {
      "field": "self_owners",
      "maximum": 1358
    },
    {
      "field": "self_hip_complements",
      "maximum": 194
    },
    {
      "field": "unit_axis_operations",
      "maximum": 2328
    },
    {
      "field": "construction_guards",
      "maximum": 3589
    },
    {
      "field": "construction_operations",
      "maximum": 21922
    },
    {
      "field": "phase_calls",
      "maximum": 97
    },
    {
      "field": "preflight_guards",
      "maximum": 97
    }
  ],
  "persistent_body_delta": {
    "old_selected_two_headers": 3200,
    "new_selected_two_headers": 3328,
    "new_two_header_growth": 128,
    "independently_added_static": 152,
    "all_other_selected_body_component_changes": 0,
    "old_selected_body": 48600,
    "new_selected_body": 48880,
    "headroom": 272,
    "old_source_carriers_and_all_constructor_or_chart_scratch_dead_before_body": true,
    "actual_lexical_and_layout_admission": "UNKNOWN"
  },
  "naming_revision": {
    "kind": "EXPLICIT_EXISTING_ROW_RESET_INITIALIZER_ONLY",
    "historical_forecast_sha256": "452324dfefb2ce0efd5a86403a0c5c530444e711196134242cd4674fcad92afc",
    "historical_author_clarification_sha256": "56cc6f79b693c98f7babdc84a52408ee09d3c0ab0f0b3e6df97dc7131be76fc8",
    "changes": "Name one fullRefusal initializer184 within each unchanged4096/6144/6144 helper; no stage/ceiling/table/schema/outcome change. Exact type target184, actual new layout UNKNOWN."
  }
}
```

Compile-only admission later requires both strictGCC/Clang exactRelease/PIC/native/included-TU layouts/SUs/symbols/disassembly, keeping ALL duplicate rows and true main/caller maxima. Bind current sources/artifacts and matching-source inherited measurements; missing frame is never0. Inspect creator/native/boot/init/auth/quad/edge/multiply/halo factories; private source/canonical/eightoptional returns/fullcatalog/tuple; newctor operation/guard specializations and certifiedsmallroot/residual/validator/primitive arguments-local-pending; originalgraph/leg/rotate/scale/internal sector/rate/scalarcoldinit; reset/emplace/initializer/full currentpendingCells; graphreturn4096/projectprimitive256; pressure/B63/Unit24/owner/hip/half-ray/solid/signed/region descendants; direct/hash/print/check/stream/error/destructors; independentchart/bodyOracle/out-of-lineyaw/libm/interval helpers. Selected branches excluded only by source/saved call-role proof, never anticipated refusal or missingrow. Library512/project primitive/currentpending/frame/global/heap/arena/caller/header/fourLimits are independent. All active BSS/guards/coldinit need symbol and lifetime evidence with explicit numerical-worker versus original-world ownership. Native world prerequisite setup and retained world inputs are measured separately from providerarena4096 and the numerical limits, without assuming warm state or omitting nonzero owners. Public numeric reporting wrappers and generic sign APIs must be classified by actual selected call path, never confused with internal compiler descendants.

All selected numerical working-storage source stages<=49152, bounded source creator<=8192 must be independently corroborated from actual lexical owners without NRVO discount. Numerical physical chains independently fit these limits, not replace frames by sourcehelperpools. Original cold world-loader and retained-world ownership evidence is reported separately without a total-engine limit; it does not substitute for numerical admission. Narrow body margin can fail admission; report missing object/stage instead of weakening audits, enlarging ceilings or fitting source. Actual sizeof/realcapacity output and logs checked separately. Only full paired/publicregistration/source+physical admission/frozen binaries/source permit ONE FIRST per compiler. Observed expectations follow retained outcomes, not this draft.

## 10. Immutable boundary and remaining gates

No implementation is authorized by this documentation. Future separately authorized implementation owns NEW Endpoint04 public/internal/producer/Test plus narrow CMake/strictFP/Test registration and END-only source/canonical, constructor/SELF and pressure adapters. No old public/private header/class/friend/version/helper/definition/result changes. Complete current prefixes are frozen in forecast: Endpoint01 CPP51455bytes SHA22720a7224d08c8009329feb84cabb31abb76c7d32d296ed6f2fc2fe205d6f76; planted CPP374618 SHA6b401090e85e91d4c7a8585abf38d6e3b28c90d8ed750e77be6ae513b462f8cf; boot CPP264868 SHA4695bfbbeae5b100cd6e34924f643c325cd0ac9447b4391516aceceba58d88ee. Every old complete byteprefix remains unchanged. Fresh owncanonical reads newKey and immutable original recipe in its typed ENDsuffix; it may not mint an oldKey or relax oldfriendship. Original compiler/mathematical definitions stay sole original current/body authority.

Proof is conditional; the supported method can remain unavailable or later refuse. Exact Test/schema/resource review and publicregistration precede coding; complete actual both-compiler source/physical admission/freeze/onceFIRST/outcome review/ordinary tests/pinnedformat20/tidy20/native checks/exactHEADCI are separate later gates. No candidate was evaluated, no body/route/gameplay success predicted, no parent/policy closed.
