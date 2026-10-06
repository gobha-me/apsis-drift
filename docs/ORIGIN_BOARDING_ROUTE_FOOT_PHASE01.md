# Route foot phase01: source-free constrained movement component

Registered under grouped [#361](https://github.com/gobha-me/apsis-drift/issues/361), 2026-10-05, before implementation and first numerical observations. Ordinary bounded source-backed pose/path discovery remains allowed separately; this component accepts mathematical phase controls, never caller source/body permission. It does not claim a successful lower-seat route.

## API, scope and ownership

Files: include/apsis_drift/origin_boarding_route_foot_phase.hpp; src/origin_boarding_route_foot_phase.cpp; src/origin_boarding_route_foot_phase_internal.hpp. Append a purpose-specific graph/cell block to existing origin_boarding_planted_legs.cpp to reuse its bounded outward scalar/jet/root arithmetic without modifying old algorithms or extracting a generic numerical engine. CMake integration and independent tests accompany this component within the grouped route branch.

New version1 `BoardingRouteFootPhaseRequest` holds WORLD root start/end as bounded constant-term coordinates, root yaw tangent-half start/end, torso-relative-lean tangent-half start/end, two foot records (WORLD sole start/end as the same bounded constant-term coordinates, sole yaw tangent-half start/end, nonnegative swing height), duration seconds, and port-reaction start/end. Fixed pelvis lean0, folded shoulders45/elbows135, neck/wrist/twist0, suit-on/pack-detached/flat soles. No lengths, boxes, knees, ankle rolls, source planes, source flags or claimed successful diagnostic enter.

Public `assess_origin_boarding_route_foot_phase(request, first=0,last=1)` returns expected<BoardingRouteFootPhaseDiagnostic,string>. Request is an explicit numerical/source-free curve definition, not a privately admitted ship route. It owns a copy of its validated controls, version, physical duration, requested endpoints/reverse, compact accepted cells, actual cover/graph work, first refusal and separate arithmetic/nominal-link/sector/derivative/timing/complete flags. Every self/source/material/world/support/load/actor/route/seat/save permission is static false. Reaction fractions are exact algebraic cues for future contact ownership; they do NOT prove unloading or finite pressure support.

Every cell owns all18 original point value/first/second enclosures, fifteen fixed part/mass bindings, full1200 mass/COM expression, actual varying root/trunk/sole frame enclosures, two leg closure/sector/angle-jet summaries, root/whole-sole/angular speed bounds and share bounds. Do not synthesize old timing leaves or own old static Body3/Self/Load children. Rounded policy3 reporting poses may later corroborate diagnostic reconstruction; they never define this graph's knees, target soles or permission.

## Exact controls and domains

Every finite literal/control is its stored binary64 value lifted into an exact-real expression. WORLD coordinate constants use a purpose-specific `BoardingRoutePhaseConstant` with1..3 active terms and inactive entries exactly0. Each term is finite abs<=8; the exact sum must be supported and inside[-8,8]. A vector is three such constants. This admits exact existing endpoint/source sums without an arbitrary expression tree: #447 end root uses X terms{.16,.07}, Y{.847,.012}, Z{-.55,-.04}; existing sole X uses{.16,+/-.14}, Y the original stored plane, Z the original stored-.5/-.8. Do not replace these by stored .23/.859/-.59/.02/.30 when composing existing evidence. The old graph's+.847 is included ONCE in these WORLD root terms, never as a second global translation. Numerical single-double convenience constants have one active term only; endpoint ties compare their exact defining terms/algebra, not rounded reports. Finite request first/last in[0,1], full/reverse/sub/point semantics. Every WORLD root/sole endpoint component abs<=8; whole-cell values must stay in that workspace. Root and each independent sole yaw carriers k in[-1,1], representing yaw in[-90,90]. This is an explicit restricted route phase domain, not a reduction of policy3's general[-180,180] static yaw. Torso carrier in[-1,1] additionally requires actual directed torso angle within original±35 throughout. Duration finite[1,120] seconds; each swing height finite[0,.25]m; reaction endpoints in[0,1]. No clamp, root/foot refit, epsilon, norm normalization, variable duration after observation or unsupported extra freedom.

The FIRST movement branch is upright pelvis ONLY. TorsoRelativeLean is allowed. Hip axial equals independent sole yaw minus root yaw and must remain within retained±45. Hip abduction must satisfy parent±35 and derived ankle roll±15; hip flex[-20,120], knee[0,135], ankle pitch±30 remain unchanged. Sagittal nonzero pelvis lean, pan contact and general route phase composition follow in the same grouped work; this component does not silently register their coupled frames.

Invalid/nonfinite request/control/dimension-like bounds are API errors before work; unsupported FP environment or geometric/derivative domains retain a refused diagnostic. Validate ORIGINAL controls before converting/overwriting them. IEC559 binary64 nearest rounding, actual tie/subnormal probes, finite supported products/denominators and bounded small-square-root proofs follow existing kernels. No production wider scalar.

## Phase and exact target graph

For u∈[0,1], S=u³(10-15u+6u²), S'=30u²(1-u)², S''=60u(1-u)(1-2u). Use complete monotone S endpoint value bounds, global S'≤15/8 and S''∈[-6,6] with registered critical proofs. At exact u0/1 first/second are0.

Every scalar endpoint carrier q=q0+(q1-q0)S. Root P uses this componentwise. Each sole C_i interpolates its WORLD endpoints and adds ONLY to Y the explicit swing term h_i W, W=64u³(1-u)³. W'=192u²(1-u)²(1-2u), W''=384u(1-u)(1-5u+5u²). W is0 at endpoints,1 at u=.5, monotone on either side; bound values with actual endpoints and included midpoint, derivatives by complete outward polynomial enclosures. W' and W'' vanish at endpoints. A stationary foot has IDENTICAL endpoint vectors/carriers and h0; retain that structural identity, not cancellation of independently rounded positions. Share w=w0+(w1-w0)S, other share exactly1-w; no force theorem inferred.

Yaw R_y(k) has c=(1-k²)/(1+k²), s=2k/(1+k²), columns(c,0,-s),(0,1,0),(s,0,c). Exact-real orthogonality follows from this compiled rational family; rounded report columns are not asserted exact. Denominator≥1. Angle rate=2k'/(1+k²), second=2k''/(1+k²)-4k(k')²/(1+k²)². Physical first/second divide byT/T², reverse negates first only. Root/trunk/foot frames keep their actual expression jets; do not reduce world boxes to fixed-frame #447 bindings.

R0=Ry(root k), Rp=R0, Rt=R0*Rx(torso carrier). H_i=P+R0*(side*.14,0,0). A_i=C_i+(0,.10,0); boot B_i=C_i+(0,.05,0), flat frame Rb_i=Ry(sole k_i). Exact boot underside is C_i since stored .10=2*stored .05; no independent Y placement, plane snapping or rounded midpoint authority. C_i is a numerical target, not an admitted source plane.

In each foot frame d=Rb_i^-1*(A_i-H_i). Require dY<0 for this selected forward/down branch. Let rho=sqrt(dx²+dy²), D=d·d, U=(-dx,-dy,0)/rho, N=(Uy,-Ux,0), q=N×d. Retain L1=.47285,L2=.47478; alpha=(L1²-L2²+D)/(2D), gamma²=((L1+L2)²-D)*(D-(L1-L2)²)/(4D²); positive gamma root. K_i=H_i+Rb_i*(alpha*d+gamma*q). q·d=0 and q²=D give exact nominal links; these are compiled identities contingent on the authentic domains, not residual-size acceptance. Proper frames come from the analytic rational carriers, not arbitrary caller matrices.

Strict reach guards require D between squared difference/sum. Derivative graph requires rho²,D,gamma² strictly positive and supported root denominators. Exact straight/collapsed singularity refuses derivative/timing completeness; do not invent finite gamma derivatives from endpoint samples. An additional independently selected singular factorization could follow within this component/branch if whole-route discovery genuinely needs it. This first useful graph covers bent-foot load shift/pivot/swing phases like the accepted mixed stance; it does not claim every static policy3 pose can be moved by this family.

## Original limits, directed sectors and angular rates

Reuse F1=alpha*rho+gamma*dz, G1=gamma*rho-alpha*dz; F2=(1-alpha)*rho-gamma*dz, G2=-((1-alpha)*dz+gamma*rho). Pitch theta1/theta2 directions are(F1,G1)/(F2,G2), not rounded atan2 inputs as authority. Positive branch fixes knee direction; knee cosine=(D-L1²-L2²)/(2L1L2).

Hip[-20,120] is the SAME parent body limit. Use oriented halfplanes G1*cos20+F1*sin20≥0 and F1*sin120-G1*cos120≥0, with supported independently bracketed constants. Width140<180 defines the actual directed cone. DO NOT retain old F1>0 or sin65 upper guard: they reject legal90..120 hip flexion. Preserve all old65-sector APIs/outputs unchanged. Knee cosine≥cos135=-sqrt2/2 plus authenticated positive branch; shin F2>0 and .5F2-(sqrt3/2)|G2|≥0 give ankle pitch±30. Roll phi=atan2(dx,-dy); enforce |dx|≤(-dy)*(2-sqrt3) for15°, derive hip abduction=side*phi and ankle roll=-phi.

Axial delta=soleYaw-rootYaw. With both yaw domains[-90,90], their difference lies[-180,180]; exact cosine relative yaw≥sqrt(1/2) uniquely selects|delta|≤45. Use the rational frame dot identity with bracketed sqrt; no rounded angle wrap or epsilon. Torso angle±35 uses the corresponding directed frame sector, not k limits alone.

Pitch rate=(F*G'-G*F')/(F²+G²), second with the actual derivative quotient; link identity permits denominator L² only after authenticated graph identity. Phi derivatives use(-dy,dx)/rho². Axial derivative=soleYaw'-rootYaw'. Do not read old frozen-ankle derivative zeros after allowing target movement.

Relative hip angular velocity for Ry(delta)*Rz(phi)*Rx(theta1) has norm²=deltaDot²+phiDot²+theta1Dot²+2*deltaDot*theta1Dot*sin(phi). The cross term is mandatory; scalar components are not orthogonal. Knee speed=|theta1Dot-theta2Dot|. Ankle Rx(-theta2)*Rz(-phi) has speed sqrt(theta2Dot²+phiDot²); no extra ankle yaw is introduced. Complete resultant bounds≤pi/6 use the existing bounded speed-threshold identity. This is angular-speed/coordinate-jet evidence, not complete angular acceleration or general dynamics.

Root speed≤.25 and root acceleration≤.10 certify from full WORLD jets. Whole finite sole speed≤.35 uses ||C'_i||+sqrt(.06²+.14²)*|soleYaw'_i|, a sufficient bound for every point in the rigid sole (not only its stationary center). Retain separate center-speed, yaw-speed and whole-sole bound. Global root yaw and torso joint angular speed also≤pi/6; no fast torso motion omitted because hips pass.

## Full body and downstream usefulness

Assemble full WORLD pelvis/trunk/helmet boxes with unchanged halves(.24,.12,.18),(.26,.2695,.18),(.16,.18,.18); their orientations are root/trunk expression frames. Trunk/helmet/eye offsets(0,.3495,0),(0,.70237,0),(0,.65237,0). Shoulders at Rt*(side*.20265,.579,0), folded upper arms Rt*(0,-.35898*c,-.35898*c), c=sqrt(1/2); forearms add Rt*(0,.386,0); hands follow Rt with original half(.04,.05,.02). Full limb capsules retain original radii/endpoints; no ellipse-as-world substitution.

Mass weights/order144,540,96 then each side120,48,12,15,10,5; exact point/midpoint expressions, sum once/divide1200. Output full COM jets and all actual varying frames. No static center or COM correction is supplied by policy3. Subsequent finite source/skin pressure and self consumers can use these SAME owned interval cells; they must authenticate the phase graph and support/source, not trust public numerical flags.

## Fixed work, output and private controls

ONE closed cover depth10,nodes2047,leaves1024; left-first bisection of unresolved complete cells only. No nested legacy planted/timing covers, per-leg DFS, retry enlargement or sample acceptance. Each node compiles both legs/all18 points/all15 mass records/frames and every selected sector/timing predicate. Record actual examined nodes and graph stage counts; cap check BEFORE work. Accepted prefix remains diagnostic, final full/sub/reverse/point completeness requires exact closed gap-free coverage.

Proposed cell≤12288B, aggregate output≤16MiB at1024cells plus fixed request/summary/refusal. New live scratch≤32768B including graph/packet/cell/helper overlaps and11 DFS frames; no 256-term inherited roots. Actual C++ sizes and deepest helper/lambda/return/control ownership must be reviewed compile-only before first query. Fixed body/request work has no source/catalog allocation. The validated constant-term request must fit2048B; its actual size/control copies are charged to the live/output ledger. Reserved vectors check real capacity against output cap; no implicit doubled capacity outside ledger.

Private bounded wrapper may LOWER depth/nodes/leaves/output and per-node graph/leg/body/sector/timing work. Numerical seams for scalar yaw, hinge direction, angular-norm and target-leg domain controls are arithmetic-only and cannot enroll source/body permission. Reject malformed bounds/unsafe environment before positive flags. One genuine request/compiled graph owns each cell, not a caller-created successful cell diagnostic.

## First controls and meaningful tests

Independent tests define permitted source-free controls before first observation, then record GCC/Clang first outcomes without presuming acceptance. Useful first controls: exact stationary mixed-height bent stance using #434 root terms{.16}, {.847}, {-.55}, source soles{.16,-.14}/{upper}/-.5 and{.16,+.14}/{lower}/-.8, yaw carriers0 and torso0, T8,h0,w.5; port-share .25→0 with stationary feet (load NOT qualified); lifted port sole with C2 hump and stationary starboard target; loaded-center pivot with yaw-changing sole and stationary center; matched common root/foot yaw (axial0) versus opposite foot yaw requiring45° axial; root/foot yaw difference beyond45; hip pitch90..120 legal sector versus old65 helper; torso motion with upright pelvis. A phase may genuinely refuse reach/timing; retain that result.

Oracles independently reconstruct unfactored sphere intersection, U/N/q nominal identities, rotated hip offsets and ankle/boot/sole terms, actual inverse rotation order, all15 shapes/masses/COM, rational yaw/trunk frames and first/second derivatives. Tests must prove moving ankle derivatives are not erased. Check directed120 cone F1<0, wrong knee branch, unsupported rho/D/gamma domains, exact zero derivatives at endpoint holds, point instantaneous jets, reverse first-only signs, subinterval originalT, full finite sole rotation speed, hip axial cross term (both signs), ankle pitch/roll inverse order, all invalid/nonfinite/workspace/carrier/hump/time/share domains, arithmetic environment/subnormal failures, zero/exact/one-less actual work/output and prefix flags.

No support, source surface, self, material or live actor result may pass from this component. Pan-only seated branch and source-supported whole route remain #361 work. The result supplies substantive reusable exact target motion/body/timing machinery while current source/pan obstructions are investigated; it is not a labels-only metadata ticket.

## Compile-only ownership clarification

Before the first numerical observation, the implementation stores the fifteen
immutable part/mass bindings once in the diagnostic. Every interval cell owns
all fifteen mass-point jets and the full COM, referring to those same bindings;
it does not duplicate the binding table. This changes storage ownership only.

Compile-only sizes are 7,248 bytes for the working graph, 7,736 for a cell,
680 for the request and 1,848 for the fixed expected result. The conservative
simultaneous live ledger is 30,776 bytes, within the registered 32 KiB cap.
Helper temporaries, bounded root scratch, DFS/control and the result/return
overlap are included. Vector capacity remains separately charged to output.

The constant and swing-value enclosures may intersect independently proven
workspace or polynomial ranges. This tightens an enclosure without changing
the exact expression. Over-domain controls refuse; there is no input repair
or contact tolerance.

## Observed results — 2026-10-06

GCC and Clang20 agree on all six frozen motions. The prior supported root
translation, a stationary reaction cue, common root/sole yaw, port sole
translation with yaw, and torso lean pass their kinematic/timing checks.
The 20mm port lift refuses at the retained joint sector on
[.1240234375,.125], after seven accepted prefix cells. None of these results
grants contact or load support. The outcome regressions pass 382,905 checks
with each compiler; body checks independently pass 3,627. Both library and
test translation units explicitly disable floating-point contraction.
