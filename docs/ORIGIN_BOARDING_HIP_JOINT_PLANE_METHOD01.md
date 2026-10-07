# Original hip joint-plane evidence method01

This [#513](https://github.com/gobha-me/apsis-drift/issues/513) registration selects one source-bound evidence method for the unchanged Endpoint04 program's pelvis–PORT-thigh pair 2, connected region 1. It can prove containment, prove existence of strict unowned common interior, or remain unresolved. It does not change the original SELF decision, posture, capsule, joint region or any acceptance flag. Implementation, actual resource admission and a frozen observation belong to a later child; every new layout, machine-frame maximum, branch outcome and candidate-specific feasibility is **UNKNOWN** here.

The source baseline is merged #511 at `c3abd7e5c756dba6fb55d5429026698f05b9c949`. The [hip audit](ORIGIN_BOARDING_HIP_CONTAINMENT_AUDIT.md), [original common-interior policy](ORIGIN_BOARDING_SELF_MODEL03.md), [Endpoint04 program](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT04.md) and [worker/world ownership scope](GODOT_ADOPTION.md) remain authoritative. No numerical coordinate, angle, geometric bound, fixture, API, compiler or runtime evaluation selected this method.

## 1. Exact-real proof and finite features

Let B be the authentic pelvis box and C the complete finite thigh capsule with hip H, knee K, radius r, actual axis length L1 and unchanged axial limit L. The [original slab](../include/apsis_drift/origin_boarding_self_model03.hpp), lines 8–15, is `C∩{(K−H)·(p−H)<=L*||K−H||}`, retaining both caps. [Validity](../src/origin_boarding_self_model03.cpp), lines 245–266, requires `r<=L<L1`. On the accepted nominal branch the genuine expression `u=(K−H)/L1` is unit; [Unit24](../src/origin_boarding_planted_legs.cpp), lines 8297–8348, encloses that expression. Reporting midpoints or rounded stored affine columns do not supply exact orthogonality or unit identities. This registration consumes the authenticated nominal phase chart, never static Model03 affine-frame output as interchangeable authority.

For BOTH signs σ=−1 PORT and +1 STAR, the original proper root frame R has `H−ROOT=σa*R.X`, with `a<hx`, positive hy,hz,r. Thus H is strict interior of B and the capsule root ball, and owned at `t=0`, where `t=u·(p−H)`. Convex mixing with H proves `closure(int(B)∩int(C))=B∩C`. Any closed common point with t>L can be mixed slightly with H to become strict common interior while retaining t>L. Consequently containment fails exactly when strict outside-slab common interior exists.

Set `J=H+L*u` and plane P={t=L}. For a strict outside point p, `α=L/t(p)∈(0,1)` and `(1−α)H+αp` is strict interior of both on P. Conversely a strict common-interior point on P has an open neighborhood, allowing a sufficiently small positive displacement along u to give t>L. No displacement is numerically chosen. Therefore

```text
B∩C ⊆ original slab  iff  P∩int(B)∩int(C) is empty.
```

Since `0<L<L1`, C on P is the genuine radius-r closed disk centered J; its strict interior uses radius strictly less than r. The closest segment point is J. This covers the full root/cylinder/distal capsule and remains valid at L=r. Plane equality is owned; strict common interior on the plane implies nearby unowned points, whereas disk/box boundary tangency alone does not.

In the proper root frame let `ξ=R^T*u`, `j=(σa,0,0)+Lξ`. The closed section Q=B∩P uses `ξ·(x−(σa,0,0))=L` and `|x_k|<=h_k`. The original box support is `O=max_B ξ·(x−(σa,0,0))`. H is strict interior at t=0, so P meets int(B) exactly when O>L. O<=L proves ownership, including equality. When O>L, Q has nonempty relative interior in P. Its minimum squared distance d² to j exists, and strict common-interior feasibility is equivalent to `d²<r²`: if a closest boundary point is strictly inside the disk, mix it slightly with a relative-interior section point. Exact `d²=r²` is permitted tangency. The O>L gate is indispensable for this interpretation.

Q is the convex hull of original on-plane box vertices and crossings of the twelve original edges. One proof is the standard active-constraint argument: a section extreme point either is an original vertex or lies on two independent box-face constraints, hence on an original edge; coplanar edges contribute endpoints. For a point/segment section the same edge/vertex description remains valid. Duplicates do not change the hull. If j is in B, j is already on P and d²=0. Otherwise a nearest section point is on the boundary in the ambient plane P: an interior point could be moved slightly toward j to reduce distance. In a two-dimensional section that boundary is made of original face-section segments whose endpoints are in V. In a one-dimensional section the entire segment is ambient-plane boundary; in dimension zero it is a vertex. Every pair chord lies in Q and the nearest boundary feature is among the vertices or pair chords. Thus, for nonempty Q and j outside B, the minimum over all vertices and all pair chords is the exact section distance. Empty Q has no minimum. No ordering, hull builder, iterative solver or generic geometry service is needed.

| Premise | Conclusion / limit |
|---|---|
| Authentic H strict in both convex solids, genuine unit and original slab | Joint-plane equivalence; unrelated tangency-only pairs do not inherit it. |
| Proper nominal R, BOTH σ bindings | Exact local box chart; no midpoint orthogonality assumption. |
| `r<=L<L1` | Genuine full-capsule disk section; caps are not shortened. |
| O<=L | Containment by whole-box ownership, including equality. |
| O>L | Nonempty section interior, enabling strict-distance equivalence. |
| j∈B, genuine j∈P | d²=0; existence follows only with O>L and r>0. |
| Complete original V / all pair chords | Exact finite distance description, including duplicate/degenerate cases. |
| d²>=r² with O>L | Containment, including exact tangency. |
| d²<r² with O>L | Existence of strict unowned interior, not a stored numerical witness or general collision classification. |
| Missing support/topology/authority | Unresolved; no converse from certificate failure. |

## 2. Public output, private issuer and unchanged call boundary

New files only: `include/apsis_drift/origin_boarding_hip_joint_plane_method01.hpp`, `src/origin_boarding_hip_joint_plane_method01_internal.hpp`, `src/origin_boarding_hip_joint_plane_method01.cpp`, and the corresponding new Test. CMake and strict FP registration are additive. No original CPP/header/friend changes, private Endpoint04 capability issuance or old constructor repair is authorized.

Public function:

```cpp
assess_origin_boarding_hip_joint_plane_method01(
    const OriginBoardingIntermediatePauseSupport&)
  -> BoardingHipJointPlaneMethod01Expected;
```

The provider is the only input. Method version1, Endpoint04 version4 and Candidate `root_y_both_ankle_reach_roll_slice` are immutable. Internally call [the unchanged public Endpoint04 assessor](../src/origin_boarding_intermediate_endpoint04.cpp) exactly once with that Candidate. This is the sole numerical original-program call; no fresh source creator, old bounded-limit call, Request inspector, caller-created report/pose, second graph or old Key/Context/Token re-mint is permitted.

`BoardingHipJointPlaneMethod01Diagnostic` owns the genuine provider and a `unique_ptr<const BoardingHipJointPlaneMethod01Data>`. It is movable, noncopyable, explicitly constructed from the provider. Expected is `expected<Diagnostic,string>`. Construct the owning result directly as `Expected result(std::in_place, provider)` through `Diagnostic(const Provider&)`: no standalone Diagnostic initializer and no by-value provider parameter/temporary may coexist with the old graph. TWO Expected slots include the current result and its full pending return; no NRVO or optimizer discount is taken. The source handle is copied directly into its header subobject; it retains the one genuine arena through shared ownership, without another creator or arena allocation. Selected ceilings are Diagnostic168 / Expected176 bytes, Limits16 bytes, payload9216 bytes, local geometry helper4096 bytes; these are forecasts and future compile-only gates, not measured sizes. Payload construction is direct in owned allocation after the original assessor returns; no full-Data stack initializer or factory/aggregate return is assumed. Name every actual return/local if future code introduces one. The selected source forecast conservatively charges TWO complete payloads even though ownership transfer normally retains one allocation.

The compact header holds provider, pointer, new work counters/masks/cursors/state/outcome/condition, original state/stop-condition/stop-stage/stop-pair/region/axis/sign and original earned-flag bits. It has no geometric operand copies. State ordinals: `not_run=0,evidence_complete=1,unresolved=2,capacity=3,unsupported=4`. Outcome ordinals: `not_run=0,contained=1,strict_unowned_exists=2,unresolved=3`. Condition ordinals: `none=0,invalid_limits=1,output_capacity=2,original_unavailable=3,capture_capacity=4,capture_identity=5,unsupported_arithmetic=6,feature_capacity=7,topology_unresolved=8,chord_capacity=9,denominator_unresolved=10,classification_unresolved=11,payload_allocation=12,evidence_identity=13`.

Payload fields and ceilings are fixed:

| Field group | Content / forecast bytes |
|---|---|
| Original metadata | Full Endpoint04 work200, Slice144, optional first Refusal192, BOTH provider source-identity records192, selected two part bindings128. Availability is explicit; no truncation of a promised full record. |
| Actual nominal capture | ROOT/H/K point value bounds and three root-frame column value bounds288; PORT Unit24, ξ and j as nine supported scalar bounds216. |
| Original dimensions | Seven doubles hx,hy,hz,a,L1,r,L: `.24,.12,.18,.14,.47285,.105,kBoardingSelfHipLengthMetres`; the latter retains the original hex constant `0x1.bb0cd605d7512p-3`. No decimal replacement or new fitted value. |
| Finite feature evidence | Twenty records80 each: three supported scalar coordinate bounds plus original vertex/edge provenance and readiness. Twenty point-distance bounds24 each. |
| Chord evidence | 190 supported distance bounds24 each in lexicographic unordered-pair order of active V; no parameter/point witness is promised. Earned prefix count identifies availability. |
| Scalars / availability | 64 bytes for owner extent, radius², minimum distance, their readiness/completeness and feature/chord provenance counters **in addition to the three bound objects described below**. |

The three final supported scalar bound objects are `owner_extent`, `radius_squared`, `minimum_distance_squared`, each `BoardingFootSiteScalarBounds`. Their 72 bytes are additionally named. The full payload forecast is therefore 8192 named plus1024 unused within9216; every geometric field resides inside this deliverable, not an uncharged scratch owner. The old provider source identity uses original type fields/key/name/plane; any string_view refers to immutable data kept alive by the owned provider, never parser scratch. Full old Refusal is copied only at its charged capture step.

The literal public field names/types are frozen below. Original type names refer to unchanged public headers; no additional geometry packet is implicit.

```text
Data:
  BoardingIntermediateEndpoint04Counters original_work;
  BoardingIntermediateEndpoint04SliceEvidence original_slice;
  optional<BoardingIntermediateEndpoint04Refusal> original_first_refusal;
  array<BoardingIntermediatePauseSourceIdentity,2> source_identities;
  array<BoardingRoutePhasePartBinding,2> selected_parts; // pelvis,PORT thigh
  array<BoardingPlantedLegPointBounds,3> points; // ROOT,H,K
  array<BoardingPlantedLegPointBounds,3> root_columns; // X,Y,Z
  array<BoardingFootSiteScalarBounds,3> unit_axis, xi, center;
  RigidVector3 pelvis_half_size_metres;
  double hip_offset_metres, thigh_length_metres, radius_metres, axial_limit_metres;
  array<BoardingHipJointPlaneMethod01Feature,20> features;
  array<BoardingFootSiteScalarBounds,20> feature_distances;
  array<BoardingFootSiteScalarBounds,190> chord_distances;
  BoardingFootSiteScalarBounds owner_extent, radius_squared, minimum_distance_squared;
  uint16_t availability, chord_distance_count;
  uint8_t feature_count, feature_distance_count;
  uint8_t minimum_lower_kind, minimum_upper_kind; // point0,chord1,none255
  uint16_t minimum_lower_index, minimum_upper_index; // none65535
  bool owner_branch, center_branch, feature_branch, minimum_complete;
Feature:
  array<BoardingFootSiteScalarBounds,3> point;
  uint8_t source_feature, kind; // original vertex0..7/edge8..19; vertex0/edge1
  array<uint8_t,2> endpoints; // vertex {i,i}; edge exact listed pair
  bool ready;
Diagnostic:
  OriginBoardingIntermediatePauseSupport source;
  unique_ptr<const Data> data;
  BoardingHipJointPlaneMethod01Counters work;
  uint64_t operation_attempted, operation_written;
  uint32_t feature_attempted, feature_written, original_flags;
  uint8_t capture_attempted, capture_written;
  uint8_t capture_cursor, feature_cursor, operation_cursor;
  uint16_t chord_cursor, operation_row;
  BoardingHipJointPlaneMethod01Stage stage, operation_stage;
  BoardingHipJointPlaneMethod01State state;
  BoardingHipJointPlaneMethod01Outcome outcome;
  BoardingHipJointPlaneMethod01Condition condition;
  BoardingIntermediateEndpoint04State original_state;
  BoardingIntermediateEndpoint04Condition original_condition;
  BoardingIntermediateEndpoint04SelfStage original_stage;
  uint16_t original_pair;
  uint8_t original_region, original_axis, original_sign;
  bool original_available, arithmetic_supported, evidence_complete;
  size_t required_output_bytes, output_capacity_bytes;
Counters:
  uint8_t preflight_guards, return_metadata_checks, endpoint_calls;
  uint16_t capture_checks, feature_checks, chord_checks, operations;
```

All counters/masks/availability start0, flags false, bounds unsupported, pointer null, enum state/outcome not_run and condition none. All absent byte cursors/provenance/minimum-kind are255, absent uint16 cursors/indices65535. Original state/condition/stage start not_run/none/not_run but have no authority until original_available. `original_flags` bits0..12 retain, in exact declaration order, the original header's arithmetic_supported/source_enrolled/kinematic_complete/constant_state/projection_complete/plane_identities/nominal_equilibrium/finite_contact_supported/nominal_load_supported/nominal_support_complete/self_body_complete/self_complete/complete; bits13/14 retain Slice arithmetic_supported/complete, higher bits0. These are recorded facts, not new qualification. Availability bits0..12 mean source identities, old work, old Slice, optional first Refusal metadata, selected parts, points, columns, unit_axis, xi, center, owner extent, radius², complete minimum respectively. Higher bits0. An earned optional Refusal may be absent: that absence is actual metadata, not a default failure. Prefix counts gate feature/chord entries; no unread tail is hashed or interpreted. Min lower/upper provenance may differ and uses first-on-equality Fold rules.

Private stable names: `detail::BoardingHipJointPlaneMethod01Limits`, `detail::BoardingHipJointPlaneMethod01FreshCallContext`, and `detail::hip_joint_plane_method01_bounded(const Provider&,Limits={}) -> Expected`. Context's private constructor takes `(const BoardingIntermediateEndpoint04Diagnostic&, BoardingHipJointPlaneMethod01Diagnostic&, const OriginBoardingIntermediatePauseSupport&)`; copy/move construction and assignment are explicitly deleted. Only that bounded issuer and its purpose-private continuation may inspect the context; no overload accepts a supplied report or extracted geometry. Public Data is returned evidence, never an input API.

Header work types: fixed `preflight_guards`, `return_metadata_checks`, `endpoint_calls` are uint8; lowerable used `capture_checks,feature_checks,chord_checks,operations` are uint16. Capture attempted/written masks are uint8. Feature masks are uint32 with only low20 bits. Current arithmetic-row attempted/written masks are uint64; every arithmetic row has at most35 operations. Counts increment only after a successful charge. Capture written means the actual boolean tuple was evaluated, including false. For a vertex row, feature written means the supported residual/sign classification was stored in helper-local state, including `unknown`; only the header feature-written mask records this step in returned evidence. Residuals and sign classes have no Data fields. For an edge row, feature written means the crossing/omission decision was resolved. The local t/f/sign records are named in the4096 helper pool and die with it. Arithmetic written means a supported result was published. False capture predicates, uncertain topology and unsupported arithmetic are distinct.

Cursors: capture255, feature255, chord65535, operation255 initially. Arithmetic suboperation is zero-based within its current row; retain separate `operation_stage` and row ID. Stages ordinals: not_run0, original_call1, capture2, chart3, vertex4, plane_gate5, center6, vertex_distance7, edge8, chord9, classification10, complete11. On capacity the attempted cursor names the upcoming unexecuted row/suboperation. Current-row masks retain already earned bits in that row; a newly entered arithmetic row has explicitly empty masks. Previously completed rows remain represented by total counts and earned Data records/prefixes. On completion capture cursor7, stage complete; last arithmetic stage/row/masks remain unchanged. Do not reinterpret a complete-stage cursor as a whole-row bitmask.

A purpose-private `FreshCallContext` is noncopyable/nonmovable and stores references to this local actual Endpoint04 value, new Diagnostic, and input provider. Only the producer's private issuer can construct it after a successful original return; it never escapes or accepts a caller report. Later capture/arithmetic uses that one context synchronously. Freeze its construction inside one issuer-local noinline post-call continuation invoked only after the original assessor returns. The continuation closure holds const provider/old-Diagnostic and new Diagnostic/Limits references (four reference slots); its Context is local to that noinline invocation. It is not std::function, a by-value aggregate return, or an exported function accepting a supplied old report. Its access derives from the lexical private bounded issuer, so it exposes no second report-authority entry. No Context exists during the old graph, and the future measured physical proof must include both the caller frame and this continuation frame, without inferring machine fit from source-lifetime death. It is not an Endpoint04 capability and cannot call an old private continuation. The old Expected/Cell remain alive through capture and feature computation, then die before the new result escapes. Their provider, source metadata and flags are retained according to the explicit output availability gates.

## 3. Preflight, limits, partial states and output

Five lowerable fields in exact order, default/maximum: `uint16_t capture_checks=8`, `feature_checks=20`, `chord_checks=190`, `operations=16384`, `size_t output_bytes=16MiB`. Raising any field is invalid; no field loosens original limits. Four Limits slots (default, current, parameter/pending and comparison ceiling) are always separately reserved. Public call uses defaults; only a purpose-private bounded test adapter exposes lowering, accepting genuine provider alone.

Literal preflight order is FP FIRST, limits, two-new-header room, complete required room, original call. One counted fixed FP guard uses the unchanged original environment checker, including fenv and MXCSR requirements. Unsupported FP returns `unexpected("hip joint plane01 unsupported floating point")` before any original call or source/payload allocation. Safe raised limits returns `unexpected("hip joint plane01 invalid limits")`. Budget below `2*sizeof(NewExpected)` returns `unexpected("hip joint plane01 output headers")`. The bounded string/error representation is charged separately; no claim of zero error allocation is made.

Required budget is `2*sizeof(NewExpected)+boarding_intermediate_endpoint04_required_output_bytes()+2*sizeof(Data)`. Check it before the original call or payload allocation. A shortfall above header room returns structured capacity/output_capacity with zero old calls and null payload. The actual owned returned output is only `2*sizeof(NewExpected)+(payload?sizeof(Data):0)`. During old report/payload coexistence charge original two headers+actual Cell capacity, new two headers, and current/pending payload capacities. REQUIRED and owned are different; exact-used replay lowers output to REQUIRED, not merely returned ownership. New output is fixed-allocation, no output vectors or hidden arena; bad_alloc maps payload_allocation/capacity without warmup/retry.

After the original call, one fixed counted nonnumeric return-metadata check inspects Expected success/error and, on success, copies the compact actual original state/flags/stop refs to the new header. This remains before any lowerable capture and preserves actual original hard state even at capture0. An original unexpected string is moved unchanged into new unexpected; external accounting records the one child call for this branch. No numeric record is inspected by this fixed check. If the actual original state is capacity or unsupported, return that same hard state immediately with null payload and no capture; do not replace it with an allocation finding. Otherwise allocate payload directly, issue the context and run capture lazily. No payload exists during the original graph.

Original capacity/unsupported is never downgraded: it remains in original metadata and overall capacity/unsupported respectively; no new geometric branch runs on it. Ordinary absent prerequisites yield unresolved/original_unavailable. New capacity preserves earlier arithmetic readiness/records and original findings. New arithmetic_supported starts false, is earned only after all fifteen chart operations have supported results with original arithmetic prerequisites, and means partial evaluated companion arithmetic. New FP or attempted-arithmetic failure clears it; capacity/ordinary inconclusive stops preserve it. No original flag is promoted or erased. Payload availability marks every record; absent/default numbers are never evidence.

All new qualification flags (`self,endpoint,world,route,actor,seat,save,dynamics,material,strength,friction,first_flight`) are permanently false. Outcome contained resolves only this mathematical hip-containment proposition. Strict_unowned_exists certifies existence by the proved section theorem; it is not a returned original strict-witness point or full SELF/collision decision. Evidence complete with outcome unresolved is possible when supported bounds overlap at final classification.

## 4. Literal capture tuples C01–C08

Charge before all tuple reads through deferred predicates. Each success writes its actual predicate boolean and corresponding bit; false is written but stops. No C01 field arithmetic substitutes for later charged operations. Old metadata copies occur only inside the stated charged row.

| Row | Exact tuple and earned publication |
|---|---|
| C01 | Same local successful old Expected, actual provider summary nonnull/complete, `old.source.summary()==provider.summary()`, context addresses exact; copy BOTH immutable source identities and optional original first Refusal under metadata availability. |
| C02 | A separate actual post-call original FP-environment guard. Unsafe maps unsupported; never reads geometry or pretends source identity failure. |
| C03 | old version4/self_version1 and fixed Candidate; source_enrolled true, source work64/full UINT64 mask; Slice complete/supported, construction work37/226, four operation-written words `{UINT64_MAX,UINT64_MAX,UINT64_MAX,0x3ffffffff}` and guard-written low37 all set; exact reporting clock2. Copy full work and Slice, preserving their old attempted/written/default bits unchanged; Data availability records this actual copy. |
| C04 | Exactly one old Cell, capacity1, old phase_calls1 and phase work1/2/1/3/6; old/Cell kinematic, constant, projection, plane, equilibrium, contact, load and nominal-support complete; phase complete/supported/nominal-links/target-sole/joint-sector/derivative/timing true; phase first0,last1. No reporting angle substitutes. |
| C05 | old/Cell self_body_complete, B63 mask0x7fffffffffffffff; Unit24 attempted/written0x00ffffff, complete mask15, cursor23; each selected PORT thigh unit component supported/finite/ordered. |
| C06 | Actual parts0/3 IDs pelvis/PORT-thigh, original pelvis box root center/frame root and half sizes, original capsule PORT H/K IDs/radius; proper nominal root-frame source recipe and authentic H identity; original positive dimensions and r<=L<L1, exact original hex L. ROOT/H/K value and frame bounds finite/ordered. Copy the two bindings, dimensions, actual six vector bounds and PORT Unit24. |
| C07 | Same context and source; C01–C06 true/written63, no original hard state, permitted actual original state accepted/unresolved/witness_refused with required earlier authority intact. This step validates eligibility, without assuming stop pair2 or a particular SELF outcome. |
| C08 | Final same-context/provider/old-value identity and C01–C07 written/true127; no hard original state. Validate the chosen branch's supported bounds, exact earned feature/chord prefixes and final inequality rule below. Publish only then. |

C03's final operation word has34 bits (`226−192`); no unused high bits may be set. The original hip tuple uses nominal original source authority, not a caller claimed unit. If any tuple fails, stop with capture_identity for structural mismatch, original_unavailable for unearned prerequisites, or unsupported for C02. C07 does not retry the original SELF decision. Numeric legacy limiting values without a supported source-owned availability flag remain unconsumed.

## 5. Fixed primitive operations and finite arithmetic program

Use binary64 strict original-supported environment. Each logical primitive is charged once before reading operands; it includes all its explicitly stated endpoint arithmetic, nextafter calls, guards and publication. No FMA, long double, exact-sign service, generic root, solver, iterative refinement or alternative math oracle is selected. All stored operands/results must be finite, ordered and not at finite overflow endpoints; failure publishes no result and stops unsupported. Internal primitive current/pending/argument intervals are independently reserved.

`POINT` copies an existing finite original binary64 constant exactly. Such constant packets are identity reads, not new computed coordinates. `COPY` is a charged supported-bound copy. `ADD/SUB`: lower/upper endpoint combination in that order, round each down/up with `nextafter`; an exact zero operand permits the algebraically identical copy/negation, and two exact zero operands publish canonical +0. `MUL`: four endpoint products in `(lo,lo),(lo,hi),(hi,lo),(hi,hi)` order, min/max with first-on-equality, then down/up; an exact zero interval returns canonical +0. `SQUARE`: two endpoint squares; if interval crosses zero lower=+0, otherwise down(min); upper=up(max), with exact zero interval returning +0. `DIV`: denominator must exclude zero; reciprocal bounds are down(1/high), up(1/low), then the same MUL. `ABS`: monotone full absolute enclosure using sign branches; crossing lower=+0, upper=max(−lo,hi), first-on-equality. `MIN/MAX`: componentwise endpoint selection, first operand on equality, no added rounding. `CLAMP01`: independently clamp lower/upper to [0,1], closed equality and canonical endpoint constants. Primitive validity guards and branch comparisons are part of the charged primitive, not hidden separate numeric work. Bounds are original expression enclosures, never fabricated by treating rounded coordinates as exact inputs.

Vertices i=0..7 use bit positions0/1/2 for X/Y/Z respectively: each bit VALUE0 selects the negative half-size and VALUE1 selects the positive half-size. Edge order `(0,1),(0,2),(0,4),(1,3),(1,5),(2,3),(2,6),(3,7),(4,5),(4,6),(5,7),(6,7)`. Active V order: on-plane vertices ascending, then strict crossings in that edge order. Edges with an on-plane endpoint add no duplicate; coplanar endpoints are already present. Distinct actual original vertices/crossing features are retained without midpoint deduplication.

The following local op tables are literal, repeated only in the indicated finite order. A new arithmetic-row resets its local masks according to the literal ledger below; successful charge sets attempted bit, supported publication sets written bit. Unsupported/capacity retains prior masks/records. Index/control arithmetic is bounded integer work, never a geometry oracle.

| Arithmetic row | Local ordered operations |
|---|---|
| Chart,15 | For each axis X,Y,Z: multiply root-frame column components by actual Unit24 components X,Y,Z (3), ADD first two, ADD third. Publish ξ component. |
| Vertex i,8 | SUB vertexX−σa; MUL that by ξX, MUL vertexY by ξY, MUL vertexZ by ξZ; ADD first two, ADD third (t_i); SUB t_i−L (f_i); COPY t_i for i0, otherwise MAX(previous O,t_i). |
| Center,8 | SQUARE r; MUL LξX, ADD σa; MUL LξY; MUL LξZ; ABS jX,jY,jZ. |
| Point-distance,8 | SUB j−V X,Y,Z; SQUARE each difference; ADD first two, ADD third. |
| Edge interpolation,11 | SUB fA−fB; DIV fA/denominator; SUB B−A X,Y,Z; MUL lambda by differences X,Y,Z; ADD A to products X,Y,Z. |
| Chord,34 | SUB B−A X,Y,Z (d); SUB j−A X,Y,Z (e); SQUARE d X,Y,Z then two ADDs (dd); MUL e*d X,Y,Z then two ADDs (ed); SQUARE e X,Y,Z then two ADDs (ee); DIV ed/dd; CLAMP01; MUL clamped parameter*d X,Y,Z; SUB e−products X,Y,Z; SQUARE residuals X,Y,Z then two ADDs (distance). |
| Fold,1 | COPY first candidate distance; otherwise MIN(previous minimum,candidate). |

Literal arithmetic-row ledger (local operation numbers above are one-based; cursor/bit indices are zero-based):

| operation_stage | operation_row | Exact row size / completed mask / cursor |
|---|---|---|
| chart3 | 0 | Chart15; low15 bits /14. |
| vertex4 | original vertex i=0..7 | Vertex8; low8 /7. |
| center6 | 0 | Center8; low8 /7. |
| center6 | 1 | Center-zero COPY(+0) only; bit0 /0. This is a distinct row, not a ninth operation of Center8. |
| vertex_distance7 | active V index0..19 | Point-distance8 **fused with Fold at slot9**; low9 /8. Used for both original on-plane vertices and newly added edge crossings. |
| edge8 | original edge ordinal0..11 | Edge interpolation11 alone; low11 /10. Subsequent point-distance/Fold uses the separate vertex_distance row for its active V index. |
| chord9 | lexicographic active unordered-pair ordinal0..n(n−1)/2−1 | Chord34 **fused with Fold at slot35**; normal low35 /34. Duplicate branch has low22 plus bit34 /34; slots23–34 stay NOT_RUN. |

On entry to each new `(operation_stage,operation_row)` pair, before attempting its first operation, set those IDs, operation_cursor0 and clear both current-row masks. This is bounded administrative work without operand reads. Before each operation set its upcoming cursor, then charge; failed charge increments no count and sets no attempted/written bit. Successful charge sets attempted before evaluating the deferred operands; supported publication sets written. Thus capacity at a first operation exposes empty new-row masks; later capacity retains the prefix or genuine branch mask of that row. A resolved omission edge does not start an arithmetic row. Entering capture/plane/classification/complete stages changes the overall stage only; last arithmetic IDs/masks/cursor remain intact. The minimum Fold is always in the fused point/chord row and updates supported minimum and first-on-equality lower/upper provenance together. No standalone Fold row exists. This ledger freezes every reset and terminal cursor without changing7049 total operations.

Execution order:

1. C01–C07, then Chart15. No feature arrays or primitive math before the original call returns.
2. Eight vertex feature charges, each followed by Vertex8. Sign class is negative iff f.upper<0, positive iff f.lower>0, zero iff supported f.lower=f.upper=0; otherwise unknown. Store t_i, f_i and the sign class only in the named helper-local interval/sign slots, and set the header feature-written bit; no returned residual/class record is implied. No topology guess from midpoint/signbit. If O.upper<=L, go directly to C08 owner branch. If not O.lower>L, stop topology_unresolved. Unknown signs may survive only the owner shortcut; otherwise stop before crossings.
3. Center8. If all `ABS(j_k).upper<=h_k`, compute charged COPY(+0) as minimum and go to C08 center branch. Otherwise require at least one `ABS(j_k).lower>h_k` to certify j outside B; if neither, stop classification_unresolved. Plane identity of j is theorem/source authority, not an interval midpoint plane test.
4. Append each zero vertex, perform Point-distance8 and Fold1. Then perform twelve edge feature charges. Opposite strict signs run Edge11; denominator excludes zero and lambda bounds must lie within [0,1], else supported uncertainty is unresolved. Append the actual algebraic crossing enclosure, Point-distance8, Fold1. Same-sign/coplanar/endpoint-zero edges are resolved omissions. Every original edge row writes its resolved completion, including omitted rows. No array entry is read unless its provenance/readiness is earned.
5. Require all20 feature rows resolved and V count n in[3,20] (O>L implies genuine two-dimensional section). Traverse every unordered pair of active indices i<j lexicographically, charge one chord row, then Chord34+Fold1. Before DIV, dd.lower>0 is required. Exact supported dd.upper=0 is the duplicate/zero-chord branch: local slot22 becomes COPY(ee), slots23–34 are NOT_RUN, and Fold retains local slot35; dd straddling zero with positive upper is denominator_unresolved. This branch treats a proved duplicate as a vertex, not an unsafe divide. Save each supported distance at that chord's prefix index only after Fold succeeds. Normal chord masks contain35 bits; the duplicate branch contains low22 bits plus bit34. The terminal suboperation cursor is34 in either branch. Unsupported dd excludes any quotient read.
6. C08. Owner branch requires supported O.upper<=L and publishes outcome contained, state evidence_complete, evidence_complete=true, condition none. It neither reads nor fabricates absent minimum-distance or radius² fields; their availability remains clear. Center branch requires O.lower>L, supported original positive r², certified center-inside and exact zero minimum. Feature branch requires O.lower>L, certified outside j, all20 resolved feature bits, n active records/point distances, exactly n(n−1)/2 complete chord distances and supported fold minimum/r². Retain evidence intervals. Capture numerical copies/operations are already available behind their respective C03/C06/arithmetic gates; C08 alone earns the completed theorem outcome, not the earlier snapshot availability. If minimum.upper<radius².lower, publish strict_unowned_exists; else if minimum.lower>=radius².upper publish contained; else publish outcome unresolved, condition classification_unresolved, state evidence_complete and evidence_complete=true. Both successful contained and strict_unowned_exists distance outcomes likewise have state evidence_complete, evidence_complete=true and condition none. This final supported interval-straddle is a completed evidence program, distinct from an earlier incomplete stop with state unresolved. Center's zero branch uses the same strict rule. Unsupported/range/topology stops never claim a completed minimum.

Conservative maximum logical operations: Chart15 + Vertex64 + Center8 + eight point/fold rows72 + twelve edge/point/fold rows240 +190 chord/fold rows6650 =7049; the separate center-zero COPY is a mutually exclusive shorter path. Default16384 is a finite selected cap, not a claim actual7049 work is reached. Capture max8, feature max20, chord max190. No multiword global operation archive is needed: current row masks plus total count, per-stage completion and earned output prefixes identify the actual finite program.

## 6. Source storage, output and future physical gates

All numeric worker owners must remain under49152 bytes; the original creator remains8192; output16MiB and retained log16KiB. Original world loading and retained geometry remain separately nonzero, with cold/reentered source/library/borrowed numerical paths explicitly measured. No warmup, engine-wide48KiB fiction or unreported world-owner deletion is permitted.

The full old Endpoint04 public-assessor stage is preserved as a whole, including original graph32768, nested full enrollment33208/28880/24680, arena4096, two old Expected1664, full Cell11032, FOUR old Limits200, current/pending Request1368, Refusal368, Key/Context/Token/control, caller2048, scalar BSS152 and conservative native globals88. Do not subtract unused old pools or forecast compiler elision. The new graph addition is exactly two176 headers352 +FOUR16 Limits64 +outer control64=480: old48624+480=49104,48 bytes headroom. New payload, feature arrays and primitive locals begin only after this call returns.

The pre-call/old-assessor outer control64 reservation remains unchanged and includes bounded references/control only, with no constructed FreshCallContext. The post-call feature stage independently uses outer control72: four continuation references32, the complete three-reference FreshCallContext24, and bounded closure/this-pointer/cursor control16. No part of Context is absorbed into the helper192 reference pool. The context dies before the independent after-return oracle, whose existing outer control64 is unchanged. Unexpected-string/error and allocator paths have independent512 reserves in their live stages; no live new error buffer is assumed during the old graph. The helper4096 names20 points as60 interval slots1440, eight t/eight residuals384, ξ/j144, center ABS72,20 work intervals480, four primitive current/pending argument slots96, original constant intervals144, provenance/sign controls28, six current/pending scalar intervals144, refs/closures/cursors192 and library/error512:3636 named/460 unused. No by-value Point/feature-array/ReportData aggregate factory is selected. Payload direct construction and copies are by destination reference; forwarding full old Refusal current/pending368 is separately charged. Every partial/exception/destructor path must preserve that lifetime proof in implementation.

The source forecast below retains every old stage number by named whole-stage inclusion. Old independent chart/body/interval audit programs and their full3072/8192/8192 pools remain unchanged historical/regression obligations; they are not callees of the public Endpoint04 assessor or this diagnostic. The new independent A01 distance oracle is a separately registered Test utility after return, not a reason to borrow those pools or delete old tests. All current/pending payload ownership is included in the new co-live post-call stage; returned output capacity is separately reported. Full existing maps/pools remain available in the original [structured inventory](ORIGIN_BOARDING_INTERMEDIATE_ENDPOINT04_TEST_INVENTORY.json).

```json
{
  "status": "FORECAST_ONLY_ACTUAL_LAYOUT_FRAME_OUTCOME_UNKNOWN",
  "source_stages": {
    "preflight": {
      "components": {
        "entire_old_preflight": 11088,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 64
      },
      "bytes": 11568,
      "ceiling": 49152,
      "headroom": 37584
    },
    "required_room_refusal": {
      "components": {
        "entire_old_required_room_refusal": 12136,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 64
      },
      "bytes": 12616,
      "ceiling": 49152,
      "headroom": 36536
    },
    "source_enrollment": {
      "components": {
        "entire_old_source_enrollment": 39488,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 64
      },
      "bytes": 39968,
      "ceiling": 49152,
      "headroom": 9184
    },
    "source_plus_disjoint_construction_overcharge": {
      "components": {
        "entire_old_source_plus_disjoint_construction_overcharge": 43584,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 64
      },
      "bytes": 44064,
      "ceiling": 49152,
      "headroom": 5088
    },
    "construction": {
      "components": {
        "entire_old_construction": 17168,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 64
      },
      "bytes": 17648,
      "ceiling": 49152,
      "headroom": 31504
    },
    "reset": {
      "components": {
        "entire_old_reset": 34834,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 64
      },
      "bytes": 35314,
      "ceiling": 49152,
      "headroom": 13838
    },
    "original_graph": {
      "components": {
        "entire_old_original_graph": 48624,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 64
      },
      "bytes": 49104,
      "ceiling": 49152,
      "headroom": 48
    },
    "canonical": {
      "components": {
        "entire_old_canonical": 31312,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 64
      },
      "bytes": 31792,
      "ceiling": 49152,
      "headroom": 17360
    },
    "pressure_SELF": {
      "components": {
        "entire_old_pressure_SELF": 29776,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 64
      },
      "bytes": 30256,
      "ceiling": 49152,
      "headroom": 18896
    },
    "region_factory": {
      "components": {
        "entire_old_region_factory": 25072,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 64
      },
      "bytes": 25552,
      "ceiling": 49152,
      "headroom": 23600
    },
    "joint_plane_features": {
      "components": {
        "old_two_expected": 3328,
        "old_cell": 11032,
        "arena": 4096,
        "old_four_limits": 800,
        "preserved_caller": 2048,
        "current_pending_request_overcharge": 1368,
        "current_pending_old_refusal_overcharge": 368,
        "scalar_error": 1024,
        "old_key_context_token_overcharge": 112,
        "scalar_global": 152,
        "native_global_overcharge": 88,
        "new_two_expected_headers": 352,
        "new_four_limits": 64,
        "new_outer_controls": 72,
        "new_geometry_helper": 4096,
        "new_current_pending_payloads": 18432,
        "new_current_pending_forwarding_refusal": 368
      },
      "bytes": 47800,
      "ceiling": 49152,
      "headroom": 1352
    },
    "new_preflight": {
      "components": {
        "new_two_expected": 352,
        "new_four_limits": 64,
        "caller": 2048,
        "library_error": 512,
        "control": 64,
        "arena_overcharge": 4096,
        "scalar_global": 152,
        "native_global_overcharge": 88
      },
      "bytes": 7376,
      "ceiling": 49152,
      "headroom": 41776
    },
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
    "required_output": {
      "components": {
        "old_two_expected": 3328,
        "old_cell": 11032,
        "new_two_expected": 352,
        "new_current_pending_payloads": 18432
      },
      "bytes": 33144,
      "ceiling": 16777216,
      "headroom": 16744072
    },
    "returned_output": {
      "components": {
        "new_two_expected_headers": 352,
        "one_payload": 9216
      },
      "bytes": 9568,
      "ceiling": 16777216,
      "headroom": 16767648
    },
    "independent_distance_audit": {
      "components": {
        "new_two_expected_headers": 352,
        "one_payload": 9216,
        "arena": 4096,
        "new_four_limits": 64,
        "preserved_caller": 2048,
        "scalar_global": 152,
        "native_global_overcharge": 88,
        "independent_reconstruction_result": 1024,
        "independent_oracle_helper": 4096,
        "independent_outer_error": 512,
        "outer_control": 64
      },
      "bytes": 21712,
      "ceiling": 49152,
      "headroom": 27440
    }
  },
  "helper_pool": {
    "named_components": {
      "twenty_point_intervals": 1440,
      "eight_t_and_eight_residual_intervals": 384,
      "xi_and_center_six_intervals": 144,
      "three_center_absolute_intervals": 72,
      "twenty_work_intervals": 480,
      "four_primitive_argument_local_pending_intervals": 96,
      "six_original_constant_intervals": 144,
      "active_provenance_twenty_and_signs_eight": 28,
      "six_scalar_current_pending_intervals": 144,
      "references_closures_cursors": 192,
      "independent_library_error": 512
    },
    "named_bytes": 3636,
    "unused_bytes": 460,
    "reservation_bytes": 4096
  },
  "payload_pool": {
    "named_components": {
      "old_work": 200,
      "old_slice": 144,
      "old_optional_first_refusal": 192,
      "two_source_identity_records": 192,
      "two_original_part_bindings": 128,
      "ROOT_H_K_and_three_root_columns": 288,
      "unit_xi_center_nine_supported_bounds": 216,
      "twenty_feature_records": 1600,
      "twenty_feature_distance_bounds": 480,
      "onehundredninety_chord_distance_bounds": 4560,
      "seven_original_dimension_doubles": 56,
      "availability_identity_prefix_scalars": 64,
      "three_final_supported_scalar_bounds": 72
    },
    "named_bytes": 8192,
    "unused_bytes": 1024,
    "reservation_bytes": 9216
  }
}
```

The final implementation gate requires BOTH GCC/Clang strict-FP object-only layouts and true PIC/native/included Test-caller maxima, full current/pending/return/allocator/error/destructor/cold initialization ancestry, raw duplicate SU rows, symbols and relocation-resolved disassembly. No absent row is zero. Match unchanged source/tool/header/generated dependencies for reused ancestry and verify actual normal object correspondence before freeze. A physical peak can fail despite this source forecast; no runtime follows a failed gate. Revise implementation or its separately reviewed resource registration before evaluation, preserving this mathematical program, original packet/policies and49152; never fit another pose or raise the cap implicitly.

## 7. Matched finite tests and publication sequence

Select29 control purposes: A01 FIRST; A02–06 zero each five fields; A07–11 raised each field; A12–16 actual-reached used−1; A17 exact-used replay; A18 FE_DOWNWARD,A19 FE_UPWARD,A20 FE_TOWARDZERO,A21 MXCSR FTZ bit15,A22 DAZ bit6,A23 MXCSR nonnearest `(control & ~(3U<<13)) | (1U<<13)` with ordinary fenv; A24 genuine moved-empty alias invalid binding; A25 valid moved-to original; A26 valid survivor after scoped original-owner destruction; A27 restored-original reuse; A28 capture0+feature0; A29 operations0+feature0+chord0. The provider has no public default constructor: no manufactured independent empty-source control, seam or additional creator is selected. Reached controls honestly skip if used0. Exact output uses REQUIRED, not returned ownership. FIRST is one genuine creator and one consumer per compiler; no raw/source-inspector/body-oracle/alternative candidate or second original query. Fixed wrapper FP and return-metadata counts include unexpected branches according to actual reached call order. Limits raising fails before the original call, and required-room shortfall avoids it.

The matched [Test manifest](ORIGIN_BOARDING_HIP_JOINT_PLANE_METHOD01_TEST_MANIFEST.md) freezes the independent caller/audit utility and exact finite controls before final publication. A01-only independent geometry/distance audit is mandatory. The Test author reconstructs the original nominal PORT chart from the actual earned source packet/Y, compares genuine captured ROOT/H/K/frame/unit/ξ/j/O/r², then independently considers zero active faces, six single-face plane intersections and15 face-pair candidates. This differs from the production all-chord recipe. Its long-double arithmetic, finite roundoff allowance, denominator/parallel/coplanar ambiguity gates, actual workspace and independent comparison policy must be frozen in the matched Test manifest. Approximate corroboration is not a rigorous production enclosure or a tolerance on original predicates. It never promotes an interval-indeterminate production result or creates source authority. No independent geometry oracle is waived to reduce output size.

Before this issue completes, exact method plus matched Test manifest/inventory must pass independent proof/source/publication review and be published together without private paths or hash cycles. The next implementation child then preserves all original bytes, implements only this registration, obtains complete actual worker admission, normal object/binary/source preservation, and freezes exactly one GCC and one Clang FIRST before ordinary regression. New evidence resolves at most the selected proposition; original complete SELF, other pairs, WORLD, route and First Flight stay unearned.
