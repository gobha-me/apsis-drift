# Endpoint05 BOTH-hip arithmetic and authority registration — #519

This [#519](https://github.com/gobha-me/apsis-drift/issues/519) contract follows the independently reviewed [BOTH-hip proof](ORIGIN_BOARDING_BOTH_HIP_CONSTRUCTION.md), merged at `1b397b154a65253965bd376bb33bf307aa79008e`. It specifies one finite version5 constructor, fresh authority, typed ownership and disjoint error alternatives for implementation in a separate child. No new numerical posture, source coordinates, source sign, angle or derived geometric bound was evaluated. Actual layouts, frames, capacities, external-library/profile applicability and feasibility remain UNKNOWN. The source forecasts are admission constraints; compiler measurement and a separately frozen first observation must follow implementation.

## 1. Preserved recipe and new meaning

One proposed version5 Candidate `root_y_both_hip_ankle_reach_roll_slice=0` starts from the exact immutable original source recipe, never Endpoint04 plus correction. Provider-only public function `assess_origin_boarding_intermediate_endpoint05(const OriginBoardingIntermediatePauseSupport&, BoardingIntermediateEndpoint05Candidate)->BoardingIntermediateEndpoint05Expected`; private `detail::intermediate_endpoint05_bounded(provider,Candidate,Limits={})` returns the same type. No input report/Y/cut/claimed axis/old key or second numerical API.

Keep original ordered rootX/Z, root-yaw half.125, sole-yaw half0, torso-half−.30, both feet/source planes, zero humps, folded arms, lengths, PORT1/16/STAR15/16 share and exact2second hold. Stored constants mean their unchanged binary64 bits, notably `kBoardingSelfHipLengthMetres=0x1.bb0cd605d7512p-3`, radiusR=.105 and pelvisHy=.12. Old enrollment must authenticate original part identities and both regions1/2/pelvis upright ROOT frame. Root offset uses unchanged ±.14; ankle offset unchanged.1. Only the two rootY coordinate packets become the one stored midpoint `{Y,+0,+0}count1` after direct verification. Preserve original full-body graph, joint/rate domains, contact/load and all SELF policies.

For each side exact heldsole frame is WORLD identity; this follows from authenticated yaw0, not interval orthogonality. The retained X/Z are the actual same-chart displacement, A the independent source vertical origin, W=−Z. New closed cuts restrict the old positive-knee/F1>0 component by the proved universal G1 descent inequalities. Threshold/sign guards deliberately target a conservative supported subset. Inconclusive/empty intervals do not refute all heights.

## 2. Exact schema and authority

Fresh `BoardingIntermediateEndpoint05` versions of Diagnostic/Cell/Refusal/Counters/Expected/State/Condition/SelfStage/Certificate follow the full Endpoint04 field order and semantics except the explicit modifications here; Expected remains `std::expected<Diagnostic,std::string>`. Version5 and new Candidate are distinct, never aliases. State0..5, Condition0..41, SelfStage0..20 and Certificate0..6 keep their original ordinal meanings. SliceCondition0..13 keep Endpoint04 meanings; append `hip_threshold_domain=14`, `hip_descent_unavailable=15`, `hip_verification_inconclusive=16`. An ordinary sufficient domain/check failure gives State::unresolved/Condition::slice_unavailable with the literal new SliceCondition. Identity→slice_identity; unsupported/FP/nonfinite/invalid root→unsupported_arithmetic. Guard/operation charge failure gives capacity/slice_guard_capacity or slice_operation_capacity and does not clear earlier supported arithmetic or mutate its next attempted bit.

```
struct BoardingIntermediateEndpoint05SliceEvidence {
  std::array<std::uint64_t,6> operation_attempted{}, operation_written{};
  std::uint64_t guard_attempted{}, guard_written{};
  BoardingFootSiteScalarBounds limiting_bound;
  double y{}, lo{}, hi{};
  std::uint16_t operation{65535};
  std::uint8_t guard{255}, side{255};
  BoardingIntermediateEndpoint05SliceCondition condition{};
  bool arithmetic_supported{}, complete{};
  std::uint8_t preflight_guards{}, zero_mask{};
};
```

Refusal preserves every old full field/nested PhaseRefusal; ONLY its `operation` becomes uint16/default65535, independently of unchanged self_region/self_axis/self_sign255 and self_pair65535. Do not retain the old184 target as a measured or guaranteed new layout. Diagnostic.stop_operation also becomes uint16/default65535; keep the following stop_self_region/axis/sign uint8/default255 as separate members, and preserve full width in every assignment/copy/hash/print. Unit24 cursor remains uint8/sentinel255: it is not this constructor cursor. Cell has no duplicate Slice/cut arrays. Counters remain uint64 phase_calls/originalphase/familiar19outer fields; source IDs/flags retained. No extra persistent threshold/radial/direct/axis certificate or p/q history. Slice lo/hi/Y are the only new selected scalar outputs and are read solely under their literal written bits.

Limits keeps all25 Endpoint04 field names/order and first23 default values. Proposed construction_guards=50/construction_operations=343. This is a new registration, not a change to old limits. Fixed public FP-first count1 is not lowerable. Output16MiB, sharedlog 16KiB, source creator 8192, worker 49152, numericalhelper 4096 unchanged. The complete fresh member/padding forecast selects ceilings Expected1664/Cell11032/Slice176/Refusal192, optionalRefusal200, Limits/Work200, Key16, Context/Token40 and validatorExpected40. Predicted Expected1640/Diagnostic1632 are source-layout forecasts only; actual sizeof remains UNKNOWN. Exact inventories and source maps below justify each selected ceiling independently; no historical actual object admits the fresh type.

Fresh ProgramKey(doubleY,Candidate) stores Y/version5/Candidate0 with private constructor friending only newbounded. Fresh AdmissionContext privately anchored to actual Diagnostic/Data/parts/ownedRequest/Key; deleted copy/move/all assignments. Fresh CurrentToken only issued by newcurrent_cell after accepted original graph/P251, anchored to that currentContext/owner/cell/Request/Data. Five pointer anchors imply40 reference-slot forecast for Context/Token, but actual sizeof is unknown. Source/constructor functions issue no capability. New source_enroll/construct/canonical/template_valid/current_cell/pressure_bridge/self_bridge/refuse/charge/definition_charge are fresh typed END-suffix functions with Endpoint04 signatures under the new prefix; old header/friend/issuer/CPP prefixes stay byte-exact. Refusal operation widening applies each fresh producer setter and first/terminal copy without narrowing. The new source adapter forwards the old source-only full Refusal.operation with the explicit expression `old.operation == uint8_t{255} ? uint16_t{65535} : static_cast<uint16_t>(old.operation)`. Source S64 reached cursors are at most63, so old255 is unambiguously UNSET there. Every other full field/stage/source prefix is copied by reference without truncation. Nested original PhaseRefusal retains its own unchanged field widths and255 protocol. Fresh Refusal/Diagnostic.stop/Slice operation defaults and reset/stop setters are uint16/65535; every signature/copy/hash/print/current or terminal comparison preserves that width. Guard/side/axis/region and the distinct Cell.Unit24 cursor retain their original255 meanings. When an unset distinct Unit24 cursor is explicitly forwarded to the new shared operation field, translate its protocol sentinel locally; never apply source-sentinel translation to a fresh constructor operation. New operation255 is the VALID zero-based cursor for O256 and must be preserved. No old header/source/friend change is needed.

## 3. Primitive/charging rules

The exact supported ADD/SUB/MUL/DIV/NEG/ABS/SQUARE/CONST/SMALL_ROOT/MAX/MIN/POINT_UPPER/POINT_LOWER/RN rows inherit Endpoint04 sections3–4. DIV remains reciprocal enclosure then multiplication. SMALL_ROOT retains original bounded exact residual correction policy/domain and charged exact0. No solver/trig/tolerance/retry. INTERSECT_NONNEG is legal only with the same-side universal closed endpoint certificate; no other radicand is clipped. All selections retain first operand on equality; canonical+0 zero branches retain the original eight literal semantic bits, remapped to new row numbers. Supported scalar singleton publication is charged, not an uncharged endpoint extraction used as a new argument.

ENDPOINT_DIV is the original `ep4_unit_divide` mathematical operation: supported finite numerator; fixed genuine L1>0; exact zero numerator returns point0; otherwise low/L1 and high/L1, finite checks and outward down/up. It is one charged bounded primitive, including both endpoint divisions; it is not rounded RN(1/L1) normalization. Proposed direct axes use this unchanged mathematical implementation behind fresh typed helpers.

Each upcoming operation writes slice stage20/cursor(row−1)/side and clears only row limiting scratch BEFORE charge; successful charge then increments count/attempted and only THEN executes the deferred expression. Valid write earns written. Guards similarly use stage19/cursor(row−1) and actual charge/read; false readiness leaves written clear. All compound predicates stop at the first failure, attach only its supported bound, and preserve earlier selected lo/hi/Y/zero/masks. Source/Slice arithmetic support lifecycle and first-vs-terminal causes follow Endpoint04. No primitive expression is precomputed before its charge.

The direct knee−hip vector explicitly reconstructs original q=(X*Z/rho,(-h)*Z/rho,−rho), then alpha*d+gamma*q, then original rotation by the AUTHENTICATED exact WORLD-identity sole frame. Each matrix scalar product still has three supported multiplications/two additions. Rounded root-yaw columns are not used to transform this WORLD axis. Exact identity-frame columns are earned by source/seed guard, not guessed from reporting values. The final axis division feeds original `sqrt(ux²+uz²)`, extent and gap; no `sqrt(1−uy²)` substitute or nominal-length renormalization. Positive F1/G1 only adds conservative constructor readiness, not a modified compiler policy.

## 4. Literal343 operation rows

Common side255, PORT0,STAR1. Labels one-based, operation cursor/bit=row−1; word=(row−1)/64, bit=(row−1)%64. Full operations use six words `{UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,0x00000000007fffff}` with unused high41 bits zero. Complete cursor342, never sentinel65535. Count256 already conflicts with old sentinel255 even though four words still suffice through256; this table is343 so both wider cursor and six words are necessary. All scratch reuse waits the last consumer; the complete typed helper inventory and343 destination slots are bound in the ownership supplement below.

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
|O010|common|ADD point1+O009|
|O011|common|SUB point1−O009|
|O012|common|DIV O011/O010 →cos|
|O013|common|MUL point2*point(rootYawHalf)|
|O014|common|DIV O013/O010 →sin|
|O015|common|NEG O014 →negative_sin|
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
|O046|common|SQUARE WHOLE b →rollFactorSquare|
|O047|common|ADD point1+rollFactorSquare →rollNormSquared|
|O048|common|SMALL_ROOT WHOLE rollNormSquared →rollNorm|
|O049|common|DIV point1/WHOLE rollNorm →retained c_roll|
|O050|common|SQUARE pointL →slabSquared|
|O051|common|SQUARE pointR →radiusSquared|
|O052|common|SQUARE pointHy →halfHeightSquared|
|O053|common|ADD slabSquared+radiusSquared →thresholdDen|
|O054|common|SUB thresholdDen−halfHeightSquared →thresholdRad|
|O055|common|SMALL_ROOT WHOLE thresholdRad →thresholdRoot|
|O056|common|MUL pointR*thresholdRoot →radiusThresholdRoot|
|O057|common|MUL pointL*pointHy →slabHalfHeight|
|O058|common|ADD slabHalfHeight+radiusThresholdRoot →thresholdNum|
|O059|common|DIV thresholdNum/thresholdDen →s_req|
|O060|common|MUL pointL*WHOLE s_req →thresholdAxial|
|O061|common|SUB thresholdAxial−pointHy →thresholdUnsquaredSign|
|O062|common|DIV WHOLE s_req/WHOLE c_roll →k_interval|
|O063|common|POINT_UPPER k_interval →retained singleton k_bar|
|O064|common|SQUARE point(k_bar) →kSquare|
|O065|common|SUB point1−kSquare →gRad|
|O066|common|SMALL_ROOT WHOLE gRad →gRoot|
|O067|common|MUL pointL1*gRoot →gInterval|
|O068|common|POINT_LOWER gInterval →retained singleton g_cut|
|O069|PORT|NEG Z_PORT →W|
|O070|PORT|SUB W−pointL1 →cutlowNum|
|O071|PORT|DIV cutlowNum/pointL2 →cutlow|
|O072|PORT|ADD W+pointL1 →cuthiNum|
|O073|PORT|DIV cuthiNum/pointL2 →cuthi|
|O074|PORT|DIV W/Lsum →cutturn|
|O075|PORT|MAX(point−.5,point(cutlow.high)) →base_p (first on equality)|
|O076|PORT|MIN(point+.5,point(cuthi.low)) →q0 (first on equality)|
|O077|PORT|MIN(q0,point(cutturn.low)) →base_q (first on equality)|
|O078|PORT|SUB WHOLE W−point(g_cut) →hipCutNum|
|O079|PORT|DIV WHOLE hipCutNum/pointL2 →hipCutLower|
|O080|PORT|MAX(base_p,point(hipCutLower.high)) →p; first operand wins equality|
|O081|PORT|DIV WHOLE W/pointL2 →hipCutUpper|
|O082|PORT|MIN(base_q,point(hipCutUpper.low)) →q; first operand wins equality|
|O083|PORT|SQUARE point(p) →tau2|
|O084|PORT|SUB point1−tau2 →cosRad|
|O085|PORT|SMALL_ROOT WHOLE cosRad →cosShin|
|O086|PORT|MUL pointL2*cosShin →F2endpoint|
|O087|PORT|MUL pointL2*point(p) →tauL2|
|O088|PORT|SUB W−tauL2 →G1endpoint|
|O089|PORT|SQUARE G1endpoint →g1sq|
|O090|PORT|SUB L1sq−g1sq →thighRad|
|O091|PORT|INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 2|
|O092|PORT|SMALL_ROOT WHOLE clip →F1endpoint|
|O093|PORT|ADD F1endpoint+F2endpoint →rho_p|
|O094|PORT|SQUARE point(q) →tau2|
|O095|PORT|SUB point1−tau2 →cosRad|
|O096|PORT|SMALL_ROOT WHOLE cosRad →cosShin|
|O097|PORT|MUL pointL2*cosShin →F2endpoint|
|O098|PORT|MUL pointL2*point(q) →tauL2|
|O099|PORT|SUB W−tauL2 →G1endpoint|
|O100|PORT|SQUARE G1endpoint →g1sq|
|O101|PORT|SUB L1sq−g1sq →thighRad|
|O102|PORT|INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 3|
|O103|PORT|SMALL_ROOT WHOLE clip →F1endpoint|
|O104|PORT|ADD F1endpoint+F2endpoint →rho_q|
|O105|PORT|POINT_UPPER rho_p →u|
|O106|PORT|POINT_LOWER rho_q →v|
|O107|PORT|SQUARE pointu →u2|
|O108|PORT|SUB u2−point(X2_PORT.low) →ankleLowerRad|
|O109|PORT|MAX_ZERO ankleLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 6|
|O110|PORT|SMALL_ROOT point(selected) →ankleLowerRoot|
|O111|PORT|ADD point(A_PORT.high)+point(ankleLowerRoot.high) →ankleLower (retain.high)|
|O112|PORT|SQUARE pointv →v2|
|O113|PORT|SUB v2−point(X2_PORT.high) →ankleUpperRad|
|O114|PORT|SMALL_ROOT point(ankleUpperRad.low) →ankleUpperRoot|
|O115|PORT|ADD point(A_PORT.low)+point(ankleUpperRoot.low) →ankleUpper (retain.low)|
|O116|PORT|SUB point(m.high)−point(C_PORT.low) →reachLowerRad|
|O117|PORT|MAX_ZERO reachLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 0|
|O118|PORT|SMALL_ROOT point(selected) →reachLowerRoot|
|O119|PORT|ADD point(A_PORT.high)+point(reachLowerRoot.high) →reachLower (retain.high)|
|O120|PORT|SUB point(M.low)−point(C_PORT.high) →reachUpperRad|
|O121|PORT|SMALL_ROOT point(reachUpperRad.low) →reachUpperRoot|
|O122|PORT|ADD point(A_PORT.low)+point(reachUpperRoot.low) →reachUpper (retain.low)|
|O123|PORT|ABS WHOLE X_PORT →retained absX|
|O124|PORT|DIV WHOLE absX/WHOLE b →rollQuotient|
|O125|PORT|ADD point(A_PORT.high)+point(rollQuotient.high) →rollLower (retain.high)|
|O126|STAR|NEG Z_STAR →W|
|O127|STAR|SUB W−pointL1 →cutlowNum|
|O128|STAR|DIV cutlowNum/pointL2 →cutlow|
|O129|STAR|ADD W+pointL1 →cuthiNum|
|O130|STAR|DIV cuthiNum/pointL2 →cuthi|
|O131|STAR|DIV W/Lsum →cutturn|
|O132|STAR|MAX(point−.5,point(cutlow.high)) →base_p (first on equality)|
|O133|STAR|MIN(point+.5,point(cuthi.low)) →q0 (first on equality)|
|O134|STAR|MIN(q0,point(cutturn.low)) →base_q (first on equality)|
|O135|STAR|SUB WHOLE W−point(g_cut) →hipCutNum|
|O136|STAR|DIV WHOLE hipCutNum/pointL2 →hipCutLower|
|O137|STAR|MAX(base_p,point(hipCutLower.high)) →p; first operand wins equality|
|O138|STAR|DIV WHOLE W/pointL2 →hipCutUpper|
|O139|STAR|MIN(base_q,point(hipCutUpper.low)) →q; first operand wins equality|
|O140|STAR|SQUARE point(p) →tau2|
|O141|STAR|SUB point1−tau2 →cosRad|
|O142|STAR|SMALL_ROOT WHOLE cosRad →cosShin|
|O143|STAR|MUL pointL2*cosShin →F2endpoint|
|O144|STAR|MUL pointL2*point(p) →tauL2|
|O145|STAR|SUB W−tauL2 →G1endpoint|
|O146|STAR|SQUARE G1endpoint →g1sq|
|O147|STAR|SUB L1sq−g1sq →thighRad|
|O148|STAR|INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 4|
|O149|STAR|SMALL_ROOT WHOLE clip →F1endpoint|
|O150|STAR|ADD F1endpoint+F2endpoint →rho_p|
|O151|STAR|SQUARE point(q) →tau2|
|O152|STAR|SUB point1−tau2 →cosRad|
|O153|STAR|SMALL_ROOT WHOLE cosRad →cosShin|
|O154|STAR|MUL pointL2*cosShin →F2endpoint|
|O155|STAR|MUL pointL2*point(q) →tauL2|
|O156|STAR|SUB W−tauL2 →G1endpoint|
|O157|STAR|SQUARE G1endpoint →g1sq|
|O158|STAR|SUB L1sq−g1sq →thighRad|
|O159|STAR|INTERSECT_NONNEG thighRad →clip: low<=0 writes canonical+0; otherwise preserve low; high unchanged; branch bit 5|
|O160|STAR|SMALL_ROOT WHOLE clip →F1endpoint|
|O161|STAR|ADD F1endpoint+F2endpoint →rho_q|
|O162|STAR|POINT_UPPER rho_p →u|
|O163|STAR|POINT_LOWER rho_q →v|
|O164|STAR|SQUARE pointu →u2|
|O165|STAR|SUB u2−point(X2_STAR.low) →ankleLowerRad|
|O166|STAR|MAX_ZERO ankleLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 7|
|O167|STAR|SMALL_ROOT point(selected) →ankleLowerRoot|
|O168|STAR|ADD point(A_STAR.high)+point(ankleLowerRoot.high) →ankleLower (retain.high)|
|O169|STAR|SQUARE pointv →v2|
|O170|STAR|SUB v2−point(X2_STAR.high) →ankleUpperRad|
|O171|STAR|SMALL_ROOT point(ankleUpperRad.low) →ankleUpperRoot|
|O172|STAR|ADD point(A_STAR.low)+point(ankleUpperRoot.low) →ankleUpper (retain.low)|
|O173|STAR|SUB point(m.high)−point(C_STAR.low) →reachLowerRad|
|O174|STAR|MAX_ZERO reachLowerRad.high →point: canonical+0 if high<=0, else same high bits; branch bit 1|
|O175|STAR|SMALL_ROOT point(selected) →reachLowerRoot|
|O176|STAR|ADD point(A_STAR.high)+point(reachLowerRoot.high) →reachLower (retain.high)|
|O177|STAR|SUB point(M.low)−point(C_STAR.high) →reachUpperRad|
|O178|STAR|SMALL_ROOT point(reachUpperRad.low) →reachUpperRoot|
|O179|STAR|ADD point(A_STAR.low)+point(reachUpperRoot.low) →reachUpper (retain.low)|
|O180|STAR|ABS WHOLE X_STAR →retained absX|
|O181|STAR|DIV WHOLE absX/WHOLE b →rollQuotient|
|O182|STAR|ADD point(A_STAR.high)+point(rollQuotient.high) →rollLower (retain.high)|
|O183|common|MAX ankleLower_PORT,ankleLower_STAR →lower; first operand on equality|
|O184|common|MAX lower,reachLower_PORT →lower; first operand on equality|
|O185|common|MAX lower,reachLower_STAR →lower; first operand on equality|
|O186|common|MAX lower,rollLower_PORT →lower; first operand on equality|
|O187|common|MAX lower,rollLower_STAR →publish slice.lo; first operand on equality|
|O188|common|MIN ankleUpper_PORT,ankleUpper_STAR →upper; first operand on equality|
|O189|common|MIN upper,reachUpper_PORT →upper; first operand on equality|
|O190|common|MIN upper,reachUpper_STAR →publish slice.hi; first operand on equality|
|O191|common|RN finite SUB hi−lo →width|
|O192|common|RN finite MUL width*.5 →halfWidth|
|O193|common|RN finite ADD lo+halfWidth →ONE storedY; publish slice.y|
|O194|PORT|SUB pointY−WHOLE A_PORT →h|
|O195|PORT|SQUARE h →h2|
|O196|PORT|ADD WHOLE X2_PORT+h2 →rho2|
|O197|PORT|ADD rho2+WHOLE Z2_PORT →D|
|O198|PORT|SMALL_ROOT WHOLE rho2 →rho|
|O199|PORT|ADD WHOLE Delta+D →alphaNum|
|O200|PORT|MUL point2*D →alphaDen|
|O201|PORT|DIV alphaNum/alphaDen →alpha|
|O202|PORT|SUB M−D →outer|
|O203|PORT|SUB D−m →inner|
|O204|PORT|MUL outer*inner →gammaNum|
|O205|PORT|SQUARE D →D2|
|O206|PORT|MUL point4*D2 →gammaDen|
|O207|PORT|DIV gammaNum/gammaDen →gamma2|
|O208|PORT|SMALL_ROOT WHOLE gamma2 →gamma|
|O209|PORT|SUB point1−alpha →beta|
|O210|PORT|MUL alpha*rho →alphaRho|
|O211|PORT|MUL gamma*WHOLE Z_PORT →gammaZ|
|O212|PORT|ADD alphaRho+gammaZ →F1|
|O213|PORT|MUL gamma*rho →gammaRho|
|O214|PORT|MUL alpha*WHOLE Z_PORT →alphaZ|
|O215|PORT|SUB gammaRho−alphaZ →G1|
|O216|PORT|MUL beta*rho →betaRho|
|O217|PORT|SUB betaRho−gammaZ →F2|
|O218|PORT|MUL beta*WHOLE Z_PORT →betaZ|
|O219|PORT|ADD betaZ+gammaRho →G2negative|
|O220|PORT|NEG G2negative →G2|
|O221|PORT|ABS WHOLE G2 →absG2|
|O222|PORT|MUL F2*point.5 →halfF2|
|O223|PORT|MUL absG2*WHOLE sqrt3half →pitchTerm|
|O224|PORT|SUB halfF2−pitchTerm →ankleMargin|
|O225|PORT|MUL h*WHOLE b →rollProduct|
|O226|PORT|SUB rollProduct−WHOLE absX →rollMargin|
|O227|PORT|MUL WHOLE X_PORT*WHOLE Z_PORT →qxNumerator|
|O228|PORT|DIV qxNumerator/WHOLE rho →qx|
|O229|PORT|NEG WHOLE h →negative_h|
|O230|PORT|MUL negative_h*WHOLE Z_PORT →qyNumerator|
|O231|PORT|DIV qyNumerator/WHOLE rho →qy|
|O232|PORT|NEG WHOLE rho →qz|
|O233|PORT|MUL WHOLE alpha*WHOLE X_PORT →alphaDx|
|O234|PORT|MUL WHOLE alpha*WHOLE negative_h →alphaDy|
|O235|PORT|MUL WHOLE alpha*WHOLE Z_PORT →alphaDz|
|O236|PORT|MUL WHOLE gamma*WHOLE qx →gammaQx|
|O237|PORT|MUL WHOLE gamma*WHOLE qy →gammaQy|
|O238|PORT|MUL WHOLE gamma*WHOLE qz →gammaQz|
|O239|PORT|ADD alphaDx+gammaQx →localKneeHipX|
|O240|PORT|ADD alphaDy+gammaQy →localKneeHipY|
|O241|PORT|ADD alphaDz+gammaQz →localKneeHipZ|
|O242|PORT|MUL localKneeHipX*point1 →rotationFirst|
|O243|PORT|MUL localKneeHipY*point0 →rotationSecond|
|O244|PORT|ADD rotationFirst+rotationSecond →rotationPair|
|O245|PORT|MUL localKneeHipZ*point0 →rotationThird|
|O246|PORT|ADD rotationPair+rotationThird →WORLD knee−hip X|
|O247|PORT|MUL localKneeHipX*point0 →rotationFirst|
|O248|PORT|MUL localKneeHipY*point1 →rotationSecond|
|O249|PORT|ADD rotationFirst+rotationSecond →rotationPair|
|O250|PORT|MUL localKneeHipZ*point0 →rotationThird|
|O251|PORT|ADD rotationPair+rotationThird →WORLD knee−hip Y|
|O252|PORT|MUL localKneeHipX*point0 →rotationFirst|
|O253|PORT|MUL localKneeHipY*point0 →rotationSecond|
|O254|PORT|ADD rotationFirst+rotationSecond →rotationPair|
|O255|PORT|MUL localKneeHipZ*point1 →rotationThird|
|O256|PORT|ADD rotationPair+rotationThird →WORLD knee−hip Z|
|O257|PORT|ENDPOINT_DIV WORLD knee−hip X/unchanged L1 →ux|
|O258|PORT|ENDPOINT_DIV WORLD knee−hip Y/unchanged L1 →uy|
|O259|PORT|ENDPOINT_DIV WORLD knee−hip Z/unchanged L1 →uz|
|O260|PORT|SQUARE WHOLE ux →uxSquared|
|O261|PORT|SQUARE WHOLE uz →uzSquared|
|O262|PORT|ADD uxSquared+uzSquared →transverseSquared|
|O263|PORT|SMALL_ROOT WHOLE transverseSquared →transverse|
|O264|PORT|MUL pointL*WHOLE uy →axialExtent|
|O265|PORT|MUL pointR*WHOLE transverse →radialExtent|
|O266|PORT|ADD axialExtent+radialExtent →extent|
|O267|PORT|NEG pointHy →bottom|
|O268|PORT|SUB bottom−WHOLE extent →strictGap|
|O269|STAR|SUB pointY−WHOLE A_STAR →h|
|O270|STAR|SQUARE h →h2|
|O271|STAR|ADD WHOLE X2_STAR+h2 →rho2|
|O272|STAR|ADD rho2+WHOLE Z2_STAR →D|
|O273|STAR|SMALL_ROOT WHOLE rho2 →rho|
|O274|STAR|ADD WHOLE Delta+D →alphaNum|
|O275|STAR|MUL point2*D →alphaDen|
|O276|STAR|DIV alphaNum/alphaDen →alpha|
|O277|STAR|SUB M−D →outer|
|O278|STAR|SUB D−m →inner|
|O279|STAR|MUL outer*inner →gammaNum|
|O280|STAR|SQUARE D →D2|
|O281|STAR|MUL point4*D2 →gammaDen|
|O282|STAR|DIV gammaNum/gammaDen →gamma2|
|O283|STAR|SMALL_ROOT WHOLE gamma2 →gamma|
|O284|STAR|SUB point1−alpha →beta|
|O285|STAR|MUL alpha*rho →alphaRho|
|O286|STAR|MUL gamma*WHOLE Z_STAR →gammaZ|
|O287|STAR|ADD alphaRho+gammaZ →F1|
|O288|STAR|MUL gamma*rho →gammaRho|
|O289|STAR|MUL alpha*WHOLE Z_STAR →alphaZ|
|O290|STAR|SUB gammaRho−alphaZ →G1|
|O291|STAR|MUL beta*rho →betaRho|
|O292|STAR|SUB betaRho−gammaZ →F2|
|O293|STAR|MUL beta*WHOLE Z_STAR →betaZ|
|O294|STAR|ADD betaZ+gammaRho →G2negative|
|O295|STAR|NEG G2negative →G2|
|O296|STAR|ABS WHOLE G2 →absG2|
|O297|STAR|MUL F2*point.5 →halfF2|
|O298|STAR|MUL absG2*WHOLE sqrt3half →pitchTerm|
|O299|STAR|SUB halfF2−pitchTerm →ankleMargin|
|O300|STAR|MUL h*WHOLE b →rollProduct|
|O301|STAR|SUB rollProduct−WHOLE absX →rollMargin|
|O302|STAR|MUL WHOLE X_STAR*WHOLE Z_STAR →qxNumerator|
|O303|STAR|DIV qxNumerator/WHOLE rho →qx|
|O304|STAR|NEG WHOLE h →negative_h|
|O305|STAR|MUL negative_h*WHOLE Z_STAR →qyNumerator|
|O306|STAR|DIV qyNumerator/WHOLE rho →qy|
|O307|STAR|NEG WHOLE rho →qz|
|O308|STAR|MUL WHOLE alpha*WHOLE X_STAR →alphaDx|
|O309|STAR|MUL WHOLE alpha*WHOLE negative_h →alphaDy|
|O310|STAR|MUL WHOLE alpha*WHOLE Z_STAR →alphaDz|
|O311|STAR|MUL WHOLE gamma*WHOLE qx →gammaQx|
|O312|STAR|MUL WHOLE gamma*WHOLE qy →gammaQy|
|O313|STAR|MUL WHOLE gamma*WHOLE qz →gammaQz|
|O314|STAR|ADD alphaDx+gammaQx →localKneeHipX|
|O315|STAR|ADD alphaDy+gammaQy →localKneeHipY|
|O316|STAR|ADD alphaDz+gammaQz →localKneeHipZ|
|O317|STAR|MUL localKneeHipX*point1 →rotationFirst|
|O318|STAR|MUL localKneeHipY*point0 →rotationSecond|
|O319|STAR|ADD rotationFirst+rotationSecond →rotationPair|
|O320|STAR|MUL localKneeHipZ*point0 →rotationThird|
|O321|STAR|ADD rotationPair+rotationThird →WORLD knee−hip X|
|O322|STAR|MUL localKneeHipX*point0 →rotationFirst|
|O323|STAR|MUL localKneeHipY*point1 →rotationSecond|
|O324|STAR|ADD rotationFirst+rotationSecond →rotationPair|
|O325|STAR|MUL localKneeHipZ*point0 →rotationThird|
|O326|STAR|ADD rotationPair+rotationThird →WORLD knee−hip Y|
|O327|STAR|MUL localKneeHipX*point0 →rotationFirst|
|O328|STAR|MUL localKneeHipY*point0 →rotationSecond|
|O329|STAR|ADD rotationFirst+rotationSecond →rotationPair|
|O330|STAR|MUL localKneeHipZ*point1 →rotationThird|
|O331|STAR|ADD rotationPair+rotationThird →WORLD knee−hip Z|
|O332|STAR|ENDPOINT_DIV WORLD knee−hip X/unchanged L1 →ux|
|O333|STAR|ENDPOINT_DIV WORLD knee−hip Y/unchanged L1 →uy|
|O334|STAR|ENDPOINT_DIV WORLD knee−hip Z/unchanged L1 →uz|
|O335|STAR|SQUARE WHOLE ux →uxSquared|
|O336|STAR|SQUARE WHOLE uz →uzSquared|
|O337|STAR|ADD uxSquared+uzSquared →transverseSquared|
|O338|STAR|SMALL_ROOT WHOLE transverseSquared →transverse|
|O339|STAR|MUL pointL*WHOLE uy →axialExtent|
|O340|STAR|MUL pointR*WHOLE transverse →radialExtent|
|O341|STAR|ADD axialExtent+radialExtent →extent|
|O342|STAR|NEG pointHy →bottom|
|O343|STAR|SUB bottom−WHOLE extent →strictGap|

## 5. Literal50 readiness rows and interleave

BeforeO001 run G01–G06 in order. Every later guard runs after its listed operation BEFORE the next operation; ascending guard number resolves a shared placement. New final generated-packet/validator guards run after O343. Guard bit/cursor=row−1; full guardmask0x0003ffffffffffff, upper14bits0, completeguardcursor49/side255.

|Row|Side|After|Ordered readiness and finite failure|
|---|---|---|---|
|G01|common|before O001|same actual owner/Data/parts/Request; valid provider; actual source helper success, count64/fullmask|
|G02|common|before O001|original FP helper|
|G03|common|before O001|new version5/Candidate0, immutable seed template (no generatedY yet)|
|G04|common|before O001|original coordinate term/count/canonical-unused/abs8 domains|
|G05|common|before O001|original compiler/link identities and finite positive fixed L1/L2<=1; exact original pelvis/root-frame, PORT/STAR thigh capsule and regions1/2: radiiR=.105, Hy=.12, L=kBoardingSelfHipLengthMetres unchangedhex; finite positive0<Hy<L, R<=L<L1, proper unchanged heldsole identity. Changed shape/slab identity→identity; authentic finite sufficient domain failure→hip_threshold_domain.|
|G06|common|before O001|S64 proper rational yaw/flat held soles and preserved seed tuple|
|G07|common|O045|supported finite b then b.low>0 (no_positive_roll_factor)|
|G08|common|O049|supported finite c_roll,0<c_roll.low<=c_roll.high<1 (hip_threshold_domain)|
|G09|common|O054|supported finite thresholdDen.low>0 then thresholdRad.low>0 (hip_threshold_domain)|
|G10|common|O059|supported finite0<s_req.low<=s_req.high<1 (hip_threshold_domain)|
|G11|common|O061|supported finite thresholdUnsquaredSign.low>0 (hip_threshold_domain)|
|G12|common|O063|supported finite singleton k_bar,0<k_bar<1 (hip_descent_unavailable)|
|G13|common|O065|supported finite gRad.low>0 (hip_descent_unavailable)|
|G14|common|O068|supported finite singleton g_cut>0; actual lower endpoint of earned gInterval (hip_descent_unavailable)|
|G15|PORT|O082|finite singleton p/q, −.5<=p<q<=.5; p>=cutlow.high, q<=cuthi.low, q<=cutturn.low; p>=hipCutLower.high, q<=hipCutUpper.low; same earned positive g_cut; save endpoint-domain certificate (no_tau_interval)|
|G16|PORT|O084|earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for p|
|G17|PORT|O090|earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for p|
|G18|PORT|O095|earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for q|
|G19|PORT|O101|earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for q|
|G20|PORT|O106|supported finite singleton u/v, 0<=u<v (no_radial_interval)|
|G21|PORT|O113|supported ankleUpperRad.low>0 (no_positive_upper)|
|G22|PORT|O120|supported reachUpperRad.low>0 (no_positive_upper)|
|G23|STAR|O139|finite singleton p/q, −.5<=p<q<=.5; p>=cutlow.high, q<=cuthi.low, q<=cutturn.low; p>=hipCutLower.high, q<=hipCutUpper.low; same earned positive g_cut; save endpoint-domain certificate (no_tau_interval)|
|G24|STAR|O141|earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for p|
|G25|STAR|O147|earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for p|
|G26|STAR|O152|earned same-side endpoint certificate and supported cosRad; cosRad.low>0 (endpoint_domain_unavailable) for q|
|G27|STAR|O158|earned same-side endpoint certificate, same p/q and W; supported thighRad.high>=0 (endpoint_domain_unavailable) for q|
|G28|STAR|O163|supported finite singleton u/v, 0<=u<v (no_radial_interval)|
|G29|STAR|O170|supported ankleUpperRad.low>0 (no_positive_upper)|
|G30|STAR|O177|supported reachUpperRad.low>0 (no_positive_upper)|
|G31|common|O190|earned finite lo/hi then lo<hi (empty_inward_interval)|
|G32|common|O193|finite storedY then abs(Y)<=8 (candidate_domain)|
|G33|common|O193|storedY>lo (midpoint_unavailable)|
|G34|common|O193|storedY<hi (midpoint_unavailable)|
|G35|PORT|O197|supported finite h/rho2/D; h.low>0, rho2.low>0, D.low>m.high, D.high<M.low in order (verification_inconclusive)|
|G36|PORT|O207|supported finite gamma2.low>0 (verification_inconclusive)|
|G37|PORT|O220|supported finite F1/F2/G1/G2; F1.low>0 then F2.low>0 (verification_inconclusive) then G1.low>0 (hip_descent_unavailable)|
|G38|PORT|O226|supported finite ankle/roll margins; ankleMargin.low>=0 then rollMargin.low>=0 (verification_inconclusive)|
|G39|PORT|O256|earned authentic same-side exact identity sole columns; supported finite WORLD knee−hip X/Y/Z, each within[-16,16] (hard unsupported if support/domain fails)|
|G40|PORT|O259|supported finite genuine endpoint-divided ux/uy/uz; uy.high<0 (hip_verification_inconclusive)|
|G41|PORT|O268|supported finite transverse/extent/bottom/strictGap; extent.high<bottom.low (hip_verification_inconclusive; supported gap is reported, no gap positivity predicate)|
|G42|STAR|O272|supported finite h/rho2/D; h.low>0, rho2.low>0, D.low>m.high, D.high<M.low in order (verification_inconclusive)|
|G43|STAR|O282|supported finite gamma2.low>0 (verification_inconclusive)|
|G44|STAR|O295|supported finite F1/F2/G1/G2; F1.low>0 then F2.low>0 (verification_inconclusive) then G1.low>0 (hip_descent_unavailable)|
|G45|STAR|O301|supported finite ankle/roll margins; ankleMargin.low>=0 then rollMargin.low>=0 (verification_inconclusive)|
|G46|STAR|O331|earned authentic same-side exact identity sole columns; supported finite WORLD knee−hip X/Y/Z, each within[-16,16] (hard unsupported if support/domain fails)|
|G47|STAR|O334|supported finite genuine endpoint-divided ux/uy/uz; uy.high<0 (hip_verification_inconclusive)|
|G48|STAR|O343|supported finite transverse/extent/bottom/strictGap; extent.high<bottom.low (hip_verification_inconclusive; supported gap is reported, no gap positivity predicate)|
|G49|common|O343|check immutable seed BEFORE writing only both rootY={Y,+0,+0}count1; exact generated packet/static tuple after assignment (identity)|
|G50|common|O343|original FP precheck; generated packet structural checks then numerical domains; current+pending original validator Expected ONCE; FP postcheck (identity/unsupported)|

All listed supported/finite/order failures are hard unsupported before ordinary inequalities are read. Finite threshold-domain failure attaches the first actually supported c_roll/den/radicand/s_req/unsquared-sign bound; k_bar/gRad/g_cut attach their own first failed supported bound. Source shape/frame/region identity failures invent no bound. Direct world-vector hard failure attaches only the first supported offending component; negative-axis finite failure attaches uy; extent finite failure attaches extent. The gap is supported evidence only: the original predicate does not require its outward lower endpoint positive. Guard inequalities use literal table order, without unsupported nested default values or later reads. G1 positivity attaches G1 only after F1/F2 were actually verified. Strengthened cut failure attaches p first for lower cuts, else q for ordering/upper cuts. The same endpoint-domain certificate now also retains actual threshold/g_cut/new cuts identity until both endpoint consumers finish.

The old output errors become exact new-prefix strings: "intermediate endpoint05 unsupported floating point", "intermediate endpoint05 invalid candidate", "intermediate endpoint05 invalid limits", "intermediate endpoint05 output headers". Original FP helper precedes Candidate, raised Limits in their25-field order, TWOheaders room, any materialization/source/constructor. Safe structured REQUIRED=2*sizeof(Expected)+sizeof(Cell) before S64; owned bytes=2*sizeof(Expected)+actual_vector_capacity*sizeof(Cell). Do not infer required room from a headers-only refusal. Pure sizeof helper has no factory or query. Allocation after constructor/Key is singlecapacity; failure disposes abnormal slot/capacity before header-only output. The disjoint source error plan below explicitly names current/pending fixed string buffers/chunk headers and all borrowed exception/allocator/unwind/cleanup controls; it is not a free string exception.

## 6. Completion/lifetimes and unchanged original continuation

Old EMPTY Endpoint03 enrollment carrier still owns FULL historical 33208/nested28880/24680 plus distinct fresh owners. Call unchanged source helper only once; copy source bindings/work/full refusal byreference. Require actual helper success/source_enrolled ANDcount64/fullmask. All old source carrier/limits/refusals/factory/catalog returns die at sourceadapter return BEFORE new constructor. Source input Request initially authenticseed. Constructor chart/threshold/cut certificates/direct arrays/validation current+pending Expected die BEFORE issuing newKey/Context/reset/graph. The full retained certificate is inside current/pending headers only, no shadow Slice archive.

Construction complete iff actual 50 true guards/all 343 supported written operations and original successful finalvalidator+FP, after source/authenticatedversion5/generatedpacket. Later graph failure preserves actual complete Slice; it does not promote graph/current/body/support/SELF arithmetic. Key and reporting_elapsed_seconds2 issue only after complete; earlierclock0. Generate exactly BOTH rootY terms in finalguard, compare seed first and generated static packet second; final guard sequence remains FPprecheck→structuralcounts/canonicalunused/static/version/Y→finite originalsum domains→oneoriginalRequestvalidator with current+pending returns→FPpostcheck. A structurally bad but numeric-supported packet must return identity, not unsupported; no oldsum shortcut may mintnewauthority.

Preserve fresh D01–03 before one unchanged full original `boarding_route_foot_phase_cell(request,0,1,false,caps,work,cell.phase,reason)` with original five phase caps 1/2/1/3/6 (body1). D04 before projection. Full P251=current identity53+11carriers*18, freshcanonicalonlyatP11; typedbootD05–31 sourceplanes/rectangles/intersections/pressure/PORT1/16 balance/all16disks. B63 fullbody/frame/region then Unit24 actual WORLD K−H/K−A, full14owners/105pairs/1470axes/2940signed/2hips. No new direct constructor proof bypasses original Unit24/body/hip expression or supplies its axis. Fullcapsule/root/cylinder/distal ownership and separation semantics stay original. All original partial prefixes/refusal tuples and unavailable flags survive. LaterWORLD/material/HALO/route/actor/save/seating/dynamics/FirstFlight unearned.

## 7. Matched independent controls

The [matched Test manifest](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT05_TEST_MANIFEST.md) and [structured inventory](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT05_TEST_INVENTORY.json) register101 finite purposes: one default,25zero,25raised,25actual-used-minus/exact-required-minus,oneexact-used,sixunsafeFP,invalidCandidate,fourgenuine-provider-lifetime,threemixed-priority andten mask-word-crossing controls. All25 fields, literal row IDs, masks, availability and reached prerequisites bind the343/50 table. Missing genuine FIRST prerequisites cause explicit SKIP. No numerical acceptance outcome is predicted.

A01 independently reconstructs the same-source chart at earnedY and corroborates original positivegamma/reach/ankle/roll, genuine knee−hip WORLD axis and direct transverse hipgap. Its4096 utility has3520 named bytes/576 unused; TWO reconstructedRequests1368 remain exterior. One construction oracle follows complete343/50, then one full15mass/pose body audit with TWO8400Oracles/Requests, then at most105pair+14owner+2hip interval records in strictly sequential scopes. Constructor/source/oracle scratch dies before the later audits. No raw query, fixture, inspector or additional consumer is introduced. Final public bytes are bound externally without a hash cycle.

## 8. Complete proposed typed ownership and disjoint error phases

The fresh field/padding forecast is explicit: Slice176, fullRefusal192/optional200, Diagnostic1632, Expected1640 with selected1664, Cell unchanged fields with selected11032, FOUR Limits800, TWOExpected3328, TWOexteriorRefusals384, TWORequests1368. Slice grows from two additional attempted and written mask words and wider operation cursor; Diagnostic.stop_operation and optional fullRefusal growth are independently counted. Cell contains no shadow Slice or new axis/cut array. Proposed mutable Test globals156 = Totals120 + checks8 + failures4 + sharedbytes8 + ONE first_failure borrowed static-label string_view16. Summary120 retains24uint32 used counts and the ten-crossing uint16 bitmap, not a six-word report archive. The exact member/padding inventory is in the embedded source forecast. Actual compiler layouts remain UNKNOWN and must satisfy selected ceilings before FIRST.

Selected constructor4096 has named3912/unused184: all original input/yaw/chart/link/cut/direct/endpoint arrays, eight selected doubles, certified bounded root/division leaf176, CONST/sign/region96, primitive local/pending192, TWOvalidatorExpected80, refs/closures/control432, library/environment512 and row-reset fullRefusal192 are simultaneous conservative declarations. The complete343 destination plan and last-consumer schedule bind scratch reuse; g_cut selected5 survives both universal cuts/closed endpoint certificates before reuse. No old unused margin supplies an omitted live array. Pressure6144 names3384/unused2760 and SELF6144 names5352/unused792, including row-reset fullRefusal192. Other factory/tuple/region/body/interval pools preserve their complete historical declarations. Independent constructor audit4096 names3520/unused576, and begins only after original query/constructor/source scratch death.

The complete old source33208 includes nested28880/24680. It survives the fresh END-only source helper and all its current/pending returns, then dies before the new constructor. The optional fresh source-versus-constructor max rule requires exact scope/death/real caller proof; the conservative source-plus-disjoint-constructor overcharge is retained in this proposal. Entire original graph32768, arena4096, selected caller1024/2048, FOURLimits, current/pending headers/Requests/Refusals/keys/context/token, fixedreturned primitive records, scalar static152 and conservative native/JSON88 are explicit. All source/physical/capacity proofs remain separate. Numerical error/unwind/destructor activations remain charged; separate nonzero world/runtime/presentation ownership is not an allocator exemption.

| Complete selected source stage | Bytes | Ceiling | Headroom |
| --- | ---: | ---: | ---: |
| creator | 7240 | 8192 | 952 |
| preflight | 11088 | 49152 | 38064 |
| required_room_refusal | 12152 | 49152 | 37000 |
| source_enrollment | 39504 | 49152 | 9648 |
| source_plus_disjoint_construction_overcharge | 43600 | 49152 | 5552 |
| construction | 17184 | 49152 | 31968 |
| reset | 34850 | 49152 | 14302 |
| original_graph | 48640 | 49152 | 512 |
| canonical | 31328 | 49152 | 17824 |
| pressure_SELF | 29792 | 49152 | 19360 |
| region_factory | 25088 | 49152 | 24064 |
| construction_audit | 28072 | 49152 | 21080 |
| body_audit | 48968 | 49152 | 184 |
| interval_audit | 32168 | 49152 | 16984 |
| owned_capacity1_output | 14360 | 16777216 | 16762856 |

Universal Test check(bool,string_view) is void and counter-only: no I/O, heap, formatting, callback, queue or extra query. ONE default-empty first_failure view stores only the first false source-static label, never resets, and needs no extra exists flag. Creator named772/unused252 and graph1887/unused161 fit unchanged selected1024/2048 with globals156 separately named. Every original math/comparison/check and failure count remains; status and first-failure printing occur only after helper/Request/Oracle/workspace death. Current/pending check-label256 and slot/index/status112 remain independent caller objects. Finite check upper524288 exceeds the literal source sum482816; all uint32 aggregate/narrow/roster and uint64 counter throw branches must be source-unreachable from the101 monotonic consumers/one construction/one full15-mass-pose body/at most121 interval records. No anticipated successful FIRST count is used.

The complete4096 error source pool partitions TWO borrowed bad_alloc slots192 each, TWOunexpected string stack wrappers32 each, caught/destructor/allocator references32, allocation/catch controls32 and external simultaneous call/unwind/TLS/personality/free/terminal activation3584. Separate fixed-error owner320 is TWO literal buffers128 each including NUL plus TWOchunk headers32 each. New fixed messages have payload<=127, no concatenation or numerical formatting. These are prospective ceilings, not actual external bounds. A complete fresh measured chain/capacity that violates a partition stops before FIRST. No512 success reservation is mislabeled error proof.

Error alternatives are creator-before-arena7304/8192, public-preflight14992, conservative structured-room16056, enrollment-error43920 (fullold33208 retained), one-Cell reserve after constructor death16992, and after-helper/Oracle-death teardown28064. All latter ceilings49152. Creator subtraction is the exact old initialization-error256 and not-yet-created arena4096; no arena-present failure may inherit this map. Reserve failure likewise has no Cell allocation yet; all exterior candidate owners remain. After successful reserve1, default Cell/emplace/reset are required fixed nonallocating source paths. Original constructor/graph/body/pressure/SELF/canonical/region/audit paths must use the registered internal fixed-array/value/borrowed-view callees and checked variants; an allocating public assessor cannot be substituted.

G50's one original validator call is mapped to every original allocating failure branch: old source S33 (zero-based32) success and fullS64 authenticate each unchanged root/sole/yaw/swing/torso/share/duration field; G49 whole-packet equality preserves those fields; G04/G50 recheck counts/canonical unused/finite domains. G32 bounds new finiteY by8; BOTH changed rootY lists are count1 {Y,+0,+0}. This single-term constant's original four-term comparison with±8 has at most two nonzero addends and cannot exhaust four slots or acquire an outside sign; original value support/clipping is retained under qualified FP. Bounded terms alone are not used as a supported-sum argument for arbitrary lists. G50 order remains FPprecheck→structure/version/Candidate/Y/static identity→numerical domains→ONE current/pending original validator80→FPpostcheck. All eight branch exclusions are explicitly embedded below. If any source predicate/immutable identity is unproved in actual implementation, this helper plan remains HOLD; never claim a4096 constructor fits co-live3912 scratch plus4096 error. Later internal graph validation consumes exactly the same immutable generated Request, not a second query.

Fresh actual admission must bind selected launch profile, current transitive dependencies/compiler flags, real object/archive/normal ELF correspondence, both compilers' complete call ancestry/ABI entries, allocator/chunk/TLS/EH/free/destructor/terminal/cold resolver paths and success512/error4096 ceilings. A possible reviewed eager-binding/zero-tcache closed Linux profile is a new explicit registration selection, not automatic #515 applicability. All actual layouts/frames/capacities/library limits and actual feasibility remain UNKNOWN. This registration authorizes no compiler/API/runtime/evaluation.

The complete source dataset is embedded below and repeated in the matched inventory. Historical author bindings are review provenance; no private artifact path is an engine build/runtime dependency.

## 9. Source references and remaining gates

[Conditional BOTH-hip proof](https://github.com/gobha-me/apsis-drift/blob/1b397b154a65253965bd376bb33bf307aa79008e/docs/ORIGIN_BOARDING_BOTH_HIP_CONSTRUCTION.md), [BOTH-ankle proof](https://github.com/gobha-me/apsis-drift/blob/1b397b154a65253965bd376bb33bf307aa79008e/docs/ORIGIN_BOARDING_BOTH_ANKLE_CONSTRUCTION.md), [Endpoint04 literalregistration](https://github.com/gobha-me/apsis-drift/blob/1b397b154a65253965bd376bb33bf307aa79008e/docs/ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT04.md), originalplanted3430/3452/3478/3534/3786/4415/8297/8396 andsourceEndpoint01 seed207 are authority. Unchanged originalcapsule/slabdimensionalidentity, not a report's successfulflags, justifies thresholdconstants and soleidentityrotation. Currentparent462 source/controls/policies preserved. This contract selects one finite source-derived program; numerical evaluation and implementation require a separate child and complete admission.

## 10. Complete source forecast

The following dataset contains the exact field, slot, helper, stage, allocation and validator constraints. These are source forecasts, not measured admission. The conservative aggregate proof uses101×(default+1), upper297041, including raised controls; FIRST-only Summary remains bounded by2940. All343/50/101 rows and counts bind the matched manifest and inventory. An external final receipt pins the public pair without a hash cycle.

```json
{
  "status": "REGISTERED_SOURCE_CONSTRAINTS_NOT_ACTUAL_ADMISSION",
  "issue": 519,
  "proof_merge": "1b397b154a65253965bd376bb33bf307aa79008e",
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
        "operation_attempted6",
        0,
        48
      ],
      [
        "operation_written6",
        48,
        48
      ],
      [
        "guard_attempted_written",
        96,
        16
      ],
      [
        "limiting_bound",
        112,
        24
      ],
      [
        "y_lo_hi",
        136,
        24
      ],
      [
        "operation_uint16",
        160,
        2
      ],
      [
        "guard_side",
        162,
        2
      ],
      [
        "condition",
        164,
        1
      ],
      [
        "arithmetic_complete",
        165,
        2
      ],
      [
        "preflight_zero",
        167,
        2
      ],
      [
        "tail_padding",
        169,
        7
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
        176
      ],
      [
        "vector_Cell",
        1352,
        24
      ],
      [
        "optional_Refusal",
        1376,
        200
      ],
      [
        "source_evaluated",
        1576,
        8
      ],
      [
        "output_capacity_bytes",
        1584,
        8
      ],
      [
        "reporting_elapsed_seconds",
        1592,
        8
      ],
      [
        "version_selfversion",
        1600,
        8
      ],
      [
        "candidate_condition_state_stage",
        1608,
        4
      ],
      [
        "stop_self_pair",
        1612,
        2
      ],
      [
        "stop_operation_uint16",
        1614,
        2
      ],
      [
        "stop_self_region_axis_sign",
        1616,
        3
      ],
      [
        "thirteen_boolean_flags",
        1619,
        13
      ]
    ],
    "predicted_sizes": {
      "Slice": 176,
      "Refusal": 192,
      "optional_Refusal": 200,
      "Diagnostic": 1632,
      "Expected": 1640,
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
      "Expected": 1664,
      "Cell": 11032,
      "Refusal": 192,
      "Slice": 176,
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
    "Expected_reason": "std::expected largest alternative is Diagnostic1632 vs string32; independent discriminator/padding8 yields1640. Actual implementation-specific layout remains compile-only gate.",
    "Cell_reason": "Every Cell member retains the identical type/extent/order; all remapped enum underlying types remainuint8; no constructor Slice/axis/threshold is added to Cell. Historical10944 is applicability input only, not freshsizeof.",
    "Summary_reason": "24 checked uint32 used96 + hash8 + output8 + uint16 crossing_bitmap2 + exists1 =115 rounded8\u2192120. No mask or report archive.",
    "Totals_reason": "26 uint32 counters104 +7 separately bounded uint16 counters14 =118 rounded4\u2192120;101consumer/101skip/102nextslot fit, no arbitrary truncation.",
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
        "references_closures_cursor_controls": 432,
        "two_validator_expected": 80,
        "library_environment_error": 512,
        "eight_scalar_selection_doubles": 64,
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
  "constructor_reference_control_components": {
    "constructor_four_borrowed_parameters": 32,
    "persistent_operation_guard_attach_closures_seven_references": 56,
    "active_operation_closure_current_pending_eight_refs_each": 128,
    "active_operation_destination_reference": 8,
    "active_guard_closure_current_pending_eight_refs_each_overcharge": 128,
    "nested_supported_attach_stop_four_reference_parameters": 32,
    "three_part_variant_views": 24,
    "two_endpoint_certificate_flags_rows_sides_predicate_loop_control": 24
  },
  "operation_destination_slots": {
    "1": {
      "destination": "input[0]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "2": {
      "destination": "input[1]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "3": {
      "destination": "input[2]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "4": {
      "destination": "input[3]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "5": {
      "destination": "input[4]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "6": {
      "destination": "input[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "7": {
      "destination": "input[6]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "8": {
      "destination": "input[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "9": {
      "destination": "yaw[0]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "10": {
      "destination": "yaw[1]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "11": {
      "destination": "yaw[2]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "12": {
      "destination": "yaw[3]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "13": {
      "destination": "yaw[2]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "14": {
      "destination": "yaw[4]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "15": {
      "destination": "yaw[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "16": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "17": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "18": {
      "destination": "endpoint[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "19": {
      "destination": "endpoint[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "20": {
      "destination": "chart[0]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "21": {
      "destination": "chart[1]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "22": {
      "destination": "chart[2]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "23": {
      "destination": "chart[3]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "24": {
      "destination": "chart[4]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "25": {
      "destination": "chart[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "26": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "27": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "28": {
      "destination": "endpoint[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "29": {
      "destination": "endpoint[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "30": {
      "destination": "chart[8]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "31": {
      "destination": "chart[9]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "32": {
      "destination": "chart[10]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "33": {
      "destination": "chart[11]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "34": {
      "destination": "chart[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "35": {
      "destination": "chart[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "36": {
      "destination": "link[0]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "37": {
      "destination": "link[1]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "38": {
      "destination": "link[2]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "39": {
      "destination": "link[3]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "40": {
      "destination": "link[4]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "41": {
      "destination": "link[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "42": {
      "destination": "link[6]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "43": {
      "destination": "yaw[0]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "44": {
      "destination": "link[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "45": {
      "destination": "link[8]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "46": {
      "destination": "direct[0]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "47": {
      "destination": "direct[0]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "48": {
      "destination": "direct[0]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "49": {
      "destination": "direct[1]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "50": {
      "destination": "direct[2]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "51": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "52": {
      "destination": "direct[4]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "53": {
      "destination": "direct[2]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "54": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "55": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "56": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "57": {
      "destination": "direct[4]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "58": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "59": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "60": {
      "destination": "direct[4]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "61": {
      "destination": "direct[4]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "62": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "63": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG",
      "charged_publication": "selected[5]=written singleton; k_bar at18, g_cut replaces it at23 after k_bar last read at19"
    },
    "64": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "65": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "66": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "67": {
      "destination": "direct[3]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG"
    },
    "68": {
      "destination": "direct[4]",
      "kind": "THRESHOLD_BEFORE_ENDPOINT_OR_DIRECT_LEG",
      "charged_publication": "selected[5]=written singleton; k_bar at18, g_cut replaces it at23 after k_bar last read at19"
    },
    "69": {
      "destination": "cut[0]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "70": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "71": {
      "destination": "cut[1]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "72": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "73": {
      "destination": "cut[2]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "74": {
      "destination": "cut[3]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "75": {
      "destination": "cut[4]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "76": {
      "destination": "cut[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "77": {
      "destination": "cut[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "78": {
      "destination": "endpoint[12]",
      "kind": "NEW_CUT_BEFORE_SAME_SIDE_ENDPOINT"
    },
    "79": {
      "destination": "endpoint[12]",
      "kind": "NEW_CUT_BEFORE_SAME_SIDE_ENDPOINT"
    },
    "80": {
      "destination": "cut[4]",
      "kind": "NEW_CUT_BEFORE_SAME_SIDE_ENDPOINT"
    },
    "81": {
      "destination": "endpoint[13]",
      "kind": "NEW_CUT_BEFORE_SAME_SIDE_ENDPOINT"
    },
    "82": {
      "destination": "cut[5]",
      "kind": "NEW_CUT_BEFORE_SAME_SIDE_ENDPOINT"
    },
    "83": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "84": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "85": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "86": {
      "destination": "endpoint[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "87": {
      "destination": "endpoint[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "88": {
      "destination": "endpoint[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "89": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "90": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "91": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "92": {
      "destination": "endpoint[17]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "93": {
      "destination": "endpoint[0]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "94": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "95": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "96": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "97": {
      "destination": "endpoint[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "98": {
      "destination": "endpoint[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "99": {
      "destination": "endpoint[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "100": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "101": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "102": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "103": {
      "destination": "endpoint[17]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "104": {
      "destination": "endpoint[1]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "105": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "106": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "107": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "108": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "109": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "110": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "111": {
      "destination": "endpoint[2]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "112": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "113": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "114": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "115": {
      "destination": "endpoint[3]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "116": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "117": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "118": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "119": {
      "destination": "endpoint[4]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "120": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "121": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "122": {
      "destination": "endpoint[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "123": {
      "destination": "chart[6]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "124": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "125": {
      "destination": "chart[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "126": {
      "destination": "cut[6]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "127": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "128": {
      "destination": "cut[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "129": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "130": {
      "destination": "cut[8]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "131": {
      "destination": "cut[9]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "132": {
      "destination": "cut[10]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "133": {
      "destination": "cut[11]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "134": {
      "destination": "cut[11]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "135": {
      "destination": "endpoint[12]",
      "kind": "NEW_CUT_BEFORE_SAME_SIDE_ENDPOINT"
    },
    "136": {
      "destination": "endpoint[12]",
      "kind": "NEW_CUT_BEFORE_SAME_SIDE_ENDPOINT"
    },
    "137": {
      "destination": "cut[10]",
      "kind": "NEW_CUT_BEFORE_SAME_SIDE_ENDPOINT"
    },
    "138": {
      "destination": "endpoint[13]",
      "kind": "NEW_CUT_BEFORE_SAME_SIDE_ENDPOINT"
    },
    "139": {
      "destination": "cut[11]",
      "kind": "NEW_CUT_BEFORE_SAME_SIDE_ENDPOINT"
    },
    "140": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "141": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "142": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "143": {
      "destination": "endpoint[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "144": {
      "destination": "endpoint[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "145": {
      "destination": "endpoint[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "146": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "147": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "148": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "149": {
      "destination": "endpoint[17]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "150": {
      "destination": "endpoint[6]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "151": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "152": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "153": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "154": {
      "destination": "endpoint[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "155": {
      "destination": "endpoint[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "156": {
      "destination": "endpoint[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "157": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "158": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "159": {
      "destination": "endpoint[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "160": {
      "destination": "endpoint[17]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "161": {
      "destination": "endpoint[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "162": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "163": {
      "destination": "endpoint[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "164": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "165": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "166": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "167": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "168": {
      "destination": "endpoint[8]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "169": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "170": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "171": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "172": {
      "destination": "endpoint[9]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "173": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "174": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "175": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "176": {
      "destination": "endpoint[10]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "177": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "178": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "179": {
      "destination": "endpoint[11]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "180": {
      "destination": "chart[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "181": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "182": {
      "destination": "chart[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "183": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "184": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "185": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "186": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "187": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "188": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "189": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "190": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "191": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "192": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "193": {
      "destination": "endpoint[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "194": {
      "destination": "direct[0]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "195": {
      "destination": "direct[1]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "196": {
      "destination": "direct[1]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "197": {
      "destination": "direct[2]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "198": {
      "destination": "direct[3]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "199": {
      "destination": "direct[4]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "200": {
      "destination": "direct[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "201": {
      "destination": "direct[4]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "202": {
      "destination": "direct[6]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "203": {
      "destination": "direct[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "204": {
      "destination": "direct[6]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "205": {
      "destination": "direct[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "206": {
      "destination": "direct[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "207": {
      "destination": "direct[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "208": {
      "destination": "direct[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "209": {
      "destination": "direct[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "210": {
      "destination": "direct[8]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "211": {
      "destination": "direct[9]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "212": {
      "destination": "direct[8]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "213": {
      "destination": "direct[10]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "214": {
      "destination": "direct[11]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "215": {
      "destination": "direct[11]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "216": {
      "destination": "direct[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "217": {
      "destination": "direct[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "218": {
      "destination": "direct[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "219": {
      "destination": "direct[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "220": {
      "destination": "direct[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "221": {
      "destination": "direct[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "222": {
      "destination": "direct[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "223": {
      "destination": "direct[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "224": {
      "destination": "direct[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "225": {
      "destination": "direct[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "226": {
      "destination": "direct[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "227": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "228": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "229": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "230": {
      "destination": "direct[5]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "231": {
      "destination": "direct[5]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "232": {
      "destination": "direct[6]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "233": {
      "destination": "direct[8]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "234": {
      "destination": "direct[9]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "235": {
      "destination": "direct[10]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "236": {
      "destination": "direct[11]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "237": {
      "destination": "direct[12]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "238": {
      "destination": "direct[13]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "239": {
      "destination": "direct[14]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "240": {
      "destination": "direct[15]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "241": {
      "destination": "direct[16]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "242": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "243": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "244": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "245": {
      "destination": "direct[3]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "246": {
      "destination": "direct[4]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "247": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "248": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "249": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "250": {
      "destination": "direct[3]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "251": {
      "destination": "direct[7]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "252": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "253": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "254": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "255": {
      "destination": "direct[3]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "256": {
      "destination": "direct[0]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "257": {
      "destination": "direct[4]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "258": {
      "destination": "direct[7]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "259": {
      "destination": "direct[0]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "260": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "261": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "262": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "263": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "264": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "265": {
      "destination": "direct[3]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "266": {
      "destination": "direct[14]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "267": {
      "destination": "direct[15]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "268": {
      "destination": "direct[16]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "269": {
      "destination": "direct[0]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "270": {
      "destination": "direct[1]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "271": {
      "destination": "direct[1]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "272": {
      "destination": "direct[2]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "273": {
      "destination": "direct[3]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "274": {
      "destination": "direct[4]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "275": {
      "destination": "direct[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "276": {
      "destination": "direct[4]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "277": {
      "destination": "direct[6]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "278": {
      "destination": "direct[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "279": {
      "destination": "direct[6]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "280": {
      "destination": "direct[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "281": {
      "destination": "direct[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "282": {
      "destination": "direct[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "283": {
      "destination": "direct[7]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "284": {
      "destination": "direct[5]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "285": {
      "destination": "direct[8]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "286": {
      "destination": "direct[9]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "287": {
      "destination": "direct[8]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "288": {
      "destination": "direct[10]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "289": {
      "destination": "direct[11]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "290": {
      "destination": "direct[11]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "291": {
      "destination": "direct[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "292": {
      "destination": "direct[12]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "293": {
      "destination": "direct[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "294": {
      "destination": "direct[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "295": {
      "destination": "direct[13]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "296": {
      "destination": "direct[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "297": {
      "destination": "direct[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "298": {
      "destination": "direct[14]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "299": {
      "destination": "direct[15]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "300": {
      "destination": "direct[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "301": {
      "destination": "direct[16]",
      "kind": "INHERITED_EXPLICIT_ARRAY_SLOT"
    },
    "302": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "303": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "304": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "305": {
      "destination": "direct[5]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "306": {
      "destination": "direct[5]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "307": {
      "destination": "direct[6]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "308": {
      "destination": "direct[8]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "309": {
      "destination": "direct[9]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "310": {
      "destination": "direct[10]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "311": {
      "destination": "direct[11]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "312": {
      "destination": "direct[12]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "313": {
      "destination": "direct[13]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "314": {
      "destination": "direct[14]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "315": {
      "destination": "direct[15]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "316": {
      "destination": "direct[16]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "317": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "318": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "319": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "320": {
      "destination": "direct[3]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "321": {
      "destination": "direct[4]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "322": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "323": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "324": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "325": {
      "destination": "direct[3]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "326": {
      "destination": "direct[7]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "327": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "328": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "329": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "330": {
      "destination": "direct[3]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "331": {
      "destination": "direct[0]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "332": {
      "destination": "direct[4]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "333": {
      "destination": "direct[7]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "334": {
      "destination": "direct[0]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "335": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "336": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "337": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "338": {
      "destination": "direct[1]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "339": {
      "destination": "direct[2]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "340": {
      "destination": "direct[3]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "341": {
      "destination": "direct[14]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "342": {
      "destination": "direct[15]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    },
    "343": {
      "destination": "direct[16]",
      "kind": "DIRECT_AFTER_SAME_SIDE_ANKLE_ROLL_GUARD"
    }
  },
  "test_source_pools": {
    "creator1024": {
      "objects": {
        "Summary_reservation": 120,
        "two_StreamCounter": 176,
        "mutable_globals_reservation": 156,
        "native_boot_Expected": 80,
        "provider_optional": 24,
        "saved_stream_pointers_and_bytes": 24,
        "references": 32,
        "current_pending_errors": 160
      },
      "named": 772,
      "unused": 252,
      "selected": 1024
    },
    "graph2048": {
      "objects": {
        "Summary_reservation": 120,
        "two_StreamCounter": 176,
        "mutable_globals_reservation": 156,
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
        "two_word_prefix_current_pending_arrays": 32,
        "interleaved_pair_array_and_initializer_list_view_peak": 136,
        "self_accounting_certificate_count_array": 14,
        "semantic_hash_boolean_initializer_list": 13,
        "edge_pointer_initializer_list_and_view": 32,
        "hash_jet_velocity_current_and_saved_original": 96,
        "separate_pending_work_array_return_no_elision": 192
      },
      "named": 1887,
      "unused": 161,
      "selected": 2048,
      "historical_registered_named": 1388,
      "historical_registered_unused": 660,
      "implementation_constraints": [
        "Retain selected2048 with named1887/unused161. No owning six-word prefix arrays: validate by reference/per-word scalar prefix helper within retained32-byte prefix allowance.",
        "No new50-entry table: sequential interleave initializer lists each at most15 pair<uint32,uint32> plus16-byte view, peak136; scopes do not overlap.",
        "Summary uses24 checked uint32 used counts, hash8/output8, exists and uint16 ten-crossing bitmap +exists (115 before final alignment, target120); no word archives.",
        "FOUR Limits200 and TWO Expected1664 are exterior to caller2048; no padding/global/header credit.",
        "All Test checks use a counter-only nonallocating/nonthrowing (after finite counter proof) void check(bool,string_view); no check prints, formats, queues or calls a stream.",
        "The sole first_failure view lives in globals156; initially empty, assigned only on the first false check from a source-static literal label. No extra exists flag, owning string, aggregate return, queue or per-record archive. It is never reset or cleared.",
        "All audit phase-status and first-failure printing occurs only after constructor/body/interval numeric helper activations and their Request/Oracle/workspace owners die; remaining owners stay counted under the complete post-helper error/presentation distinction.",
        "Current/pending string_view check arguments remain in existing256 error/check-argument slots; references remain separately named, slot/index/status controls within112. Later actual code must match this exact source plan."
      ]
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
    "construction4096": {
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
    }
  },
  "source_stage_maps": {
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
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "library_error": 512,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 11088,
      "ceiling": 49152,
      "headroom": 38064
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
        "current_pending_refusal": 384,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 12152,
      "ceiling": 49152,
      "headroom": 37000
    },
    "source_enrollment": {
      "components": {
        "full_old_endpoint03_enrollment": 33208,
        "new_two_headers": 3328,
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
      "bytes": 39504,
      "ceiling": 49152,
      "headroom": 9648
    },
    "source_plus_disjoint_construction_overcharge": {
      "components": {
        "full_old_endpoint03_enrollment": 33208,
        "new_two_headers": 3328,
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
      "bytes": 43600,
      "ceiling": 49152,
      "headroom": 5552
    },
    "construction": {
      "components": {
        "two_headers": 3328,
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
      "bytes": 17184,
      "ceiling": 49152,
      "headroom": 31968
    },
    "reset": {
      "components": {
        "two_full_cells": 22064,
        "two_headers": 3328,
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
      "bytes": 34850,
      "ceiling": 49152,
      "headroom": 14302
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
        "current_pending_refusal": 384,
        "context": 40,
        "token_reservation": 40,
        "current_pending_key": 32,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 48640,
      "ceiling": 49152,
      "headroom": 512
    },
    "canonical": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3328,
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
      "bytes": 31328,
      "ceiling": 49152,
      "headroom": 17824
    },
    "pressure_SELF": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3328,
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
      "bytes": 29792,
      "ceiling": 49152,
      "headroom": 19360
    },
    "region_factory": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3328,
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
      "bytes": 25088,
      "ceiling": 49152,
      "headroom": 24064
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
        "construction_audit_utility": 4096,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 28072,
      "ceiling": 49152,
      "headroom": 21080
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
        "body_utility": 8192,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 48968,
      "ceiling": 49152,
      "headroom": 184
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
        "interval_utility": 8192,
        "native_prepared_and_source_json_global_overcharge": 88
      },
      "bytes": 32168,
      "ceiling": 49152,
      "headroom": 16984
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
  "error_stage_maps": {
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
        "two_headers": 3328,
        "arena": 4096,
        "four_limits": 800,
        "caller": 2048,
        "control": 64,
        "phase_scalar_limits_global": 152,
        "native_prepared_and_source_json_global_overcharge": 88,
        "complete_error_pool": 4096,
        "fixed_error_buffers_and_chunk_headers": 320
      },
      "bytes": 14992,
      "ceiling": 49152,
      "headroom": 34160,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    },
    "structured_room_conservative_error_alternative": {
      "components": {
        "two_headers": 3328,
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
      "bytes": 16056,
      "ceiling": 49152,
      "headroom": 33096,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    },
    "source_enrollment_error_overcharge": {
      "components": {
        "full_old_endpoint03_enrollment": 33208,
        "new_two_headers": 3328,
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
      "bytes": 43920,
      "ceiling": 49152,
      "headroom": 5232,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    },
    "single_cell_reserve_after_constructor_return": {
      "components": {
        "two_headers": 3328,
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
      "bytes": 16992,
      "ceiling": 49152,
      "headroom": 32160,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    },
    "post_helper_or_audit_teardown": {
      "components": {
        "full_cell": 11032,
        "two_headers": 3328,
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
      "bytes": 28064,
      "ceiling": 49152,
      "headroom": 21088,
      "kind": "PROPOSED_SOURCE_FORECAST_NOT_MEASUREMENT"
    }
  },
  "all_base_stage_allocation_and_exception_classifications": {
    "creator": "Fixed unexpected strings and the one arena allocation may enter the creator_before_arena alternative. The Data ctor/port copy/initialize have no owning dynamic fields beyond already-created shared references and the one arena; successful construction may not enter an additional throwing allocation after the arena exists. This exact source/normal-object property is a future admission requirement.",
    "preflight": "FP/Candidate/lowered-limit/header-room failure strings may allocate before constructor/source helpers or output Cell exist. Public_preflight retains TWO headers and the arena conservatively, even where the former do not yet exist.",
    "required_room_refusal": "Expected in-place Diagnostic, Refusal resets/copies, borrowed-provider aliases, and structured output refusal use fixed fields/string_views/empty vector and allocate nothing. A conservative error alternative is retained anyway; it does not authorize arbitrary allocator entry with constructor scratch alive.",
    "source_enrollment": "All 33208 old enrollment bytes, including 28880 and nested 24680, remain. Any old preflight/validator fixed error allocation before S64 success is covered by the additive enrollment alternative; no old capability or creator is added. The complete new constructor has not been entered.",
    "construction": "343 operations/50 charged guards, fixed arrays and fixed Refusal/validator Expected objects only. G50 branches are mapped individually below. Do not coadd the 4096 error pool to the 3912 constructor while claiming helper4096: all allocating validator branches must be source-unreachable by the earned packet predicates; otherwise this plan is HOLD until a separately reviewed lifetime boundary is registered.",
    "reset": "Cell storage was reserved once and emplace has capacity. Cell/default-member/reset/Refusal field assignments are fixed and nonallocating; the actual reserved-one allocator max_size and noexcept/default-field closure must be verified. Reset cannot inherit an arbitrary error pool just because its source total fits.",
    "original_graph": "Use the unchanged private one-Cell compiler, not any allocating public phase assessor. Generated Request is immutable after G50; its repeated original validator has the same branch map. Full graph/body/other/region paths use fixed arrays, fundamental Interval/Jet/Point values, borrowed strings and checked tags; source get_if/part identity guards must dominate throwing variant get. Full32768 is retained and no predicted refusal excludes a descendant.",
    "canonical": "Original phase4 request controls and their Step02/Step01 source factories return fixed Request/optional<Request> records and mutate only fixed fields. They do not call the public allocating step assessor. All current/pending Request returns remain charged.",
    "pressure_SELF": "Fresh Context/Token and fixed pressure/SELF pools with row-reset Refusal192, fixed current/pending tuple returns, bounded nominal fields/variants, borrowed source names, and unchanged primitives; no owning containers or allocating public evaluator may replace the selected internal bridges.",
    "region_factory": "The genuine original region getter uses fixed values and checked identities. Current/pending region returns are in the named typed pool; there is no heap catalog creator or report-as-input authority.",
    "construction_audit": "Independent source/Y-based oracle uses only registered fixed arrays/fundamental arithmetic and guarded C libm/fenv; no production threshold/cut feedback or extra query. Checked counters/aggregate narrowing are source-unreachable throw branches under the finite Test ledger.",
    "body_audit": "Independent TWO8400 Oracles and full8192 utility remain live and counted. The ONE full15-mass/pose body audit is conditional/sequential with the separate <=121 interval records and the construction audit. Fixed numeric helpers allocate/throw no C++ exceptions. Counter/aggregate/roster guards are discharged by the finite Test ledger, not observed pass results. Universal counter-only checks perform no I/O/heap/callback; status/first-failure reporting starts only after helper/Request/Oracle/workspace death. A sole source-static first-failure string_view16 is explicitly named inside globals156 and preserved caller1024/2048. Later code/objects must prove this selected cut.",
    "interval_audit": "At most121 original interval records; fixed arrays/fundamental helpers, no extra API query. Same finite check/aggregate proof and same selected universal quiet-check/after-audit reporting cut as body audit; full8192 retained.",
    "owned_capacity1_output": "Two full Expected headers and one capacity-one Cell output remain14360. The actual live owning arena and Cell each have separately charged allocator chunk headers within the selected success leaf/metadata plan; language move transfers each sole buffer, while TWO headers and all pending frames remain charged. No copy/NRVO reduction.",
    "post_helper_or_audit_teardown": "Enter only after constructor/source/graph/pressure/SELF or independent Oracle/helper activation returns and any audit Oracle objects have been destroyed. Keep full remaining exterior owners plus error/cleanup pool. Destructor/free/EH paths and reference-count last-owner applicability are future actual proof obligations; this cut is not a scope exemption."
  },
  "original_validator_branch_map": [
    {
      "branch": "root constant count/finite term/unused/supported-sum failure",
      "earned_or_precheck": "G01 genuine full S64 source enrollment includes original S33 (zero-based32) request validator success; G04 term/count/unused domains; G49 byte-exact unchanged non-Y fields and count1 {Y,+0,+0} in BOTH Y constants; G32 finite abs(Y)<=8; G50 structural/domain recheck before original validator",
      "proof": "Every unchanged root constant retains authenticated original phase_constant support. New single-term Y has at most two nonzero addends in either four-term comparison with +/-8, finite bounded sums and no expansion-capacity overflow; exact sign tests cannot establish Y<-8 or Y>8. The original interval value clips to [-8,8] and remains supported under the original qualified FP environment."
    },
    {
      "branch": "sole constant count/finite term/unused/supported-sum failure",
      "earned_or_precheck": "G01 original S33 validator success; G04 domains; G49 exact whole generated packet identity; G50 structural/domain recheck",
      "proof": "Sole term lists and counts are byte-identical to the validated seed; there is no second source value or changed sole-Y term."
    },
    {
      "branch": "sole yaw finite/abs1 failure",
      "earned_or_precheck": "G01 original validator success; G49 exact seed yaw fields; G50 generated packet identity/domain checks",
      "proof": "Both endpoint yaw fields remain the original authenticated fields."
    },
    {
      "branch": "swing finite/[0,.25] failure",
      "earned_or_precheck": "G01 original validator success; G49 whole-packet identity; G50 identity/domain checks",
      "proof": "Both original swing fields are unchanged; no constructor row writes them."
    },
    {
      "branch": "root yaw finite/abs1 failure",
      "earned_or_precheck": "G01 original validator success; G49 whole-packet identity; G50 identity/domain checks",
      "proof": "Both root-yaw carrier entries are unchanged."
    },
    {
      "branch": "torso finite/abs1 failure",
      "earned_or_precheck": "G01 original validator success; G49 whole-packet identity; G50 identity/domain checks",
      "proof": "Both torso carrier entries are unchanged."
    },
    {
      "branch": "reaction fraction finite/[0,1] failure",
      "earned_or_precheck": "G01 original validator success; G49 whole-packet identity; G50 identity/domain checks",
      "proof": "Both original reaction fractions are unchanged."
    },
    {
      "branch": "seconds finite/[1,120] failure",
      "earned_or_precheck": "G01 original validator success; G49 whole-packet identity; G50 identity/domain checks",
      "proof": "Original duration is unchanged."
    }
  ],
  "finite_test_counter_plan": {
    "consumers": 101,
    "next_monotonic_slot_upper": 102,
    "phase_calls_upper": 101,
    "fp_guards_upper": 101,
    "aggregate_fields": 26,
    "each_aggregate_upper": 297041,
    "summary_narrow_used_cap_upper": 2940,
    "checks_components": {
      "consumer_checks_101_times4096": 413696,
      "one_constructor_audit": 1024,
      "one_body_audit": 2048,
      "interval_records_121_times512": 61952,
      "fixed_checks": 4096
    },
    "checks_named_sum": 482816,
    "selected_check_ceiling": 524288,
    "failure_counter_type": "uint32_t",
    "check_counter_type": "uint64_t",
    "evidence": "Test author source-registration proposal; future exact callsites, loops, early returns, duplicate accounting and malformed-availability handling must corroborate. No observed FIRST value is used.",
    "aggregate_reason": "At most101 consumers, each charged used count bounded by its admitted limit; raised-limit controls may reach default+1 before rejection. Conservative max101*(2940+1)=297041. FIRST-only Summary remains bounded by default2940."
  },
  "quiet_check_design": [
    "All Test checks use a counter-only nonallocating/nonthrowing (after finite counter proof) void check(bool,string_view); no check prints, formats, queues or calls a stream.",
    "The sole first_failure view lives in globals156; initially empty, assigned only on the first false check from a source-static literal label. No extra exists flag, owning string, aggregate return, queue or per-record archive. It is never reset or cleared.",
    "All audit phase-status and first-failure printing occurs only after constructor/body/interval numeric helper activations and their Request/Oracle/workspace owners die; remaining owners stay counted under the complete post-helper error/presentation distinction.",
    "Current/pending string_view check arguments remain in existing256 error/check-argument slots; references remain separately named, slot/index/status controls within112. Later actual code must match this exact source plan."
  ],
  "actual_layout_frame_capacity_library_and_feasibility": "UNKNOWN",
  "evaluations": 0
}
```
