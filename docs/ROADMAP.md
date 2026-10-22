# Apsis Drift: freedom first

Approved direction: 2026-09-17. This is the high-level product map, not a claim
that the features below are implemented or a promise of release dates.

Build a compact, deterministic **open universe** whose scale comes from recipes,
reusable parts and player choices rather than storing every place in advance.
Prove that players can travel, land, walk, return and persist their journey
before choosing the missions, economy or story that give those actions purpose.

## What is firm now

- Native graphical play is primary. Godot was selected for native presentation
  on 2026-09-18; [the ownership decision](GODOT_ADOPTION.md) preserves the C++
  application. Production qualification remains work, not a reason to build
  a competing SDL graphics engine.
- Existing C++ generation, simulation, identities, supported saves, audio,
  assets and tests are preserved. A frontend must not invent a second world.
- Terminal presentation is a desirable secondary path, not a mandatory parity
  gate for new features. Archive recoverably only if a concrete conflict
  justifies it. Headless simulation/testing remains regardless.
- Players can attempt physically hazardous choices and learn consistent,
  understandable consequences. Default loss permits recovery, not permadeath.
- Two fuels: one shared by sub-light and surface flight, one special to jumps.
- Planet-specific physical rotation and star lighting share authoritative
  simulation time. Optional time acceleration is deferred and must advance the
  whole simulation, not an independent fast lighting clock. Preserve legacy
  rotation/lighting recipes explicitly until versioned native integration.
- New native universes also use physically scaled planetary orbital periods,
  derived from orbital distance and an explicitly versioned stellar mass
  parameter. Keep existing worlds/demo catalogs on their original recipes;
  do not change saved generator defaults or silently reinterpret short years.
- Compact content is an engineering discipline, not a fixed size ceiling.
  Measure content, runtime, total installation, caches, saves, RAM and VRAM
  separately. A small download that silently generates an enormous cache does
  not satisfy the intention.
- Preserve extensibility through stable identities, explicit ownership and
  versioned recipes. Do not prebuild generic frameworks for speculative features.
- Contributor instructions use portable capabilities and paths, never private
  hostnames, hardware inventories or maintainer access infrastructure.

## Next milestone: Freedom

Owner-approved feature order, 2026-10-16: **surface landmarks**, then
**flight and landing usability**, then **moons and asteroids**. Deliver bounded
playable increments in that order without reopening the priority question.
The first increments in all three areas are now delivered. The landmark
increment is [seeded geological exposures](FREEDOM_SURFACE_LANDMARKS.md)
([#611](https://github.com/gobha-me/apsis-drift/issues/611)): visible ground
bands, on-foot inspection and retained first visits using the existing terrain.
The flight/landing increment keeps aid feedback visible through its
actual lifecycle ([#613](https://github.com/gobha-me/apsis-drift/issues/613)).
The third increment is [distant optical contacts](DISTANT_CONTACTS.md)
([#214](https://github.com/gobha-me/apsis-drift/issues/214)): a separately
selected moon/minor catalog, true apparent size and phase, and explicit
close-observation limits. New solid bodies, gravity and landing were deferred in that optical increment.
On 2026-10-19 the owner selected **one airless moon: approach, land and walk**
as the next bounded expansion ([#623](https://github.com/gobha-me/apsis-drift/issues/623)).
[The integration contract](AIRLESS_MOON.md) preserves its existing identity and
C++ ownership; scalar/frame providers alone do not complete the playable trip.
The next bounded continuation is [asteroid close inspection](ASTEROID_INSPECTION.md)
([#629](https://github.com/gobha-me/apsis-drift/issues/629)): actual charged
home–minor–home flight, saved state and a disclosed inspection stand-off guard.
This does not activate asteroid landing, mining or the wider heterogeneous horizon.
The next carry-over, [Moon geological exposures](FREEDOM_SURFACE_LANDMARKS.md#moon-walks)
([#631](https://github.com/gobha-me/apsis-drift/issues/631)), adds visible sites,
actual supported walking/inspection and retained first visits to that Moon.
It changes no terrain, collision, mission or resource rules.
[Reusing those local round trips](ASTEROID_INSPECTION.md#versioned-ownership-and-compatibility)
([#633](https://github.com/gobha-me/apsis-drift/issues/633)) removes the one-way
asteroid-to-Moon history restriction. The same save can visit either companion
again after returning home and physically docking for the existing free service.
It adds no direct companion transfer, new destination or economy.
The bounded [Origin return rendezvous aid](PORT_APPROACH_AID.md)
([#635](https://github.com/gobha-me/apsis-drift/issues/635)) follows with optional
real thrusters and fuel from the existing home return region, then hands over to
the unchanged close-range aid. Capture remains explicit; general navigation and
obstacle avoidance remain separate.

On 2026-10-21 the owner selected [orbital terrain surveys](FREEDOM_ORBITAL_SURVEY.md)
([#652](https://github.com/gobha-me/apsis-drift/issues/652)): deliberate acquisition
in actual stable orbital flight, retained partial progress and a coarse terrain
model with survey provenance. It does not activate probes, survey payment,
missions, full mapping coverage or the broader #207 network horizon.

The owner then selected **four charted systems with unexplored planets**
([#229](https://github.com/gobha-me/apsis-drift/issues/229)). The
[optional finite region](FREEDOM_TOPOLOGY.md#optional-four-system-region) keeps
the existing pair and adds two seeded destinations. It grants system anchors;
planetary knowledge still comes from observation. Manual single-leg jumps retain
three charges and free service after physical docking at Origin. Existing saves
and the default two-system New Game keep their current selection. Larger
networks, automatic routing, missions and the economy remain separate scope.

The owner next selected **carried geological specimens with retained discovery
notes** ([#660](https://github.com/gobha-me/apsis-drift/issues/660)). The bounded
[hand-specimen increment](FREEDOM_SURFACE_LANDMARKS.md#hand-specimens) uses existing
inspected planet/Moon sites, a four-slot pouch, explicit discard and first notes
that survive specimen loss. It adds no sale, extraction, probes or missions;
the wider #207 horizon remains separate.

The owner-selected follow-through [#662](https://github.com/gobha-me/apsis-drift/issues/662)
adds a separate [planetary-chart start](FREEDOM_PLANET_TRAVEL.md) and manual jumps
between known generated planets in a system. It grants only navigation facts,
retains the existing charge rules and composes prior route receipts. This is the
bounded implementation scope selected on 2026-10-21;
existing journeys and the broader deferred horizon keep their meanings.

2026-09-18 implementation increment: [controller foundation 06](CONTROLLER_FOUNDATION_06.md)
adds analog input and a remappable, controller-navigable native control surface.
Controller-first play is the design direction; hardware qualification and the
physical Freedom loop remain open. This does not mark the milestone complete.

The [thrust flight lab 09](THRUST_FLIGHT_LAB_09.md) now provides opt-in full
attitude, analog main/retro forces, inertia, gravity and drag for handling
playtests. It remains unsaved and retains a test floor guard; collision,
landing, actuator/fuel integration and hardware feel are not qualified.

[Presentation 10](FLIGHT_PRESENTATION_10.md) connects cockpit instruments,
cleans up diagnostics, adds an orbit camera and identity-backed preview stars,
and proves continuous ascent/coast/atmospheric return in the C++ lab. Explicit
practice starts support short playtests. Landing, walking, consequences and
a navigable galaxy remain open; review flight feel before damage.

[Tracker reconciliation 13](TRACKER_RECONCILIATION_13.md) records the checkpoint
audit and follow-ups: local impact/fracture proof #253, native ship audio #254,
and numerical compatibility #255. These are bounded work items, not completed
features or new economy/mission prerequisites.

The 2026-09-20 [occupied-cockpit study](OCCUPIED_PILOT_MOTION.md) connects seated
pilot arms and movable controls to resolved flight commands, with first-person
head masking and separate matched cabin exports. The owner reports build 33
looks good; this is incremental playtest feedback, not final character-art,
restraint/collision or animation acceptance. The accepted recording mix stays
unchanged. A [paused flight reference](NATIVE_FLIGHT_REFERENCE.md) explains the
current orbit/coast/guidance behavior and unimplemented mechanics.

The owner accepted build34's screenshot-level thumb-button/index-trigger fit
as sufficient to continue iteration. Both pilot variants retain the established
seat/eye/wrist/control datums and existing stick motion. This is not acceptance
of final character art, a full controller playtest, or button-press animation;
no new action bindings were added. Restraint refinements remain a separate study.

The [physical orbital catalog](PHYSICAL_LOCAL_SYSTEM.md),
[rotation/star geometry provider](PLANET_ROTATION.md) and
[rigid-frame handoffs](RIGID_FRAME_HANDOFF.md) supply the C++ foundations now
consumed by [saved native flight](SAVED_NATIVE_FLIGHT.md) and landing. The opt-in
[physical-home native study](NATIVE_PHYSICAL_HOME.md) remains its earlier,
separate checkpoint; its standalone fixture is not the persistent game.
Current saved journeys select their actual recipes, shared rotation/star clock
and terrain owner. Broader visual/hardware acceptance remains #244/#245.

[Freedom milestone](https://github.com/gobha-me/apsis-drift/milestone/10) and
[tracking epic #243](https://github.com/gobha-me/apsis-drift/issues/243) replace
the former version-number release ladder as the active next work.

| Step | Outcome | Owner |
| --- | --- | --- |
| Qualify the selected frontend | Godot selected; continue measuring integration, streaming, platform support and quality budgets using real C++ world/state | [#244](https://github.com/gobha-me/apsis-drift/issues/244) |
| Prove physical freedom | One shuttle, one station, generated terrain, freely chosen suitable landing, suited walking, reboarding and ascent | Existing physical/terrain issues and [#199](https://github.com/gobha-me/apsis-drift/issues/199), composed by [#245](https://github.com/gobha-me/apsis-drift/issues/245) |
| Prove a neighboring trip | Star chart, in-range jump, physical arrival, return; no mission/mastery prerequisite | [#174](https://github.com/gobha-me/apsis-drift/issues/174), [#176](https://github.com/gobha-me/apsis-drift/issues/176), [#193](https://github.com/gobha-me/apsis-drift/issues/193), [#194](https://github.com/gobha-me/apsis-drift/issues/194) |
| Make operation repeatable | Shared flight fuel plus three jump charges; free initial station refueling; standard loss/recovery | Contract [#133](https://github.com/gobha-me/apsis-drift/issues/133), implementation [#251](https://github.com/gobha-me/apsis-drift/issues/251), service [#246](https://github.com/gobha-me/apsis-drift/issues/246), recovery [#247](https://github.com/gobha-me/apsis-drift/issues/247) |
| Define understandable risk | Local gravity/environment, alignment, distance and condition influence disclosed jump consequences | [#248](https://github.com/gobha-me/apsis-drift/issues/248), integrating with #193 |

The starter is a compact 1–4-person-rated shuttle/lander, with one controlled
player. This capacity does not imply multiplayer, NPC crew or multiple playable
characters. It has no sleeping quarters or galley and is not a long-haul home.

The first playable has **three discrete jump charges**: outbound, return,
reserve. Each valid in-range jump consumes one charge. Range and risk still
matter, but there is no hidden distance/load-dependent jump fuel bill. Flight
fuel is separate and must account for braking, assistance, hover where supported,
landing and ascent; flight endurance and exact burn curves need measurement.

Both fuels can be replenished at stations without currency for the initial
playable. Resource/service operations remain distinct from payment policy so
an economy is possible later, not a prerequisite for the first trip.

Standard loss preserves universe identity, discoveries and persistent changes,
then returns the player to the last valid safe station with a serviceable
starter shuttle and baseline fuel. Define a safe fallback if that station is
invalid. Loss cause determines whether there could be a recoverable wreck;
irrecoverable destruction must not require retrieving one. Cargo consequences,
upgraded-ship recovery, repair economics and optional Ironman are later decisions.

Continuous play is an aspiration to playtest, not a demand for real-time empty
travel. Saving, reloading, renderer quality and frame cadence must not reroll a
world, refund a committed jump, duplicate a craft or fabricate progress.

### Owner minimum playable handoff — 2026-09-30

2026-10-09 delivery amendment: prioritize the playable interaction over complete
anatomical certification. [The boarding prototype](PLAYABLE_BOARDING_PROTOTYPE.md)
uses an application-owned authored route, simple gameplay body proxy and explicit
saved actor phases. Preserve the detailed studies as deferred research, record
imperfections and revisit them after the journey works. Their negative or
unresolved findings no longer gate prototype boarding or imply completion.

Before asking the owner to playtest, ordinary play must start on the station;
allow walking to the craft, boarding and sitting, takeoff and station exit;
show animated exhaust from actual applied propulsion; and preserve controllable
atmospheric exit, flight across terrain and return home in the same world.
This explicitly adds the bounded station-to-craft traversal to active scope,
superseding the earlier exclusion of station walking. It is the minimum handoff,
not a complete 1.0/MVP/beta requirement or an instruction to stop there. See the
[dated #245 amendment](https://github.com/gobha-me/apsis-drift/issues/245).

The [saved native flight consumer](SAVED_NATIVE_FLIGHT.md) connects format-18
Continue, C++ fixed-step controls, terrain projection and applied main exhaust.
The [native port lifecycle](NATIVE_PORT_LIFECYCLE.md) adds physical capture,
constrained station co-motion, same-tick release and explicit format19 persistence.
Fresh New Game now starts a format21 saved starting assembly around the supported
format20 station walk described below. The selected stowed Wayfarer presentation
and C++ contact share that assembly; historical saves retain their original model.
Boarding, seating, departure, surface walking and physical home return are now
integrated in the [station-to-surface journey](FREEDOM_SURFACE_WALK.md).
The complete owner handoff remains open for manual/controller and hardware
qualification; integration evidence does not claim that acceptance.

Missions, economy, carriers, shipbuilder UI, populated stations and full
large-ship interiors remain outside this first path. Existing mission/career
evidence stays preserved; Freedom gets an explicit new-game baseline rather
than fake completions or a silent redefinition of old saves.

## Fuzzy horizons: options preserved, not implementation commitments

These are discussion placeholders. Their ordering, scope and release numbering
are deliberately not fixed. Before selecting one, agree its player value,
smallest proof, technical owner, compatibility consequences and recovery rules.

| Horizon | Vision / question to revisit | Tracking |
| --- | --- | --- |
| Wider universe | More systems, multi-leg choices, asteroids/comets, varied bodies, incidental discoveries, observations/probes and getting lost without arbitrary map erasure | [#207](https://github.com/gobha-me/apsis-drift/issues/207), #214, #229, #195 |
| Modular ships and carriers | Purpose-built shuttle and starship roles, carried craft, versioned assemblies and eventual player construction | [#249](https://github.com/gobha-me/apsis-drift/issues/249), [#209](https://github.com/gobha-me/apsis-drift/issues/209) |
| Habitation and embodiment | Human-scale modular interiors, useful stations/large ships, future space EVA and what makes places worth inhabiting | [#210](https://github.com/gobha-me/apsis-drift/issues/210), #198, #90 |
| Economy and logistics | Paid/limited services, field fuel, production, repairs, cargo, insurance and recovery without mandatory early grind | [#250](https://github.com/gobha-me/apsis-drift/issues/250) |
| Reasons to explore | Missions, onboarding, story/world character, earned upgrades and optional advanced drives layered onto functioning freedom | [#106](https://github.com/gobha-me/apsis-drift/issues/106), [#205](https://github.com/gobha-me/apsis-drift/issues/205) |
| Higher-stakes play | Optional permanent pilot loss, succession versus Ironman, consequences appropriate to cause | [#103](https://github.com/gobha-me/apsis-drift/issues/103) |
| Jump-field consequences | A fold volume beyond the hull, nearby matter, docked craft, atmospheric escape and cumulative damage | [#248](https://github.com/gobha-me/apsis-drift/issues/248), #249 |

A bounded test region is not a permanent universe boundary. A resource is not
an economy. A modular asset kit is not a completed builder. A surface walking
controller is not proof of space EVA. An engine import is not a finished game.

## Tracker reconciliation and preservation

The initial update created #243–#251, moved 36 existing technical issues into
Freedom, and retained 48 other open issues as deferred/horizon work with their
original specifications. Six older milestone containers now explicitly describe
historical backlog or discussion horizons instead of the next release sequence.

[#237](https://github.com/gobha-me/apsis-drift/issues/237) was closed as
**superseded / not planned**, not completed. Its Kitty-only CPU experiment and
sister-project wait no longer gate native evaluation #244. Its archaeology and
original specification remain preserved; no terminal code was deleted.

Active issues have dated scope amendments that override conflicting legacy
milestone, prerequisite and presentation wording. Deferred issues retain their
old specifications as historical reference, not commands to implement an old
plan. No issue closure asserts an unimplemented feature is finished.

For detailed reasoning, candidate mechanics and the original conflict audit,
see [Freedom design notes](FREEDOM_DESIGN_PROPOSAL.md). Runtime documentation
continues to describe shipped behavior until implementation changes it. No
release, tag, new remote, engine installation or game-code migration is
authorized merely by this roadmap.

## Supported station walking checkpoint — 2026-10-01

[#344](https://github.com/gobha-me/apsis-drift/issues/344) composes a first-person
station actor, source-bound C++ support/contact checks and attached D1 Wayfarer
through the existing shared flight clock. The original checkpoint used explicit
format20. New Game now wraps that unchanged journey in the
[format21 starting assembly](NATIVE_STARTING_ASSEMBLY.md); Save As/Continue retains
the actual actor, voyage and selected hardware. Historical formats16–20 retain
their contracts and original presentation. The bounded hub/workshop/D1 route is documented in
[saved station walking](SAVED_STATION_WALK.md).

Open-hatch/ladder boarding and the seat transition are integrated (#291).
New Game departure, atmospheric flight and home return now have the
[uninterrupted native voyage regression](PLANETARY_VOYAGE_PROTOTYPE.md), retaining
one craft model and matching C++ saves throughout. Manual/controller play and
hardware presentation qualification remain separate parts of #245. The
[generated terrain pad assessment](TERRAIN_TOUCHDOWN.md) supplies read-only
contact diagnostics; landing/liftoff, hull clearance and impact consequences
remain separate work. These checkpoints do not activate NPC/dialogue, AG
failure, EVA, weapons, missions or paid-economy horizons.

## Landing and surface preparation checkpoint — 2026-10-09

The [landed craft prototype](LANDED_CRAFT.md) now integrates actual gear,
generated whole-pad support, bounded hull clearance, planet-fixed idle,
Save/Continue and continuous thruster liftoff (#202/#203). The assisted Landing
action operates only within a bounded aligned local envelope; manual Landing
deploys gear without a pose snap. Both compiler traces and native lifecycle
saves agree. Manual/controller play and hardware rendering remain separate
qualification; close-ground presentation and gear/exhaust refinements are
documented without gating further development.

[Immutable ambient environment](PLANET_AMBIENT.md) supplies separately versioned
planetary hazard envelopes for capability and suited walking consumers. The
[fixed starter assessment](CRAFT_ENVIRONMENT_ASSESSMENT.md) now separates
shielding/thermal, structural and propulsion margins, with a read-only native
surface survey redacted through actual knowledge. Saved commitment policy and
the composed operation acceptance remain #102.
It preserves existing worlds and saves and introduces no weather clock, force,
damage or raw cockpit disclosure. Local/neighboring observations, resource state,
bounded jump integration and explicit standard recovery are implemented. The
[surface walking prototype](FREEDOM_SURFACE_WALK.md) adds ground movement and outside
save/resume; authored hatch transfer and exposure policy remain #199 work. Saved
environmental commitment policy (#102), automatic
loss consequences and wider manual/controller/hardware acceptance remain work.

## Resource implementation checkpoint — 2026-10-09

The [measured endurance recipe](FREEDOM_ENDURANCE.md) selects shared gross-effort
flight fuel and three charges (#133). [Live C++ resource state](FREEDOM_RESOURCES.md)
now composes new native sessions with exact saved quantities, whole-tick passive
depletion, cockpit readings and explicit free attached-port replenishment. Old
saves retain their resource-unselected behavior. The recorded home voyage and
separate initialized near-ground reserve-funded ascent qualify this starter
budget without introducing an economy or new assets.

The retained historical jump adapter preserves its atomic charge accounting.
[Mission-free native neighboring travel](FREEDOM_NATIVE_TRAVEL.md) now persists
actual spool, commitment, transit and arrival. The
[continuous round trip](FREEDOM_NATIVE_ROUNDTRIP.md) qualifies normal station
departure, actual ALIGNED/OFFSET Advanced arrival, neighboring observation, physical
return docking, replenishment and disembarking with both compilers and native
replays. [Explicit standard recovery](FREEDOM_RECOVERY.md) preserves the world and
knowledge while replacing a declared lost craft. These selected consumers
complete #193/#194/#189/#251/#246/#247. Full integrated #245 acceptance remains
open; recovery does not yet detect every loss automatically.

## Starting chart and knowledge checkpoint — 2026-10-09

The [bounded topology decision](FREEDOM_TOPOLOGY.md) reuses the existing seeded
origin and nearby anchors. [Persistent knowledge](FREEDOM_KNOWLEDGE.md) explicitly
grants starting infrastructure and stores per-fact confidence/provenance in new
native sessions. Older saves retain their selected semantics. The collapsible
cockpit chart shows granted locations and actual charge affordability; reading
it grants no visits or observations. Actual survey triggers #189, wider sensor
presentation #171/#215 and physical native jumps #193/#176/#194 remain separate
implementation work.

## Local observation checkpoint — 2026-10-09

New native games select knowledge recipe2/local observation policy1. Real
committed flight, qualified touchdown and port capture update the persistent
ledger; station walking, chart focus, camera and Save As/Continue do not. Coarse
planet models and separate physical presence remain bounded/read-only to Godot.
See [the selected local policy](FREEDOM_OBSERVATIONS.md). Neighboring arrival,
complete knowledge-filtered rendering and recovery remain their existing owners.

## Native travel checkpoint — 2026-10-09

[Native First Jump](FREEDOM_NATIVE_TRAVEL.md) now composes the installed starter
with the known neighboring route: actual flight during spool, cancel, one-charge
commitment, immutable arrival, world/terrain handoff and physical return. Explicit
formats 26/27 and knowledge recipe3 preserve current ownership and every phase;
older Save/Continue remains unchanged. Neighbor observations use the actual world,
and the authored craft survives without a phantom home station or resource service.
The composed surface/return-docking proof #194, wider consequences #248/#253,
replacement recovery #247 and final native visual qualification remain open.

## Neighboring round-trip checkpoint — 2026-10-09

[The bounded normal trip](FREEDOM_NATIVE_ROUNDTRIP.md) now starts with actual
station walking/boarding and returns through physical moving-station capture,
service and disembarking. Assisted/Advanced traces retain an uninterrupted session
beside each checkpoint-resumed session, meter actual thrust, and leave one jump
charge before service. Native replay consumes the same controls and checks full
C++ saves, current-world cues and pilot continuity. Deliberate Advanced traces align
both departures or hold a twenty-degree offset with real actuators, retaining
the offset's larger arrival envelope through the actual return. Both also complete
a separate Compatibility/llvmpipe graphical replay with matching saves at nineteen
checkpoints each. This covers the bounded #194 trip; broader hardware and manual
First Flight qualification remain #244/#245. Standard replacement is recorded below.

## Standard recovery checkpoint — 2026-10-09

[Standard recovery](FREEDOM_RECOVERY.md) now provides an explicit pending loss,
validated station/fallback, retired individual craft and one fresh starter with
baseline resources. Before/pending/completed Save/Continue and duplicate events
preserve the universe, chart and clock. Native replacement stages its assets and
terrain before committing a new station/walker view. The declared loss fixtures
follow actual neighboring-system travel; collision fatality detection, living-
pilot rescue, costs and hazardous-jump laws are separate or unselected. Wider
Broader native hardware and manual First Flight qualification remain #244/#245.

## Planetary scale contract — 2026-10-10

The [physical/visual contract](PLANETARY_SCALE_CONTRACT.md) records metre scale,
reference altitude versus generated clearance, exact selected-world ownership,
frozen-camera projection targets and measurable feature/sample criteria.
Its fixture matrix covers approach, entry, landed and actual suited eyes at
compact/1080p/4K outputs with explicit internal resolution. This is the #208
research definition, not new terrain, a renderer implementation, a dynamic
resolution setting or proof that every target view has passed. Preserve the
working prototype and record missing consumer/hardware evidence separately.

The owner-selected first drive-recovery increment is scoped in [Freedom jump recovery](FREEDOM_JUMP_RECOVERY.md) (#664), following charted planetary travel (#662). It adds no mission, upgrade or economy gate.

The owner-selected follow-through on 2026-10-22 is **temporary drive strain first,
actual rescue afterward**. [#667](https://github.com/gobha-me/apsis-drift/issues/667)
adds cumulative, naturally cooling recovery debt through the existing
[drive contract](FREEDOM_JUMP_RECOVERY.md#temporary-drive-strain), with explicit
new-journey selection and preserved historical behavior. The next bounded increment is [Origin fuel service](ORIGIN_FUEL_SERVICE.md)
([#669](https://github.com/gobha-me/apsis-drift/issues/669)): one actual finite-fuel
responder for a supported home-Moon ship, with shared time and Save/Continue.
This adds no instant evacuation, economy, maintenance or permanent hull damage.
