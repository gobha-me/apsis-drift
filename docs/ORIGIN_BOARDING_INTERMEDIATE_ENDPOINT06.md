# Endpoint06 independent-BASE chart-factor registration — #529

This [#529](https://github.com/gobha-me/apsis-drift/issues/529) contract selects ONE fresh version6 source-generated root-height program from the [same-domain chart-factor proof](ORIGIN_BOARDING_CHART_FACTOR_CONSTRUCTION.md), merged at `bb5b9860e984e4fc36c22af961a071c1c340616a`. First derive and fix the genuine BOTH ankle/reach/roll BASE; only then derive each height floor, chart/roll factor, hip cap and restricted image. Intersect both images with that SAME BASE before selecting one midpoint. This is registration for a separate implementation child. Supported feasibility, actual layouts, frames, capacities, external ancestry and full endpoint acceptance remain UNKNOWN. No source coordinate, sign, angle, derived geometric bound or candidate was evaluated.

## 1. Immutable source and conditional theorem

Candidate `root_y_same_base_chart_factor_both_hip_ankle_reach_roll_slice=0` preserves the original ordered rootX/Z, root-yaw half .125, sole-yaw half0, torso-half−.30, both genuine feet/planes, zero humps, folded arms, lengths, PORT1/16/STAR15/16 share and exact2second hold. Original constants mean their unchanged binary64 bits, particularly `kBoardingSelfHipLengthMetres=0x1.bb0cd605d7512p-3`; no rounded replacement. Authenticate original pelvis/upright ROOT frame, both thigh/root/cylinder/distal identities, regions1/2 and unchanged sole frames. Only the two rootY packets may become `{Y,+0,+0}count1` after all direct checks. No old constructor invocation, pose correction, fallback BASE, second candidate or new numerical inspection API.

For each distinct side keep signed X,Z, independent origin A and W=−Z from the genuine held chart. With h=Y−A, rho²=X²+h² and the positive-knee/F1>0 component, the original nominal knee vector is `K−H=Rs(−F1 U−G1 ez)`, `U=(−X,h,0)/rho`, `F1²+G1²=L1²`. Authenticated yaw0 makes Rs the exact WORLD identity; root yaw is proper nominal rational rotation, not an interval-orthogonality premise. Thus downward axial factor is `s=−uY=(F1/L1)*(h/rho)`, with u=(K−H)/L1. Exact transverse identity is useful to prove the theorem; the later direct check MUST reconstruct WORLD u and `sqrt(uX²+uZ²)` using supported original arithmetic.

BASE `(base_lo,base_hi)` comes from literal O001–157, before any hip domain/cap/factor. G24 fixes its finite nonempty interval. Only O154 writes base_lo and only O157 writes base_hi; no later assignment, runtime saved-original comparison or hidden BASE snapshot exists. For each side, O174–207 earn finite `H>0` from the downward endpoint of `point(base_lo)−point(A.high)`, and `M>=0` from ABS(X).high. For Y in SAME BASE, h>H and |X|<=M. The function h/sqrt(X²+h²) is nondecreasing with h and nonincreasing with |X|; M=0 permits factor1. Supported `H/sqrt(M²+H²)` and original roll factor produce the finite lower factor C actually published/used, with `0<C<=1`. No strict improvement requirement. Using a larger ideal exact maximum in the threshold division would be invalid; the denominator is exactly point(published C).

For `L>Hy>0`, `r>0`, `r<=L<L1`, the original threshold is `s_req=(L*Hy+r*sqrt(L²+r²−Hy²))/(L²+r²)`. Its selected positive root and unsquared sign guard certify the unique root in (0,1) of `L*s−r*sqrt(1−s²)=Hy`. Earn k_bar as an UPPER bound for s_req/C, require `0<k_bar<1`, then earn a LOWER g_cut for `L1*sqrt(1−k_bar²)`. Original positive-turn tau cuts are strengthened by universal signed `p>=upper((W−g_cut)/L2)` and `q<=lower(W/L2)`. No W-positive assumption. For interior p<tau<q, `0<G1=W−L2*tau<g_cut`, so F1/L1>k_bar and s>s_req on SAME BASE. The all-caps strict complement follows conditionally.

Original BASE closed p0/q0 endpoints may have F1=0 and supply values via their original endpoint certificate and explicitly legal nonnegative intersection. The NEW tightened closed p/q cuts instead imply 0<=G1<=g_cut<L1 and therefore F1>0. Straight-turn equality can still occur at a closed endpoint. Neither endpoint supplies derivative authority; strict radial monotonicity is asserted only inside the regular positive-turn component. Inward radial/height projections retain each original A/X and supported upper/lower directions. They may extend outside BASE as VALUE images; neither factor theorem nor hip guarantee is extended there. Literal final intersection is BASE∩PORT-image∩STAR-image. Emptiness, nonpositive floor, unsupported factor, k>=1 or inconclusive cuts mean this one conservative construction is unavailable, never all-height impossibility or collision.

## 2. Fresh schema, API and current-call authority

Public API is `assess_origin_boarding_intermediate_endpoint06(const OriginBoardingIntermediatePauseSupport&, BoardingIntermediateEndpoint06Candidate)->BoardingIntermediateEndpoint06Expected`; private bounded API is `detail::intermediate_endpoint06_bounded(provider,Candidate,BoardingIntermediateEndpoint06Limits={})`. Public Candidate has only the enum0 above. The pure sizeof helper `boarding_intermediate_endpoint06_required_output_bytes()` returns `2*sizeof(Expected)+sizeof(Cell)`; it performs no materialization/query. Inputs never include Y, report, claimed axis, cut, witness or old Key.

All Endpoint05 public field order/types, State0..5, Condition0..41, SelfStage0..20, Certificate0..6, Counters, Refusal and Cell are repeated under the fresh Endpoint06 prefix. Only version6, new Candidate, selected Expected ceiling1744 and the explicitly expanded Slice below differ. Cell has no shadow Slice/factor/base/cut record; unchanged Cell ceiling11032 is a source forecast requiring fresh full member/layout corroboration. Expected remains `std::expected<Diagnostic,std::string>` and Diagnostic's constructor takes provider by const-reference. SliceCondition0..16 retain old ordinals; append `base_interval_unavailable=17`, `height_floor_unavailable=18`, `chart_factor_unavailable=19`.

```cpp
struct BoardingIntermediateEndpoint06SliceEvidence {
  std::array<std::uint64_t,8> operation_attempted{}, operation_written{};
  std::array<std::uint64_t,2> guard_attempted{}, guard_written{};
  BoardingFootSiteScalarBounds limiting_bound;
  double y{}, lo{}, hi{}, base_lo{}, base_hi{};
  std::array<double,2> factor_lower{};
  std::uint16_t operation{65535};
  std::uint8_t guard{255}, side{255};
  BoardingIntermediateEndpoint06SliceCondition condition{};
  bool arithmetic_supported{}, complete{};
  std::uint8_t preflight_guards{};
  std::uint16_t zero_mask{};
};
```

Limits repeats Endpoint05's five original phase caps and other20 field names/order; only construction_guards75 and construction_operations458 change. The complete literal25 defaults are embedded below. All limits lowerable only; raised controls are invalid-limits tests, not raised work admission. Fixed public FP-first1 is not lowerable. Fresh source-enroll/construct/canonical/template-valid/current-cell/pressure/SELF/refuse/charge/definition-charge declarations have the corresponding Endpoint05 signatures with Endpoint06 types. Old files/headers/friends/issuers remain byte-preserved; source-only END adapters may call original source/math helpers without issuing old typed evidence/capabilities.

Fresh ProgramKey stores generatedY/version6/Candidate0 and is privately constructed only by new bounded producer after complete constructor. Fresh AdmissionContext anchors actual owner/Data/parts/owned Request/new Key and deletes copy/move/assignments. Fresh CurrentToken anchors currentContext/owner/current Cell/Request/Data; only fresh current_cell can mint it after actual accepted original graph/current P251. Source/constructor/BASE/factor publication issues no token. Context and Token each retain its complete40-byte source forecast. Canonical generation consumes this fresh Key only; no old Key reminting or accepted report admission.

Every new Slice/Refusal/Diagnostic.stop operation field/setter/copy/hash/print is uint16/default65535. The source-only adapter translates old fullRefusal operation `old==uint8_t{255}?uint16_t{65535}:uint16_t(old)`; original S64 cursor<=63 makes UNSET unambiguous. Actual fresh cursor255 is valid O256. Nested unchanged PhaseRefusal, Unit24, guard/side/region/axis keep their distinct255 protocols, with local translation only when an unset Unit24 cursor is explicitly forwarded to the shared operation field. No narrowing of458 counters or terminal457.

Ordinary sufficient failures give unresolved/slice_unavailable and the literal SliceCondition; identity gives slice_identity, unsupported FP/nonfinite/root/divide gives unsupported_arithmetic. Operation/guard charge failure gives capacity/slice_operation_capacity or slice_guard_capacity with corresponding SliceCondition, retaining prior supported evidence and setting no next attempted bit. `arithmetic_supported` means earned supported arithmetic, not constructor/domain/graph acceptance. Successful constructor is complete only after all458 written operations/75 written guards and successful final validator+FP; later graph refusal preserves this actual Slice without promoting later flags.

## 3. Charged primitive and evidence protocol

The exact ADD/SUB/MUL/DIV/NEG/ABS/SQUARE/CONST/SMALL_ROOT/MAX/MIN/POINT_UPPER/POINT_LOWER/RN implementations remain Endpoint04's supported rules. DIV is reciprocal enclosure then multiplication, never a singleton reciprocal shortcut. SMALL_ROOT retains bounded exact residual correction, exact0/domain handling and qualified FP checks. ENDPOINT_DIV is unchanged ep4_unit_divide: genuine fixed L1>0, exact zero maps to point0; otherwise low/L1/high/L1 with finite checks and outward down/up, one charged operation. No solver, trig fitting, tolerance, iteration retry or unsupported repair.

Each operation first sets fresh stage20, upcoming cursor(row−1)/side and clears only row limiting scratch; then charges; only after successful charge executes its deferred expression. Supported valid return is assigned to the literal slot, then explicit scalar publication occurs and written bit is earned. Primitive current/pending Interval returns are separately reserved. Unsupported return earns attempted but not written. Guards set stage19/upcoming cursor(row−1)/side, charge, earn attempted, then read predicates in literal order; only a true full predicate earns written. First hard unsupported stops before ordinary comparisons. First supported inequality failure attaches only the actual retained supported bound, not a future/unread value.

Rows are one-based; operation/guard cursor and bit are row−1. Operation word/bit are `(row−1)/64`, `(row−1)%64`; guard uses the same formula independently. Terminal operation457/guard74 are distinct from sentinels65535/255. Complete operation words are seven UINT64_MAX and final0x3ff; complete guard words are UINT64_MAX and final0x7ff. All unused bits zero. Canonical zero_mask bits0..7 preserve original BASE branches; bits8/9 PORT new p/q thigh intersection,10/11 STAR,12/13 new PORT/STAR ankle MAX_ZERO. VALUE0 preserves positive operand, VALUE1 selects actual canonical+0. A bit is meaningful only if its charged operation is written.

Scalar readability follows only its publication written bit: BASElo O154, BASEhi O157, factorsPORT O183/STAR O200, final lo O303/hi O305/Y O308. Certified BASE additionally needs G24; same-BASE positive factor claims need G30–32/G36–38 plus common prerequisites; final nonempty domain needs G56 and stored Y needs G57–59. Default/unearned doubles are not observations. H/M/chart quotients/caps and endpoint certificate bits are helper-local, not returned report fields. Erased whole Interval values are not reread: successful written primitives establish extraction provenance, subsequent guards inspect retained singleton/double plus the named live denominator/current interval. No shadow history is allocated.

## 4. Literal458 operations and destination order

Common side255, PORT0, STAR1. Every operand name below denotes the same-side value established by earlier rows or the fixed authenticated primitive constant. WHOLE means the full supported Interval, point means exactly the listed supported endpoint/scalar. Old O09/O10 references occur only in unchanged BASE prefix and keep their literal original row meaning. Assignment/publication is inside the written row, with no extra charged math hidden. Ties choose first operand. Each row's slot is normative; overwrite only after its listed operand's last consumer.

| Row | Side | Charged expression | Destination |
| --- | --- | --- | --- |
| O001 | common | CONST rootX | input[0] |
| O002 | common | CONST rootZ | input[1] |
| O003 | common | CONST PORT soleX | input[2] |
| O004 | common | CONST PORT soleY | input[3] |
| O005 | common | CONST PORT soleZ | input[4] |
| O006 | common | CONST STAR soleX | input[5] |
| O007 | common | CONST STAR soleY | input[6] |
| O008 | common | CONST STAR soleZ | input[7] |
| O009 | common | SQUARE point(rootYawHalf) | yaw[0] |
| O010 | common | ADD point1+O09 | yaw[1] |
| O011 | common | SUB point1−O09 | yaw[2] |
| O012 | common | DIV O11/O10 →cos | yaw[3] |
| O013 | common | MUL point2*point(rootYawHalf) | yaw[2] |
| O014 | common | DIV O13/O10 →sin | yaw[4] |
| O015 | common | NEG O14 →negative_sin | yaw[5] |
| O016 | PORT | MUL point(−.14)*cos →offsetX | endpoint[12] |
| O017 | PORT | MUL point(−.14)*negative_sin →offsetZ | endpoint[13] |
| O018 | PORT | ADD rootX+offsetX →hipX | endpoint[14] |
| O019 | PORT | ADD rootZ+offsetZ →hipZ | endpoint[15] |
| O020 | PORT | SUB soleX−hipX →retained X_PORT | chart[0] |
| O021 | PORT | SUB soleZ−hipZ →retained Z_PORT | chart[1] |
| O022 | PORT | SQUARE X_PORT →retained X2_PORT | chart[2] |
| O023 | PORT | SQUARE Z_PORT →retained Z2_PORT | chart[3] |
| O024 | PORT | ADD X2_PORT+Z2_PORT →retained C_PORT | chart[4] |
| O025 | PORT | ADD soleY+point.1 →retained A_PORT | chart[5] |
| O026 | STAR | MUL point(+.14)*cos →offsetX | endpoint[12] |
| O027 | STAR | MUL point(+.14)*negative_sin →offsetZ | endpoint[13] |
| O028 | STAR | ADD rootX+offsetX →hipX | endpoint[14] |
| O029 | STAR | ADD rootZ+offsetZ →hipZ | endpoint[15] |
| O030 | STAR | SUB soleX−hipX →retained X_STAR | chart[8] |
| O031 | STAR | SUB soleZ−hipZ →retained Z_STAR | chart[9] |
| O032 | STAR | SQUARE X_STAR →retained X2_STAR | chart[10] |
| O033 | STAR | SQUARE Z_STAR →retained Z2_STAR | chart[11] |
| O034 | STAR | ADD X2_STAR+Z2_STAR →retained C_STAR | chart[12] |
| O035 | STAR | ADD soleY+point.1 →retained A_STAR | chart[13] |
| O036 | common | ADD pointL1+pointL2 →retained Lsum | link[0] |
| O037 | common | SQUARE Lsum →M | link[1] |
| O038 | common | SUB pointL1−pointL2 →Ldiff | link[2] |
| O039 | common | SQUARE Ldiff →m | link[3] |
| O040 | common | SQUARE pointL1 →L1sq | link[4] |
| O041 | common | SQUARE pointL2 →L2sq | link[5] |
| O042 | common | SUB L1sq−L2sq →Delta | link[6] |
| O043 | common | SMALL_ROOT point3 →sqrt3 | yaw[0] |
| O044 | common | DIV sqrt3/point2 →sqrt3half | link[7] |
| O045 | common | SUB point2−sqrt3 →b | link[8] |
| O046 | PORT | NEG Z_PORT →W | cut[0] |
| O047 | PORT | SUB W−pointL1 →cutlowNum | endpoint[12] |
| O048 | PORT | DIV cutlowNum/pointL2 →cutlow | cut[1] |
| O049 | PORT | ADD W+pointL1 →cuthiNum | endpoint[12] |
| O050 | PORT | DIV cuthiNum/pointL2 →cuthi | cut[2] |
| O051 | PORT | DIV W/Lsum →cutturn | cut[3] |
| O052 | PORT | MAX(point−.5,point(cutlow.high)) →p (first on equality) | cut[4] |
| O053 | PORT | MIN(point+.5,point(cuthi.low)) →q0 (first on equality) | cut[5] |
| O054 | PORT | MIN(q0,point(cutturn.low)) →q (first on equality) | cut[5] |
| O055 | PORT | SQUARE point(p) →tau2 | endpoint[12] |
| O056 | PORT | SUB point1−tau2 →cosRad | endpoint[13] |
| O057 | PORT | SMALL_ROOT WHOLE cosRad →cosShin | endpoint[13] |
| O058 | PORT | MUL pointL2*cosShin →F2endpoint | endpoint[14] |
| O059 | PORT | MUL pointL2*point(p) →tauL2 | endpoint[15] |
| O060 | PORT | SUB W−tauL2 →G1endpoint | endpoint[15] |
| O061 | PORT | SQUARE G1endpoint →g1sq | endpoint[16] |
| O062 | PORT | SUB L1sq−g1sq →thighRad | endpoint[16] |
| O063 | PORT | INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 2 | endpoint[16] |
| O064 | PORT | SMALL_ROOT WHOLE clip →F1endpoint | endpoint[17] |
| O065 | PORT | ADD F1endpoint+F2endpoint →rho_p | endpoint[0] |
| O066 | PORT | SQUARE point(q) →tau2 | endpoint[12] |
| O067 | PORT | SUB point1−tau2 →cosRad | endpoint[13] |
| O068 | PORT | SMALL_ROOT WHOLE cosRad →cosShin | endpoint[13] |
| O069 | PORT | MUL pointL2*cosShin →F2endpoint | endpoint[14] |
| O070 | PORT | MUL pointL2*point(q) →tauL2 | endpoint[15] |
| O071 | PORT | SUB W−tauL2 →G1endpoint | endpoint[15] |
| O072 | PORT | SQUARE G1endpoint →g1sq | endpoint[16] |
| O073 | PORT | SUB L1sq−g1sq →thighRad | endpoint[16] |
| O074 | PORT | INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 3 | endpoint[16] |
| O075 | PORT | SMALL_ROOT WHOLE clip →F1endpoint | endpoint[17] |
| O076 | PORT | ADD F1endpoint+F2endpoint →rho_q | endpoint[1] |
| O077 | PORT | POINT_UPPER rho_p →u | endpoint[12] |
| O078 | PORT | POINT_LOWER rho_q →v | endpoint[13] |
| O079 | PORT | SQUARE pointu →u2 | endpoint[12] |
| O080 | PORT | SUB u2−point(X2_PORT.low) →ankleLowerRad | endpoint[12] |
| O081 | PORT | MAX_ZERO ankleLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 6 | endpoint[12] |
| O082 | PORT | SMALL_ROOT point(selected) →ankleLowerRoot | endpoint[12] |
| O083 | PORT | ADD point(A_PORT.high)+point(ankleLowerRoot.high) →ankleLower (retain.high) | endpoint[2] |
| O084 | PORT | SQUARE pointv →v2 | endpoint[12] |
| O085 | PORT | SUB v2−point(X2_PORT.high) →ankleUpperRad | endpoint[12] |
| O086 | PORT | SMALL_ROOT point(ankleUpperRad.low) →ankleUpperRoot | endpoint[12] |
| O087 | PORT | ADD point(A_PORT.low)+point(ankleUpperRoot.low) →ankleUpper (retain.low) | endpoint[3] |
| O088 | PORT | SUB point(m.high)−point(C_PORT.low) →reachLowerRad | endpoint[12] |
| O089 | PORT | MAX_ZERO reachLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 0 | endpoint[12] |
| O090 | PORT | SMALL_ROOT point(selected) →reachLowerRoot | endpoint[12] |
| O091 | PORT | ADD point(A_PORT.high)+point(reachLowerRoot.high) →reachLower (retain.high) | endpoint[4] |
| O092 | PORT | SUB point(M.low)−point(C_PORT.high) →reachUpperRad | endpoint[12] |
| O093 | PORT | SMALL_ROOT point(reachUpperRad.low) →reachUpperRoot | endpoint[12] |
| O094 | PORT | ADD point(A_PORT.low)+point(reachUpperRoot.low) →reachUpper (retain.low) | endpoint[5] |
| O095 | PORT | ABS WHOLE X_PORT →retained absX | chart[6] |
| O096 | PORT | DIV WHOLE absX/WHOLE b →rollQuotient | endpoint[12] |
| O097 | PORT | ADD point(A_PORT.high)+point(rollQuotient.high) →rollLower (retain.high) | chart[7] |
| O098 | STAR | NEG Z_STAR →W | cut[6] |
| O099 | STAR | SUB W−pointL1 →cutlowNum | endpoint[12] |
| O100 | STAR | DIV cutlowNum/pointL2 →cutlow | cut[7] |
| O101 | STAR | ADD W+pointL1 →cuthiNum | endpoint[12] |
| O102 | STAR | DIV cuthiNum/pointL2 →cuthi | cut[8] |
| O103 | STAR | DIV W/Lsum →cutturn | cut[9] |
| O104 | STAR | MAX(point−.5,point(cutlow.high)) →p (first on equality) | cut[10] |
| O105 | STAR | MIN(point+.5,point(cuthi.low)) →q0 (first on equality) | cut[11] |
| O106 | STAR | MIN(q0,point(cutturn.low)) →q (first on equality) | cut[11] |
| O107 | STAR | SQUARE point(p) →tau2 | endpoint[12] |
| O108 | STAR | SUB point1−tau2 →cosRad | endpoint[13] |
| O109 | STAR | SMALL_ROOT WHOLE cosRad →cosShin | endpoint[13] |
| O110 | STAR | MUL pointL2*cosShin →F2endpoint | endpoint[14] |
| O111 | STAR | MUL pointL2*point(p) →tauL2 | endpoint[15] |
| O112 | STAR | SUB W−tauL2 →G1endpoint | endpoint[15] |
| O113 | STAR | SQUARE G1endpoint →g1sq | endpoint[16] |
| O114 | STAR | SUB L1sq−g1sq →thighRad | endpoint[16] |
| O115 | STAR | INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 4 | endpoint[16] |
| O116 | STAR | SMALL_ROOT WHOLE clip →F1endpoint | endpoint[17] |
| O117 | STAR | ADD F1endpoint+F2endpoint →rho_p | endpoint[6] |
| O118 | STAR | SQUARE point(q) →tau2 | endpoint[12] |
| O119 | STAR | SUB point1−tau2 →cosRad | endpoint[13] |
| O120 | STAR | SMALL_ROOT WHOLE cosRad →cosShin | endpoint[13] |
| O121 | STAR | MUL pointL2*cosShin →F2endpoint | endpoint[14] |
| O122 | STAR | MUL pointL2*point(q) →tauL2 | endpoint[15] |
| O123 | STAR | SUB W−tauL2 →G1endpoint | endpoint[15] |
| O124 | STAR | SQUARE G1endpoint →g1sq | endpoint[16] |
| O125 | STAR | SUB L1sq−g1sq →thighRad | endpoint[16] |
| O126 | STAR | INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 5 | endpoint[16] |
| O127 | STAR | SMALL_ROOT WHOLE clip →F1endpoint | endpoint[17] |
| O128 | STAR | ADD F1endpoint+F2endpoint →rho_q | endpoint[7] |
| O129 | STAR | POINT_UPPER rho_p →u | endpoint[12] |
| O130 | STAR | POINT_LOWER rho_q →v | endpoint[13] |
| O131 | STAR | SQUARE pointu →u2 | endpoint[12] |
| O132 | STAR | SUB u2−point(X2_STAR.low) →ankleLowerRad | endpoint[12] |
| O133 | STAR | MAX_ZERO ankleLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 7 | endpoint[12] |
| O134 | STAR | SMALL_ROOT point(selected) →ankleLowerRoot | endpoint[12] |
| O135 | STAR | ADD point(A_STAR.high)+point(ankleLowerRoot.high) →ankleLower (retain.high) | endpoint[8] |
| O136 | STAR | SQUARE pointv →v2 | endpoint[12] |
| O137 | STAR | SUB v2−point(X2_STAR.high) →ankleUpperRad | endpoint[12] |
| O138 | STAR | SMALL_ROOT point(ankleUpperRad.low) →ankleUpperRoot | endpoint[12] |
| O139 | STAR | ADD point(A_STAR.low)+point(ankleUpperRoot.low) →ankleUpper (retain.low) | endpoint[9] |
| O140 | STAR | SUB point(m.high)−point(C_STAR.low) →reachLowerRad | endpoint[12] |
| O141 | STAR | MAX_ZERO reachLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 1 | endpoint[12] |
| O142 | STAR | SMALL_ROOT point(selected) →reachLowerRoot | endpoint[12] |
| O143 | STAR | ADD point(A_STAR.high)+point(reachLowerRoot.high) →reachLower (retain.high) | endpoint[10] |
| O144 | STAR | SUB point(M.low)−point(C_STAR.high) →reachUpperRad | endpoint[12] |
| O145 | STAR | SMALL_ROOT point(reachUpperRad.low) →reachUpperRoot | endpoint[12] |
| O146 | STAR | ADD point(A_STAR.low)+point(reachUpperRoot.low) →reachUpper (retain.low) | endpoint[11] |
| O147 | STAR | ABS WHOLE X_STAR →retained absX | chart[14] |
| O148 | STAR | DIV WHOLE absX/WHOLE b →rollQuotient | endpoint[12] |
| O149 | STAR | ADD point(A_STAR.high)+point(rollQuotient.high) →rollLower (retain.high) | chart[15] |
| O150 | common | MAX ankleLower_PORT,ankleLower_STAR →lower; first operand on equality | endpoint[12] |
| O151 | common | MAX lower,reachLower_PORT →lower; first operand on equality | endpoint[12] |
| O152 | common | MAX lower,reachLower_STAR →lower; first operand on equality | endpoint[12] |
| O153 | common | MAX lower,rollLower_PORT →lower; first operand on equality | endpoint[12] |
| O154 | common | MAX lower,rollLower_STAR →publish slice.base_lo; first operand on equality; base_lo = returned Interval.high; readable if O154 written; BASE authority only after G24 written | endpoint[12] |
| O155 | common | MIN ankleUpper_PORT,ankleUpper_STAR →upper; first operand on equality | endpoint[12] |
| O156 | common | MIN upper,reachUpper_PORT →upper; first operand on equality | endpoint[12] |
| O157 | common | MIN upper,reachUpper_STAR →publish slice.base_hi; first operand on equality; base_hi = returned Interval.low; readable if O157 written; BASE authority only after G24 written | endpoint[12] |
| O158 | common | SQUARE WHOLE b →rollFactorSquare | direct[0] |
| O159 | common | ADD point1+rollFactorSquare →rollNormSquared | direct[0] |
| O160 | common | SMALL_ROOT WHOLE rollNormSquared →rollNorm | direct[0] |
| O161 | common | DIV point1/WHOLE rollNorm →retained c_roll | direct[1] |
| O162 | common | SQUARE pointL →slabSquared | direct[2] |
| O163 | common | SQUARE pointR →radiusSquared | direct[3] |
| O164 | common | SQUARE pointHy →halfHeightSquared | direct[4] |
| O165 | common | ADD slabSquared+radiusSquared →thresholdDen | direct[2] |
| O166 | common | SUB thresholdDen−halfHeightSquared →thresholdRad | direct[3] |
| O167 | common | SMALL_ROOT WHOLE thresholdRad →thresholdRoot | direct[3] |
| O168 | common | MUL pointR*thresholdRoot →radiusThresholdRoot | direct[3] |
| O169 | common | MUL pointL*pointHy →slabHalfHeight | direct[4] |
| O170 | common | ADD slabHalfHeight+radiusThresholdRoot →thresholdNum | direct[3] |
| O171 | common | DIV thresholdNum/thresholdDen →s_req | direct[3] |
| O172 | common | MUL pointL*WHOLE s_req →thresholdAxial | direct[4] |
| O173 | common | SUB thresholdAxial−pointHy →thresholdUnsquaredSign | direct[4] |
| O174 | PORT | SUB point(slice.base_lo)−point(A_PORT.high) →floorInterval | endpoint[12] |
| O175 | PORT | POINT_LOWER floorInterval →singleton H_PORT; selected[4] = H low, identical scalar used for BOTH numerator and denominator | endpoint[12] |
| O176 | PORT | POINT_UPPER WHOLE absX_PORT →singleton Xmax_PORT; selected[5] = Xmax high | endpoint[13] |
| O177 | PORT | SQUARE point(Xmax_PORT) →lateralSquare | direct[4] |
| O178 | PORT | SQUARE point(H_PORT) →floorSquare | direct[5] |
| O179 | PORT | ADD lateralSquare+floorSquare →chartNormSquared | direct[4] |
| O180 | PORT | SMALL_ROOT WHOLE chartNormSquared →chartNorm | direct[4] |
| O181 | PORT | DIV point(H_PORT)/WHOLE chartNorm →chartFactorInterval | endpoint[14] |
| O182 | PORT | POINT_LOWER chartFactorInterval →singleton chartLower | endpoint[14] |
| O183 | PORT | MAX point(c_roll.low),point(chartLower.low) →publish slice.factor_lower[0]; first operand wins equality; slice.factor_lower[0] = returned Interval.low, finite lower factor ACTUALLY USED; factor authority requires its own range guard | endpoint[15] |
| O184 | PORT | DIV WHOLE s_req/point(slice.factor_lower[0]) →kInterval | direct[4] |
| O185 | PORT | POINT_UPPER kInterval →singleton k_bar; selected[6] = k_bar high | direct[4] |
| O186 | PORT | SQUARE point(k_bar) →kSquare | direct[4] |
| O187 | PORT | SUB point1−kSquare →gRad | direct[4] |
| O188 | PORT | SMALL_ROOT WHOLE gRad →gRoot | direct[4] |
| O189 | PORT | MUL pointL1*WHOLE gRoot →gInterval | direct[4] |
| O190 | PORT | POINT_LOWER gInterval →singleton g_cut_PORT; cuts[0] = g_cut low, retained through this side cut/endpoint certificate consumers | direct[4] |
| O191 | STAR | SUB point(slice.base_lo)−point(A_STAR.high) →floorInterval | endpoint[12] |
| O192 | STAR | POINT_LOWER floorInterval →singleton H_STAR; selected[4] = H low, identical scalar used for BOTH numerator and denominator | endpoint[12] |
| O193 | STAR | POINT_UPPER WHOLE absX_STAR →singleton Xmax_STAR; selected[5] = Xmax high | endpoint[13] |
| O194 | STAR | SQUARE point(Xmax_STAR) →lateralSquare | direct[4] |
| O195 | STAR | SQUARE point(H_STAR) →floorSquare | direct[5] |
| O196 | STAR | ADD lateralSquare+floorSquare →chartNormSquared | direct[4] |
| O197 | STAR | SMALL_ROOT WHOLE chartNormSquared →chartNorm | direct[4] |
| O198 | STAR | DIV point(H_STAR)/WHOLE chartNorm →chartFactorInterval | endpoint[14] |
| O199 | STAR | POINT_LOWER chartFactorInterval →singleton chartLower | endpoint[14] |
| O200 | STAR | MAX point(c_roll.low),point(chartLower.low) →publish slice.factor_lower[1]; first operand wins equality; slice.factor_lower[1] = returned Interval.low, finite lower factor ACTUALLY USED; factor authority requires its own range guard | endpoint[15] |
| O201 | STAR | DIV WHOLE s_req/point(slice.factor_lower[1]) →kInterval | direct[4] |
| O202 | STAR | POINT_UPPER kInterval →singleton k_bar; selected[6] = k_bar high | direct[4] |
| O203 | STAR | SQUARE point(k_bar) →kSquare | direct[4] |
| O204 | STAR | SUB point1−kSquare →gRad | direct[4] |
| O205 | STAR | SMALL_ROOT WHOLE gRad →gRoot | direct[4] |
| O206 | STAR | MUL pointL1*WHOLE gRoot →gInterval | direct[4] |
| O207 | STAR | POINT_LOWER gInterval →singleton g_cut_STAR; cuts[1] = g_cut low, retained through this side cut/endpoint certificate consumers | direct[4] |
| O208 | PORT | NEG Z_PORT →W | cut[0] |
| O209 | PORT | SUB W−pointL1 →cutlowNum | endpoint[12] |
| O210 | PORT | DIV cutlowNum/pointL2 →cutlow | cut[1] |
| O211 | PORT | ADD W+pointL1 →cuthiNum | endpoint[12] |
| O212 | PORT | DIV cuthiNum/pointL2 →cuthi | cut[2] |
| O213 | PORT | DIV W/Lsum →cutturn | cut[3] |
| O214 | PORT | MAX(point−.5,point(cutlow.high)) →base_p (first on equality) | cut[4] |
| O215 | PORT | MIN(point+.5,point(cuthi.low)) →q0 (first on equality) | cut[5] |
| O216 | PORT | MIN(q0,point(cutturn.low)) →base_q (first on equality) | cut[5] |
| O217 | PORT | SUB WHOLE W−point(g_cut_PORT) →hipCutNum | endpoint[12] |
| O218 | PORT | DIV WHOLE hipCutNum/pointL2 →hipCutLower | endpoint[12] |
| O219 | PORT | MAX(base_p,point(hipCutLower.high)) →p; first operand wins equality | cut[4] |
| O220 | PORT | DIV WHOLE W/pointL2 →hipCutUpper | endpoint[13] |
| O221 | PORT | MIN(base_q,point(hipCutUpper.low)) →q; first operand wins equality | cut[5] |
| O222 | PORT | SQUARE point(p) →tau2 | endpoint[12] |
| O223 | PORT | SUB point1−tau2 →cosRad | endpoint[13] |
| O224 | PORT | SMALL_ROOT WHOLE cosRad →cosShin | endpoint[13] |
| O225 | PORT | MUL pointL2*cosShin →F2endpoint | endpoint[14] |
| O226 | PORT | MUL pointL2*point(p) →tauL2 | endpoint[15] |
| O227 | PORT | SUB W−tauL2 →G1endpoint | endpoint[15] |
| O228 | PORT | SQUARE G1endpoint →g1sq | endpoint[16] |
| O229 | PORT | SUB L1sq−g1sq →thighRad | endpoint[16] |
| O230 | PORT | INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 8 | endpoint[16] |
| O231 | PORT | SMALL_ROOT WHOLE clip →F1endpoint | endpoint[17] |
| O232 | PORT | ADD F1endpoint+F2endpoint →rho_p | endpoint[0] |
| O233 | PORT | SQUARE point(q) →tau2 | endpoint[12] |
| O234 | PORT | SUB point1−tau2 →cosRad | endpoint[13] |
| O235 | PORT | SMALL_ROOT WHOLE cosRad →cosShin | endpoint[13] |
| O236 | PORT | MUL pointL2*cosShin →F2endpoint | endpoint[14] |
| O237 | PORT | MUL pointL2*point(q) →tauL2 | endpoint[15] |
| O238 | PORT | SUB W−tauL2 →G1endpoint | endpoint[15] |
| O239 | PORT | SQUARE G1endpoint →g1sq | endpoint[16] |
| O240 | PORT | SUB L1sq−g1sq →thighRad | endpoint[16] |
| O241 | PORT | INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 9 | endpoint[16] |
| O242 | PORT | SMALL_ROOT WHOLE clip →F1endpoint | endpoint[17] |
| O243 | PORT | ADD F1endpoint+F2endpoint →rho_q | endpoint[1] |
| O244 | PORT | POINT_UPPER rho_p →u | endpoint[12] |
| O245 | PORT | POINT_LOWER rho_q →v | endpoint[13] |
| O246 | PORT | SQUARE pointu →u2 | endpoint[12] |
| O247 | PORT | SUB u2−point(X2_PORT.low) →ankleLowerRad | endpoint[12] |
| O248 | PORT | MAX_ZERO ankleLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 12 | endpoint[12] |
| O249 | PORT | SMALL_ROOT point(selected) →ankleLowerRoot | endpoint[12] |
| O250 | PORT | ADD point(A_PORT.high)+point(ankleLowerRoot.high) →ankleLower (retain.high); cuts[2] = side tightened ankleLower.high | endpoint[2] |
| O251 | PORT | SQUARE pointv →v2 | endpoint[12] |
| O252 | PORT | SUB v2−point(X2_PORT.high) →ankleUpperRad | endpoint[12] |
| O253 | PORT | SMALL_ROOT point(ankleUpperRad.low) →ankleUpperRoot | endpoint[12] |
| O254 | PORT | ADD point(A_PORT.low)+point(ankleUpperRoot.low) →ankleUpper (retain.low); cuts[3] = side tightened ankleUpper.low | endpoint[3] |
| O255 | STAR | NEG Z_STAR →W | cut[6] |
| O256 | STAR | SUB W−pointL1 →cutlowNum | endpoint[12] |
| O257 | STAR | DIV cutlowNum/pointL2 →cutlow | cut[7] |
| O258 | STAR | ADD W+pointL1 →cuthiNum | endpoint[12] |
| O259 | STAR | DIV cuthiNum/pointL2 →cuthi | cut[8] |
| O260 | STAR | DIV W/Lsum →cutturn | cut[9] |
| O261 | STAR | MAX(point−.5,point(cutlow.high)) →base_p (first on equality) | cut[10] |
| O262 | STAR | MIN(point+.5,point(cuthi.low)) →q0 (first on equality) | cut[11] |
| O263 | STAR | MIN(q0,point(cutturn.low)) →base_q (first on equality) | cut[11] |
| O264 | STAR | SUB WHOLE W−point(g_cut_STAR) →hipCutNum | endpoint[12] |
| O265 | STAR | DIV WHOLE hipCutNum/pointL2 →hipCutLower | endpoint[12] |
| O266 | STAR | MAX(base_p,point(hipCutLower.high)) →p; first operand wins equality | cut[10] |
| O267 | STAR | DIV WHOLE W/pointL2 →hipCutUpper | endpoint[13] |
| O268 | STAR | MIN(base_q,point(hipCutUpper.low)) →q; first operand wins equality | cut[11] |
| O269 | STAR | SQUARE point(p) →tau2 | endpoint[12] |
| O270 | STAR | SUB point1−tau2 →cosRad | endpoint[13] |
| O271 | STAR | SMALL_ROOT WHOLE cosRad →cosShin | endpoint[13] |
| O272 | STAR | MUL pointL2*cosShin →F2endpoint | endpoint[14] |
| O273 | STAR | MUL pointL2*point(p) →tauL2 | endpoint[15] |
| O274 | STAR | SUB W−tauL2 →G1endpoint | endpoint[15] |
| O275 | STAR | SQUARE G1endpoint →g1sq | endpoint[16] |
| O276 | STAR | SUB L1sq−g1sq →thighRad | endpoint[16] |
| O277 | STAR | INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 10 | endpoint[16] |
| O278 | STAR | SMALL_ROOT WHOLE clip →F1endpoint | endpoint[17] |
| O279 | STAR | ADD F1endpoint+F2endpoint →rho_p | endpoint[6] |
| O280 | STAR | SQUARE point(q) →tau2 | endpoint[12] |
| O281 | STAR | SUB point1−tau2 →cosRad | endpoint[13] |
| O282 | STAR | SMALL_ROOT WHOLE cosRad →cosShin | endpoint[13] |
| O283 | STAR | MUL pointL2*cosShin →F2endpoint | endpoint[14] |
| O284 | STAR | MUL pointL2*point(q) →tauL2 | endpoint[15] |
| O285 | STAR | SUB W−tauL2 →G1endpoint | endpoint[15] |
| O286 | STAR | SQUARE G1endpoint →g1sq | endpoint[16] |
| O287 | STAR | SUB L1sq−g1sq →thighRad | endpoint[16] |
| O288 | STAR | INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 11 | endpoint[16] |
| O289 | STAR | SMALL_ROOT WHOLE clip →F1endpoint | endpoint[17] |
| O290 | STAR | ADD F1endpoint+F2endpoint →rho_q | endpoint[7] |
| O291 | STAR | POINT_UPPER rho_p →u | endpoint[12] |
| O292 | STAR | POINT_LOWER rho_q →v | endpoint[13] |
| O293 | STAR | SQUARE pointu →u2 | endpoint[12] |
| O294 | STAR | SUB u2−point(X2_STAR.low) →ankleLowerRad | endpoint[12] |
| O295 | STAR | MAX_ZERO ankleLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 13 | endpoint[12] |
| O296 | STAR | SMALL_ROOT point(selected) →ankleLowerRoot | endpoint[12] |
| O297 | STAR | ADD point(A_STAR.high)+point(ankleLowerRoot.high) →ankleLower (retain.high); cuts[4] = side tightened ankleLower.high | endpoint[8] |
| O298 | STAR | SQUARE pointv →v2 | endpoint[12] |
| O299 | STAR | SUB v2−point(X2_STAR.high) →ankleUpperRad | endpoint[12] |
| O300 | STAR | SMALL_ROOT point(ankleUpperRad.low) →ankleUpperRoot | endpoint[12] |
| O301 | STAR | ADD point(A_STAR.low)+point(ankleUpperRoot.low) →ankleUpper (retain.low); cuts[5] = side tightened ankleUpper.low | endpoint[9] |
| O302 | common | MAX point(slice.base_lo),point(cuts[2]) →lower; first operand wins equality | endpoint[12] |
| O303 | common | MAX lower,point(cuts[4]) →publish slice.lo; first operand wins equality; slice.lo = returned Interval.high | endpoint[12] |
| O304 | common | MIN point(slice.base_hi),point(cuts[3]) →upper; first operand wins equality | endpoint[12] |
| O305 | common | MIN upper,point(cuts[5]) →publish slice.hi; first operand wins equality; slice.hi = returned Interval.low | endpoint[12] |
| O306 | common | RN finite SUB hi−lo →width | endpoint[12] |
| O307 | common | RN finite MUL width*.5 →halfWidth | endpoint[12] |
| O308 | common | RN finite ADD lo+halfWidth →ONE storedY; publish slice.y; slice.y = ONE returned RN scalar; no alternative height or adjustment | endpoint[12] |
| O309 | PORT | SUB pointY−WHOLE A_PORT →h | direct[0] |
| O310 | PORT | SQUARE h →h2 | direct[1] |
| O311 | PORT | ADD WHOLE X2_PORT+h2 →rho2 | direct[1] |
| O312 | PORT | ADD rho2+WHOLE Z2_PORT →D | direct[2] |
| O313 | PORT | SMALL_ROOT WHOLE rho2 →rho | direct[3] |
| O314 | PORT | ADD WHOLE Delta+D →alphaNum | direct[4] |
| O315 | PORT | MUL point2*D →alphaDen | direct[5] |
| O316 | PORT | DIV alphaNum/alphaDen →alpha | direct[4] |
| O317 | PORT | SUB M−D →outer | direct[6] |
| O318 | PORT | SUB D−m →inner | direct[7] |
| O319 | PORT | MUL outer*inner →gammaNum | direct[6] |
| O320 | PORT | SQUARE D →D2 | direct[7] |
| O321 | PORT | MUL point4*D2 →gammaDen | direct[7] |
| O322 | PORT | DIV gammaNum/gammaDen →gamma2 | direct[7] |
| O323 | PORT | SMALL_ROOT WHOLE gamma2 →gamma | direct[7] |
| O324 | PORT | SUB point1−alpha →beta | direct[5] |
| O325 | PORT | MUL alpha*rho →alphaRho | direct[8] |
| O326 | PORT | MUL gamma*WHOLE Z_PORT →gammaZ | direct[9] |
| O327 | PORT | ADD alphaRho+gammaZ →F1 | direct[8] |
| O328 | PORT | MUL gamma*rho →gammaRho | direct[10] |
| O329 | PORT | MUL alpha*WHOLE Z_PORT →alphaZ | direct[11] |
| O330 | PORT | SUB gammaRho−alphaZ →G1 | direct[11] |
| O331 | PORT | MUL beta*rho →betaRho | direct[12] |
| O332 | PORT | SUB betaRho−gammaZ →F2 | direct[12] |
| O333 | PORT | MUL beta*WHOLE Z_PORT →betaZ | direct[13] |
| O334 | PORT | ADD betaZ+gammaRho →G2negative | direct[13] |
| O335 | PORT | NEG G2negative →G2 | direct[13] |
| O336 | PORT | ABS WHOLE G2 →absG2 | direct[14] |
| O337 | PORT | MUL F2*point.5 →halfF2 | direct[15] |
| O338 | PORT | MUL absG2*WHOLE sqrt3half →pitchTerm | direct[14] |
| O339 | PORT | SUB halfF2−pitchTerm →ankleMargin | direct[15] |
| O340 | PORT | MUL h*WHOLE b →rollProduct | direct[16] |
| O341 | PORT | SUB rollProduct−WHOLE absX →rollMargin | direct[16] |
| O342 | PORT | MUL WHOLE X_PORT*WHOLE Z_PORT →qxNumerator | direct[1] |
| O343 | PORT | DIV qxNumerator/WHOLE rho →qx | direct[1] |
| O344 | PORT | NEG WHOLE h →negative_h | direct[2] |
| O345 | PORT | MUL negative_h*WHOLE Z_PORT →qyNumerator | direct[5] |
| O346 | PORT | DIV qyNumerator/WHOLE rho →qy | direct[5] |
| O347 | PORT | NEG WHOLE rho →qz | direct[6] |
| O348 | PORT | MUL WHOLE alpha*WHOLE X_PORT →alphaDx | direct[8] |
| O349 | PORT | MUL WHOLE alpha*WHOLE negative_h →alphaDy | direct[9] |
| O350 | PORT | MUL WHOLE alpha*WHOLE Z_PORT →alphaDz | direct[10] |
| O351 | PORT | MUL WHOLE gamma*WHOLE qx →gammaQx | direct[11] |
| O352 | PORT | MUL WHOLE gamma*WHOLE qy →gammaQy | direct[12] |
| O353 | PORT | MUL WHOLE gamma*WHOLE qz →gammaQz | direct[13] |
| O354 | PORT | ADD alphaDx+gammaQx →localKneeHipX | direct[14] |
| O355 | PORT | ADD alphaDy+gammaQy →localKneeHipY | direct[15] |
| O356 | PORT | ADD alphaDz+gammaQz →localKneeHipZ | direct[16] |
| O357 | PORT | MUL localKneeHipX*point1 →rotationFirst | direct[1] |
| O358 | PORT | MUL localKneeHipY*point0 →rotationSecond | direct[2] |
| O359 | PORT | ADD rotationFirst+rotationSecond →rotationPair | direct[1] |
| O360 | PORT | MUL localKneeHipZ*point0 →rotationThird | direct[3] |
| O361 | PORT | ADD rotationPair+rotationThird →WORLD knee−hip X | direct[4] |
| O362 | PORT | MUL localKneeHipX*point0 →rotationFirst | direct[1] |
| O363 | PORT | MUL localKneeHipY*point1 →rotationSecond | direct[2] |
| O364 | PORT | ADD rotationFirst+rotationSecond →rotationPair | direct[1] |
| O365 | PORT | MUL localKneeHipZ*point0 →rotationThird | direct[3] |
| O366 | PORT | ADD rotationPair+rotationThird →WORLD knee−hip Y | direct[7] |
| O367 | PORT | MUL localKneeHipX*point0 →rotationFirst | direct[1] |
| O368 | PORT | MUL localKneeHipY*point0 →rotationSecond | direct[2] |
| O369 | PORT | ADD rotationFirst+rotationSecond →rotationPair | direct[1] |
| O370 | PORT | MUL localKneeHipZ*point1 →rotationThird | direct[3] |
| O371 | PORT | ADD rotationPair+rotationThird →WORLD knee−hip Z | direct[0] |
| O372 | PORT | ENDPOINT_DIV WORLD knee−hip X/unchanged L1 →ux | direct[4] |
| O373 | PORT | ENDPOINT_DIV WORLD knee−hip Y/unchanged L1 →uy | direct[7] |
| O374 | PORT | ENDPOINT_DIV WORLD knee−hip Z/unchanged L1 →uz | direct[0] |
| O375 | PORT | SQUARE WHOLE ux →uxSquared | direct[1] |
| O376 | PORT | SQUARE WHOLE uz →uzSquared | direct[2] |
| O377 | PORT | ADD uxSquared+uzSquared →transverseSquared | direct[1] |
| O378 | PORT | SMALL_ROOT WHOLE transverseSquared →transverse | direct[1] |
| O379 | PORT | MUL pointL*WHOLE uy →axialExtent | direct[2] |
| O380 | PORT | MUL pointR*WHOLE transverse →radialExtent | direct[3] |
| O381 | PORT | ADD axialExtent+radialExtent →extent | direct[14] |
| O382 | PORT | NEG pointHy →bottom | direct[15] |
| O383 | PORT | SUB bottom−WHOLE extent →strictGap | direct[16] |
| O384 | STAR | SUB pointY−WHOLE A_STAR →h | direct[0] |
| O385 | STAR | SQUARE h →h2 | direct[1] |
| O386 | STAR | ADD WHOLE X2_STAR+h2 →rho2 | direct[1] |
| O387 | STAR | ADD rho2+WHOLE Z2_STAR →D | direct[2] |
| O388 | STAR | SMALL_ROOT WHOLE rho2 →rho | direct[3] |
| O389 | STAR | ADD WHOLE Delta+D →alphaNum | direct[4] |
| O390 | STAR | MUL point2*D →alphaDen | direct[5] |
| O391 | STAR | DIV alphaNum/alphaDen →alpha | direct[4] |
| O392 | STAR | SUB M−D →outer | direct[6] |
| O393 | STAR | SUB D−m →inner | direct[7] |
| O394 | STAR | MUL outer*inner →gammaNum | direct[6] |
| O395 | STAR | SQUARE D →D2 | direct[7] |
| O396 | STAR | MUL point4*D2 →gammaDen | direct[7] |
| O397 | STAR | DIV gammaNum/gammaDen →gamma2 | direct[7] |
| O398 | STAR | SMALL_ROOT WHOLE gamma2 →gamma | direct[7] |
| O399 | STAR | SUB point1−alpha →beta | direct[5] |
| O400 | STAR | MUL alpha*rho →alphaRho | direct[8] |
| O401 | STAR | MUL gamma*WHOLE Z_STAR →gammaZ | direct[9] |
| O402 | STAR | ADD alphaRho+gammaZ →F1 | direct[8] |
| O403 | STAR | MUL gamma*rho →gammaRho | direct[10] |
| O404 | STAR | MUL alpha*WHOLE Z_STAR →alphaZ | direct[11] |
| O405 | STAR | SUB gammaRho−alphaZ →G1 | direct[11] |
| O406 | STAR | MUL beta*rho →betaRho | direct[12] |
| O407 | STAR | SUB betaRho−gammaZ →F2 | direct[12] |
| O408 | STAR | MUL beta*WHOLE Z_STAR →betaZ | direct[13] |
| O409 | STAR | ADD betaZ+gammaRho →G2negative | direct[13] |
| O410 | STAR | NEG G2negative →G2 | direct[13] |
| O411 | STAR | ABS WHOLE G2 →absG2 | direct[14] |
| O412 | STAR | MUL F2*point.5 →halfF2 | direct[15] |
| O413 | STAR | MUL absG2*WHOLE sqrt3half →pitchTerm | direct[14] |
| O414 | STAR | SUB halfF2−pitchTerm →ankleMargin | direct[15] |
| O415 | STAR | MUL h*WHOLE b →rollProduct | direct[16] |
| O416 | STAR | SUB rollProduct−WHOLE absX →rollMargin | direct[16] |
| O417 | STAR | MUL WHOLE X_STAR*WHOLE Z_STAR →qxNumerator | direct[1] |
| O418 | STAR | DIV qxNumerator/WHOLE rho →qx | direct[1] |
| O419 | STAR | NEG WHOLE h →negative_h | direct[2] |
| O420 | STAR | MUL negative_h*WHOLE Z_STAR →qyNumerator | direct[5] |
| O421 | STAR | DIV qyNumerator/WHOLE rho →qy | direct[5] |
| O422 | STAR | NEG WHOLE rho →qz | direct[6] |
| O423 | STAR | MUL WHOLE alpha*WHOLE X_STAR →alphaDx | direct[8] |
| O424 | STAR | MUL WHOLE alpha*WHOLE negative_h →alphaDy | direct[9] |
| O425 | STAR | MUL WHOLE alpha*WHOLE Z_STAR →alphaDz | direct[10] |
| O426 | STAR | MUL WHOLE gamma*WHOLE qx →gammaQx | direct[11] |
| O427 | STAR | MUL WHOLE gamma*WHOLE qy →gammaQy | direct[12] |
| O428 | STAR | MUL WHOLE gamma*WHOLE qz →gammaQz | direct[13] |
| O429 | STAR | ADD alphaDx+gammaQx →localKneeHipX | direct[14] |
| O430 | STAR | ADD alphaDy+gammaQy →localKneeHipY | direct[15] |
| O431 | STAR | ADD alphaDz+gammaQz →localKneeHipZ | direct[16] |
| O432 | STAR | MUL localKneeHipX*point1 →rotationFirst | direct[1] |
| O433 | STAR | MUL localKneeHipY*point0 →rotationSecond | direct[2] |
| O434 | STAR | ADD rotationFirst+rotationSecond →rotationPair | direct[1] |
| O435 | STAR | MUL localKneeHipZ*point0 →rotationThird | direct[3] |
| O436 | STAR | ADD rotationPair+rotationThird →WORLD knee−hip X | direct[4] |
| O437 | STAR | MUL localKneeHipX*point0 →rotationFirst | direct[1] |
| O438 | STAR | MUL localKneeHipY*point1 →rotationSecond | direct[2] |
| O439 | STAR | ADD rotationFirst+rotationSecond →rotationPair | direct[1] |
| O440 | STAR | MUL localKneeHipZ*point0 →rotationThird | direct[3] |
| O441 | STAR | ADD rotationPair+rotationThird →WORLD knee−hip Y | direct[7] |
| O442 | STAR | MUL localKneeHipX*point0 →rotationFirst | direct[1] |
| O443 | STAR | MUL localKneeHipY*point0 →rotationSecond | direct[2] |
| O444 | STAR | ADD rotationFirst+rotationSecond →rotationPair | direct[1] |
| O445 | STAR | MUL localKneeHipZ*point1 →rotationThird | direct[3] |
| O446 | STAR | ADD rotationPair+rotationThird →WORLD knee−hip Z | direct[0] |
| O447 | STAR | ENDPOINT_DIV WORLD knee−hip X/unchanged L1 →ux | direct[4] |
| O448 | STAR | ENDPOINT_DIV WORLD knee−hip Y/unchanged L1 →uy | direct[7] |
| O449 | STAR | ENDPOINT_DIV WORLD knee−hip Z/unchanged L1 →uz | direct[0] |
| O450 | STAR | SQUARE WHOLE ux →uxSquared | direct[1] |
| O451 | STAR | SQUARE WHOLE uz →uzSquared | direct[2] |
| O452 | STAR | ADD uxSquared+uzSquared →transverseSquared | direct[1] |
| O453 | STAR | SMALL_ROOT WHOLE transverseSquared →transverse | direct[1] |
| O454 | STAR | MUL pointL*WHOLE uy →axialExtent | direct[2] |
| O455 | STAR | MUL pointR*WHOLE transverse →radialExtent | direct[3] |
| O456 | STAR | ADD axialExtent+radialExtent →extent | direct[14] |
| O457 | STAR | NEG pointHy →bottom | direct[15] |
| O458 | STAR | SUB bottom−WHOLE extent →strictGap | direct[16] |

## 5. Literal75 guards and full interleave

Initial guards afterO000 precede all operations. At a shared after-row boundary run guards in increasing G order before the next operation. Each repeated BASE/newimage endpoint domain certificate is a distinct earned domain even when using the same two local bool slots. Newcut G42/G49 first reset its same-side endpoint_certificate=false after guard charge, then establish the NEW closed p/q domain and set true only on full success. Old certificate dies after its original BASE endpoint consumers and G24; factor theorem never borrows endpoint derivative validity.

| Row | Side | After | Ordered predicate/cause |
| --- | --- | --- | --- |
| G01 | common | O000 | same actual owner/Data/parts/Request; valid provider; actual source helper success, count64/fullmask |
| G02 | common | O000 | original FP helper |
| G03 | common | O000 | new version6/Candidate0, immutable seed template (no generatedY yet) |
| G04 | common | O000 | original coordinate term/count/canonical-unused/abs8 domains |
| G05 | common | O000 | original compiler/link identities and finite positive fixed L1/L2<=1 |
| G06 | common | O000 | S64 proper rational yaw/flat held soles and preserved seed tuple |
| G07 | common | O045 | supported finite b then b.low>0 (no_positive_roll_factor) |
| G08 | PORT | O054 | finite singleton p/q, −.5<=p<q<=.5; p>=cutlow.high, q<=cuthi.low, q<=cutturn.low; save endpoint-domain certificate (no_tau_interval) |
| G09 | PORT | O056 | earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for p |
| G10 | PORT | O062 | earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for p |
| G11 | PORT | O067 | earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for q |
| G12 | PORT | O073 | earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for q |
| G13 | PORT | O078 | supported finite singleton u/v, 0<=u<v (no_radial_interval) |
| G14 | PORT | O085 | supported ankleUpperRad.low>0 (no_positive_upper) |
| G15 | PORT | O092 | supported reachUpperRad.low>0 (no_positive_upper) |
| G16 | STAR | O106 | finite singleton p/q, −.5<=p<q<=.5; p>=cutlow.high, q<=cuthi.low, q<=cutturn.low; save endpoint-domain certificate (no_tau_interval) |
| G17 | STAR | O108 | earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for p |
| G18 | STAR | O114 | earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for p |
| G19 | STAR | O119 | earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for q |
| G20 | STAR | O125 | earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for q |
| G21 | STAR | O130 | supported finite singleton u/v, 0<=u<v (no_radial_interval) |
| G22 | STAR | O137 | supported ankleUpperRad.low>0 (no_positive_upper) |
| G23 | STAR | O144 | supported reachUpperRad.low>0 (no_positive_upper) |
| G24 | common | O157 | earned finite BASElo/BASEhi then BASElo<BASEhi (base_interval_unavailable); fix immutable BASE |
| G25 | common | O157 | G24 BASE is written; exact original pelvis/root frame, thigh capsule regions1/2 and same immutable identity sole frames; finite r>0,0<Hy<L,r<=L<L1 with L unchanged kBoardingSelfHipLengthMetres hex. Altered identity→identity; finite sufficient domain failure→hip_threshold_domain. No BASE endpoint depends on these hip tests. |
| G26 | common | O161 | supported finite c_roll,0<c_roll.low<=c_roll.high<1 (hip_threshold_domain) |
| G27 | common | O166 | supported finite thresholdDen.low>0 then thresholdRad.low>0 (hip_threshold_domain) |
| G28 | common | O171 | supported finite0<s_req.low<=s_req.high<1 (hip_threshold_domain) |
| G29 | common | O173 | supported finite thresholdUnsquaredSign.low>0 (hip_threshold_domain) |
| G30 | PORT | O175 | BASE G24 written and BASE no-later-writer source invariant (only O154/O157 write respective fields); no snapshot comparison; prior written SUB and POINT_LOWER establish retained singleton H from floorInterval.low. The erased floorInterval is not reread. Supported finite retained H>0 (height_floor_unavailable); first false attaches singleton H. |
| G31 | PORT | O179 | Supported finite singleton Xmax=absX.high, Xmax>=0; same selected H in BOTH squared denominator and numerator. Supported finite chartNormSquared then chartNormSquared.low>0 (chart_factor_unavailable); no clipping. |
| G32 | PORT | O183 | Prior written DIV/POINT_LOWER establish chartLower provenance; erased chartFactorInterval is not reread. Supported finite retained chartNorm and chartLower,c_roll, published factor_lower; chartNorm.low>0; MAX first-on-equality. Published factor_lower equals selected lower singleton,0<factor_lower<=1 (chart_factor_unavailable). Authority ONLY on SAME fixed BASE; no strict improvement predicate. |
| G33 | PORT | O185 | Prior written DIV/POINT_UPPER establish k_bar provenance; erased kInterval is not reread. Supported finite retained singleton k_bar, denominator was point(PUBLISHED factor_lower actually used),0<k_bar<1 (hip_descent_unavailable). |
| G34 | PORT | O187 | Supported finite gRad then gRad.low>0 (hip_descent_unavailable). |
| G35 | PORT | O190 | Prior written MUL/POINT_LOWER establish cap provenance; erased gInterval is not reread. Supported finite retained singleton g_cut>0 (hip_descent_unavailable), cuts[0] equals the scalar assigned within that written POINT_LOWER row; retain double through this side cut/certificate consumers. No fallback. |
| G36 | STAR | O192 | BASE G24 written and BASE no-later-writer source invariant (only O154/O157 write respective fields); no snapshot comparison; prior written SUB and POINT_LOWER establish retained singleton H from floorInterval.low. The erased floorInterval is not reread. Supported finite retained H>0 (height_floor_unavailable); first false attaches singleton H. |
| G37 | STAR | O196 | Supported finite singleton Xmax=absX.high, Xmax>=0; same selected H in BOTH squared denominator and numerator. Supported finite chartNormSquared then chartNormSquared.low>0 (chart_factor_unavailable); no clipping. |
| G38 | STAR | O200 | Prior written DIV/POINT_LOWER establish chartLower provenance; erased chartFactorInterval is not reread. Supported finite retained chartNorm and chartLower,c_roll, published factor_lower; chartNorm.low>0; MAX first-on-equality. Published factor_lower equals selected lower singleton,0<factor_lower<=1 (chart_factor_unavailable). Authority ONLY on SAME fixed BASE; no strict improvement predicate. |
| G39 | STAR | O202 | Prior written DIV/POINT_UPPER establish k_bar provenance; erased kInterval is not reread. Supported finite retained singleton k_bar, denominator was point(PUBLISHED factor_lower actually used),0<k_bar<1 (hip_descent_unavailable). |
| G40 | STAR | O204 | Supported finite gRad then gRad.low>0 (hip_descent_unavailable). |
| G41 | STAR | O207 | Prior written MUL/POINT_LOWER establish cap provenance; erased gInterval is not reread. Supported finite retained singleton g_cut>0 (hip_descent_unavailable), cuts[1] equals the scalar assigned within that written POINT_LOWER row; retain double through this side cut/certificate consumers. No fallback. |
| G42 | PORT | O221 | finite singleton p/q, −.5<=p<q<=.5; p>=cutlow.high, q<=cuthi.low, q<=cutturn.low; p>=hipCutLower.high, q<=hipCutUpper.low; same earned positive g_cut_PORT; reset old same-side certificate then save NEW restricted endpoint-domain certificate; no factor guarantee outside BASE (no_tau_interval) |
| G43 | PORT | O223 | earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for p |
| G44 | PORT | O229 | earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for p |
| G45 | PORT | O234 | earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for q |
| G46 | PORT | O240 | earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for q |
| G47 | PORT | O245 | supported finite singleton u/v, 0<=u<v (no_radial_interval) |
| G48 | PORT | O252 | supported ankleUpperRad.low>0 (no_positive_upper) |
| G49 | STAR | O268 | finite singleton p/q, −.5<=p<q<=.5; p>=cutlow.high, q<=cuthi.low, q<=cutturn.low; p>=hipCutLower.high, q<=hipCutUpper.low; same earned positive g_cut_STAR; reset old same-side certificate then save NEW restricted endpoint-domain certificate; no factor guarantee outside BASE (no_tau_interval) |
| G50 | STAR | O270 | earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for p |
| G51 | STAR | O276 | earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for p |
| G52 | STAR | O281 | earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for q |
| G53 | STAR | O287 | earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for q |
| G54 | STAR | O292 | supported finite singleton u/v, 0<=u<v (no_radial_interval) |
| G55 | STAR | O299 | supported ankleUpperRad.low>0 (no_positive_upper) |
| G56 | common | O305 | BASE G24 and BOTH new endpoint/radial/height certificates written; BASE only-writer O154/O157 source invariant, no saved-original comparison; supported finite final lo/hi generated by literal BASE∩PORT∩STAR, then lo<hi (empty_inward_interval). First false attaches supported hi. |
| G57 | common | O308 | finite storedY then abs(Y)<=8 (candidate_domain) |
| G58 | common | O308 | storedY>lo (midpoint_unavailable) |
| G59 | common | O308 | storedY<hi (midpoint_unavailable) |
| G60 | PORT | O312 | supported finite h/rho2/D; h.low>0, rho2.low>0, D.low>m.high, D.high<M.low in order (verification_inconclusive) |
| G61 | PORT | O322 | supported finite gamma2.low>0 (verification_inconclusive) |
| G62 | PORT | O335 | supported finite F1/F2/G1/G2; F1.low>0 then F2.low>0 (verification_inconclusive) then G1.low>0 (hip_descent_unavailable) |
| G63 | PORT | O341 | supported finite ankle/roll margins; ankleMargin.low>=0 then rollMargin.low>=0 (verification_inconclusive) |
| G64 | PORT | O371 | earned authentic same-side exact identity sole columns; supported finite WORLD knee−hip X/Y/Z, each within[-16,16] (hard unsupported if support/domain fails) |
| G65 | PORT | O374 | supported finite genuine endpoint-divided ux/uy/uz; uy.high<0 (hip_verification_inconclusive) |
| G66 | PORT | O383 | supported finite transverse/extent/bottom/strictGap; extent.high<bottom.low (hip_verification_inconclusive; supported gap is reported, no gap positivity predicate) |
| G67 | STAR | O387 | supported finite h/rho2/D; h.low>0, rho2.low>0, D.low>m.high, D.high<M.low in order (verification_inconclusive) |
| G68 | STAR | O397 | supported finite gamma2.low>0 (verification_inconclusive) |
| G69 | STAR | O410 | supported finite F1/F2/G1/G2; F1.low>0 then F2.low>0 (verification_inconclusive) then G1.low>0 (hip_descent_unavailable) |
| G70 | STAR | O416 | supported finite ankle/roll margins; ankleMargin.low>=0 then rollMargin.low>=0 (verification_inconclusive) |
| G71 | STAR | O446 | earned authentic same-side exact identity sole columns; supported finite WORLD knee−hip X/Y/Z, each within[-16,16] (hard unsupported if support/domain fails) |
| G72 | STAR | O449 | supported finite genuine endpoint-divided ux/uy/uz; uy.high<0 (hip_verification_inconclusive) |
| G73 | STAR | O458 | supported finite transverse/extent/bottom/strictGap; extent.high<bottom.low (hip_verification_inconclusive; supported gap is reported, no gap positivity predicate) |
| G74 | common | O458 | check immutable seed BEFORE writing only both rootY={Y,+0,+0}count1; exact generated packet/static tuple after assignment (identity) |
| G75 | common | O458 | original FP precheck; generated packet structural checks then numerical domains; current+pending original validator Expected ONCE; FP postcheck (identity/unsupported) |

All guard support/finite checks precede the corresponding ordinary inequality. G25 adds original hip domain after independent BASE; its failure does not invalidate an already certified BASE. G29 checks the threshold's unsquared sign. G32/G38 verify the already-published finite C actually used, including valid C=1. Side-specific k/cap/cut failures are unavailable, not an alternative factor choice. Endpoint clips require that NEW same-side closed endpoint certificate and no other radicand gets clipped. Direct hip guards require supported transverse/extent/bottom/gap, negative uy and extent.high<bottom.low; there is NO gap.low>0 predicate. Supported gap is reporting evidence only. No report publishes retained direct intermediate arrays.

## 6. Fixed slots, last consumers and no-elision lifetimes

Select one directly initialized local aggregate: input[8],yaw[6],chart[16],link[9],cut[12],endpoint[18],direct[18] Intervals; cuts[10],selected[8] doubles; endpoint_certificate[2] bools. With Interval24 this is87×24+18×8+2+6padding=2240. Full declarations stay counted through constructor return; no heap/arena scratch, aggregate factory/return, owning closure/std::function or assumed inline/NRVO credit. Borrowed Control has only owner/refusal/Limits refs24 and forwarded deferred lambdas. Every literal destination is listed above, including all sequential endpoint/root/direct overwrites.

| Slot group | Fixed meaning and reuse boundary |
| --- | --- |
| input8, yaw6 | Original authenticated coordinate/yaw calculations; yaw original rational columns remain through final direct checks. Temporary sqrt3 replaces yaw0 only after original yaw denominator/cos/sin consumers; never claim rounded root frame orthogonality. |
| chart[0..7], chart[8..15] | Per-side X,Z,X²,Z²,C,A,ABS(X),rollLower. Signed X/Z and A remain through final direct checks; ABS(X) remains through per-side M extraction; rollLower last readO154, never overwritten with a new origin. |
| link9 | Lsum,M,Ldiff,m,L1²,L2²,Delta,sqrt3half,b. Retain originals through all later endpoint/direct consumers. No threshold overwrites their old authority. |
| cut12 | Each side W,cutlow,cuthi,cutturn,p,q. BASE p/q no longer certify new endpoints after G24. O208–221/O255–268 recompute original universal cuts, then tighten with side g and signed W upper; overwrite old p/q only after all old BASE endpoint consumers. |
| endpoint[0..11] | Per-side rho_p/rho_q,ankleLower/Upper,reachLower/Upper. Original ankle/reach endpoints last readO154/O157 and may be reused for tightened images; no stored BASE shadow pair. New rho endpoint values live through radial/height projection; new ankleLower/Upper published into cuts2..5 before their slot is reused. |
| endpoint[12..17] | Sequential fixed six-slot endpoint scratch. FloorInterval→singletonH atO175/O192, chartFactorInterval→chartLower atO182/O199. Subsequent guards use retained values and extraction provenance, never erased whole intervals. Same H in numerator/denominator. |
| direct[1], direct[3] | Common c_roll retained throughO200; s_req throughO202. Neither is overwritten by PORT/STAR factor/cap workspace. Both die before direct reconstruction startsO309. |
| direct[4], direct[5] | Squared M/H,norm,k,radical/cap sequential work. H retained endpoint12/selected4; M selected5; k selected6. chartNorm remains through factor-range guard before k overwrites it; quotient/POINT provenance survives without erased Interval reads. |
| cuts[0],cuts[1] | gPORT published atO190,gSTAR atO207; keep gPORT through PORT signed cut/closed endpoint certificate and gSTAR through STAR equivalent, then neither used by final direct checks. |
| cuts[2..5] | Original10-double height scratch last readBASE154/157, then tightened PORT lower/upper and STAR lower/upper. Last read final O302–305. Other allocated cuts slots remain charged. |
| selected[0..7] | Original MAX_ZERO/RN/endpoint selections. FactorH/M/k use4/5/6 only after BASE outputs consume their old values; after both caps, reuse by original endpoint selections/direct scratch. Every live primitive argument copy belongs to separate192 allowance. |
| endpoint_certificate2 | Original BASE closed-domain values until old endpoint last consumers; reset at each newcut charged guard, then NEW closed-domain bool through side endpoint clips. Bool domain is not report evidence/factor proof. |
| Slice | BASE/factor/final outputs reside only in owning current/pending headers. BASE doubles never change afterO154/O157. Factor publication is finite lower actually used; no H/M/k/g retained schema. |

Constructor named3912/unused184 includes workspace2240; eight primitive current/pending/local Intervals192; fixedCONST/sign/region96; certified root scratch176; params/refs/current-pending closures/control424; TWOvalidatorExpected80; success library/environment512; row-reset fullRefusal192. Deep forwarding56 is Control::guard-this8 + Predicate&&8 + lambda-this8 + why8 + supported-this/why/Bound24. Attach48/refusal48 are alternatives, not a co-added sequential path. Workspace2240 includes its padding; no independent8-byte spare is claimed. Ref424 includes4parameterrefs32+Control24+TWOcomputeclosures128+destination8+TWOguardclosures128+deep56+threepartviews24+control24. Each closure may capture at most eight borrowed refs, explicitly INCLUDING its workspace reference; aggregate padding never stores or discounts that reference. Control owns only its three borrowed refs.  separate source/forwarded/aliased references remain charged. Any implementation requiring more live refs/closures/primitive returns must revise/review source plan before measurement/FIRST.

Original EMPTY enrollment carrier owns complete33208/nested28880/24680 during the fresh source-only adapter. Old source carrier/Limits/refusals/factory/catalog returns die at actual source adapter return BEFORE constructor. This proposal preserves conservative combined source-plus-constructor overcharge; a max-of-disjoint-stages improvement is not silently selected. Constructor workspace, Control, primitive current/pending returns and TWOvalidator die BEFORE Key/Context/reset/full graph. TWOExpected headers remain full and separate; direct in-place Expected construction from const-provider avoids a standalone Diagnostic/by-value provider but returns are still counted without NRVO. Reserve exactly one Cell only after constructor death; reserve/default/emplace/reset need their exact nonallocating checked source closure. Pending reset Cell/header/Request/Refusal/Key/Context/token owners are retained in complete maps, not subtracted from frame measurements.

## 7. Genuine direct verification and unchanged original continuation

O309–458 repeat unchanged Endpoint05 direct reconstruction under remapped rows/destinations. Derive original rho,D,positivegamma,alpha then genuine q=(X*Z/rho,−h*Z/rho,−rho), alpha*d+gamma*q, and AUTHENTICATED identity sole-frame rotation with three products/two sums per component. ENDPOINT_DIV by genuine L1 yields WORLD u; direct SQUARE ux/uz→ADD→SMALL_ROOT→MUL radius, plus L*uy→extent→bottom−extent. Never substitute sqrt(1−uy²), rounded nominal unit identity, cached Endpoint04 axes or report midpoint geometry. Original reach/ankle/roll and positiveknee/F1/F2/G1 conditions remain direct supported guards at storedY; BOTH distinct sources/origins are preserved.

G74 checks unchanged immutable seed BEFORE assigning only BOTH rootY count1 {Y,+0,+0}; check static generated tuple after assignment. G75 runs FPprecheck→packet counts/canonical unused/version6/Candidate/generatedY/held nonY identity→original finite sum domains→ONE current/pending original validator→FPpost. All eight original validator error exclusions are explicitly embedded. Generated finite |Y|<=8 plus original count1 comparison has at most two nonzero addends; this proof is specific to constantY, not an unsupported general bounded-list shortcut. Structurally bad but numerically supported packet is identity. Unproved exclusion remains a blocking plan obligation; never add error4096 to live3912 constructor and call helper4096 satisfied.

New Key/reportelapsed2 only after complete75/458 and final validator; earlier elapsed0. Preserve fresh D01–03 before unchanged full private `boarding_route_foot_phase_cell(request,0,1,false,caps,work,cell.phase,reason)` with original phase caps1/2/1/3/6(body1), D04 before projection. P251=currentidentity53+11carriers×18 with freshcanonical atP11, typed bootD05–31 planes/rectangles/pressure/PORT1/16 equilibrium/16disks, B63 wholebody/regions then original Unit24 WORLD K−H/K−A. Full14owners/105pairs/1470axes/2940signed/2hips and original fullcapsule/root/cylinder/distal/separation contract remain unchanged. Construction sufficient proof never bypasses original full graph/Unit24/hip expression.

Partial original flags/refusal first-vs-terminal metadata are retained exactly. WORLD/material/HALO/route/actor/save/seat/dynamics/strength/friction/FirstFlight remain unearned. No constructor success certifies these future requirements, contact/body/SELF, or playable boarding by itself.

## 8. Independent controls and reporting boundary

The separately authored [Test manifest](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT06_TEST_MANIFEST.md) and [inventory](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT06_TEST_INVENTORY.json) must bind every literal row, capacity/priority/readiness/domain and owner. Selected finite roster107 is91 generic original-purpose controls plus14 real operation mask-crossing and2 guard mask-crossing controls; final independent labels/prerequisite masks and count proof are normative in that matched manifest. No default-empty provider constructor or new source issuer. One genuine provider creator, one default FIRST per compiler, lower-zero/raised/actual-used-minus/exact-required-minus/exact-used/FP/lifetime/priority controls only. Missing FIRST prerequisites yield explicit SKIP, not success.

A01 uses genuine immutable source and actual earned scalar outputs: independently reconstruct BASE/floor/chart factor/cap/intersection arithmetic for corroboration, and original F1/G1 knee vector plus direct WORLD transverse/extent at actualY. Published factors/BASE are claims to corroborate, never independent axis operands or production authority. Readable but uncertified partial fields receive digest tags/explicit NOT_RUN; no H/M/k/g fields are invented. Exact supported production predicates are distinguished from independent finite long-double approximate comparison. Conditional zero-lateral C=1/floor/cap/equality boundaries are symbolic/finite controls, not synthetic geometric fixtures or extra queries.

One constructor oracle with fixed4096 utility, ONE full15mass/pose body audit with TWO8400Oracles/TWORequests1368, then at most105pair+14owner+2hip interval records run sequentially after respective scratch death. Helper/table/source field counts must match new independent plan; no old audit or current/pending return is dropped. Summary120 stores24 uint32 work counts plus16-bit eligibility bitmap/hash/output/exists, not8-word mask archives. Universal check(bool,string_view) is void/counter-only, never I/O/heap/callback/queue; one global default-empty first_failure borrowed static-label view16 remembers firstfalse only. Globals156, creator772/252 inside1024 and caller1887/161 inside2048 stay explicit. Printing starts only after audit/helper/Oracle/Request workspace death. Finite counter bound must use107 controls and raised defaults+1, independently enumerated all26 aggregate slots; old101 totals are historical only. Finite aggregate maximum314687=107×2941 accounts raised controls before invalid-limit refusal; source check sum507392 remains below selected524288. Slots0..23 are24 numerical work aggregates, slot24 phase_calls, slot25 exportedFP; output_bytes is not a work aggregate. next_slot108 and at most107 SKIPs follow monotonic107-purpose dispatch. No actual counts/passes are predicted.

## 9. Complete typed ownership, errors and future actual admission

Forecast Slice256, Diagnostic1712, predictedExpected1720 and selectedExpected1744 account all new fields/padding. TWOExpected3488, selected Cell11032, Refusal192/optional200, FOUR Limits800, TWO exteriorRefusals384, TWORequests1368, Key16,Context/Token40 and TWOvalidator80 stay charged. Provider/catalog arena4096, complete source33208/nested28880/24680, complete original graph32768, fixed current/pending returns, scalar static152 and conservative native/JSON88 remain named. The body source49128 leaves24; graph48800 leaves352. Those small margins are source forecasts, not current new machine admission. Actual source/runtime/header/native-frame/allocator-chunk alignment may fail even when these integer sums fit.

Success512 includes only prospective bounded numerical leaves/environment/metadata, not complete errors. The selected error4096 partition is TWO borrowed bad_alloc192 each+TWO unexpected-string stack wrappers32 each+allocator/caught/destructor refs32+controls32+external simultaneous unwind/personality/TLS/free/terminal3584. Exterior fixed error owner320 is TWO literal buffers128 includingNUL+TWOchunk headers32. New prefix fixed messages are "intermediate endpoint06 unsupported floating point", "intermediate endpoint06 invalid candidate", "intermediate endpoint06 invalid limits", "intermediate endpoint06 output headers"; length<=127, no concatenation/numeric formatting. Complete fresh ELF/static source/call applicability remains UNKNOWN, never historical pass by label.

Six disjoint alternatives retain source owners at actual cutpoints: creator-before-arena7304/8192; preflight15152; structured-room16216; complete old enrollment error44080; one-Cell reserve after constructor death17152; post-helper/audit/Oracle-death teardown28224 (lastfive49152). Not-yet-created arena/Cell and absent constructor scratch are source/language lifetime facts required from actual code; arena-present later failures cannot inherit creator-before-arena. Success default/emplace/reset/graph/canonical/pressure/SELF/region/audits must use exact registered nonallocating checked fixed-value/borrowed-view paths. No allocating public assessor/unchecked variant get substitute. All pending validator/header/string returns and bad_alloc/current exception owners remain explicit.

Numerical error/unwind/destructor activations remain in numerical caps even if caught/printed later. Native/boot setup errors before creator charge, cold WORLD data/loader, immutable persistent world/contact owners, eager process-loader state/runtime main_arena/EH residents and post-worker presentation are separately NONZERO, with no guessed total-engine RAM cap or hidden warmup. Reentered loader/authentication entered by numerical work requires complete current-call proof; no implicit free-world/library scope transfer.

Register prospective closed Linux qualification using the same explicit stock profile: LD_BIND_NOW=1, LC_ALL=C, LANG=C, GLIBC_TUNABLES=glibc.malloc.tcache_count=0; exact27 injection/tuning unsets in current matched profile. Both generated CTest arrays and frozen FIRST environment must be independently verified, including stock libraries/loader/default handler/single-thread/no registration/interposition applicability. Other hosts remain conditional. This selects no eager source query/warmup and does not itself bound malloc/free/EH/libm/stdio. Fresh both-compiler dependency-complete objects, raw duplicate SU/prologue/CFI/non-tail return slots, normal object/archive/ELF correspondence, full old/new call ancestry and allocator/error/cleanup/cold leaf proof must precede FIRST. Any source/frame/heap/header/native/error/primitive helper/wholeworker violation stops before evaluation; no capraise, inferred missing0, optimizer/NRVO discount or reuse of an old near-limit admission.

| Complete source stage | Bytes | Ceiling | Headroom |
| --- | ---: | ---: | ---: |
| creator | 7240 | 8192 | 952 |
| preflight | 11248 | 49152 | 37904 |
| required_room_refusal | 12312 | 49152 | 36840 |
| source_enrollment | 39664 | 49152 | 9488 |
| source_plus_disjoint_construction_overcharge | 43760 | 49152 | 5392 |
| construction | 17344 | 49152 | 31808 |
| reset | 35010 | 49152 | 14142 |
| original_graph | 48800 | 49152 | 352 |
| canonical | 31488 | 49152 | 17664 |
| pressure_SELF | 29952 | 49152 | 19200 |
| region_factory | 25248 | 49152 | 23904 |
| construction_audit | 28232 | 49152 | 20920 |
| body_audit | 49128 | 49152 | 24 |
| interval_audit | 32328 | 49152 | 16824 |
| owned_capacity1_output | 14520 | 16777216 | 16762696 |

The complete dataset below is the selected prospective source constraint and allocation/error branch map. No private review artifact is a build dependency. Public final method/Test bytes are pinned externally without a hash cycle; historical earlier proposals remain provenance.

## 10. Authentic sources and complete structured forecast

[Original chart and full original continuation](../src/origin_boarding_planted_legs.cpp), phase_leg3430–3558, self02_hip_expression4415–4434, original unit/direct/owner functions; [source seed and enrollment](../src/origin_boarding_intermediate_endpoint01.cpp), seed207 and full S64; [pressure source](../src/origin_boarding_boot_support.cpp); [BOTH-ankle proof](ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md), [BOTH-hip proof](ORIGIN_BOARDING_BOTH_HIP_CONSTRUCTION.md), [same-domain factor proof](ORIGIN_BOARDING_CHART_FACTOR_CONSTRUCTION.md), [Endpoint04 immutable math](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT04.md), [Endpoint05 preserved direct recipe](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT05.md). Source equations/recipe authority bind the verified proof merge `bb5b9860e984e4fc36c22af961a071c1c340616a`; actual implementation authority must later bind its own fresh files/dependencies. Original observations/policies remain unchanged.

```json
{
  "issue": 529,
  "status": "AUTHOR_COMPLETE_LITERAL_METHOD_SOURCE_FORECAST_HELD_PENDING_PAIRED_REVIEW",
  "counts": {
    "operations": 458,
    "guards": 75,
    "operation_words": 8,
    "guard_words": 2,
    "operation_cursor_bits": 16,
    "operation_sentinel": 65535,
    "guard_cursor_bits": 8,
    "guard_sentinel": 255,
    "operation_lastword": "0x3ff",
    "guard_lastword": "0x7ff",
    "zero_mask_bits": 16,
    "zero_branches": 14
  },
  "scalar_availability": {
    "base_lo": 154,
    "base_hi": 157,
    "factor_lower_PORT": 183,
    "factor_lower_STAR": 200,
    "lo": 303,
    "hi": 305,
    "y": 308
  },
  "layout_forecast": {
    "basis": "SOURCE_MEMBER_ORDER_FORECAST_ON_FROZEN_8BYTE_POINTER_ALIGNMENT_PROFILE_NOT_NEW_SIZEOF",
    "primitive_sizes": {
      "double": 8,
      "uint64": 8,
      "uint32": 4,
      "uint16": 2,
      "uint8_bool": 1,
      "pointer_reference": 8,
      "Provider": 16,
      "PhasePartBinding": 64,
      "vector": 24,
      "string": 32,
      "ScalarBounds": 24,
      "PhaseRefusal": 64,
      "optional_size_t": 16,
      "TriangleKey": 12,
      "optional_TriangleKey": 16,
      "string_view": 16
    },
    "Slice": [
      [
        "operation_attempted8",
        0,
        64
      ],
      [
        "operation_written8",
        64,
        64
      ],
      [
        "guard_attempted2",
        128,
        16
      ],
      [
        "guard_written2",
        144,
        16
      ],
      [
        "limiting_bound",
        160,
        24
      ],
      [
        "y_lo_hi",
        184,
        24
      ],
      [
        "base_lo_hi",
        208,
        16
      ],
      [
        "factor_lower2",
        224,
        16
      ],
      [
        "operation_uint16",
        240,
        2
      ],
      [
        "guard_side",
        242,
        2
      ],
      [
        "condition",
        244,
        1
      ],
      [
        "arithmetic_complete",
        245,
        2
      ],
      [
        "preflight",
        247,
        1
      ],
      [
        "zero_mask_uint16",
        248,
        2
      ],
      [
        "tail_padding",
        250,
        6
      ]
    ],
    "Refusal": [
      [
        "condition_predicate",
        0,
        2
      ],
      [
        "alignment_padding",
        2,
        6
      ],
      [
        "limiting_bound",
        8,
        24
      ],
      [
        "phase",
        32,
        64
      ],
      [
        "optional_side_edge_axis",
        96,
        48
      ],
      [
        "optional_source_key",
        144,
        16
      ],
      [
        "source_name",
        160,
        16
      ],
      [
        "self_pair",
        176,
        2
      ],
      [
        "operation_uint16",
        178,
        2
      ],
      [
        "self_region_axis_sign",
        180,
        3
      ],
      [
        "self_stage",
        183,
        1
      ],
      [
        "source_edge",
        184,
        1
      ],
      [
        "tail_padding",
        185,
        7
      ]
    ],
    "Diagnostic": [
      [
        "Provider",
        0,
        16
      ],
      [
        "parts15",
        16,
        960
      ],
      [
        "Counters",
        976,
        200
      ],
      [
        "Slice",
        1176,
        256
      ],
      [
        "vector_Cell",
        1432,
        24
      ],
      [
        "optional_Refusal",
        1456,
        200
      ],
      [
        "source_evaluated",
        1656,
        8
      ],
      [
        "output_capacity_bytes",
        1664,
        8
      ],
      [
        "reporting_elapsed_seconds",
        1672,
        8
      ],
      [
        "version_selfversion",
        1680,
        8
      ],
      [
        "candidate_condition_state_stage",
        1688,
        4
      ],
      [
        "stop_self_pair",
        1692,
        2
      ],
      [
        "stop_operation_uint16",
        1694,
        2
      ],
      [
        "stop_self_region_axis_sign",
        1696,
        3
      ],
      [
        "thirteen_boolean_flags",
        1699,
        13
      ]
    ],
    "predicted_sizes": {
      "Slice": 256,
      "Refusal": 192,
      "optional_Refusal": 200,
      "Diagnostic": 1712,
      "Expected": 1720,
      "Cell": 10944,
      "Limits": 200,
      "Work": 200,
      "Key": 16,
      "Context": 40,
      "Token": 40,
      "validatorExpected": 40,
      "Summary": 120,
      "Totals": 120
    },
    "proposed_ceilings": {
      "Expected": 1744,
      "Cell": 11032,
      "Refusal": 192,
      "Slice": 256,
      "Limits": 200,
      "Work": 200,
      "Key": 16,
      "Context": 40,
      "Token": 40,
      "validatorExpected": 40,
      "Summary": 120,
      "Totals": 120,
      "mutable_globals": 156,
      "Oracle": 8400
    },
    "Expected_reason": "Diagnostic1712 vs string32, independent discriminator/padding8 gives predicted1720; selected1744. Source forecast only, actual fresh both-compiler sizeof unknown.",
    "Cell_reason": "Every Cell member retains the identical type/extent/order; all remapped enum underlying types remainuint8; no constructor Slice/axis/threshold is added to Cell. Historical10944 is applicability input only, not freshsizeof.",
    "Summary_reason": "24 checked uint32 used96 + hash8 + output8 + uint16 crossing_bitmap2 + exists1 =115 rounded8\u2192120. No mask or report archive.",
    "Totals_reason": "107 consumers/107 SKIPs/next_slot108 from91 generic+14 operation crossings+2 guard crossings; no observed count. Summary16 eligibility bits fitsuint16/120 source target.",
    "mutable_globals_components": {
      "Totals": 120,
      "checks_uint64": 8,
      "failures_uint32": 4,
      "shared_bytes_size_t": 8,
      "first_failure_static_label_string_view": 16
    }
  },
  "core_source_pools": {
    "constructor": {
      "named_components": {
        "direct_local_aggregate_87Intervals24_18double8_2bool_padding6": 2240,
        "eight_primitive_argument_local_pending_intervals": 192,
        "constant_sign_arrays": 96,
        "certified_small_root_terms_scratch_scalars": 176,
        "full_parameters_Control_forwarded_current_pending_closures_deep_refs_views_controls": 424,
        "two_validator_expected": 80,
        "success_library_environment": 512,
        "row_reset_full_refusal_initializer": 192
      },
      "named_bytes": 3912,
      "unused_bytes": 184,
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
        "row_reset_full_refusal_initializer": 192
      },
      "named_bytes": 3384,
      "unused_bytes": 2760,
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
        "row_reset_full_refusal_initializer": 192
      },
      "named_bytes": 5352,
      "unused_bytes": 792,
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
        "twenty_LD_bounds32": 640,
        "eight_LD_vectors96_by_reference": 768,
        "sixtyfour_LD_scalars16": 1024,
        "forty_double8_slots": 320,
        "references_environment_indices_control": 256,
        "independent_success_library_leaf": 512
      },
      "named_bytes": 3520,
      "unused_bytes": 576,
      "reservation_bytes": 4096
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
        "fiftyfour_Intervals24": 1296,
        "twentyfour_points72": 1728,
        "sixteen_frames72": 1152,
        "TWO_solid_allowance": 1024,
        "pending_proof_returns": 512,
        "scalar_owner_hip_error_helpers_INCLUDING_success_library512": 2480
      },
      "named_bytes": 8192,
      "unused_bytes": 0,
      "reservation_bytes": 8192
    },
    "creator_caller": {
      "named_components": {
        "summary": 120,
        "two_streams": 176,
        "mutable_globals_target": 156,
        "boot_native_expected": 80,
        "provider": 24,
        "saved_buffers_bytes": 24,
        "references": 32,
        "errors": 160
      },
      "named_bytes": 772,
      "unused_bytes": 252,
      "reservation_bytes": 1024
    },
    "graph_caller": {
      "named_components": {
        "entire_preserved_endpoint03_named_caller": 1871,
        "ONE_static_first_failure_borrowed_view": 16
      },
      "named_bytes": 1887,
      "unused_bytes": 161,
      "reservation_bytes": 2048
    }
  },
  "source15_maps": {
    "creator": {
      "components": {
        "entire_old_creator": 7000,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 7240,
      "ceiling": 8192,
      "headroom": 952
    },
    "preflight": {
      "components": {
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "library_error": 512,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 11248,
      "ceiling": 49152,
      "headroom": 37904
    },
    "required_room_refusal": {
      "components": {
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "library_error": 512,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "current_request": 680,
        "current_pending_refusal": 384,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 12312,
      "ceiling": 49152,
      "headroom": 36840
    },
    "source_enrollment": {
      "components": {
        "full_old_endpoint03_enrollment": 33208,
        "new_two_headers": 3488,
        "new_four_limits": 800,
        "new_current_pending_refusal": 384,
        "new_context": 40,
        "new_token_reservation": 40,
        "new_current_pending_key": 32,
        "new_control": 64,
        "new_exterior_current_pending_request_overcharge": 1368,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 39664,
      "ceiling": 49152,
      "headroom": 9488
    },
    "source_plus_disjoint_construction_overcharge": {
      "components": {
        "full_old_endpoint03_enrollment": 33208,
        "new_two_headers": 3488,
        "new_four_limits": 800,
        "new_current_pending_refusal": 384,
        "new_context": 40,
        "new_token_reservation": 40,
        "new_current_pending_key": 32,
        "new_control": 64,
        "new_exterior_current_pending_request_overcharge": 1368,
        "phase_scalar_limits_global": 152,
        "disjoint_full_constructor_pool": 4096,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 43760,
      "ceiling": 49152,
      "headroom": 5392
    },
    "construction": {
      "components": {
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 384,
        "current_pending_key": 32,
        "context": 40,
        "old_caps_reason_work": 176,
        "constructor_pool": 4096,
        "independent_error": 512,
        "outer_control": 64,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 17344,
      "ceiling": 49152,
      "headroom": 31808
    },
    "reset": {
      "components": {
        "two_full_cells": 22064,
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 384,
        "context": 40,
        "current_pending_key": 32,
        "old_caps_reason_work": 176,
        "two_initializer_arrays": 210,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 35010,
      "ceiling": 49152,
      "headroom": 14142
    },
    "original_graph": {
      "components": {
        "entire_original_graph": 32768,
        "two_headers": 3488,
        "arena": 4096,
        "cell_delta": 3296,
        "four_limits": 800,
        "caller": 2048,
        "old_caps": 72,
        "old_reason": 64,
        "pending_optional_request": 688,
        "current_request": 680,
        "current_pending_refusal": 384,
        "context": 40,
        "token_reservation": 40,
        "current_pending_key": 32,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 48800,
      "ceiling": 49152,
      "headroom": 352
    },
    "canonical": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 384,
        "context_token": 80,
        "current_pending_key": 32,
        "old_caps_reason_work": 176,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "factory_pool": 7680,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 31488,
      "ceiling": 49152,
      "headroom": 17664
    },
    "pressure_SELF": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 384,
        "context_token": 80,
        "current_pending_key": 32,
        "old_caps_reason_work": 176,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "helper_pool": 6144,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 29952,
      "ceiling": 49152,
      "headroom": 19200
    },
    "region_factory": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 384,
        "context_token": 80,
        "current_pending_key": 32,
        "old_caps_reason_work": 176,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "region_factory": 1440,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 25248,
      "ceiling": 49152,
      "headroom": 23904
    },
    "construction_audit": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "scalar_error": 1024,
        "inactive_key_context_overcharge": 40,
        "phase_scalar_limits_global": 152,
        "construction_audit_utility": 4096,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 28232,
      "ceiling": 49152,
      "headroom": 20920
    },
    "body_audit": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "scalar_error": 1024,
        "inactive_key_context_overcharge": 40,
        "phase_scalar_limits_global": 152,
        "two_oracles": 16800,
        "body_utility": 8192,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 49128,
      "ceiling": 49152,
      "headroom": 24
    },
    "interval_audit": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "scalar_error": 1024,
        "inactive_key_context_overcharge": 40,
        "phase_scalar_limits_global": 152,
        "interval_utility": 8192,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 32328,
      "ceiling": 49152,
      "headroom": 16824
    },
    "owned_capacity1_output": {
      "components": {
        "two_headers": 3488,
        "capacity1_cell": 11032
      },
      "bytes": 14520,
      "ceiling": 16777216,
      "headroom": 16762696
    }
  },
  "source6_error_maps": {
    "creator_before_arena": {
      "components": {
        "current_pending_ctor_evidence": 1040,
        "port_partition": 376,
        "current_pending_creator_expected": 80,
        "creator_limits": 96,
        "native_boot_aliases": 32,
        "full_selected_creator_caller": 1024,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88,
        "complete_error_pool": 4096,
        "fixed_error_buffers_and_chunk_headers": 320
      },
      "bytes": 7304,
      "ceiling": 8192,
      "headroom": 888,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    },
    "public_preflight": {
      "components": {
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88,
        "complete_error_pool": 4096,
        "fixed_error_buffers_and_chunk_headers": 320
      },
      "bytes": 15152,
      "ceiling": 49152,
      "headroom": 34000,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    },
    "structured_room_conservative_error_alternative": {
      "components": {
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "current_request": 680,
        "current_pending_refusal": 384,
        "native_prepared_and_source_json_global_overcharge": 88,
        "complete_error_pool": 4096,
        "fixed_error_buffers_and_chunk_headers": 320
      },
      "bytes": 16216,
      "ceiling": 49152,
      "headroom": 32936,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    },
    "source_enrollment_error_overcharge": {
      "components": {
        "full_old_endpoint03_enrollment": 33208,
        "new_two_headers": 3488,
        "new_four_limits": 800,
        "new_current_pending_refusal": 384,
        "new_context": 40,
        "new_token_reservation": 40,
        "new_current_pending_key": 32,
        "new_control": 64,
        "new_exterior_current_pending_request_overcharge": 1368,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88,
        "complete_error_pool": 4096,
        "fixed_error_buffers_and_chunk_headers": 320
      },
      "bytes": 44080,
      "ceiling": 49152,
      "headroom": 5072,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    },
    "single_cell_reserve_after_constructor_return": {
      "components": {
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 384,
        "current_pending_key": 32,
        "context": 40,
        "old_caps_reason_work": 176,
        "outer_control": 64,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88,
        "complete_error_pool": 4096,
        "fixed_error_buffers_and_chunk_headers": 320
      },
      "bytes": 17152,
      "ceiling": 49152,
      "headroom": 32000,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    },
    "post_helper_or_audit_teardown": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3488,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "current_pending_request": 1368,
        "current_pending_refusal": 384,
        "context_token": 80,
        "current_pending_key": 32,
        "old_caps_reason_work": 176,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88,
        "complete_error_pool": 4096,
        "fixed_error_buffers_and_chunk_headers": 320
      },
      "bytes": 28224,
      "ceiling": 49152,
      "headroom": 20928,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    }
  },
  "conditional_registered_source_design": true,
  "new_compiler_size_frames_resource_feasibility": "UNKNOWN",
  "numeric_evaluations": 0,
  "proof_merge": "bb5b9860e984e4fc36c22af961a071c1c340616a",
  "validator_error_applicability": [
    {
      "branch": "root constant count/finite term/unused/supported-sum failure",
      "earned_or_precheck": "G01 genuine full S64 source enrollment includes original S33 (zero-based32) request validator success; G04 term/count/unused domains; G74 byte-exact unchanged non-Y fields and count1 {Y,+0,+0} in BOTH Y constants; G57 finite abs(Y)<=8; G75 structural/domain recheck before original validator",
      "proof": "Every unchanged root constant retains authenticated original phase_constant support. New single-term Y has at most two nonzero addends in either four-term comparison with +/-8, finite bounded sums and no expansion-capacity overflow; exact sign tests cannot establish Y<-8 or Y>8. The original interval value clips to [-8,8] and remains supported under the original qualified FP environment."
    },
    {
      "branch": "sole constant count/finite term/unused/supported-sum failure",
      "earned_or_precheck": "G01 original S33 validator success; G04 domains; G74 exact whole generated packet identity; G75 structural/domain recheck",
      "proof": "Sole term lists and counts are byte-identical to the validated seed; there is no second source value or changed sole-Y term."
    },
    {
      "branch": "sole yaw finite/abs1 failure",
      "earned_or_precheck": "G01 original validator success; G74 exact seed yaw fields; G75 generated packet identity/domain checks",
      "proof": "Both endpoint yaw fields remain the original authenticated fields."
    },
    {
      "branch": "swing finite/[0,.25] failure",
      "earned_or_precheck": "G01 original validator success; G74 whole-packet identity; G75 identity/domain checks",
      "proof": "Both original swing fields are unchanged; no constructor row writes them."
    },
    {
      "branch": "root yaw finite/abs1 failure",
      "earned_or_precheck": "G01 original validator success; G74 whole-packet identity; G75 identity/domain checks",
      "proof": "Both root-yaw carrier entries are unchanged."
    },
    {
      "branch": "torso finite/abs1 failure",
      "earned_or_precheck": "G01 original validator success; G74 whole-packet identity; G75 identity/domain checks",
      "proof": "Both torso carrier entries are unchanged."
    },
    {
      "branch": "reaction fraction finite/[0,1] failure",
      "earned_or_precheck": "G01 original validator success; G74 whole-packet identity; G75 identity/domain checks",
      "proof": "Both original reaction fractions are unchanged."
    },
    {
      "branch": "seconds finite/[1,120] failure",
      "earned_or_precheck": "G01 original validator success; G74 whole-packet identity; G75 identity/domain checks",
      "proof": "Original duration is unchanged."
    }
  ],
  "stage_allocation_and_exception_classification": {
    "creator": "Fixed unexpected strings and the one arena allocation may enter the creator_before_arena alternative. The Data ctor/port copy/initialize have no owning dynamic fields beyond already-created shared references and the one arena; successful construction may not enter an additional throwing allocation after the arena exists. This exact source/normal-object property is a future admission requirement.",
    "preflight": "FP/Candidate/lowered-limit/header-room failure strings may allocate before constructor/source helpers or output Cell exist. Public_preflight retains TWO headers and the arena conservatively, even where the former do not yet exist.",
    "required_room_refusal": "Expected in-place Diagnostic, Refusal resets/copies, borrowed-provider aliases, and structured output refusal use fixed fields/string_views/empty vector and allocate nothing. A conservative error alternative is retained anyway; it does not authorize arbitrary allocator entry with constructor scratch alive.",
    "source_enrollment": "All 33208 old enrollment bytes, including 28880 and nested 24680, remain. Any old preflight/validator fixed error allocation before S64 success is covered by the additive enrollment alternative; no old capability or creator is added. The complete new constructor has not been entered.",
    "construction": "458 operations/75 charged guards, fixed arrays and fixed Refusal/validator Expected objects only. G75 branches are mapped individually below. Do not coadd the 4096 error pool to the 3912 constructor while claiming helper4096: all allocating validator branches must be source-unreachable by the earned packet predicates; otherwise this plan is HOLD until a separately reviewed lifetime boundary is registered.",
    "reset": "Cell storage was reserved once and emplace has capacity. Cell/default-member/reset/Refusal field assignments are fixed and nonallocating; the actual reserved-one allocator max_size and noexcept/default-field closure must be verified. Reset cannot inherit an arbitrary error pool just because its source total fits.",
    "original_graph": "Use the unchanged private one-Cell compiler, not any allocating public phase assessor. Generated Request is immutable after G75; its repeated original validator has the same branch map. Full graph/body/other/region paths use fixed arrays, fundamental Interval/Jet/Point values, borrowed strings and checked tags; source get_if/part identity guards must dominate throwing variant get. Full32768 is retained and no predicted refusal excludes a descendant.",
    "canonical": "Original phase4 request controls and their Step02/Step01 source factories return fixed Request/optional<Request> records and mutate only fixed fields. They do not call the public allocating step assessor. All current/pending Request returns remain charged.",
    "pressure_SELF": "Fresh Context/Token and fixed pressure/SELF pools with row-reset Refusal192, fixed current/pending tuple returns, bounded nominal fields/variants, borrowed source names, and unchanged primitives; no owning containers or allocating public evaluator may replace the selected internal bridges.",
    "region_factory": "The genuine original region getter uses fixed values and checked identities. Current/pending region returns are in the named typed pool; there is no heap catalog creator or report-as-input authority.",
    "construction_audit": "Independent source/Y-based oracle uses only registered fixed arrays/fundamental arithmetic and guarded C libm/fenv; no production threshold/cut feedback or extra query. Checked counters/aggregate narrowing are source-unreachable throw branches under the finite Test ledger.",
    "body_audit": "Independent TWO8400 Oracles and full8192 utility remain live and counted. The ONE full15-mass/pose body audit is conditional/sequential with the separate <=121 interval records and the construction audit. Fixed numeric helpers allocate/throw no C++ exceptions. Counter/aggregate/roster guards are discharged by the finite Test ledger, not observed pass results. Universal counter-only checks perform no I/O/heap/callback; status/first-failure reporting starts only after helper/Request/Oracle/workspace death. A sole source-static first-failure string_view16 is explicitly named inside globals156 and preserved caller1024/2048. Later code/objects must prove this selected cut.",
    "interval_audit": "At most121 original interval records; fixed arrays/fundamental helpers, no extra API query. Same finite check/aggregate proof and same selected universal quiet-check/after-audit reporting cut as body audit; full8192 retained.",
    "owned_capacity1_output": "Two full Expected headers and one capacity-one Cell output remain14520. The actual live owning arena and Cell each have separately charged allocator chunk headers within the selected success leaf/metadata plan; language move transfers each sole buffer, while TWO headers and all pending frames remain charged. No copy/NRVO reduction.",
    "post_helper_or_audit_teardown": "Enter only after constructor/source/graph/pressure/SELF or independent Oracle/helper activation returns and any audit Oracle objects have been destroyed. Keep full remaining exterior owners plus error/cleanup pool. Destructor/free/EH paths and reference-count last-owner applicability are future actual proof obligations; this cut is not a scope exemption."
  },
  "complete_error_pool": {
    "bytes": 4096,
    "kind": "PROSPECTIVE_ALL_INCLUSIVE_SOURCE_ERROR_CEILING",
    "typed_owner_constraints": {
      "two_borrowed_bad_alloc_exception_storage_slots_including_header_chunk": 384,
      "current_pending_unexpected_string_stack_wrappers": 64,
      "caught_exception_and_destructor_allocator_reference_slots": 32,
      "allocation_count_branch_and_catch_control": 32,
      "remaining_simultaneous_external_call_frames_and_unwind_personality_tls_cleanup_terminal_leaf_budget": 3584
    },
    "note": "These disjoint named charges sum4096; two exception slots each have selected192-byte ceiling. This is a proposed budget partition, not an actual allocator/ELF upper proof. A fresh complete measured chain that violates any owner limit or total stops before FIRST; no cap raise, warmup, arbitrary callback or predicted failure exclusion."
  },
  "fixed_error_string_and_chunk_owner_plan": {
    "bytes": 320,
    "components": {
      "two_live_or_pending_fixed_literal_string_buffers_each128_including_NUL_capacity": 256,
      "two_separate_allocator_chunk_headers_each32": 64
    },
    "constraints": "All planned new fixed preflight/creator/validator error messages have payload length<=127. No formatting, concatenation, request-value stringification or owning source-name copy. Future pinned string allocator capacity<=128 and chunk header<=32 must be measured/proved. The string32 stack owners are already in TWO Expected or the explicit error-pool wrappers; borrowed bad_alloc chunks are separate within4096."
  },
  "error_pre_FIRST_gates": [
    "Complete fresh source lexical proof for all15 maps and6 error alternatives; G75 and repeated private compiler validator branch coverage",
    "Fresh sizeof/current-pending layouts, full allocation/chunk/object capacities and error class/typed-owner limits",
    "GCC/Clang complete real caller/helper/library/EH/free/destructor/TLS/terminal chains, ABI entries and cold resolver/loader applicability under selected exact launch profile",
    "Actual success512 library/metadata and error4096 partition proof; no missing frame=0, reserved=measured or old companion reroot",
    "Universal quiet-check/after-audit reporting design and source-static first-failure view/counter/aggregate bounds exactly match actual Test source; all current/pending numeric parents remain counted",
    "Current/normal object/archive/ELF/dependency correspondence and immutable one-FIRST packet before any execution"
  ],
  "quiet_check_design": {
    "status": "ROOT_SELECTED_SOURCE_DESIGN; noimplementation oractualproof",
    "check": "void check(bool,string_view), universally counter-only; no stream/heap/formatting/callback/queue; finite counter branches unreachable",
    "first_failure": {
      "type": "std::string_view",
      "bytes": 16,
      "lifetime": "one mutableglobal, defaultempty; assign only firstfalse source-static registered literal; neverreset; noexistsflag, noowninglabel/report/archive",
      "globals_named": 156,
      "creator_named": 772,
      "creator_unused": 252,
      "graph_named": 1887,
      "graph_unused": 161
    },
    "argument_and_return_ownership": "current/pending why arguments in named256 callercheck/error arguments; reference-return from assignment borrowed, existingrefslots; noownedaggregate return; helpervoid; finalprint reads globaldirectly under existing112 status/index and256labelargument slots",
    "reporting": "FIRST/refusal/skip/final retained; explicit auditstatus/firstfailure print only afterhelper AND Request/Oracle/workspace scopesdie. Remainingsource/Data/Cell/parentowners staycharged. StockStreamCountercallbacks/formatting-profile evidence stillmandatory.",
    "authority": "nooldTestchange, no math/checkorder/count/producerpolicychange"
  },
  "source_refusal_adapter": {
    "original_full_operation_type": "uint8",
    "original_unset": 255,
    "fresh_full_operation_type": "uint16",
    "fresh_unset": 65535,
    "mapping": "old UNSET255 -> new UNSET65535; actual old source cursors0..63 widen faithfully; preserve side/stage/condition; nested unchanged PhaseRefusal operation255 stays255",
    "fresh_fields": [
      "Slice.operation",
      "Refusal.operation",
      "Diagnostic.stop_operation",
      "Summary terminal checks/hash/printf"
    ]
  },
  "constructor_ref_control_components": {
    "four_constructor_parameter_references": 32,
    "borrowed_three_reference_Control": 24,
    "current_pending_compute_closures_eight_references_each": 128,
    "destination_reference": 8,
    "current_pending_guard_closures_eight_references_each": 128,
    "deep_guard_forwarding_supported_path": 56,
    "three_borrowed_part_views": 24,
    "loop_cursor_certificate_controls": 24,
    "capture_semantics": "Every current/pending compute or guard closure includes explicit workspace reference within its at-most-eight-ref allowance; Control carries owner/refusal/Limits only. No separate workspace-ref owner is hidden in aggregate padding. Forwarded lambda object-reference and closure-this are separately counted by deep56. If actual source needs more than these role maxima registration is HOLD."
  },
  "constructor_workspace_fields": {
    "input_Intervals": 8,
    "yaw_Intervals": 6,
    "chart_Intervals": 16,
    "link_Intervals": 9,
    "cut_Intervals": 12,
    "endpoint_Intervals": 18,
    "direct_Intervals": 18,
    "cuts_double": 10,
    "selected_double": 8,
    "endpoint_certificate_bool": 2,
    "alignment_padding_bytes": 6
  },
  "operation_destination_plan": [
    {
      "row": 1,
      "slot": "input[0]"
    },
    {
      "row": 2,
      "slot": "input[1]"
    },
    {
      "row": 3,
      "slot": "input[2]"
    },
    {
      "row": 4,
      "slot": "input[3]"
    },
    {
      "row": 5,
      "slot": "input[4]"
    },
    {
      "row": 6,
      "slot": "input[5]"
    },
    {
      "row": 7,
      "slot": "input[6]"
    },
    {
      "row": 8,
      "slot": "input[7]"
    },
    {
      "row": 9,
      "slot": "yaw[0]"
    },
    {
      "row": 10,
      "slot": "yaw[1]"
    },
    {
      "row": 11,
      "slot": "yaw[2]"
    },
    {
      "row": 12,
      "slot": "yaw[3]"
    },
    {
      "row": 13,
      "slot": "yaw[2]"
    },
    {
      "row": 14,
      "slot": "yaw[4]"
    },
    {
      "row": 15,
      "slot": "yaw[5]"
    },
    {
      "row": 16,
      "slot": "endpoint[12]"
    },
    {
      "row": 17,
      "slot": "endpoint[13]"
    },
    {
      "row": 18,
      "slot": "endpoint[14]"
    },
    {
      "row": 19,
      "slot": "endpoint[15]"
    },
    {
      "row": 20,
      "slot": "chart[0]"
    },
    {
      "row": 21,
      "slot": "chart[1]"
    },
    {
      "row": 22,
      "slot": "chart[2]"
    },
    {
      "row": 23,
      "slot": "chart[3]"
    },
    {
      "row": 24,
      "slot": "chart[4]"
    },
    {
      "row": 25,
      "slot": "chart[5]"
    },
    {
      "row": 26,
      "slot": "endpoint[12]"
    },
    {
      "row": 27,
      "slot": "endpoint[13]"
    },
    {
      "row": 28,
      "slot": "endpoint[14]"
    },
    {
      "row": 29,
      "slot": "endpoint[15]"
    },
    {
      "row": 30,
      "slot": "chart[8]"
    },
    {
      "row": 31,
      "slot": "chart[9]"
    },
    {
      "row": 32,
      "slot": "chart[10]"
    },
    {
      "row": 33,
      "slot": "chart[11]"
    },
    {
      "row": 34,
      "slot": "chart[12]"
    },
    {
      "row": 35,
      "slot": "chart[13]"
    },
    {
      "row": 36,
      "slot": "link[0]"
    },
    {
      "row": 37,
      "slot": "link[1]"
    },
    {
      "row": 38,
      "slot": "link[2]"
    },
    {
      "row": 39,
      "slot": "link[3]"
    },
    {
      "row": 40,
      "slot": "link[4]"
    },
    {
      "row": 41,
      "slot": "link[5]"
    },
    {
      "row": 42,
      "slot": "link[6]"
    },
    {
      "row": 43,
      "slot": "yaw[0]"
    },
    {
      "row": 44,
      "slot": "link[7]"
    },
    {
      "row": 45,
      "slot": "link[8]"
    },
    {
      "row": 46,
      "slot": "cut[0]"
    },
    {
      "row": 47,
      "slot": "endpoint[12]"
    },
    {
      "row": 48,
      "slot": "cut[1]"
    },
    {
      "row": 49,
      "slot": "endpoint[12]"
    },
    {
      "row": 50,
      "slot": "cut[2]"
    },
    {
      "row": 51,
      "slot": "cut[3]"
    },
    {
      "row": 52,
      "slot": "cut[4]"
    },
    {
      "row": 53,
      "slot": "cut[5]"
    },
    {
      "row": 54,
      "slot": "cut[5]"
    },
    {
      "row": 55,
      "slot": "endpoint[12]"
    },
    {
      "row": 56,
      "slot": "endpoint[13]"
    },
    {
      "row": 57,
      "slot": "endpoint[13]"
    },
    {
      "row": 58,
      "slot": "endpoint[14]"
    },
    {
      "row": 59,
      "slot": "endpoint[15]"
    },
    {
      "row": 60,
      "slot": "endpoint[15]"
    },
    {
      "row": 61,
      "slot": "endpoint[16]"
    },
    {
      "row": 62,
      "slot": "endpoint[16]"
    },
    {
      "row": 63,
      "slot": "endpoint[16]"
    },
    {
      "row": 64,
      "slot": "endpoint[17]"
    },
    {
      "row": 65,
      "slot": "endpoint[0]"
    },
    {
      "row": 66,
      "slot": "endpoint[12]"
    },
    {
      "row": 67,
      "slot": "endpoint[13]"
    },
    {
      "row": 68,
      "slot": "endpoint[13]"
    },
    {
      "row": 69,
      "slot": "endpoint[14]"
    },
    {
      "row": 70,
      "slot": "endpoint[15]"
    },
    {
      "row": 71,
      "slot": "endpoint[15]"
    },
    {
      "row": 72,
      "slot": "endpoint[16]"
    },
    {
      "row": 73,
      "slot": "endpoint[16]"
    },
    {
      "row": 74,
      "slot": "endpoint[16]"
    },
    {
      "row": 75,
      "slot": "endpoint[17]"
    },
    {
      "row": 76,
      "slot": "endpoint[1]"
    },
    {
      "row": 77,
      "slot": "endpoint[12]"
    },
    {
      "row": 78,
      "slot": "endpoint[13]"
    },
    {
      "row": 79,
      "slot": "endpoint[12]"
    },
    {
      "row": 80,
      "slot": "endpoint[12]"
    },
    {
      "row": 81,
      "slot": "endpoint[12]"
    },
    {
      "row": 82,
      "slot": "endpoint[12]"
    },
    {
      "row": 83,
      "slot": "endpoint[2]"
    },
    {
      "row": 84,
      "slot": "endpoint[12]"
    },
    {
      "row": 85,
      "slot": "endpoint[12]"
    },
    {
      "row": 86,
      "slot": "endpoint[12]"
    },
    {
      "row": 87,
      "slot": "endpoint[3]"
    },
    {
      "row": 88,
      "slot": "endpoint[12]"
    },
    {
      "row": 89,
      "slot": "endpoint[12]"
    },
    {
      "row": 90,
      "slot": "endpoint[12]"
    },
    {
      "row": 91,
      "slot": "endpoint[4]"
    },
    {
      "row": 92,
      "slot": "endpoint[12]"
    },
    {
      "row": 93,
      "slot": "endpoint[12]"
    },
    {
      "row": 94,
      "slot": "endpoint[5]"
    },
    {
      "row": 95,
      "slot": "chart[6]"
    },
    {
      "row": 96,
      "slot": "endpoint[12]"
    },
    {
      "row": 97,
      "slot": "chart[7]"
    },
    {
      "row": 98,
      "slot": "cut[6]"
    },
    {
      "row": 99,
      "slot": "endpoint[12]"
    },
    {
      "row": 100,
      "slot": "cut[7]"
    },
    {
      "row": 101,
      "slot": "endpoint[12]"
    },
    {
      "row": 102,
      "slot": "cut[8]"
    },
    {
      "row": 103,
      "slot": "cut[9]"
    },
    {
      "row": 104,
      "slot": "cut[10]"
    },
    {
      "row": 105,
      "slot": "cut[11]"
    },
    {
      "row": 106,
      "slot": "cut[11]"
    },
    {
      "row": 107,
      "slot": "endpoint[12]"
    },
    {
      "row": 108,
      "slot": "endpoint[13]"
    },
    {
      "row": 109,
      "slot": "endpoint[13]"
    },
    {
      "row": 110,
      "slot": "endpoint[14]"
    },
    {
      "row": 111,
      "slot": "endpoint[15]"
    },
    {
      "row": 112,
      "slot": "endpoint[15]"
    },
    {
      "row": 113,
      "slot": "endpoint[16]"
    },
    {
      "row": 114,
      "slot": "endpoint[16]"
    },
    {
      "row": 115,
      "slot": "endpoint[16]"
    },
    {
      "row": 116,
      "slot": "endpoint[17]"
    },
    {
      "row": 117,
      "slot": "endpoint[6]"
    },
    {
      "row": 118,
      "slot": "endpoint[12]"
    },
    {
      "row": 119,
      "slot": "endpoint[13]"
    },
    {
      "row": 120,
      "slot": "endpoint[13]"
    },
    {
      "row": 121,
      "slot": "endpoint[14]"
    },
    {
      "row": 122,
      "slot": "endpoint[15]"
    },
    {
      "row": 123,
      "slot": "endpoint[15]"
    },
    {
      "row": 124,
      "slot": "endpoint[16]"
    },
    {
      "row": 125,
      "slot": "endpoint[16]"
    },
    {
      "row": 126,
      "slot": "endpoint[16]"
    },
    {
      "row": 127,
      "slot": "endpoint[17]"
    },
    {
      "row": 128,
      "slot": "endpoint[7]"
    },
    {
      "row": 129,
      "slot": "endpoint[12]"
    },
    {
      "row": 130,
      "slot": "endpoint[13]"
    },
    {
      "row": 131,
      "slot": "endpoint[12]"
    },
    {
      "row": 132,
      "slot": "endpoint[12]"
    },
    {
      "row": 133,
      "slot": "endpoint[12]"
    },
    {
      "row": 134,
      "slot": "endpoint[12]"
    },
    {
      "row": 135,
      "slot": "endpoint[8]"
    },
    {
      "row": 136,
      "slot": "endpoint[12]"
    },
    {
      "row": 137,
      "slot": "endpoint[12]"
    },
    {
      "row": 138,
      "slot": "endpoint[12]"
    },
    {
      "row": 139,
      "slot": "endpoint[9]"
    },
    {
      "row": 140,
      "slot": "endpoint[12]"
    },
    {
      "row": 141,
      "slot": "endpoint[12]"
    },
    {
      "row": 142,
      "slot": "endpoint[12]"
    },
    {
      "row": 143,
      "slot": "endpoint[10]"
    },
    {
      "row": 144,
      "slot": "endpoint[12]"
    },
    {
      "row": 145,
      "slot": "endpoint[12]"
    },
    {
      "row": 146,
      "slot": "endpoint[11]"
    },
    {
      "row": 147,
      "slot": "chart[14]"
    },
    {
      "row": 148,
      "slot": "endpoint[12]"
    },
    {
      "row": 149,
      "slot": "chart[15]"
    },
    {
      "row": 150,
      "slot": "endpoint[12]"
    },
    {
      "row": 151,
      "slot": "endpoint[12]"
    },
    {
      "row": 152,
      "slot": "endpoint[12]"
    },
    {
      "row": 153,
      "slot": "endpoint[12]"
    },
    {
      "row": 154,
      "slot": "endpoint[12]"
    },
    {
      "row": 155,
      "slot": "endpoint[12]"
    },
    {
      "row": 156,
      "slot": "endpoint[12]"
    },
    {
      "row": 157,
      "slot": "endpoint[12]"
    },
    {
      "row": 158,
      "slot": "direct[0]"
    },
    {
      "row": 159,
      "slot": "direct[0]"
    },
    {
      "row": 160,
      "slot": "direct[0]"
    },
    {
      "row": 161,
      "slot": "direct[1]"
    },
    {
      "row": 162,
      "slot": "direct[2]"
    },
    {
      "row": 163,
      "slot": "direct[3]"
    },
    {
      "row": 164,
      "slot": "direct[4]"
    },
    {
      "row": 165,
      "slot": "direct[2]"
    },
    {
      "row": 166,
      "slot": "direct[3]"
    },
    {
      "row": 167,
      "slot": "direct[3]"
    },
    {
      "row": 168,
      "slot": "direct[3]"
    },
    {
      "row": 169,
      "slot": "direct[4]"
    },
    {
      "row": 170,
      "slot": "direct[3]"
    },
    {
      "row": 171,
      "slot": "direct[3]"
    },
    {
      "row": 172,
      "slot": "direct[4]"
    },
    {
      "row": 173,
      "slot": "direct[4]"
    },
    {
      "row": 174,
      "slot": "endpoint[12]"
    },
    {
      "row": 175,
      "slot": "endpoint[12]"
    },
    {
      "row": 176,
      "slot": "endpoint[13]"
    },
    {
      "row": 177,
      "slot": "direct[4]"
    },
    {
      "row": 178,
      "slot": "direct[5]"
    },
    {
      "row": 179,
      "slot": "direct[4]"
    },
    {
      "row": 180,
      "slot": "direct[4]"
    },
    {
      "row": 181,
      "slot": "endpoint[14]"
    },
    {
      "row": 182,
      "slot": "endpoint[14]"
    },
    {
      "row": 183,
      "slot": "endpoint[15]"
    },
    {
      "row": 184,
      "slot": "direct[4]"
    },
    {
      "row": 185,
      "slot": "direct[4]"
    },
    {
      "row": 186,
      "slot": "direct[4]"
    },
    {
      "row": 187,
      "slot": "direct[4]"
    },
    {
      "row": 188,
      "slot": "direct[4]"
    },
    {
      "row": 189,
      "slot": "direct[4]"
    },
    {
      "row": 190,
      "slot": "direct[4]"
    },
    {
      "row": 191,
      "slot": "endpoint[12]"
    },
    {
      "row": 192,
      "slot": "endpoint[12]"
    },
    {
      "row": 193,
      "slot": "endpoint[13]"
    },
    {
      "row": 194,
      "slot": "direct[4]"
    },
    {
      "row": 195,
      "slot": "direct[5]"
    },
    {
      "row": 196,
      "slot": "direct[4]"
    },
    {
      "row": 197,
      "slot": "direct[4]"
    },
    {
      "row": 198,
      "slot": "endpoint[14]"
    },
    {
      "row": 199,
      "slot": "endpoint[14]"
    },
    {
      "row": 200,
      "slot": "endpoint[15]"
    },
    {
      "row": 201,
      "slot": "direct[4]"
    },
    {
      "row": 202,
      "slot": "direct[4]"
    },
    {
      "row": 203,
      "slot": "direct[4]"
    },
    {
      "row": 204,
      "slot": "direct[4]"
    },
    {
      "row": 205,
      "slot": "direct[4]"
    },
    {
      "row": 206,
      "slot": "direct[4]"
    },
    {
      "row": 207,
      "slot": "direct[4]"
    },
    {
      "row": 208,
      "slot": "cut[0]"
    },
    {
      "row": 209,
      "slot": "endpoint[12]"
    },
    {
      "row": 210,
      "slot": "cut[1]"
    },
    {
      "row": 211,
      "slot": "endpoint[12]"
    },
    {
      "row": 212,
      "slot": "cut[2]"
    },
    {
      "row": 213,
      "slot": "cut[3]"
    },
    {
      "row": 214,
      "slot": "cut[4]"
    },
    {
      "row": 215,
      "slot": "cut[5]"
    },
    {
      "row": 216,
      "slot": "cut[5]"
    },
    {
      "row": 217,
      "slot": "endpoint[12]"
    },
    {
      "row": 218,
      "slot": "endpoint[12]"
    },
    {
      "row": 219,
      "slot": "cut[4]"
    },
    {
      "row": 220,
      "slot": "endpoint[13]"
    },
    {
      "row": 221,
      "slot": "cut[5]"
    },
    {
      "row": 222,
      "slot": "endpoint[12]"
    },
    {
      "row": 223,
      "slot": "endpoint[13]"
    },
    {
      "row": 224,
      "slot": "endpoint[13]"
    },
    {
      "row": 225,
      "slot": "endpoint[14]"
    },
    {
      "row": 226,
      "slot": "endpoint[15]"
    },
    {
      "row": 227,
      "slot": "endpoint[15]"
    },
    {
      "row": 228,
      "slot": "endpoint[16]"
    },
    {
      "row": 229,
      "slot": "endpoint[16]"
    },
    {
      "row": 230,
      "slot": "endpoint[16]"
    },
    {
      "row": 231,
      "slot": "endpoint[17]"
    },
    {
      "row": 232,
      "slot": "endpoint[0]"
    },
    {
      "row": 233,
      "slot": "endpoint[12]"
    },
    {
      "row": 234,
      "slot": "endpoint[13]"
    },
    {
      "row": 235,
      "slot": "endpoint[13]"
    },
    {
      "row": 236,
      "slot": "endpoint[14]"
    },
    {
      "row": 237,
      "slot": "endpoint[15]"
    },
    {
      "row": 238,
      "slot": "endpoint[15]"
    },
    {
      "row": 239,
      "slot": "endpoint[16]"
    },
    {
      "row": 240,
      "slot": "endpoint[16]"
    },
    {
      "row": 241,
      "slot": "endpoint[16]"
    },
    {
      "row": 242,
      "slot": "endpoint[17]"
    },
    {
      "row": 243,
      "slot": "endpoint[1]"
    },
    {
      "row": 244,
      "slot": "endpoint[12]"
    },
    {
      "row": 245,
      "slot": "endpoint[13]"
    },
    {
      "row": 246,
      "slot": "endpoint[12]"
    },
    {
      "row": 247,
      "slot": "endpoint[12]"
    },
    {
      "row": 248,
      "slot": "endpoint[12]"
    },
    {
      "row": 249,
      "slot": "endpoint[12]"
    },
    {
      "row": 250,
      "slot": "endpoint[2]"
    },
    {
      "row": 251,
      "slot": "endpoint[12]"
    },
    {
      "row": 252,
      "slot": "endpoint[12]"
    },
    {
      "row": 253,
      "slot": "endpoint[12]"
    },
    {
      "row": 254,
      "slot": "endpoint[3]"
    },
    {
      "row": 255,
      "slot": "cut[6]"
    },
    {
      "row": 256,
      "slot": "endpoint[12]"
    },
    {
      "row": 257,
      "slot": "cut[7]"
    },
    {
      "row": 258,
      "slot": "endpoint[12]"
    },
    {
      "row": 259,
      "slot": "cut[8]"
    },
    {
      "row": 260,
      "slot": "cut[9]"
    },
    {
      "row": 261,
      "slot": "cut[10]"
    },
    {
      "row": 262,
      "slot": "cut[11]"
    },
    {
      "row": 263,
      "slot": "cut[11]"
    },
    {
      "row": 264,
      "slot": "endpoint[12]"
    },
    {
      "row": 265,
      "slot": "endpoint[12]"
    },
    {
      "row": 266,
      "slot": "cut[10]"
    },
    {
      "row": 267,
      "slot": "endpoint[13]"
    },
    {
      "row": 268,
      "slot": "cut[11]"
    },
    {
      "row": 269,
      "slot": "endpoint[12]"
    },
    {
      "row": 270,
      "slot": "endpoint[13]"
    },
    {
      "row": 271,
      "slot": "endpoint[13]"
    },
    {
      "row": 272,
      "slot": "endpoint[14]"
    },
    {
      "row": 273,
      "slot": "endpoint[15]"
    },
    {
      "row": 274,
      "slot": "endpoint[15]"
    },
    {
      "row": 275,
      "slot": "endpoint[16]"
    },
    {
      "row": 276,
      "slot": "endpoint[16]"
    },
    {
      "row": 277,
      "slot": "endpoint[16]"
    },
    {
      "row": 278,
      "slot": "endpoint[17]"
    },
    {
      "row": 279,
      "slot": "endpoint[6]"
    },
    {
      "row": 280,
      "slot": "endpoint[12]"
    },
    {
      "row": 281,
      "slot": "endpoint[13]"
    },
    {
      "row": 282,
      "slot": "endpoint[13]"
    },
    {
      "row": 283,
      "slot": "endpoint[14]"
    },
    {
      "row": 284,
      "slot": "endpoint[15]"
    },
    {
      "row": 285,
      "slot": "endpoint[15]"
    },
    {
      "row": 286,
      "slot": "endpoint[16]"
    },
    {
      "row": 287,
      "slot": "endpoint[16]"
    },
    {
      "row": 288,
      "slot": "endpoint[16]"
    },
    {
      "row": 289,
      "slot": "endpoint[17]"
    },
    {
      "row": 290,
      "slot": "endpoint[7]"
    },
    {
      "row": 291,
      "slot": "endpoint[12]"
    },
    {
      "row": 292,
      "slot": "endpoint[13]"
    },
    {
      "row": 293,
      "slot": "endpoint[12]"
    },
    {
      "row": 294,
      "slot": "endpoint[12]"
    },
    {
      "row": 295,
      "slot": "endpoint[12]"
    },
    {
      "row": 296,
      "slot": "endpoint[12]"
    },
    {
      "row": 297,
      "slot": "endpoint[8]"
    },
    {
      "row": 298,
      "slot": "endpoint[12]"
    },
    {
      "row": 299,
      "slot": "endpoint[12]"
    },
    {
      "row": 300,
      "slot": "endpoint[12]"
    },
    {
      "row": 301,
      "slot": "endpoint[9]"
    },
    {
      "row": 302,
      "slot": "endpoint[12]"
    },
    {
      "row": 303,
      "slot": "endpoint[12]"
    },
    {
      "row": 304,
      "slot": "endpoint[12]"
    },
    {
      "row": 305,
      "slot": "endpoint[12]"
    },
    {
      "row": 306,
      "slot": "endpoint[12]"
    },
    {
      "row": 307,
      "slot": "endpoint[12]"
    },
    {
      "row": 308,
      "slot": "endpoint[12]"
    },
    {
      "row": 309,
      "slot": "direct[0]"
    },
    {
      "row": 310,
      "slot": "direct[1]"
    },
    {
      "row": 311,
      "slot": "direct[1]"
    },
    {
      "row": 312,
      "slot": "direct[2]"
    },
    {
      "row": 313,
      "slot": "direct[3]"
    },
    {
      "row": 314,
      "slot": "direct[4]"
    },
    {
      "row": 315,
      "slot": "direct[5]"
    },
    {
      "row": 316,
      "slot": "direct[4]"
    },
    {
      "row": 317,
      "slot": "direct[6]"
    },
    {
      "row": 318,
      "slot": "direct[7]"
    },
    {
      "row": 319,
      "slot": "direct[6]"
    },
    {
      "row": 320,
      "slot": "direct[7]"
    },
    {
      "row": 321,
      "slot": "direct[7]"
    },
    {
      "row": 322,
      "slot": "direct[7]"
    },
    {
      "row": 323,
      "slot": "direct[7]"
    },
    {
      "row": 324,
      "slot": "direct[5]"
    },
    {
      "row": 325,
      "slot": "direct[8]"
    },
    {
      "row": 326,
      "slot": "direct[9]"
    },
    {
      "row": 327,
      "slot": "direct[8]"
    },
    {
      "row": 328,
      "slot": "direct[10]"
    },
    {
      "row": 329,
      "slot": "direct[11]"
    },
    {
      "row": 330,
      "slot": "direct[11]"
    },
    {
      "row": 331,
      "slot": "direct[12]"
    },
    {
      "row": 332,
      "slot": "direct[12]"
    },
    {
      "row": 333,
      "slot": "direct[13]"
    },
    {
      "row": 334,
      "slot": "direct[13]"
    },
    {
      "row": 335,
      "slot": "direct[13]"
    },
    {
      "row": 336,
      "slot": "direct[14]"
    },
    {
      "row": 337,
      "slot": "direct[15]"
    },
    {
      "row": 338,
      "slot": "direct[14]"
    },
    {
      "row": 339,
      "slot": "direct[15]"
    },
    {
      "row": 340,
      "slot": "direct[16]"
    },
    {
      "row": 341,
      "slot": "direct[16]"
    },
    {
      "row": 342,
      "slot": "direct[1]"
    },
    {
      "row": 343,
      "slot": "direct[1]"
    },
    {
      "row": 344,
      "slot": "direct[2]"
    },
    {
      "row": 345,
      "slot": "direct[5]"
    },
    {
      "row": 346,
      "slot": "direct[5]"
    },
    {
      "row": 347,
      "slot": "direct[6]"
    },
    {
      "row": 348,
      "slot": "direct[8]"
    },
    {
      "row": 349,
      "slot": "direct[9]"
    },
    {
      "row": 350,
      "slot": "direct[10]"
    },
    {
      "row": 351,
      "slot": "direct[11]"
    },
    {
      "row": 352,
      "slot": "direct[12]"
    },
    {
      "row": 353,
      "slot": "direct[13]"
    },
    {
      "row": 354,
      "slot": "direct[14]"
    },
    {
      "row": 355,
      "slot": "direct[15]"
    },
    {
      "row": 356,
      "slot": "direct[16]"
    },
    {
      "row": 357,
      "slot": "direct[1]"
    },
    {
      "row": 358,
      "slot": "direct[2]"
    },
    {
      "row": 359,
      "slot": "direct[1]"
    },
    {
      "row": 360,
      "slot": "direct[3]"
    },
    {
      "row": 361,
      "slot": "direct[4]"
    },
    {
      "row": 362,
      "slot": "direct[1]"
    },
    {
      "row": 363,
      "slot": "direct[2]"
    },
    {
      "row": 364,
      "slot": "direct[1]"
    },
    {
      "row": 365,
      "slot": "direct[3]"
    },
    {
      "row": 366,
      "slot": "direct[7]"
    },
    {
      "row": 367,
      "slot": "direct[1]"
    },
    {
      "row": 368,
      "slot": "direct[2]"
    },
    {
      "row": 369,
      "slot": "direct[1]"
    },
    {
      "row": 370,
      "slot": "direct[3]"
    },
    {
      "row": 371,
      "slot": "direct[0]"
    },
    {
      "row": 372,
      "slot": "direct[4]"
    },
    {
      "row": 373,
      "slot": "direct[7]"
    },
    {
      "row": 374,
      "slot": "direct[0]"
    },
    {
      "row": 375,
      "slot": "direct[1]"
    },
    {
      "row": 376,
      "slot": "direct[2]"
    },
    {
      "row": 377,
      "slot": "direct[1]"
    },
    {
      "row": 378,
      "slot": "direct[1]"
    },
    {
      "row": 379,
      "slot": "direct[2]"
    },
    {
      "row": 380,
      "slot": "direct[3]"
    },
    {
      "row": 381,
      "slot": "direct[14]"
    },
    {
      "row": 382,
      "slot": "direct[15]"
    },
    {
      "row": 383,
      "slot": "direct[16]"
    },
    {
      "row": 384,
      "slot": "direct[0]"
    },
    {
      "row": 385,
      "slot": "direct[1]"
    },
    {
      "row": 386,
      "slot": "direct[1]"
    },
    {
      "row": 387,
      "slot": "direct[2]"
    },
    {
      "row": 388,
      "slot": "direct[3]"
    },
    {
      "row": 389,
      "slot": "direct[4]"
    },
    {
      "row": 390,
      "slot": "direct[5]"
    },
    {
      "row": 391,
      "slot": "direct[4]"
    },
    {
      "row": 392,
      "slot": "direct[6]"
    },
    {
      "row": 393,
      "slot": "direct[7]"
    },
    {
      "row": 394,
      "slot": "direct[6]"
    },
    {
      "row": 395,
      "slot": "direct[7]"
    },
    {
      "row": 396,
      "slot": "direct[7]"
    },
    {
      "row": 397,
      "slot": "direct[7]"
    },
    {
      "row": 398,
      "slot": "direct[7]"
    },
    {
      "row": 399,
      "slot": "direct[5]"
    },
    {
      "row": 400,
      "slot": "direct[8]"
    },
    {
      "row": 401,
      "slot": "direct[9]"
    },
    {
      "row": 402,
      "slot": "direct[8]"
    },
    {
      "row": 403,
      "slot": "direct[10]"
    },
    {
      "row": 404,
      "slot": "direct[11]"
    },
    {
      "row": 405,
      "slot": "direct[11]"
    },
    {
      "row": 406,
      "slot": "direct[12]"
    },
    {
      "row": 407,
      "slot": "direct[12]"
    },
    {
      "row": 408,
      "slot": "direct[13]"
    },
    {
      "row": 409,
      "slot": "direct[13]"
    },
    {
      "row": 410,
      "slot": "direct[13]"
    },
    {
      "row": 411,
      "slot": "direct[14]"
    },
    {
      "row": 412,
      "slot": "direct[15]"
    },
    {
      "row": 413,
      "slot": "direct[14]"
    },
    {
      "row": 414,
      "slot": "direct[15]"
    },
    {
      "row": 415,
      "slot": "direct[16]"
    },
    {
      "row": 416,
      "slot": "direct[16]"
    },
    {
      "row": 417,
      "slot": "direct[1]"
    },
    {
      "row": 418,
      "slot": "direct[1]"
    },
    {
      "row": 419,
      "slot": "direct[2]"
    },
    {
      "row": 420,
      "slot": "direct[5]"
    },
    {
      "row": 421,
      "slot": "direct[5]"
    },
    {
      "row": 422,
      "slot": "direct[6]"
    },
    {
      "row": 423,
      "slot": "direct[8]"
    },
    {
      "row": 424,
      "slot": "direct[9]"
    },
    {
      "row": 425,
      "slot": "direct[10]"
    },
    {
      "row": 426,
      "slot": "direct[11]"
    },
    {
      "row": 427,
      "slot": "direct[12]"
    },
    {
      "row": 428,
      "slot": "direct[13]"
    },
    {
      "row": 429,
      "slot": "direct[14]"
    },
    {
      "row": 430,
      "slot": "direct[15]"
    },
    {
      "row": 431,
      "slot": "direct[16]"
    },
    {
      "row": 432,
      "slot": "direct[1]"
    },
    {
      "row": 433,
      "slot": "direct[2]"
    },
    {
      "row": 434,
      "slot": "direct[1]"
    },
    {
      "row": 435,
      "slot": "direct[3]"
    },
    {
      "row": 436,
      "slot": "direct[4]"
    },
    {
      "row": 437,
      "slot": "direct[1]"
    },
    {
      "row": 438,
      "slot": "direct[2]"
    },
    {
      "row": 439,
      "slot": "direct[1]"
    },
    {
      "row": 440,
      "slot": "direct[3]"
    },
    {
      "row": 441,
      "slot": "direct[7]"
    },
    {
      "row": 442,
      "slot": "direct[1]"
    },
    {
      "row": 443,
      "slot": "direct[2]"
    },
    {
      "row": 444,
      "slot": "direct[1]"
    },
    {
      "row": 445,
      "slot": "direct[3]"
    },
    {
      "row": 446,
      "slot": "direct[0]"
    },
    {
      "row": 447,
      "slot": "direct[4]"
    },
    {
      "row": 448,
      "slot": "direct[7]"
    },
    {
      "row": 449,
      "slot": "direct[0]"
    },
    {
      "row": 450,
      "slot": "direct[1]"
    },
    {
      "row": 451,
      "slot": "direct[2]"
    },
    {
      "row": 452,
      "slot": "direct[1]"
    },
    {
      "row": 453,
      "slot": "direct[1]"
    },
    {
      "row": 454,
      "slot": "direct[2]"
    },
    {
      "row": 455,
      "slot": "direct[3]"
    },
    {
      "row": 456,
      "slot": "direct[14]"
    },
    {
      "row": 457,
      "slot": "direct[15]"
    },
    {
      "row": 458,
      "slot": "direct[16]"
    }
  ],
  "selected_limits": {
    "phase.graphs": 1,
    "phase.legs": 2,
    "phase.bodies": 1,
    "phase.sectors": 3,
    "phase.timing": 6,
    "source_guards": 64,
    "projection_guards": 256,
    "definition_guards": 31,
    "sole_extrema": 4,
    "source_coordinates": 8,
    "intersection_operations": 4,
    "midpoint_operations": 4,
    "allocation_operations": 6,
    "pressure_candidates": 2,
    "disk_edges": 16,
    "self_body_guards": 63,
    "self_pairs": 105,
    "self_axes": 1470,
    "self_signed_trials": 2940,
    "self_owners": 14,
    "self_hip_complements": 2,
    "unit_axis_operations": 24,
    "output_bytes": 16777216,
    "construction_guards": 75,
    "construction_operations": 458
  },
  "proposed_matched_control_count": 107,
  "finite_counter_integer_envelope": {
    "aggregate_upper": 314687,
    "bound_formula": "107*(default+1); maximum107*2941",
    "check_source_sum": 507392,
    "selected_check_upper": 524288,
    "next_slot": 108,
    "eligibility_bits": 16,
    "consumer_count": 107,
    "SKIP_upper": 107,
    "actual_counts": "UNKNOWN"
  },
  "BASE_no_later_writer_invariant": {
    "base_lo_only_writer": 154,
    "base_hi_only_writer": 157,
    "later_assignments": 0,
    "guard_identity_semantics": "G30/G36/G56 check prior G24 readiness and use immutable fields; no old-value comparison, saved snapshot or duplicate BASE owner"
  },
  "closed_Linux_profile": {
    "assignments": {
      "LD_BIND_NOW": "1",
      "LC_ALL": "C",
      "LANG": "C",
      "GLIBC_TUNABLES": "glibc.malloc.tcache_count=0"
    },
    "unsets": [
      "LD_PRELOAD",
      "LD_AUDIT",
      "LD_LIBRARY_PATH",
      "LD_PROFILE",
      "LD_DEBUG",
      "LD_DEBUG_OUTPUT",
      "LD_BIND_NOT",
      "LD_ORIGIN_PATH",
      "LD_ASSUME_KERNEL",
      "LD_HWCAP_MASK",
      "LD_TRACE_LOADED_OBJECTS",
      "LD_USE_LOAD_BIAS",
      "GLIBCXX_TUNABLES",
      "MALLOC_CHECK_",
      "MALLOC_PERTURB_",
      "MALLOC_ARENA_MAX",
      "MALLOC_ARENA_TEST",
      "MALLOC_MMAP_THRESHOLD_",
      "MALLOC_MMAP_MAX_",
      "MALLOC_TRIM_THRESHOLD_",
      "MALLOC_TOP_PAD_",
      "MALLOC_MXFAST_",
      "LD_DYNAMIC_WEAK",
      "LD_SHOW_AUXV",
      "LD_PROFILE_OUTPUT",
      "LD_VERBOSE",
      "LD_WARN"
    ],
    "actual_generated_environment_ELF_and_startup_applicability": "UNKNOWN until implementation admission"
  }
}
```
