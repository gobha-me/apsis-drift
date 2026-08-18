# Deterministic Intersystem Mission and Travel Contract

Version 1 defines the authoritative boundary for the first complete
station-to-system contract loop. It records identities, time, coordinate
ownership, legal travel phases, and save implications before the individual
generation, rendering, flight, and mission-board systems implement them. It is
not a galaxy generator, a general mission framework, or a flight engine.

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

Seed domains `star=8`, `orbit=9`, and `mission=10` are additive version 1
inputs. Planet descriptor identity continues to use the existing `planet`
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

Version 1 jump timing is intentionally short and explicit:

- spooling lasts 360 ticks (three seconds) and may be canceled;
- committed transit lasts 240 ticks (two seconds);
- commitment binds the destination and later arrival pose before any transit
  presentation begins;
- committed transit cannot be canceled or rerolled;
- invalid preconditions are a refused command and never a committed failure.

The committed state has no meaningful continuous interstellar position. It
simulates a bounded deterministic transition and advances authoritative time;
camera motion, flashes, skipped frames, and no-animation/headless completion
are presentation choices. Assisted arrival uses the optimal corridor that #85
will specify. The optional Pilot profile may choose another deterministic pose
inside the same correct target system, but cannot change its identity.

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
It is neither the system barycenter nor the universe origin. Docking uses a
bounded rendezvous predicate and confirmation; detailed docking physics remain
outside this contract.

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
| `origin_system_flight` | Origin-system inertial craft state. |
| `outbound_jump_spooling` | Origin system; cancelable timer. |
| `outbound_jump_committed` | Origin system plus bound target destination and commit tick. |
| `target_system_flight` | Target-system inertial craft state. |
| `target_planet_flight` | Target planet and existing orbital/atmospheric/terrain state. |
| `return_jump_spooling` | Target system; cancelable timer. |
| `return_jump_committed` | Target system plus bound origin destination and commit tick. |
| `origin_system_return` | Origin-system inertial state with station rendezvous available. |

The legal first-loop sequence is:

```text
offer -> accept -> launch
-> outbound spool -> commit -> target arrival
-> target approach -> planet entry -> objective completion
-> planet departure -> return spool -> commit -> origin arrival
-> station rendezvous -> dock -> turn in
```

Spooling may return to flight in its source system. Planetary flight may return
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
No first-loop recovery requires fuel, repair, damage, or a new objective. Pilot
thermal consequences may later make recovery harder, but they must remain
bounded and cannot turn a valid location into corrupt mission state.

## Save projection and compatibility

The v0.4.7 mission board introduces save format version 3 and records:

- generator versions for the system catalog, ephemeris, and first mission;
- stable origin/target system, star, mission-planet, station, mission, and
  objective IDs needed to validate regeneration;
- `universe_tick`, mission phase, current system, and the active craft-location
  variant;
- the high-level active location and jump phase fields defined here;
- existing discoveries and sparse world deltas unchanged.

Camera state, terminal capabilities, render profile, ephemeris caches, transit
animation progress, interpolation remainder, and cockpit formatting are never
save state.

Formats 1 and 2 remain readable. They retain their saved origin-system planet,
flight state, objective, discoveries, and deltas as a legacy local Signal Run.
An in-flight legacy save resumes in flight; a docked save resumes docked. It is
not moved to system ordinal 1, assigned a generated jump, or given synthetic
mission progress. Legacy careers remain explicitly local in v0.4.7;
conversion into the intersystem career is not synthesized. Older readers must
reject format 3 before discarding any of these fields.

## Implementation boundaries

- #82 generates the bounded target system and analytic ephemeris described in
  the [local-system generation contract](LOCAL_SYSTEM_GENERATION.md).
- The v0.4.6 local-system presentation renders that authoritative state and
  derives target navigation without adding travel physics.
- #84 binds and presents the first mission.
- #85 implements the Assisted jump and arrival solution.
- #86 owns system-space craft flight and the planet handoff.
- #87 preserves entry-anywhere Planetfall and objective identity.
- #88 implements explicit return, docking, and turn-in.
- #89 composes the complete deterministic acceptance path.

None of those systems may move terminal protocol handling into the game or
extract a generic engine before repeated working systems demonstrate one.
