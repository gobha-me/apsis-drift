# Deterministic Intersystem Mission and Travel Contract

Version 4 defines the authoritative boundary for the first complete
station-to-system contract loop. Version 1 established identities, time,
coordinate ownership, legal travel phases, and save implications before the
individual generation, rendering, flight, and mission-board systems implement
them. It is not a galaxy generator, a general mission framework, or a flight
engine.

## Bounded first route

The first route is fixed by stable ordinals, not catalog consumption order:

```text
universe seed
|-- system/0                         origin system
|   `-- settlement/0                 origin station
|       `-- mission/0                first intersystem mission
`-- system/1                         first target system
    |-- star/0                       target star
    |-- planet/0                     mission planet descriptor and identity
    `-- orbit/0                      mission planet orbital parameters
```

`SystemId`, `StarId`, `PlanetId`, `OriginStationId`, and `MissionId` are the
corresponding derived 64-bit seeds. Their canonical diagnostic forms use the
`system-`, `star-`, `planet-`, `station-`, and `mission-` prefixes followed by
sixteen lowercase hexadecimal digits. Catalog indices may locate an item but
never replace its stable ID in mission or save state.

Seed domains `star=8`, `orbit=9`, `mission=10`, and `jump_alignment=11` are
additive inputs. Planet descriptor identity continues to use the existing `planet`
domain. Keeping `orbit/ordinal` beside `planet/ordinal` allows orbital metadata
to evolve without perturbing the existing planet, terrain, signal, or local-sun
streams. Derivation is stateless, so inspecting or adding one child cannot
advance another child.

The first route deliberately has one target system, one mission planet, and
one existing Signal Run objective. Later catalogs may add destinations without
changing these ordinals or deriving them through mutable selection state.

## Authoritative time

One unsigned 64-bit `universe_tick` is the time authority for system
ephemerides, jump phases, mission transitions, and active planetary flight. A
tick is one existing 120 Hz fixed simulation step. It advances only when the
application advances deterministic simulation; pause, discarded host catch-up
time, render cadence, terminal throughput, and animation do not advance it.

An active `PlanetaryFlightState::tick` is the same value as `universe_tick`,
not a second local clock. At a system/planet handoff both representations are
resolved at that exact tick. Saving and restoring the tick therefore restores
the same body positions, daylight, and legal jump boundary.

Version 2 jump timing is intentionally short and explicit:

- spooling lasts 360 ticks (three seconds) and may be canceled;
- committed transit lasts 240 ticks (two seconds);
- commitment binds the destination and later arrival pose before any transit
  presentation begins;
- committed transit cannot be canceled or rerolled;
- invalid preconditions are a refused command and never a committed failure.

Raw batch advancement may land exactly on the next spool or committed-transit
boundary but cannot cross it. The application-owned jump tick operation is the
only path that publishes commitment or arrival, and it does so only when its
one-tick advance reaches the canonical boundary. Late transitions and every
cross-boundary batch reject without changing the contract.

The committed state has no meaningful continuous interstellar position. It
simulates a bounded deterministic transition and advances authoritative time;
camera motion, flashes, skipped frames, and no-animation/headless completion
are presentation choices. Assisted arrival uses the documented ten-radius
matched-velocity target corridor. Pilot grades fixed-point heading and velocity
alignment and binds an aligned, offset, or opposite-phase pose inside the same
correct target system, but cannot change its identity.

## Coordinate ownership and handoffs

System flight owns a finite double-precision position and velocity in the
right-handed system-inertial frame from the coordinate contract. Planetary
flight owns the existing geodetic pose, local east/north/up velocity, heading,
and flight regime. Exactly one of those craft representations is authoritative
at a time.

At an approach or departure boundary, the analytic ephemeris supplies the
planet center, center velocity, planet-fixed orientation, and angular velocity
at `universe_tick`. The rigid transform in the coordinate contract maps the
craft without changing the tick. The handoff preserves the selected planet,
arrival side, position, and velocity; it cannot place the craft above the
objective or synthesize mission progress.

The origin station is an explicit generated waypoint in origin-system space.
It is neither the system barycenter nor the universe origin. Launch creates a
station-relative craft five kilometres along the positive-X corridor with
matched station velocity. The same application-owned flight state supports
free flight before the outbound jump and the return approach. Docking uses a
bounded rendezvous predicate and confirmation; before departure it returns the
mission to accepted/docked, while after objective completion it advances the
mission to returned. Detailed docking physics remain outside this contract.

## Mission and travel state

Mission phase and craft travel phase are separate authoritative values. This
prevents a location change from silently completing or turning in the mission.

Mission phases are:

| Phase | Meaning |
| --- | --- |
| `offered` | The deterministic contract is visible at the origin station. |
| `accepted` | Target identities are bound; the craft remains docked. |
| `active` | The craft has launched toward or is operating at the target. |
| `objective_complete` | The Signal Run delta exists, but the craft has not returned. |
| `returned` | The craft has docked at the origin station. |
| `turned_in` | Explicit station turn-in completed the contract. |

Travel phases are:

| Phase | Required location |
| --- | --- |
| `docked_at_origin` | Origin system and station; no craft flight pose. |
| `origin_system_flight` | Live station-relative origin craft state. |
| `outbound_jump_spooling` | Origin system; cancelable timer with the launch-side craft frozen at spool start. |
| `outbound_jump_committed` | Origin system plus bound target destination, commit tick, and canonical target arrival. |
| `target_system_flight` | Target-system inertial craft state plus its immutable target arrival. |
| `target_planet_flight` | Target planet and existing flight state plus its immutable target arrival. |
| `return_jump_spooling` | Target system; cancelable timer retaining the target arrival needed to cancel exactly. |
| `return_jump_committed` | Target system plus bound origin destination, commit tick, and canonical origin arrival. |
| `origin_system_return` | Origin-system inertial state with its immutable origin arrival and station rendezvous available. |

The legal first-loop sequence is:

```text
offer -> accept -> launch -> free flight
-> redock and relaunch, or outbound spool -> commit -> target arrival
-> target approach -> planet entry -> objective completion
-> planet departure -> return spool -> commit -> origin arrival
-> station rendezvous -> dock -> turn in
```

Spooling may return to flight in its source system. Outbound cancellation
retimes the unchanged station-relative craft to the current authoritative tick.
Planetary flight may return
to target-system flight before or after objective completion. Every other
out-of-order, repeated, unknown, mistimed, or identity-inconsistent command is
rejected transactionally. A committed destination, mission target, collected
world delta, or completed phase is never regenerated in response to a retry.

## Entry and recovery

The mission objective supplies guidance, not an atmospheric-entry gate. Entry
at every finite valid latitude, longitude, and heading is legal, including an
early or opposite-side entry. The target signal identity and terrain remain
unchanged.

A poor entry may be recovered by atmospheric or surface travel, or by climbing
back through the existing regimes into orbit and departing the planet again.
No first-loop recovery requires fuel, repair, damage, or a new objective. At
the thermal limit, Pilot forces a bounded climb to orbit, clears held descent,
cools, and permits deliberate reentry. The consequence cannot turn a valid
location into corrupt mission state.

## Rule profiles

Version 4 retains exactly two authoritative rule profiles. `ASSISTED` is the
fresh-career default. `PILOT` opts into deterministic thermal
consequences and the implemented alignment consequences. The player may select
either profile only while docked with the mission offered or accepted; launch
locks it for the active mission. Profile commands are tick-addressed and never
change seeds, identities, ephemerides, terrain, objectives, or mission phases.

The thermal abort, arrival-quality bands, authoritative/derived state boundary,
and recovery cap are specified in
[Deterministic Rule Profiles](RULE_PROFILES.md).

## Save projection and compatibility

Current save format 16 requires the complete contract projection:

- generator versions for the system catalog, ephemeris, and first mission;
- stable origin/target system, star, mission-planet, station, mission, and
  objective IDs needed to validate regeneration;
- `universe_tick`, mission phase, current system, and the active craft-location
  variant;
- the high-level active location and jump phase fields defined here;
- the immutable destination, arrival tick, system-space position, and velocity
  bound when a jump commits;
- existing discoveries and sparse world deltas unchanged.

Origin-system free flight and return share one station-relative state. Outbound
spooling retains it frozen at the phase-start tick for exact save/resume and
cancellation. Target-system flight stores its tick, identities, inertial
position and velocity, attitude basis, controls, flight mode, and bounded time
scale.
Target-planet flight stores exact geodetic pose, local velocity, clearance,
controls, regime, transition, thermal load, and abort latch. Return spooling
retains the frozen target-system craft and immutable arrival required for exact
cancellation; origin arrival owns one matching station-approach state.

The v0.4.7 through v0.4.17 releases introduced historical formats 3 through 10
as the mission board, jump, system flight, Planetfall, return, rule profile,
Pilot alignment, and thermal state landed. Those documents remain useful
historical evidence but are not current player-save inputs. Format 14 rejects
formats 1 through 15 at the root boundary with an explicit unsupported-alpha
diagnostic and never synthesizes missing fields.

Camera state, terminal capabilities, render profile, ephemeris caches, transit
animation progress, interpolation remainder, and cockpit formatting are never
save state.

Format 14 preserves current state exactly and rejects absent arrival, craft,
profile, alignment, thermal, or journal data when the active phase requires it.
The application version that most recently wrote the file is diagnostic root
metadata and never changes deterministic contract state.

## Implementation boundaries

- #82 generates the bounded target system and analytic ephemeris described in
  the [local-system generation contract](LOCAL_SYSTEM_GENERATION.md).
- The v0.4.6 local-system presentation renders that authoritative state and
  derives target navigation without adding travel physics.
- #84 binds and presents the first mission.
- The v0.4.8 Assisted jump and arrival solution are documented in
  [Deterministic Assisted FTL Transit](INTERSYSTEM_JUMP.md).
- The v0.4.9 system-flight path owns system-space craft motion, moving-target
  guidance, and the preserved planet handoff described in
  [Deterministic Sub-light System Flight](SYSTEM_FLIGHT.md).
- #87 preserves entry-anywhere Planetfall and objective identity.
- The v0.4.12 [return path](INTERSYSTEM_RETURN.md) implements #88's explicit
  departure, home jump, docking, and turn-in.
- The v0.4.13
  [complete-contract acceptance](INTERSYSTEM_CONTRACT_ACCEPTANCE.md) composes
  #89's deterministic station-to-system verification path.

None of those systems may move terminal protocol handling into the game or
extract a generic engine before repeated working systems demonstrate one.
