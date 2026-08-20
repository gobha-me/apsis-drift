# Guided Contract Two: Origin-System Transfer

The v0.4.33 contract-two slice sends the starter craft from the Origin Station
to one distinct body in the generated origin system and back without FTL. It
reuses the system-flight, Planetfall, signal-survey, planetary-departure, and
moving-station rendezvous systems already proved by contract one and the first
intersystem mission.

## Deterministic binding

Generator version 1 selects origin-system planet ordinal 1; ordinal 0 remains
the station's home planet. The contract ID comes from mission stream ordinal 1
and the surface objective from encounter stream ordinal 0 of the selected
planet. These streams are independent of terrain, the home Signal Run, and the
first intersystem contract. Validation regenerates every ID and rejects unknown
or altered identities instead of rerolling them.

The bounded state machine is:

```text
offered -> accepted -> station_departure -> outbound_transfer
        -> target_planet -> objective_complete -> return_transfer
        -> station_rendezvous -> returned -> turned_in
```

Commands are addressed to the authoritative career tick and apply
transactionally. A wrong tick or phase leaves the state unchanged.

## Physical route and guidance

Launch begins in the moving station-relative frame. The transfer handoff
preserves the resolved physical pose and velocity, selects the fixed target,
and enters origin-system inertial flight. Direct target assist accelerates and
brakes against the moving body's ephemeris. The cockpit exposes selection,
distance, ETA, target-relative velocity, stopping distance, opening/closing or
braking state, the six-radius approach boundary, and the three-radius/low-speed
orbit-insertion gate before Enter can insert orbit.

Outside an approach boundary, `[` and `]` select 1x, 4x, or 16x. Each host
step still executes bounded 120 Hz authoritative substeps. Render cadence,
terminal encoding, and proxy throughput never enter ephemeris or flight state.

The target operation is the existing bounded surface-signal survey. After
collection, the player ascends to orbit, departs without changing the physical
pose, and retargets the home planet. Enter inside the home approach converts
the inertial pose into the moving station-relative rendezvous; docking and an
explicit station-board turn-in complete the chapter.

## Persistence and knowledge boundary

Save format 16 records the immutable contract binding, exact phase, career
tick, one phase-appropriate craft representation, and separate bounded
origin-system discovery and delta arrays. Before turn-in, the original arrays
retain the completed home-contract history. Turn-in promotes the home and
target records, in that order, to the revealed origin-system career knowledge
without exposing any unrelated generated system.

The canonical acceptance replay round-trips saves at outbound transfer,
time-scaled cruise, target approach, objective completion, return transfer,
station rendezvous, and turn-in. It replays at two presentation cadences and
requires identical authoritative reports, final saves, and application
framebuffers. Run it with:

```bash
./build/apsis-drift --origin-system-contract-acceptance \
  --profile remote --report origin-system-contract.json \
  --snapshot origin-system-contract.ppm
```

Arbitrary route planning, procedural mission selection, fuel, encounters,
moons, N-body transfers, and a generic mission engine remain outside this
contract.
