# Freedom endurance contract v 1

#133 selects numerical resource rules; #251 implements actual state and
consumption. This document is a measured initial game contract. The initial research cut used resource-free native flight. The subsequent
[resource implementation](FREEDOM_RESOURCES.md) applies this contract to new
native sessions; historical saves retain their resource-unselected behavior.
Test observers measure actual allocations without changing a control, save, pose
or velocity.

The approved Freedom resource count is **two**: one flight reserve for all
sub-light/surface propulsion, plus **three discrete jump charges**. The retained
historical zero/one/two research question is superseded by the 2026-09-17 scope.
No currency, third suit fuel pool, field harvesting, inventory or implicit
conversion between pools is introduced.

## Flight quantity and burn

Use one effort-equivalent fuel unit, derived from the existing gross positive
and negative physical force/torque channels. It is an authored energy/propulsion
budget, not propellant kilograms, a measured specific impulse or a change to
the existing 8,000 kg dry-mass flight model. This avoids inventing an unqualified
mass/inertia migration inside a resource feature.

The version 1 effective torque coupling arm is 6.5 m. It gives rotational effort
a cost comparable with translation on the present roughly 13 m long Wayfarer.
The current C++ physics models force and torque as independently allocated
actuators; this rule does not pretend to resolve shared physical nozzle plumbing.

For each **committed 120 Hz simulation tick**:

1. Read the six gross force channels (positive XYZ, negative XYZ) and six gross
   torque channels after actual authority/assistance allocation.
2. Reject non-finite, negative or out-of-domain measurements. Existing craft
   actuator definitions bound a channel to at most 1,000,000,000 N or N·m.
3. Quantize each binary64 value with the explicit expression
   `floor(value * 1000.0 + 0.5)`, floating-point contraction disabled. Positive
   half-quantum ties round upward; ±zero becomes integer zero. A positive
   firing that would round to zero costs one channel quantum, so an arbitrarily
   small nonzero command cannot produce free thrust. Force is in
   milli-newtons and torque in milli-newton-metres.
4. Add the integer tick debit
   `Q = 13 * sum(force_mN) + 2 * sum(torque_mNm)`.

One equivalent N·s is **1,560,000 Q**: the denominator is 13×1,000×120. Retain
integer Q directly. Do not round an accumulated bill, convert through a display
percentage, or keep a renderer-owned floating remainder. Checked unsigned 64-bit
arithmetic refuses overflow before any live state commit. The channel products
fit below or equal 9×10¹³ Q per tick even at the definition's extreme bound.

The gross channels already include realized assistance. Requested force and
the assist algebraic delta are diagnostic values, not additional fuel bills.
Opposing firings cost fuel even when their net force/torque is zero. Gravity,
drag, lift, passive atmospheric torque and terrain/station support are external
effects and never fuel channels.

## Starter capacity and reserve

| Quantity | Exact Q | Main-thrust equivalent |
| --- | ---: | ---: |
| Capacity | 3,032,640,000,000,000 | 5,400 seconds / 90 minutes at 360 kN |
| Reserve warning | 1,010,880,000,000,000 | 1,800 seconds / 30 minutes at 360 kN |
| One full-main tick | 4,680,000,000 | 1/120second |
| One second at full main | 561,600,000,000 | 1second |

Capacity and reserve are immutable properties of this resource recipe version,
not a generated random tank size. Quantity is mutable per craft. Show reserve
at **remaining≤one third of capacity**, including equality. Capacity, initial
amount and the exact nominal main rating must qualify against the selected
Wayfarer frame/version; a future frame needs its own explicit resource recipe.
At full 360 kN main the tank lasts 90 simulated minutes; full 72 kN retro lasts 450
minutes. Simultaneously firing every rated translation and torque channel can
empty it in about 18.71 minutes. It is not 90 minutes of free maximum everything.

The reserve is an advisory turn-back/escape cue, not a guarantee for every
planet, attitude or damaged craft. Do not silently spend it differently from
ordinary fuel or gate manual movement on a mission/upgrade/economy.

## Measured starter workloads

The [checked reference measurements](research/freedom-endurance-v1.json) retain
the exact accumulated channel counters, per-phase totals and four-body matrix.
Their Q totals also agree with an independent integer sum of those channels.

The public-command seed 42 voyage uses ordinary New Game, station walking,
boarding, withdrawal, atmospheric cruise, ascent, moving-home interception,
capture and disembark. `--commands --endurance` retains the 15-tick recorded input
cadence of the already qualified uninterrupted native voyage. All 22 phase and
next-tick saves remain byte-identical to that original fixture; both compiler
meters agree exactly. Only committed flight steps are billed; discarded
checkpoint/next-tick candidates and walking/boarding are excluded.

| Workload | Committed flight ticks | Exact effort Q | Full-main equivalent |
| --- | ---: | ---: | ---: |
| Recorded station→atmosphere→home | 608,575 | 750,955,958,960,775 | 22.286 minutes |
| Explicit near-ground assisted landing→liftoff→stable orbit | 111,819 | 266,483,611,299,623 | 7.908 minutes |
| Conservative sum of those separate traces | — | 1,017,439,570,260,398 | 30.195 minutes /33.55% of tank |

The second trace lands in 332 ticks and clears the pads in 246 ticks; the same
craft then reaches space and a stable orbit using actual rated thrust. The
space checksum is 265037222577224962; final stable-orbit checksum is
10323980539098967965. The station voyage returns at tick 614535, checksum
448305920546241237. No relocation or velocity writes occur during either flight
trace. The landing fixture **starts at an explicitly initialized near-ground
site**, so adding the costs is a budget comparison, not a newly demonstrated
single station→surface landing→station session.

The recorded cruise-end→home cost plus the entire near-ground landing/ascent
cost is 806,364,148,970,358 Q. The selected one-third reserve exceeds that
conservative additive escape/return comparison by about 25.36%. The total tank
exceeds the sum of both traces by about 198%. These margins support a forgiving
initial Freedom budget without constant refueling; they do not certify manual
piloting, arbitrary terrain, damage, fuel-enabled integration or every new seed.

## Atmosphere/gravity reference matrix

Each row uses an actual generated physical catalog 2/ephemeris 2 body and the
8,000 kg Wayfarer, at 100m above its reference radius. Initial velocity matches
the existing rotating atmosphere. One actual 120 Hz step samples a translational
hover demand compensating gravity and rotating-ground acceleration. No synthetic
atmosphere/gravity is injected. All four demands fit the existing 208 kN +Y and
lateral authority. This is **one-tick demand qualification**, not a sustained
hover, safe landing or full ascent proof in each environment.

| Class / system seed / PlanetId | Gravity milli-g | Surface pressure mbar | Sample density kg/m³ | Hover-equivalent N | Constant-demand tank minutes |
| --- | ---: | ---: | ---: | ---: | ---: |
| Airless /1 /1987781385134422649 | 1,071 | 0 | 0 | 83,995.716 | 385.73 |
| Tenuous /0 /10586422272737837210 | 1,012 | 235 | 0.279415 | 79,383.070 | 408.15 |
| Temperate /0 /8354106865770247801 | 1,728 | 636 | 0.759919 | 135,366.418 | 239.35 |
| Dense /0 /17283368493640605437 | 663 | 1,699 | 2.035470 | 51,866.103 | 624.69 |

Pressure, altitude, relative airspeed, drag and flight path affect the demanded
thrust through the existing atmospheric owner; they are not another hidden
fuel multiplier. Dense low-gravity hover can cost less than thin high-gravity
hover. Airless does not mean gravity-free. The current model has no active
ambient gust or condition force. Thermal/environment capability remains #102;
dynamic heating retains its existing owner.

## Operations, time and depletion

| Operation | Rule for #251 |
| --- | --- |
| Forward/reverse/braking/surface/ascent | Debit realized gross propulsion using the same formula |
| Attitude/stabilization/port or landing aid | Debit realized torque/force, including saturated corrections |
| Neutral coast with no correction | Zero propulsion bill; gravity/aerodynamics and clock still advance |
| Landed, docked, walking, boarding, pause or offline wait | No propulsion bill, refill or wall-clock resource changes |
| Time compression/render batches | Process the same authoritative ticks and debits; change only their scheduling |
| Invalid/refused step | No physics or resource commit |

The measurement observer is not a consumable implementation. #251 must couple
the candidate physics step, Q debit, shared tick and any transition atomically.
At a positive quantity too small to afford the requested whole tick, apply
**no propulsion for that tick**, charge nothing, cancel active propulsion aids
and advance normal passive physics from the pre-step state. Retain the residual
quantity: the pilot may request a smaller affordable input. A neutral advance
must remain valid; do not freeze a falling craft or erase momentum on depletion.
Do not execute an unaffordable full-thrust tick or silently burn fuel without
force. A below-minimum deliverable remainder (less than 2 Q in this quantized
contract) is operationally empty and remains recorded exactly until service.

This whole-tick rule deliberately avoids a new partial-tick integrator or
silent throttle controller. #251 must validate it against actual assistance
allocation and ensure an unfunded correction is not reintroduced during its
passive step. Assisted play must explicitly report why an aid stops and retain
valid coast/landed/service/recovery paths; manual risk remains possible. Neither
mode invents fuel, deletes a landed craft or depends on a paid rescue.

## Jump charges and recovery

Start with exactly 3 charges. A valid in-range **committed** jump consumes exactly
one charge, regardless of distance/load. Availability must be checked before
spooling and again at atomic commit. Refused, interrupted or canceled spool
consumes none; repeated completion cannot debit twice. Zero charges refuses
the jump while preserving other state. Departure/arrival/history and charge
debit commit together. Outbound plus return leaves one reserve:3→2→1.

For this playable, no cooldown, pressure/gravity risk or fuel conversion hides
an extra charge. Beyond-rated/repeated-use/condition risk remains #193/#248 and
must not quietly redefine this in-range charge contract. Actual neighboring
native travel and its fuel-enabled acceptance remain #176/#194/#251.

Free deterministic station replenishment belongs to #246: verify the actual
craft is attached to a supported service port, then refill both pools to their
exact versioned capacities atomically. Proximity, pause/load, elapsed real time
or a speculative market quote does not refill. Surface depletion can leave
jump charges intact; no automatic surface jump or damage immunity is implied.
Standard replacement/recovery belongs to #247 and must remain available when
ordinary return is impossible. No field harvesting, tow economy or suit fuel
pool is invented by this contract.

## Optional continuous interstellar flight

There is no distance toll or passive coast drain. Under the present fixed-mass
ideal propulsion model, acceleration plus braking to 60km/s costs
`2 * 8,000 * 60,000 = 960,000,000N·s`, or 44.444 full-main minutes, leaving more
than the one-third reserve in this tank before other maneuvering. This is a
kinematic budget bound, not a demonstrated interstellar native trace.

The retained first-route 48–96 light-hour separation then requires roughly
27.36–54.72 simulated years at that speed. At the existing 65,536× ceiling the
coast would take about 3.66–7.32 wall-clock hours; this optional slow route is
resource-feasible but unattractive as the primary Freedom journey. The ordinary
jump remains the practical route. No new time scale, route, speed cap, topology
or analytic fast-forward is implemented to conceal that cost. #193/#195 own
travel qualification; no full future multi-leg career is required to size this
starter reserve.

## Persistence, presentation and validation boundary

#251 should introduce an explicit resource recipe version plus exact unsigned
Q remaining and 0–3 charges, bound to the existing craft identity/frame. Derive
capacity from the supported recipe; do not serialize a generated fuel catalog
or mutable capacity. Validate version/owner, quantity≤capacity, charge bounds,
missing/duplicate/extra fields, negative/non-integer/non-finite values, integer
overflow and tick/history contradictions before replacing live state.
Checkpoint/resume must reproduce both exact quantities and the uninterrupted
physics/debit sequence. A pending jump never owns a prepaid charge.

Historical formats 17–23 have not selected a resource recipe. Preserve their
existing semantics on Continue and identify that explicitly. Do not infer past
fuel expenditure from elapsed ticks or retrofit a partly empty tank. A current
resource-enabled New Game writes its new schema; an optional legacy transition
may occur through explicit free station service, with a validated attached
craft, rather than a hidden migration mid-flight. #251 owns the schema and
actual conversion tests; this research adds neither.

Cockpit quantity and charge count must come from C++. Display fuel percent plus
current burn and **main-thrust-equivalent endurance**, clearly distinguished
from travel range or a guaranteed escape time. With zero burn, report coasting
instead of an infinite range promise. Reserve/depleted/unaffordable-aid notices
need text and controller-accessible service/recovery actions; color is optional.
Never expose unknown hazard facts to improve an estimate; #175/#102 own those
knowledge/capability boundaries.

Reference arithmetic covers 0/half-quantum/maximum/non-finite inputs, overflow
rollback, opposing channels, non-duplicated assist, batch partition and integer
checkpoint continuation. Actual four-atmosphere samples distinguish coasting,
hover, main thrust, opposing input and stabilization. GCC/Clang produce identical
integer counters, matrix JSON and all original voyage saves. Upstream host-libm
replay limitations remain #255; an integer ledger does not cure unrelated
physics differences.

Run the opt-in measurements in fresh output locations:

```sh
build/src/godot/apsis-drift-freedom-planetary-fixture "$PWD/voyage" --commands --endurance
build/apsis-drift-landed-craft-tests --endurance "$PWD/landed-endurance.json"
build/apsis-drift-flight-endurance-research-tests --report "$PWD/endurance-matrix.json"
```

The voyage directory must already exist and be empty. Report files must be new,
with existing parent directories. Configure/build the normal C++ core and Godot
fixture targets as described in [development](DEVELOPMENT.md). Measurements
require no new assets, image study, renderer, music or fuel implementation.
