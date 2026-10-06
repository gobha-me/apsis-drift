# Constructive intermediate pause support02

Selected method for [#473](https://github.com/gobha-me/apsis-drift/issues/473),
after #471 / PR#472 merged. The existing quarter-load pause is refuted; this
method tests one lighter, nonzero port load at the same held geometry. A positive
static pause would be a prerequisite for a separately qualified movement.

This registration precedes implementation and every new source, graph, pressure,
math fixture and oracle query. The selected request changes only the port load
share to 1/16; the starboard share is 15/16. Numerical outcome remains unknown.
No alternative load split, pressure recipe or posture is sampled after a refusal.

Baseline `b0a4d9f61b56948a2695f354975f0f0ba238c0fb`: all 829 original inputs are pinned.
The only planned existing-source edit is an append at the end of the complete
108954-byte boot implementation, whose frozen prefix SHA256 is
`77db700b00d4a57ca2de28d1c756c663583d44d35bff6064adc52fc8cff9050f`. All earlier implementations and results remain
unchanged. Ordinary new CMake wiring and documentation are separate.

Independent source-only method and literal-manifest review passed. Schema SHA256
`fd324b2f60cbf281ad4e49675700675ac65a688660f4325eed54e0b4da8c6b90`; test manifest
`588a0303841f4f69e9be6890c2fd1ea2452f0e374121981dcf09dfc286421e5f`; review
`eb3cf64f7416cb80a4cfc7d60ced16a32c6bc387d45f01f6ab1c256639c3adaa`.
Resource numbers below are forecasts. Actual layouts and complete source, caller,
helper, pending-return, allocator and arena lifetimes on both compilers must
pass before the first execution.

Public constants selected before code: version2, output4096, scratch49152,
projection256 and definition31; use the corresponding
`kBoardingIntermediatePauseSupport02*` names. The actual expected return must
fit the source proof, whose forecast reserves two returns of at most3072 bytes.

The exact schema and literal tests below are the registered implementation
contract. Registration makes these choices authoritative; drafting language
inside the retained prescriptions describes their preimplementation origin.

# Support02 exact proposed API, guard and operation prescription

READ-ONLY local draft, before code or new query. Parent selects registration after471merge. One new static v2 request changes ONLY portreaction to1/16 at the unchanged E/I two-second held pose. No trajectory, source/asset/body change or adaptive candidate. The source provider remains the existing admitted v1 provider; every NEW v2 graph/pressure/math/oracle outcome is UNKNOWN. Preserve v1 and all469/471/467/foot-phase kernels/results.

## Selected names and ownership

New files `include/apsis_drift/origin_boarding_intermediate_pause_support02.hpp`, `src/origin_boarding_intermediate_pause_support02.cpp`, `_internal.hpp` and dedicated test. Public function:

    assess_origin_boarding_intermediate_pause_support02(
      const OriginBoardingIntermediatePauseSupport&, bool reverse=false)
      -> expected<BoardingIntermediatePauseSupport02Diagnostic,string>;

Existing immutable source handle is the ONLY input. No new creator, source arena, share/pressure/body input or arbitrary caller accepted-cell API. One old public source creator before all tests. Diagnostic version2 owns source handle and compact projected/support records, no full Cell or old Diagnostic. Private `intermediate_pause_support02_request()` returns COPY of original phase4 request with ONLY portfraction{.0625,.0625}; caller copies cannot enter production. Public new assessor fresh compiles its OWN request once over[0,1], T2, graph1/legs2/body1/sectors3/timing6. Its cell share point is1/16, not an external reinterpretation of old .25.

New private `BoardingIntermediatePauseSupport02ProjectionToken`: private constructor; only named `intermediate_pause_support02_bounded` friend may issue after the fresh complete owned cell/immutable v2 request/projected fields authenticate. Token borrows const source owner, cell and immutable request; local pressure bridge consumes it synchronously and cannot retain it. Before dereference, bridge checks source valid/sameData against diagnostic owner. No old469 token reused; no caller cell/bool/numeric fixture can mint it.

Appended END-only boot bridge `intermediate_pause_support02_pressure_bridge(token,Limits const&,Diagnostic&)` reuses unchanged small interval/site-edge primitive. Only new include at appended boundary if necessary. No old function/expression/API/layout edits or generic helper extraction. New boot whole-file/link hash changes explicitly registered; historical prefix exact and all other original kernels/data pinned; fresh local Godot compatibility if reuse identity differs.

Public enum names: `BoardingIntermediatePauseSupport02State` (not_run, prerequisite_refused, capacity, unsupported, witness_refused, unresolved, supported), `BoardingIntermediatePauseSupport02Condition` (none, invalid_limits, output_capacity, invalid_binding, unsupported_arithmetic, phase_prerequisite, projection_capacity, projection_identity, sole_plane, source_rectangle, empty_intersection, denominator, definition_capacity, sole_extrema_capacity, source_coordinate_capacity, intersection_capacity, midpoint_capacity, allocation_capacity, pressure_capacity, edge_capacity, symbolic_equilibrium, sole_disk, source_disk).

`BoardingIntermediatePauseSupport02Refusal`: condition; optional side/axis/vertex/edge/operation; bool source_edge; supported limiting interval; original `BoardingRouteFootPhaseRefusal phase`. `first_refusal` retains first ordinary edge finding; `stop_condition` separately records terminal arithmetic/capacity/prerequisite. Source/refused or cap fields never become a success-shaped permission.

`BoardingIntermediatePauseSupport02Diagnostic` fields:

- source, version2, reverse, duration_seconds2, reactions{.0625,.9375};
- com_xz[2], boot_centers_xz[2][2], source_planes[2]; Scalar=`BoardingFootSiteScalarBounds` (supported bit retained);
- port_sole_lower_xz[2], port_sole_upper_xz[2], port_source_lower_xz[2], port_source_upper_xz[2], intersection_lower_xz[2], intersection_upper_xz[2];
- pressure_xz[2][2]; source_coordinate_evaluated[4][2], sole_extrema_evaluated[2][2], intersection_evaluated[2][2], midpoint_evaluated[2][2], allocation_evaluated[2][3], pressure_evaluated[2];
- source descriptors2 (borrowed genuine name, two keys, sourceplane), two existing compact site disk records (four sole/four source edge evidence/evaluated per side; plane_identity/status/evaluated/complete); original emitted predicate bounds remain plain genuine copied data;
- work, definition_evaluated[31], projected_carrier_complete[11], first_refusal, stop_condition, output_bytes, state;
- arithmetic_supported, kinematic_complete, constant_state, projection_complete, intersection_complete, nominal_equilibrium, finite_contact_supported, nominal_load_supported, complete;
- staticfalse self/material/WORLD/route/seat/actor/save/strength/friction/dynamics/FirstFlight. New positive load/contact only on all actual finite predicates, never global route/physical certification.

No owned request in compact output; fixed request inspector supports fieldwise oracle/identity inspection. Request680 exists privately and is explicitly charged before graph, not hidden in old proof. Scalar properties never allow caller source admission.

## Caps/counters EXACT fields

Private `BoardingIntermediatePauseSupport02Limits`:

    BoardingIntermediatePausePhaseLimits phase; // five old limits1/2/1/3/6
    size_t projection_guards=256, pressure_candidates=2, disk_edges=16,
           output_bytes=4096;
    size_t sole_extrema=4, source_coordinates=8,
           intersection_operations=4, midpoint_operations=4,
           allocation_operations=6;

Four actual limit carriers forecast112B each; measure before FIRST, no unused DFS fields. All14 effective ceilings validated BEFORE output/provider/env/graph, not raised by lowerings. Reduced fixed output unexpected before report retention or compiler call. Definition31 is FIXED ceiling, not an invented caller cap. Static exact count/manifests below distinguish extra guard work from the copied251 projection; never assume new guards fit original spare5.

Work fields `phase_calls` charged BEFORE each actual original compiler invocation; `phase` actual old counters; `projection_guards`; `definition_guards`; five new scalar-stage fields; `pressure_candidates`; `disk_edges`. Masks indicate reached/evaluated after charge. Fixed tuple/domain guard records are separately counted, not claimed scalar instructions. Per-edge old helper work/frames charged in live ledger. Actual strict O3/counters freeze before FIRST.

Pressure candidate order is STAGED: after both clip axes certify nonempty, charge candidate0 BEFORE first port midpoint ADD; compute both port axes. After supported port COP, charge candidate1 BEFORE first star w*Pport operation; compute star axes. Cap1 can retain actual port candidate but cannot claim computed star or equilibrium. No eager double charge, no omitted candidate count. Raw allocation uses the identical order. Ordinary disk findings do NOT stop remaining edges; only unsupported/capacity does.

## Original projection protocol251, independently capped256

Implement the same finite record logic additively in the new controller, without changing/extracting old469 function. Change ONLY two share comparisons to1/16 (requestheld and actualcellshare).

53 identity records, charged before read/check: providervalid1; seven complete/arithmetic/link/target/sector/derivative/timing flags7; intervalfirst/last2; heldroot coordinate expressions3; two feetheldsole expression coordinates6, yawzero2, humpzero2; heldrootyaw/torso/share/T4; original15 part IDs15; perboot BOX/center/fullhalf/frame/sourceplane5 each10; actualcellsharepoint1. Total53.

Then11 carriers COM, two boots, two ankles and six sole-frame columns;18 scalar endpoint records each (value/first/second ×XYZ ×lower/upper) =198. Charge BEFORE each scalar access/domain/zero-containing derivative check. Genuine equal held controls and zero humps establish constant semantics; outward zero-containing width is not interpreted as real motion. Total251, no new rectangle/denominator/equilibrium work hidden there. Carriercomplete only after all18. Unsupported/rejected cell cannot provide default COM/othercarrier data.

The original15-part catalog is built ONCE after graph in the new projection stage, after the first of its15 part-ID projection charges (following the same provider+flag/control prefix); that first charge then checks ID0, with the remaining14 charged separately, not before projection0. Charge full original conversion helper1920 catalogs and returned960 catalog including pending storage; no old graph/compiler frame overlaps conversion.

## Additional definition manifest31, order fixed

Each record charged BEFORE the named tuple/access/check. Tuple finite scans are fixed-size and explicit; no traversal/search/graph or uncharged extrema. Masks indexed0..30. Some records inspect computed outputs after counted arithmetic; no such guard computes an uncharged second expression. There is no unknown comparator/cap policy.

D01 provider valid/same ownedsource present (before graph/borrow).
D02 exported original FP environment safe (before graph).
D03 original fixedphase4 request available; named v2 request T2 and exact declaredshare selected; construction changes ONLY share, all other immutable fields pinned/fieldwise inspected by tests. No caller request.
D04 actual compiler result accepted (before cell access); preserve oldreason on failure.

Then projection251. On failure no later definition/source/allocation work.

D05 projected COM.X supported/order/finite/abs<=8.
D06 projected COM.Z same.
D07 port bootcenter.X same.
D08 port bootcenter.Z same.
D09 star bootcenter.X same.
D10 star bootcenter.Z same.
D11 genuine PORT partition/descriptor: nonnull, provider version/admission complete, matching two keys/name/plane; no source lookup/capture.
D12 genuine STAR partition/descriptor same; keep authentic trapezoid, no rectangular assumption.

Then sole_extrema4 (Xminus/Xplus/Zminus/Zplus).
D13 all four supported computed fullsole extrema/order/domains<=16; actual halves .06/.14 and soleYaw0 already originalprojection-authenticated, no sampledcorner reconstruction.

Then source_coordinates8 (vertex0X,0Z,1X,1Z,2X,2Z,3X,3Z). Each charge BEFORE reading respective actual perimeter coordinate and updating BOTH min and max endpoint folds. This counter counts eight coordinate records, not pretending to count one of the bounded comparison instructions; explicit folds each update lower/upper min/max. Publish completed source extrema only after all8. Coordinate finite/abs<=8 checks occur at each charged record.
D14 all four completed source extrema supported/order/domain8.
D15 PORT axis-aligned rectangle in this same frame: A.X=D.X, B.X=C.X, A.Z=B.Z, C.Z=D.Z, each exact supported singleton equality on copied authentic coordinates. Horizontal Y already genuinequad/sourceplane proof; no rawauthoring/tolerance.
D16 strict ordered rectangle: B.X<A.X and A.Z<C.Z. Raw nonrect extrema fixtures do not claim D15/D16/source authority.
D17 actual exact pointw1/16, strictly positive and greater than unchanged .010 load margin.
D18 actual exact pointstar15/16, strictly positive/greater than .010, denominator identical to this star coefficient (dyadic complement expression), no variable reciprocal/nearzero.

Intersection operations: Xlower=max thenXupper=min; D19 supported STRICT nonempty X (`lower.upper<upper.lower`). Zlower=max thenZupper=min; D20 strict nonempty Z. The four counts cover ONLY min/max, not these two guards. Failure is this recipe's empty/uncertain intersection, not exhaustion of all pressure choices.

Charge portcandidate0. Xmid ADD thenmultiply.5; D21 supported/domain32 complete portCOP.X. Zmid ADD thenmultiply.5; D22 sameportCOP.Z. Four midpoint operations, no reassociation/empirical coordinate.

Charge starcandidate1. X w*sameportCOP expression thenCOM SUBTRACT; D23 supported ordered finite residual<=32. Divide by SAME exact pointstar15/16; D24 supported/starCOP.X domain32. Z multiply/SUBTRACT; D25 residual; DIVIDE; D26 supported/starCOP.Z. Six allocation operations. No extra reciprocal operation unless explicitly charged under this DIVIDE primitive's bounded two-endpoint implementation; fixed positive pointdivision down(a.lower/d),up(a.upper/d) has no hidden widening root/array.

D27 exact force-coefficient policy pointw+pointstar=1 from the registered dyadic constants, not an inferred floating moment equality. Nonnegative fixed vertical forces only.
D28 symbolic X/Z moment recipe issued: BOTHmidpoint axis masks4 and allocation masks6 complete, same named Pport functional expression referenced by star solve, same denominator as coefficient. This is a typed algebraic identity, no overlap/equality test between independent rounded reports; no second allocation call.
D29 both pressure points assigned their genuine sourceplanes via targetsole+.05−fullhalf.05 identities; supported finite original disk operands available. Source/sole edges themselves remain the unchanged proof.

Original predicate at EVERY edge is signed_side.lower>0 AND squared_margin_gap.lower>=0 for containment; strict refutation when signed_side.upper<=0 OR squared_margin_gap.upper<0; otherwise unresolved. Do not silently change squared-gap equality or signed-side strictness. Evaluate16 edges exactly: portsole0..3, portsource0..3, starsole0..3, starsource0..3. Charge before each helper call. Earliest ordinary disk finding retained; continue allremaining. Unsupported/capstop halts nextoperation honestly.
D30 all16 evaluated supported and no terminal stop (ordinary disk finding may remain).
D31 both site disks contained, plane/equilibrium identities complete: selected nominal support success iff true. If ordinary finding exists, keep it rather than overwrite firstrefusal with a vague finaldefinition refusal. Strict negative vs unresolved stays olddiskstatus-specific. Record31 is reached on completed default positive or negative roster; no false support after a partial cap.

## Shared scalar order and domains

All evidence uses supported intervals and both endpoints. sole extrema=a±originalhalf; min/max lower&upper independently. For each axis Pport=.5*(max(fullsolemin,fullsourcemin)+min(fullsolemax,fullsourcemax)); orderADD→multiply. Star allocation multiplyw→COMSUBTRACT→DIVIDE bypoint15/16. Multiplication and tiny direct outward ADD/SUBTRACT/DIVIDE under ORIGINAL exported environment; no newFPpolicy/roots. Inputs/source/COM/centers abs<=8; derived sole extrema<=16; all computed primitives abs<=32. No tolerance/clamp/inverted/default supported fallback. Any old edge-kernel workspace restriction remains unchanged and refuses honestly if a synthetic computed COP exceeds it; numeric allocation completion alone is not disk support.

## Raw fixture API (ONE allocation seam, one call per slot)

`BoardingIntermediatePauseSupport02AllocationMathInput`: `com_xz[2]`, `port_boot_center_xz[2]`, `port_source_vertices_xz[4][2]`, all supported Scalars. Fullhalves(.06,.14) and shares1/16/15/16 fixed inside. No starcenter, provider, request, cell, positiveflag, share or denominator input. Numeric allocation may use deliberately nonrect vertex sets to exercise complete extrema; it does NOT claim production's D15/D16 rectangle authority. Production requires exactrectangle BEFORE successful pressure permission. Synthetic computed ranges/masks/refusal remain arithmetic-only.

`BoardingIntermediatePauseSupport02AllocationMathLimits`: five scalar caps4/8/4/4/6 plus pressure_candidates2. `intermediate_pause_support02_allocation_math(input,limits={}) ->AllocationMath`, returns sole/source/clip intervals, COPs, counters/masks/condition/state, allsource/body/contact/load/support/WORLD/actor permissions staticfalse. Shared genuine scalar kernel, no calls to source/graph/disk admission.

Raw Allocation fixed validation records are EXACT18, distinct from production source authority: COM.X/COM.Z/portcenter.X/portcenter.Z (4, before derived sole work); completedsoleextrema (1); completedsourceextrema (1); immutablew andstar/denominator (2); clipnonempty X/Z (2); portCOP X/Z (2); starresidual X/Z plusstarCOP X/Z (4); symbolicforce+moment/mask recipe (2). Charge each before check, retained numeric validation_evaluated[18] mask. Source-coordinate input checks stay in their eight actual coordinate records, not an uncounted second traversal. No productionprovider/rectangle/plane guard is claimed by this raw18.

Exactly24 arithmetic slots are ALL allocation-only literalinputs, each exactly one call to the same allocation seam. Root-selected roster: sixteen original planned allocation records; two closed +/-8 domain records; five zero-new-stage records; one overmax rawcap record. No disk seam declaration/call, no old disk rerun/auxiliary query, no mutable divisor. Preserve old469 raw128 boundary coverage; new FIRST actual16edges and genuine candidate/edge-cap controls exercise the real integration. Tests must freeze exact literal payloads/aggregate bounds before registration. Two FIRST-only LD bodies/allocations use actual fresh v2request, staged after kernel/oldcell returns; no oracle in controls.

## Resources and registration gate

Four112B limits forecast; new compact Expected TARGET<=3072 (unknown), two slots6144; wholeoldgraph32768 +sourcearena4096 +new request680 (explicit beyond original reserve) +limits448 +oldphaseLimits72 +oldrefusal64 +caller/controller2048 = **46320** conservative sourceforecast. If actual newExpected exceeds3072 or completecaller/compiler/helper/oldcatalog/error/return stage exceeds49152, stage smaller or hold; no cap increase. Newoutputceiling4096, full creator8192 including one4096arena, compound49152, stream16384.

Projection/source/pressure/catalog AFTERgraph stages separately charge original1920 bodycatalog temporary/pending+returned960, fullcurrentCell, actualnewreport/pending, newrequest680, sourcearena4096, all original kernel .su helpers and 512 error/allocator/scalar allowance. No hidden oldData copy, secondarena, old1312candidate10-table load helper or general engine. Strict O3 production fPIC GCC+pinned20 actualsizeof/source+caller+all .su chains BEFORE FIRST; source/operation/fixture outcome UNKNOWN until Root freezes exactimplementation.

Consumer48 exactly: FIRST2; allexact2; newfive isolatedexact5; newfivezero5; reachedone-less5; newfiveovermax5; inheritedoldninezero9; unsafe6; genuine lifetime4; ownoutputzero/less/overmax3; mixedprecedence2. Conditional unreachable/duplicate lowerings skipwithoutsubstitution. Exactly one creator,<=48freshcellcalls/graphs,96legs48body144sectors288timing,<=12288projection,<=1488fixeddefinitions, scalarceilings48*(4,8,4,4,6),96pressureobligations768edgecalls,24rawcalls and2FIRSToracles/compiler. A44output0 duplicates inheritedA33output0 because there is ONE outputcap; explicitly SKIP_DUPLICATE_A33 with no substitute, indexed48/max48 remains. Raw contribution gets its OWN separately frozen totals, not hidden in consumer bound. All new actual input/operation/guard/fixture literals, static source pins, independent method/resource reviews and caps registered BEFOREcode/query. 471 must merge first.


# Static support02 exact test-method proposal (source-only)

No code, tracked edits, new creator/source/candidate/graph/pressure/math/oracle/test query, capture or build has occurred for this proposal. Implement only after471 merges, a new issue/public registration selects the exact API/counter order and independent review passes. The frozen hypothesis is portshare1/16, star15/16, unchanged source/body/T2 and constructive source-clip midpoint; its actual numerical outcome is UNKNOWN. This is neither a pressure/share family nor a source/pose repair.

## Authority and minimal seams

ONE existing genuine PUBLIC pause-source creator before FIRST; no repeated constructor/second arena or invented distinct-owned native binding. New v2 assessor owns ONE fresh whole[0,1] unchanged phase01 graph with request identical to Step02phase4 except both portreaction endpoints1/16. Allgeometry/time/soleplanes/humps/root/sole/torso yaws remain fieldwise exact. Never feed changed controls through469's .25 projection token or accept caller successful cells/reports. Typed v2 source/body projection authorizes only this selected candidate. Old469/471 assertions remain untouched.

Production authenticates exact flat sole frame, original(.06,.05,.14) boot and axis-aligned PORT source rectangle. Star source has no rectangle premise. Port COP is midpoint of the componentwise full-unshrunk-sole/source intersection; star COP=(COM−(1/16)Pport)/(15/16). Denominator hardcoded positive15/16; full original disk radius.020+edge margin.010 tests all16 genuine finite edges. Intersection/equilibrium alone is not support. Ordinary edge finding survives all remaining finite edges; late terminalcapacity is separately recorded and never becomes complete support.

ONE RAW allocation seam for ALL M01–M24, exactly one call/slot: COMxz[2], PORTbootcenterxz[2], PORTsourceverticesxz[4][2]. Fullsole halfX.06/halfZ.14 and1/16,15/16,.5 fixed internally. No fake unused starcenter, callerweight/denominator, plane/body/source token. Shared scalar bounds/extrema/intersection/midpoint/allocation kernel. Production rectangle identity authenticated OUTSIDE raw arithmetic; synthetic four-point extents (including M09 trapezoid) are numeric only and never claim rectangle/source geometry permission. Rawlimits have five scalarcaps4/8/4/4/6 PLUS pressure_candidates2. Exact staged candidate0before portmidpoint, candidate1beforestarallocation is shared withproduction. Rawvalidation18 distinctfromproductiondefinition31, allrawauthorityfalse.

There is NO new raw disk seam, standalone oldDisk call, four-edge auxiliary fixture or hidden allocation→disk chain. Preserve unchanged469raw128 disk boundary coverage; newgenuineFIRST16actualedges plus genuinecandidate/edgezero/exact/one-less slots verifyintegration. Existing oldfixture tests are part of normal fullCTest, not extra new48/24 hypothesis calls.
No separate division fixture invocation. A malformed/nearzero caller denominator is unreachable because the seam and genuine controls fix15/16. That guard is unclaimed as injectable; M03/M04/M05 independently corroborate signed divide by15/16 and exact1/16 moment identity. Do not add a mutable denominator merely to create a test.

## Exact Consumer48 slots

Define FIVE new fields, frozen order/defaults: sole_extrema4,source_coordinates8,intersection_operations4,midpoint_operations4,allocation_operations6. The nine inherited logical fields are originalphase graphs1/legs2/body1/sectors3/timing6,projection_guards256 with exact additive copied protocol251; extra definition31 separately charged,pressure_candidates2,disk_edges16,output4096. NewprivateLimits has only these14 effective fields, forecast112 actualbytes. No hidden depth/node/request ranges. Newdefinition31 is a fixedguard ceiling, not an additional cap; rawvalidation18 separatelycounted. The entire v2request680 is explicitlycharged. No guards silently folded into256projection.

A01 FIRST publicforward; A02 FIRST publicreverse. Each prints success/refusal precisely, allactualwork/masks/site status, first ordinary finding and terminal stop, before numerical expectations. Then at most its ONE genuine constant-pose LD oracle if actual kinematic+constant+projection complete. One fullExpected at a time; compact summary/hash only afterwards.
A03/A04 reached measured ALLcaps exactforward/reverse, including logical requiredoutput even when support fails.
A05–A09 each new field alone measured exactforward; zero/unreached follows genuine prior refusal, no presumed completion.
A10–A14 each new field alone0; exactcapacity only if honestly reached; stop before next operation and retain actualprefix/masks.
A15–A19 each REACHED new field consumed−1; skip0 and duplicate1→0, no replacement.
A20–A24 each new field default ceiling+1, invalid preflight before graph/source/math.
A25–A33 each of nine inherited fields alone0 in order phasegraphs/legs/body/sectors/timing/projection/candidates/edges/output. Preserve actual original leg sector/timing-before-body precedence. Candidate0/edge0 retains only genuine prior completed stages. Output0 refuses before any graph. No old.25 consumer wrapper is invoked by these v2 slots.
A34–A39 six unsafeFP modes FE_UPWARD/FE_DOWNWARD/FE_TOWARDZERO/MXCSRFTZ/DAZ/nonRN. Unavailable modes skipwithoutreplacement; definition preflight may charge, compiler/pressure/edgework0. Restore mode before nextslot; exact observed totals later must guard actualmodeavailability.
A40 moved-fromprovider honest invalidbinding; A41 copy retained after moving original; A42 copy outlives originalcaller; A43 genuine cachedsource alias copy outlives moved callerhandle. No extra creator/candidate and no unavailable distinct-owner fiction.
A44 output0; A45 fixed actualExpectedrequired−1 ifnotduplicate0; A46 outputceiling+1. These DUPLICATE A33 output0 intentionally? See exact correction below.
A47 sole_extrema0+source_coordinates0; A48 intersection_operations0+allocation_operations0, genuine source-order precedence only if reached.

### Exact selected duplicate handling BEFORE registration

A33 inherited/output0 and A44 output0 are identical because this v2 assessor has ONE output budget (unlike471's childoutput and outeroutput). They do not define two independent caps. Selected: A33 output0 is canonical; A44 is predeclared SKIPPED_DUPLICATE_A33, no substitute/call. This yields <=47 actual calls even when other fields fullyreached. Ceiling48 remains exact indexed48manifest; other measured−1 duplicates skip asdeclared. A45/A46 retain exactone-less/overmax output. No second fictional outputcap/API exists.

Allconsumer caps/slots fixed beforecode; no fullreport archive or scalar-query retry. Max48API/compiler calls,96legs,48body,144sectors,288timing,12288projection,1488fixeddefinitions,96pressurecandidate,768edge; new aggregate maxima192sole_extrema/384sourcecoordinates/192intersection/192midpoint/288allocation. Count actual compiler invocation before compiler (not inferred from graphs). Every failed output/preflight slot still counts the APIcall; skipped duplicate slots do not. No extra actualcandidate or post-FIRST oracle.

## Scalar notation, domain and literal Allocation24 records

P(x) = supported exactbinary64 interval[x,x]. I(a,b)=supported[a,b]. V(x,z) means coordinate pair[P(x),P(z)]. No ordinateY or sourceplane input. False(0) means[0,0]supportedfalse. NaN=quiet_NaN<double>(), Inf=positiveinfinity<double>(), N8=nextafter(8.,Inf), exactlyone nextafter expression (not search). RawCOM/boot/source coordinate domains closedabs<=8, orderedfinite/supported. Derivedsolebound<=16; all computedinterval domains<=32. Ordinary literals.06/.14 are unchanged stored original dimensions, widened independently for LD; dyadic source bounds below are exact.

Source vertices clockwise in X/Z:0=(upperX,lowerZ),1=(lowerX,lowerZ),2=(lowerX,upperZ),3=(upperX,upperZ). The table FULLYspecifies bothCOMcoords,bothbootcoords,ALLfourvertices andSIX rawstagecaps [sole4,source8,intersection4,midpoint4,allocation6,candidates2]. Rows never acquire real rectangle authority.

| Slot | COMxz | PORTbootxz | PORTsourceverticesxz[4] | caps | expected arithmetic property |
|---|---|---|---|---|---|
| M01 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,8,4,4,6,2] | fullsoleinside source; portmidpoint0,star0; complete allocation only |
| M02 | V(0,0) | V(0,0) | [V(.03125,-.0625),V(-.03125,-.0625),V(-.03125,.0625),V(.03125,.0625)] | [4,8,4,4,6,2] | sourceinside fullsole; source-derived L/U,midpoint0 |
| M03 | V(.25,.5) | V(0,0) | [V(.03125,0),V(0,0),V(0,.0625),V(.03125,.0625)] | [4,8,4,4,6,2] | midpoint(.015625,.03125);star exactnamed moment solve/positive15/16 divisor |
| M04 | V(2,-2) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,8,4,4,6,2] | signed starCOP=(32/15,-32/15),domain32; no disk/support inference |
| M05 | V(-.25,-.5) | V(0,0) | [V(0,-.0625),V(-.03125,-.0625),V(-.03125,0),V(0,0)] | [4,8,4,4,6,2] | negative midpoint(-.015625,-.03125),signed star solve |
| M06 | V(0,0) | V(0,0) | [V(2,-1),V(1,-1),V(1,1),V(2,1)] | [4,8,4,4,6,2] | X intersection genuinelyempty; refuses recipe,midpoint/allocation NOTRUN |
| M07 | V(0,0) | V(0,0) | [V(.5,-1),V(.06,-1),V(.06,1),V(.5,1)] | [4,8,4,4,6,2] | exactsole/sourceXtouch at stored.06; strictnonempty guard refuses,no tolerance |
| M08 | V(0,0) | V(0,0) | [(P(1),P(-1)),(I(-.125,.125),P(-1)),(I(-.125,.125),P(1)),(P(1),P(1))] | [4,8,4,4,6,2] | Xintersection widthstraddles; not certified nonempty,no pressure fallback |
| M09 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,2)] | [4,8,4,4,6,2] | LASTcoordinate vertex3Z defines sourceMax2; rawtrapezoid extents numeric only;clip still fullsole |
| M10 | (P(NaN),P(0)) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,8,4,4,6,2] | malformedCOMx,no complete allocation |
| M11 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),(P(1),P(Inf))] | [4,8,4,4,6,2] | nonfinite sourcevertex3Z,no complete extrema/COP |
| M12 | (P(0),I(.5,-.5)) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,8,4,4,6,2] | invertedCOMz,no defaultoperand |
| M13 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),(P(N8),P(1))] | [4,8,4,4,6,2] | sourcevertex3Xjustoutside8,refuse |
| M14 | V(0,0) | (False(0),P(0)) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,8,4,4,6,2] | unsupportedbootx never becomes fullsole bound |
| M15 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,8,4,4,5,2] | lastallocation divide NOTRUN after genuinely charged5,completefirstaxis only |
| M16 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,2)] | [4,7,4,4,6,2] | sourcevertex3Zunvisited; exact7coordinateprefix/no supportedcomplete source max |
| M17 | V(8,8) | V(8,8) | [V(8,7.875),V(7.875,7.875),V(7.875,8),V(8,8)] | [4,8,4,4,6,2] | inclusive+8 inputs; derivedsoleextrema and balancedstarCOP may exceed8 yet remain supported computed-domain32; no clamp/support |
| M18 | V(-8,-8) | V(-8,-8) | [V(-7.875,-8),V(-8,-8),V(-8,-7.875),V(-7.875,-7.875)] | [4,8,4,4,6,2] | inclusive-8 inputs; signed derivedextrema/COP below-8 insidecomputed32, no clamp |
| M19 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [0,8,4,4,6,2] | sole_extrema0 reached beforefirstADD/SUB; no sole/source/pressurecompletion |
| M20 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,0,4,4,6,2] | all4soleextrema retained; source_coordinates0 beforefirstcoordinate,no completefold |
| M21 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,8,0,4,6,2] | complete sourceextrema; intersection0 beforefirstmax/no clip/COP |
| M22 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,8,4,0,6,2] | bothstrictnonemptyclips retained; portcandidate0 charged1; midpoint0 beforefirstADD,no completedCOP |
| M23 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,8,4,4,0,2] | bothportmidpoint axes complete; bothcandidateobligations charged2; starallocation0 beforefirstmultiply |
| M24 | V(0,0) | V(0,0) | [V(1,-1),V(-1,-1),V(-1,1),V(1,1)] | [4,8,4,4,7,2] | allocationcap overfrozen6, invalidlimits preflight; ALLscalar/candidate/validationwork0 |

M15's exactprefix assumes axis-major star solve Xmul/sub/div then Zmul/sub/div; declare it in finalschema. M16 assumes coordinate order vertex0X,Z throughvertex3X,Z; finalsource order must match registration. Genuine production sole extrema order axisXlower/upper then Zlower/upper; source extrema consume all8 before rectangleauth/nonempty. Twointersection operations peraxis plus separatelynamed strictguard. Invalid finiteflags are attempted evaluations, not successful operands; never demand finite default outputs merely because evaluation began. Independent LD fixture arithmetic enclosures do not change producer strict guards.

## Raw operation totals and disk integration boundary

Exactly24 allocationseam calls/compiler, one directcall perliteralrecord. No new disk-fixture call/API or secondarysource/refinementmath operation. Max96soleextrema/192sourcecoordinates/96intersection/96midpoint/144allocation/48candidate obligations/432rawvalidation records. Include malformed/cap/raised-limit slots and honestattempted masks. Scalar calculations for independentfixtureexpectations are ordinary localtest arithmetic, not additionalproducer/seam queries. Stage oneInput/result at a time outgraph; no24fullfixturearchive.

Unchanged469raw128 remains mandatory existing coverage for radius/margin/tangent/sign/nonfinite/inverted/edgecaps. The NEW genuine path's twoFIRST16edge arrays and A25–33 candidate/edge/output caps, A03–04 exactall budgets verify actual v2integration at authenticsource/currentbody. Do not rerunoldprimitive fixtures as hiddennew24calls or turn numericallocation complete into disk/source/supportpermission.

## Exactly TWO FIRST-only independent actualpose oracles

Only A01/A02, after graph/pressure producer returns and kinematic+constant+projection complete: independent unfactored3D two-sphere knees/all18points/15masspoints, original offsets/rod lengths/soleflatframes/fullhalfX.06/Z.14. Reaction1/16 affects only force/moment model, not bodygeometry. Borrow genuine alreadyowned sourcecornerrecords; widen exact decodedbinary64 (not decimalrawauthoring). Compute independent port Ls/Us/Lq/Uq andstrictnonempty intersection, portmidpoint, star signed moment solve at fixed15/16; corroborate ONLY retained/evaluated fields andall16 actualedge predicates whenavailable.

If intersection/pressure/edgesfail or stop, never fallthrough to defaultCOP/geometry. Oracle may corroborate actual completeprojection evenwhen laterrecipe fails, but gateeachlaterfield on its evaluated/successmask. Max2bodyposes,36points/30masspoints,32edgecorroborations; no controlpose or validation-grid samples. Sample/LD results never confer load/continuous/source permission or repair strict predicates. No owningExpected querywrapper; actualgraph frames are inactive beforeLD. PendingOracle/bodyreturn is separatelycharged.

## Resource forecast and publicacceptance scope

Reserve WHOLEoriginal32768 graph allowance, TWO owning/pending newExpected slots<=3072each6144, ONEexisting4096arena, NEWimmutablev2request680, FOURprivateLimits112each448, oldPhaseLimits72/refusal64 andnewcontroller/caller2048 =>46320<=49152. No oldgraph slack subtraction or hiddenold.25report. Projection parts/catalogs and pressurediskhelpers are noinline AFTERgraph. Actual source/lifetime/arena/error/allocator/pending return andbothstrictO3 .su mustfreeze before FIRST, not inferred from timing or successful execution.

Main namedmetadata forecast: TWO scalarFirstBaselines<=512total; streams176; totals<=160; existingnative/bootExpected80; optionalprovider24; savedptrs16/bytes8;modeavailability8 =>984before smallargs/errorreserve (charge<=2048controller/caller honestly, not promised1024ifinsufficient). Keep futureFixture/LD storage outmain. Firstcreatorhelper target<=128; forecast machine main<=1024+helper128+publicwrapper224+make1152+init16+arena4096+leaf512=7152GCC (Clangsimilarlower); ENTIRE8192mustmeasure. Arena remainsone throughrawfixtures; no creatorreplays.

FIRST LDsource phase forecast twoOracle1728each +two newExpected<=6144 +4096arena +caller2048 +LDutility8192 +scalar/error1024 =24960<=49152, inactivegraph not added. Prepared projection/quad arrays may addbounded512 andmustmeasure. Raw24allocation roster oneInput/MathResult+pending records/limits/helpers/arena/caller separatelymeasured; no16/24fullfixturearchive. Shared complete stdout+stderr<=16384; printSOURCEFIRST, TWOFULLrequest findings/masks/work andcompactvalidation summary, not48reports/24geometrypackets.

Apositive would qualify THIS source-backed nonzero static2s pause/constructiveallocation at1/16 only. It does not turn old.25pause/transfer into success, qualify moving acquisition, self/material/WORLD, seat/actor/save or FirstFlight. Keepparentsopen. Arefusal stops this ONEhypothesis; no fallbackshare,COPfit,source/foot/root/radius change or additional query without separate registeredwork.

## Open final-schema items (pre-registration)

1 Exactprojection copied251 capped256 is selected; additionaldefinition31/rawvalidation18 separatelynamed/counted, actualsource/machine/storagefreeze remainsmandatory.
2 Allocation24 ONLY: no rawwhole16disk/API/rerun. Exactgenuine16edgebridge/end-onlyhistoricalprefixpreservation andcallerlifetimes needbeforecode registration; no rawsynthetic token or old.25authorityreuse.
3 ONEoutputmeansA44 duplicate ofA33 isSKIPPED, not an invented secondoutputbudget.
4 Confirmallocation axismajor3operations/division andsource vertexmajor8reads forM15/M16 exactprefix meaning.
5 Rawnonrectangle M09 only tests extents/lastread and cannotassertauthenticrectangle geometry; genuine production rectangle pins mandatory.
6 Baddivisor/sourceownerinjection unavailable throughimmutablepublicAPI, explicitlyunclaimed. Preserve oldinputnegative tests instead of manufacturingauthority.


## Measured implementation gate before FIRST (2026-10-06)

The registered method and literal roster are unchanged. Final strict object-only
compiles on GCC16 and Clang20 measure Diagnostic2488, Expected2496, Limits112,
Request680, Token32, Math784, Input288 and MathLimits48 bytes. Output2496 fits
4096. No source creator, graph, pressure, fixture, test or oracle was executed
to obtain these measurements.

The source graph bound is **45,168 <=49,152**: the whole original32768 proof,
two actual expected returns4992, one existing arena4096, request680, four limits448,
phase limits72, phase refusal64 and controller/caller/error reserve2048. Optional
request control and token metadata are charged in that reserve; factory copies
are staged before the graph. Separate complete source bounds are request
factory16,088, postgraph catalog/pressure27,984, raw16,640 and oracle25,632 bytes.
The complete old boot prefix108954 bytes remains exact; the new tail is6881 bytes.

Measured full graph machine chains are38,000 /35,320 bytes on GCC/Clang, including
explicit pending return and arena. The entire unchanged public source-creator
chain is7,008 /6,696 bytes, including the4096-byte arena and512-byte allocator/error
allowance, below8192. No graph reserve, caller slack or return elision supplies
space for new records. The earlier quarter-load results and all other original
inputs remain unchanged. The new numerical outcome is still unknown before the
frozen first execution.
