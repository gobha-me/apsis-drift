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
- Compact content is an engineering discipline, not a fixed size ceiling.
  Measure content, runtime, total installation, caches, saves, RAM and VRAM
  separately. A small download that silently generates an enormous cache does
  not satisfy the intention.
- Preserve extensibility through stable identities, explicit ownership and
  versioned recipes. Do not prebuild generic frameworks for speculative features.
- Contributor instructions use portable capabilities and paths, never private
  hostnames, hardware inventories or maintainer access infrastructure.

## Next milestone: Freedom

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

The [rotation/star geometry provider](PLANET_ROTATION.md) and
[rigid-frame handoffs](RIGID_FRAME_HANDOFF.md) are tested C++ foundations, not
yet replacement gameplay physics. Catalog-backed native startup, saved recipe
selection, coherent rotating terrain/ship/light presentation and the remaining
physical landing loop are still required. Do not relabel the nonrotating lab
or its standalone seed-42 fixture as a completed persistent universe.

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

No missions, economy, carrier, shipbuilder UI, full large-ship interior or
walkable station interior is required for this first slice. Existing missions
and career evidence stay preserved; Freedom gets an explicit new-game baseline
rather than fake completions or a silent redefinition of old saves.

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
