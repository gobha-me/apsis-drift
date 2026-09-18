# Freedom: open-universe design and roadmap reconciliation

Discussion record: 2026-09-17. Status: approved Freedom direction plus explicitly
unresolved future mechanics, not an implemented feature or release commitment.
The five product decisions were confirmed and the GitHub roadmap reconciled on
this date. Start with [the high-level roadmap](ROADMAP.md) and
[Freedom #243](https://github.com/gobha-me/apsis-drift/issues/243); the issue audit
below preserves the reasoning and distinguishes old conflicts from new policy.

## Direction established in the discussion

- Build freedom of movement and exploration before missions, rewards, economy,
  or an authored reason to travel. Existing gameplay remains preserved.
- Evaluate Godot against the actual game; do not assume an engine migration or
  replace the authoritative simulation with engine physics without evidence.
- The intended scale is an open universe. A bounded technical test is not a
  permanent boundary on where the finished game permits exploration.
- Land and traverse where physically suitable. Hazardous attempts have
  consistent consequences for player and craft, not arbitrary mission gates.
- Default loss returns the player to the last valid safe station with a
  serviceable starter shuttle and baseline fuel, preserving universe identity,
  discoveries and persistent changes. Loss cause determines wreck recoverability;
  cargo/upgraded-ship recovery, exact fallback selection and optional Ironman
  are later decisions, not grounds to make standard recovery unwinnable.
- Use two distinct fuel reserves: special jump fuel, and flight fuel shared by
  sub-light interplanetary travel and local/surface flight. Resource roles are
  selected. The starter has three discrete jump charges, one per valid in-range
  jump; flight burn curves remain to measure. Stations initially replenish both
  without currency. An economy is possible later, not required for the first trip.
- Continuous play is the aspiration. Travel pacing and transitions need
  playtesting; seamless presentation does not require real-time empty travel.
- Minimize shipped game data without an arbitrary size ceiling. Report runtime
  separately, and also report total installation, caches, RAM, VRAM, and saves.
  Large assets must justify their player value. Do not conceal size in an
  unbounded first-run cache or mandatory content download.
- Preserve source, assets, supported saves, deterministic identities, independent
  streams, tests, audio, and reproducibility. Open-source forks should be able
  to understand and change the recipes and content definitions.

### Desktop direction: clarified

Use a GPU-capable desktop development environment to build, capture, inspect,
and refine the native game. Build instructions must be portable and independent
of any maintainer's machine name, private hardware inventory, remote-access
route, cluster topology, or absolute workspace path. Development access is
distinct from the eventual native Linux/SteamOS display target.

Native graphical play is the primary product direction. Terminal presentation
remains a desirable secondary path, not an unconditional feature-parity blocker.
If it materially obstructs development, propose a recoverable archive rather
than delete its source, tests, history, or assets. No archive is performed by
this document; the exact boundary and support policy must be recorded when
there is a concrete conflict. Headless simulation/testing is not the same thing
as terminal presentation and must remain supported regardless.

Verify the selected graphics backend actually uses the GPU. Saved offscreen
renders support review, but interactive controller, fullscreen, frame-pacing,
and HDR validation still need a real graphical session and a representative
target display. Report portable capabilities and reproducible test conditions;
do not publish private hardware/access details from design conversations.

Update 2026-09-18: Godot is now the selected native presentation engine; see
[the ownership decision](GODOT_ADOPTION.md). C++ simulation/world authority
remains. Any terminal archive still needs a concrete, explicit decision.
Preserve historical terminal/headless evidence even if future
native rendering gets different acceptance criteria. Do not require exact GPU
pixel identity across drivers; simulation equivalence and visual tolerances
are separate checks.

## Current implementation versus intended experience

The current [jump contract](INTERSYSTEM_JUMP.md) has a cancelable three-second
spool followed by two seconds of committed transit. Assisted/Advanced alignment
affects arrival placement within the selected generated system. It does not
currently implement arbitrary wrong-system jumps or consumable fuel.

The [navigation contract](UNIVERSE_NAVIGATION.md) separates known, valid,
authorized, affordable, available, and selectable. Its implemented route is a
bounded origin/target pair and affordability currently assumes no consumables.
Neither this pair nor the open milestone issues should be mistaken for a
completed arbitrary-universe implementation.

Procedural terrain is present in the code and called from planetary
presentation. The reported visually poor surface flight is not yet reproduced
in this work. Issue #212 already documents restoration of recognizable relief
and explicitly rejects an objective placed at arrival as a surface-search loop.
Current minimum-clearance flight is not a completed landed/on-foot lifecycle;
#202/#203 own that distinction. Do not call the generator disconnected without
reproducing the user's actual play path.

## Approved first playable: Freedom

Use one compact shuttle, a station, authoritative generated landable terrain,
and one neighboring-system round trip. A larger catalog, minor-body fixture,
multi-leg network, carriers, full interiors, builder UI and economy are not
prerequisites. These bounds limit the workload, not the universe architecture.

Acceptance sequence:

1. Start without an active mission, invented completion history, or unlock grind.
2. Fly freely; navigation cues can be ignored without disabling movement.
3. Choose an unmarked surface location and descend through the same physical
   planet and terrain used in space presentation.
4. Touch down from actual pose, velocity, support, and slope. Selection or
   guidance never moves the craft to an objective or manufactures a landing.
5. Leave in a suit, walk, return to the same craft, and reboard. Test space EVA
   separately; a landed surface controller does not prove free-space traversal.
6. Lift off, revisit the location, and save/reload at each transition without
   changing the world's identity or losing player state.
7. Exercise a bounded hazard and the approved standard recovery baseline. Do
   not introduce unapproved cargo, upgrade, currency or progression penalties.
8. Plot the known nearby route, jump out and back using two of three charges,
   return to the station, replenish both fuels without payment and repeat.

A full-time suit is a candidate visual/control convention, not yet a mandatory
oxygen timer. Walking interiors require a physically appropriate frame. A
starter seat/airlock-to-surface transition need not wait for a large-ship fleet
or a fully traversable station. Missions can later observe these mechanisms;
they must not be required to initialize or operate them.

## Star chart and jump loop: candidate rules

The chart depicts known systems and honestly labeled uncertain contacts. It
does not reveal hidden generated catalogs just because they exist in memory.
Keep persistent knowledge separate from immutable universe truth.

### Connected sky: design direction discussed 2026-09-18

The intended stellar sky is a view of the generated universe, not an unrelated
random star texture. Individually rendered stars should resolve to stable C++
universe identities and three-dimensional positions derived from versioned
generation inputs. The view, chart and eventual destination must refer to the
same object. Traveling changes their apparent directions; returning or changing
render quality must not reroll their positions or destination systems.

Generate bounded, observer-relevant stellar summaries for presentation; generate
system and planetary detail on demand. Do not instantiate the universe's terrain
to draw its stars. Visibility is not complete navigational knowledge, drive
reach, fuel affordability or permission to manufacture a safe arrival. A star
can be visible without its distance or planets being known to the pilot.

The long-term intention is that individually rendered stars are visitable in
principle, not that the starter can jump directly to all of them. Total spatial
scope, unresolved stellar light, distant galaxies and optical visibility rules
remain design decisions. A physically meaningful bright exposure may hide faint
stars; the contract is identity consistency, not filling every frame with dots.

This is not implemented by the film's empty space background. A bounded first
proof should connect a visible neighboring star to its chart identity, arrival
and return, without requiring the entire wider-universe horizon first.

### Candidate route sequence

Proposed player sequence:

```text
inspect chart -> select destination/route -> inspect reach, cost and uncertainty
-> orient/prepare -> cancelable spool -> explicit commitment
-> transit -> physical arrival -> assess position and remaining reserves
```

- Plotting never moves the craft or advances the universe. Longer/multi-leg
  plans commit one leg at a time; future legs can become unaffordable or unsafe.
- Show three different limits: drive maximum single-jump radius, currently
  fuel-affordable reach, and estimated return/escape endurance. A larger tank
  does not automatically increase the drive's maximum single-jump range.
- Reach is a three-dimensional radial distance around the documented departure
  reference, not a fixed list of unlocked mission routes. Choose whether that
  reference is the live craft or a system anchor before defining exact units.
- Better/larger drives can increase nominal reach. Actual capability also
  depends on the supported craft/load and the eventual drive rules; no numerical
  distance or burn curve is approved here.
- Alignment and distance can influence arrival uncertainty, building on #193.
  Initially arrive in the selected system; do not silently add random
  wrong-system jumps, destination rerolls, or arrival at a ground objective.
- Reuse the existing spool/commit/save invariants. The current 3s/2s timing is
  a baseline for playtesting, not a permanent cinematic or pacing decision.
- Warnings about an uncertain return are not necessarily bans. A malformed
  route or physically impossible jump is different from a valid risky choice.
  Standard play must have a defined recovery if the player becomes stranded.

### Can the player get lost?

Proposed answer: yes, through exploration and logistics, not arbitrary deletion
of chart information. Distinguish these states before implementing them:

| State | Experience | Candidate way forward |
| --- | --- | --- |
| Uncharted surroundings | The current position is known but nearby routes, services, or hazards are not | Observe, scan, obtain charts, or retrace recorded travel |
| Offset arrival | The ship is far from its intended approach corridor inside the correct system | Local navigation, maneuvering, and alignment recovery |
| Stranded | Position/home are known, but reserves or capability cannot complete the next/return leg | Explicit recovery/respawn, or a later approved refueling mechanism |
| Navigation solution lost | Instruments cannot resolve position or a route | Separate future damage/uncertainty mechanic; not implied by the first three |

Record visited systems and a route breadcrumb/history; a known home anchor is
not a promise that the player has fuel or capability to reach it. Do not
guarantee both unrestricted risky travel and a free physical return. Standard
recovery prevents a dead save without pretending the trip was safe. Standard
safe-station respawn and free initial station replenishment are approved;
additional cargo/upgrade penalties, rescue delays and field replenishment still
require discussion.

Blind jumps and misjumps remain open questions. They conflict with the current
rule that CONTACT/PROBABLE knowledge cannot produce a valid jump solution and
must not be smuggled in as a cosmetic jump effect.

### Gravity wells and the jump field

The user's preferred direction is safest departure toward a system's outskirts,
with jumps near gravity wells becoming less accurate and/or dangerous. This
is a fictional drive rule to design, not a claim about real FTL physics. Model
local conditions from relevant bodies rather than drawing an arbitrary safe
circle around the system; a distant planet or moon can still create a local
hazard. Distance from home alone does not establish safety.

Candidate consequences should remain distinct and readable:

- Navigation uncertainty: a larger arrival envelope or offset.
- Craft stress: a disclosed risk of drive/hull damage.
- Jump-field interaction: the affected spatial volume is not necessarily
  identical to the exterior hull. Nearby atmosphere, structures, docked craft,
  or other matter may matter at departure and arrival.

The user's in-atmosphere tactical-jump example motivates deliberate dangerous
choices. It does not yet approve combat, a particular damage formula, a blast
radius, or actual atmosphere simulation. Begin with bounded geometric volumes
and environment classifications if this mechanic is selected. Test accuracy
and structural consequences independently before making every failure happen
at once. Plotting should disclose known hazards and label unknown estimates.

Open questions include field shape/extent, treatment of enclosed and partly
intersecting objects, whether attached craft must be wholly inside the field,
and safe handling of intersections with terrain or structures. Do not silently
teleport anything merely because its bounding box touches the jump volume.

Moving to a safe departure region is initially ordinary/local travel, not
automatically an extra chargeable FTL jump. If local jumps are later allowed,
define their resource cost explicitly: an outbound/return/reserve budget must
not secretly require additional departure and arrival jumps.

## Two-fuel direction; consumption and economy remain to be designed

The selected design uses **flight fuel** for both sub-light planet-to-planet
travel and local/surface flight, and separate special **jump fuel** for FTL.
"Maneuvering propellant" and "jump reserve" below describe their functions,
not settled chemistry. These are two pools, not separate space-flight and
atmospheric tanks. No third suit fuel is implied.

The approved starter has **three discrete charges: one outbound, one return,
and one backup**. Each valid jump within the installed drive's range consumes
one charge. Distance, gravity, alignment and condition affect disclosed risk,
not a hidden variable jump-fuel bill. Variable distance/load-dependent jump
consumption is a later design option, not an unresolved gate on this playable.
The UI shows remaining charges, valid reach, known risk and return affordability.
Maneuvering reserve must separately support reaching the safe departure region,
arrival recovery, and the reference return. The backup jump is an option, not
a guarantee of rescue or a substitute for a replenishment policy.

| Resource | Distinct decision to test | Candidate consumption rule |
| --- | --- | --- |
| Maneuvering propellant | Can I brake, descend, hover if supported, and ascend again? | Applied propulsion uses reserve; inertial coasting does not. Braking is thrust too. Atmosphere/gravity change required work. |
| Jump reserve | Which in-range destination and number of legs leave return/reserve options? | One charge per valid in-range jump; three starter charges. Proposed debit is atomic at commitment. Distance/load pricing is deferred. |

Implement and balance the two roles in a bounded experiment under #133 before
normal-play adoption. The earlier zero/one/two selection question has been
resolved by the design discussion; do not reopen it as a prerequisite. The
remaining research is consumption, endurance, useful decisions, replenishment,
and recovery without repetitive refueling. No consumption is implemented by
this document or retroactively imposed on existing saves.

Ordinary maneuvering and surface exploration must not drain jump fuel. They can
still exhaust flight fuel: separation preserves an emergency option, not an
unconditional guarantee against stranding. Intended scenario: an incautious
pilot spends too much flight fuel on the surface to ascend to a safe departure
region, but retains jump fuel and a working drive. A dangerous surface jump can
then be a deliberate escape attempt. A supported reference case should exercise
this choice; it does not imply every surface or damaged craft permits a jump.
Shared ship damage may disable either propulsion system despite full tanks.

Size is a useful baseline for tank capacity and propulsion scale. Loaded mass,
engine efficiency, environment, and thrust demand qualify it: a large ship can
hold more fuel and still have worse endurance. Drive size/class is separate
from ship size. Avoid a monotonic "larger is better at everything" ladder.

Before implementation specify:

- Capacity versus quantity, units, precision, overflow bounds, and deterministic
  tick-addressed integration. Time acceleration advances the same consumption
  for the same simulated actions; pause and offline waiting do not.
- Spool cancellation cost. Initial hypothesis: no jump-reserve charge until
  commitment; if another cost is chosen it must be visible and tested.
- Exactly-once atomic reserve debit plus committed destination/arrival state.
  Invalid commits consume nothing; reload cannot duplicate or refund a commit.
- Minimum escape/landing/ascent reserves and truthful uncertainty when local
  environment knowledge is incomplete. A reachable outbound jump is not proof
  of a survivable landing or an affordable return.
- Deterministic depletion/recovery and explicit station refill transactions for
  both pools. Initial service is free; keep payment policy separate so a later
  economy is possible. Do not assume mining, scooping, crafting, passive recharge
  or a mandatory economy is already approved.
- Versioned save behavior. Old supported saves must not receive silently
  invented exhaustion or changes to generated worlds.

## Learnable consequences and cumulative damage

Players should be able to learn "I did X, Y happened" through play. A manual
can explain details, but reading one must not be required to discover basic
operating hazards. Favor consistent causes, graduated warnings, readable
instrumentation, and concise explanations over surprise punishments.

Proposed communication loop:

1. Before commitment, show known gravity/atmosphere interference, drive/hull
   condition, expected uncertainty or stress, and the option to move outward.
2. During the event, reinforce the cause through instruments, sound, and
   restrained visual effects; effects must not hide essential controls.
3. Afterward, state the observed consequence and changed capability in plain
   language. Do not use an unexplained damage roll as the only feedback.

Damage persists and can compound if ignored. A repeat maneuver in a damaged
ship may be worse than the first, but that difference must follow visible
condition-dependent rules, not unexplained escalation. Define which damage
affects thrust, fuel loss, jump accuracy, or structural tolerance before using
those effects. A failed first attempt must not silently repair the ship or reset
its condition on reload. Repair methods and costs remain unselected.

Knowledge can be incomplete without the rules being inconsistent. Label
estimated/unknown hazards honestly rather than exposing hidden generated truth.
Fixed complete state and inputs remain reproducible; commit/save semantics
must prevent rerolling a jump result. High-detail graphics can reinforce this
through material, lighting, and motion changes, but actionable information must
also be readable without color-only cues or subtle visual damage inspection.

Acceptance should include safe outer-region departure, an outer-system location
near a local gravity well, warned deep-well departure, low-flight-fuel surface
escape, repeated use with accumulated damage, and save/resume at commitment and
after damage. Exact damage thresholds and outcomes are design work, not supplied
by a modern renderer. Standard recovery follows the approved safe-station and
replacement-shuttle baseline; condition-specific extensions need discussion.

## Modular spacecraft and eventual ship construction

The user's long-term intent includes a player-facing ship builder. Establish
assembly-capable data and working craft first; builder UI is not required for
the first playable. Modularity is a foundation, not evidence that a builder is
"80 percent complete": validation, editing, collision, persistence, flight
balance, and usable authoring tools remain substantive work.

Starter craft direction: a small 1-4-person shuttle/lander with limited jump
capability, seats, and essential equipment; no sleeping quarters or galley and
no intended long-haul habitation. Larger ships can carry the shuttle, remain
in space, and support extended operations. Landability remains a physical
capability assessment, not a universal ban based on class name or size alone.

Proposed modular foundation:

- Reusable parts have stable IDs/versions, local transforms, permitted size
  variants, attachment interfaces, bounds, collision/contact geometry, and
  functional contributions. Visual-only variants cannot secretly change mass
  or tank capacity.
- Assembled craft account for dry and loaded mass, fuel capacity, drive limits,
  thrust placement, center of mass, and inertia at the fidelity actually needed
  by the working flight model. Do not use triangle count or scaled mesh volume
  as an implicit physical specification.
- Shared part assets/materials plus a compact assembly recipe avoid shipping a
  complete unique asset set for every ship. Keep bounds on assembly complexity
  and generated caches; instancing saves duplication, not all rendering cost.
- Populate a larger galley with additional human-scale tables, seats, storage,
  and work areas. Repeat/stretch suitable structural sections; do not uniformly
  scale furniture, doors, steps, or control reach. Record allowed scaling axes
  and ranges where scaling is used.
- Exterior dimensions, habitable volume, access routes, hatches, and docking
  clearances must agree. A docked shuttle remains its own persistent craft with
  owned fuel/cargo/state; include its mass once in the carrier assembly and
  restore continuous pose/velocity on separation.
- Carrier FTL rules must specify field coverage and whether/how carried mass
  affects cost. A docked shuttle cannot disappear, duplicate, consume its own
  jump reserve implicitly, or become free mass during a carrier jump.
- Preserve module versions and stable craft identities in saves. Forks may
  change parts/recipes with explicit compatibility decisions; loading an old
  assembly must not silently replace its physical properties.

Stage the work as fixed tested craft -> code/data-authored modular assemblies
-> validated player editing. Prove at least a shuttle and a carrier assembly
before extracting generalized building infrastructure. No builder UI, economy,
crafting, interiors for every frame, or arbitrary module count is authorized by
this proposal.

## GitHub reconciliation: original conflict audit and applied direction

The table records the original conflicts found in issue bodies before the
approved update. Existing technical owners were retained. Current issue headers
and [ROADMAP.md](ROADMAP.md) record the applied status; old milestone numbers
are historical placement, not new dates. #243–#251 add the Freedom tracker,
engine evaluation, integration, service, recovery, hazard contract and two fuzzy
horizons, plus a separate fuel-state implementation owner after #133's contract.
#237 is closed as superseded, not completed; no terminal code is deleted.

| Existing issue | Relationship/conflict | Proposed action |
| --- | --- | --- |
| [#133 Fuel/endurance](https://github.com/gobha-me/apsis-drift/issues/133) | Zero/one/two selection is superseded by the chosen two-fuel direction; endurance research remains necessary | Retain/re-scope to shared sub-light/surface flight fuel plus special jump fuel, the three-jump starter target, Freedom workloads, replenishment, and recovery; do not close the whole issue as obsolete |
| [#193 Jump reach](https://github.com/gobha-me/apsis-drift/issues/193) | Owns radial reach and distance/alignment arrival envelopes; excludes fuel and arbitrary drive classes | Keep as per-leg provider; remove earned-career prerequisite for the sandbox; expose inputs for later drive/load variation |
| [#174 Topology](https://github.com/gobha-me/apsis-drift/issues/174), [#229 Network](https://github.com/gobha-me/apsis-drift/issues/229) | Two-system then bounded network scope fits proof workloads, not the final exploration ceiling | Preserve IDs/versioning and bounded working sets; distinguish test-region size from eventual addressable universe |
| [#175 Knowledge](https://github.com/gobha-me/apsis-drift/issues/175), [#176 Navigation](https://github.com/gobha-me/apsis-drift/issues/176) | Useful chart/redaction and selection semantics; #176 requires earned drive #223 | Reuse knowledge contract; provide an explicitly equipped Freedom start without fake mastery, visits, or discoveries |
| [#195 Multi-leg routes](https://github.com/gobha-me/apsis-drift/issues/195) | Already owns direct/short-leg tradeoffs and persistence | Keep owner; integrate approved fuel costs later and distinguish known-return geometry from affordable return |
| [#196 Craft frames](https://github.com/gobha-me/apsis-drift/issues/196) | Already separates frame and installed capability | Keep; add measured tank/load/drive relationships only after #133, not a generic ship builder |
| [#208 Scale](https://github.com/gobha-me/apsis-drift/issues/208), [#211 Terrain detail](https://github.com/gobha-me/apsis-drift/issues/211), [#212 Surface presentation](https://github.com/gobha-me/apsis-drift/issues/212) | Directly address the reported surface experience; generation must not be replaced by a toy Godot planet | Prioritize physical samples, visible relief, and continuity; retain versioned old terrain and explicitly retarget presentation criteria for native evaluation |
| [#202 Contact](https://github.com/gobha-me/apsis-drift/issues/202), [#203 Landed lifecycle](https://github.com/gobha-me/apsis-drift/issues/203) | Already distinguish minimum-clearance flight from real landing; landing cannot complete a mission | Keep and prioritize as Freedom foundations |
| [#198 Interiors](https://github.com/gobha-me/apsis-drift/issues/198), [#199 Suited EVA](https://github.com/gobha-me/apsis-drift/issues/199), [#210 Embodied epic](https://github.com/gobha-me/apsis-drift/issues/210) | Embodiment is deferred behind fleet/career milestones; EVA is bounded and environment-gated | Bring a minimal suitable-craft/surface test forward; preserve safe transition validation but separate hazardous voluntary action from corrupt/impossible state; streaming bounds must not become unexplained walking walls |
| [#221 Home mastery](https://github.com/gobha-me/apsis-drift/issues/221), [#223 Earned First Jump](https://github.com/gobha-me/apsis-drift/issues/223) | Explicitly hide FTL until career milestones; directly conflicts with mission-free jump testing | Preserve as possible later career rules; do not use them as prerequisites for Freedom; reconcile release promises before changing public roadmap |
| [#103 Legacy Pilot](https://github.com/gobha-me/apsis-drift/issues/103) | Already preserves standard recovery and proposes optional permanent pilot loss with succession | Keep question-gated; do not equate succession with whole-save Ironman or settle respawn penalties implicitly |
| [#237 Kitty renderer experiment](https://github.com/gobha-me/apsis-drift/issues/237) | CPU/Kitty-only scope, explicit no-GPU boundary, and sister-project evidence gate do not authorize a Godot experiment | Keep historical scope/evidence; propose a separate bounded Godot evaluation and explicitly revise renderer gating rather than pretending #237 covers it |

Cross-cutting conflict: several issues require new features to remain complete
on both Kitty and ANSI. Revise those requirements to distinguish native-primary
acceptance from optional terminal coverage, and record any concrete archive
decision explicitly. Preserve legacy evidence and headless simulation tests.
Do not close technical work as complete merely because an old presentation
target is no longer a gate.

### Superseded scope versus completed work

Issues were reconciled under three dispositions: retain/re-scope, defer, or
supersede with a linked replacement. Obsolete assumptions are not completed
features. In particular, #133's resource-count question is superseded, not its
endurance/recovery work; #221/#223's progression can be deferred without deleting
it; #237's terminal-only experiment was superseded by native evaluation #244,
not completed and not proof that a native renderer or adopted engine exists.

The eventual builder and jump-field/damage behavior need explicitly bounded
follow-ups after their contracts are agreed. Existing #196 excludes a generic
builder, and #193's arrival envelope does not itself implement persistent damage
or displaced surrounding matter. #248/#249 preserve those design gates. The
roadmap update changed issue scope/milestone metadata and closed #237 as not
planned; it did not implement gameplay, choose an engine, or publish a release.

## Proposed next work, in order

1. Follow the reconciled Freedom milestone #243; use a GPU-capable desktop for
   build/render iteration. Godot was selected on 2026-09-18; follow
   [the ownership decision](GODOT_ADOPTION.md). Any terminal archive still needs
   an explicit evidence-based decision.
2. Inventory retained behavior/tests and reproduce the reported surface defect.
3. Continue qualifying Godot using real C++ terrain/state and release-size,
   memory, streaming, coordinate, controller, and native-display measurements.
   Compare to the retained implementation, not to hypothetical SDL performance.
4. Prove free flight -> actual landing -> suit traversal -> reboarding -> ascent
   with no active mission; preserve repeatability and save/resume throughout.
5. Add the chart/per-leg jump experiment, then calibrate the selected two-fuel
   model with stranding/recovery traces and a small multi-system fixture.
6. Choose progression, missions, detailed recovery penalties, and world character
   after playtesting the functional parts. No release is authorized by this note.

Open decisions: precise terminal maintenance/archive boundary; definition of
lost/blind jumps; departure reference for range; jump-field extent and gravity
hazard rules; flight consumption curves; station service timing and safe-station
fallback details; future cargo/upgraded-ship recovery; cumulative damage and
repair; module/drive relationships and carrier accounting; transition timing;
remaining native platform qualification. Three fixed charges, free initial station refueling and the
standard recovery baseline are settled for Freedom, not open questions.
