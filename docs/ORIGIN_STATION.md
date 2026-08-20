# Origin Station and Tutorial Home

Origin Station version 2 gives every fresh universe one deterministic,
tutorial-safe home planet and one analytic station orbit without making either
the physical or procedural center of unrelated content. It is a bounded first
system, not an N-body simulation or a generic celestial hierarchy.

## Deterministic identity

The origin station is derived through one fixed hierarchy:

```text
universe seed
`-- system domain, ordinal 0       (origin system)
    |-- planet domain, ordinal 0       (tutorial home)
    `-- settlement domain, ordinal 0   (origin station)
```

The station ID is the resulting 64-bit settlement seed. Its canonical textual
form for diagnostics and future saves is `station-` followed by 16 lowercase
hexadecimal digits. The seed-derivation version and origin-station generator
version must accompany any saved recipe that relies on this mapping.

The station is a player-facing home and navigation anchor. It is not the
universe root, system barycenter, coordinate origin, or parent seed for
planets, terrain, weather, settlements elsewhere, or encounters. Deriving it
does not consume mutable random state. Version 1 golden vectors and
non-perturbation checks are executable in `test/test.cpp`.

The setting calls its containing system the **origin system** or **home
system**. This contract does not identify it with the real Sol system and does
not assign the station a lore name.

## Tutorial-safe home

Origin planet ordinal zero retains the ordinary `planet/0` seed and stable ID.
Origin-home generator version 1 uses new permanent child-stream ordinals to
constrain only its role-specific descriptor:

| Property | Inclusive envelope | Starter margin |
| --- | ---: | --- |
| Radius | 5,000–6,500 km | Bounded horizon and ascent scale |
| Surface gravity | 0.750–1.100 g | Avoids extreme landing or ascent loads |
| Atmosphere | Temperate, 700–1,200 mbar | Avoids airless or crushing entry cases |
| Terrain | Plains | Avoids an all-rugged or volcanic landing tutorial |
| Water | 10–45% | Guarantees substantial land while retaining seed variation |

The normal planet generator still supplies the stable name and palette. Other
planet ordinals, terrain, weather, encounters, signals, missions, and later
systems retain their existing seeds and generators. The home is known starting
infrastructure and never creates a player-earned discovery or world delta.

## Station orbit and ephemeris

The station identity remains independently derived from `settlement/0`.
Station generator version 2 adds the home `PlanetId` and independent orbit
streams for altitude, period, epoch phase, and orientation. Altitude is
400–600 km above the generated home radius, period is 90–120 minutes at the
authoritative 120 Hz clock, inclination is within ±5 degrees, and epoch phase
and ascending node cover a complete unsigned 32-bit turn.

`resolve_origin_station_ephemeris()` reduces the authoritative tick modulo the
station period before evaluating the circular planet-relative orbit. It then
adds the host planet's analytic system-space position and velocity and
quantizes position to metres and velocity to millimetres per second. One
station period therefore repeats the host-relative pose exactly, including at
maximum 64-bit ticks; absolute system position continues to follow the host.

Launch pose, free-flight rendering, the exterior marker, home-jump arrival,
return pose, range/relative-speed guidance, and docking all consume this same resolver.
Docking requires distance no greater than 5 km and relative speed no greater
than 25 m/s at the current tick, so an obsolete station position cannot pass.

## Origin-system free flight

The intersystem mission launches into an application-owned station-relative
craft state five kilometres along the station's positive-X corridor, with its
velocity matched to the station. The ordinary fixed-step flight controls,
assist, and cockpit guidance are active before the outbound jump. Enter may
redock inside the bounded rendezvous envelope without advancing mission
progress; J freezes the current craft and begins the cancelable jump spool.

Pilot alignment adds the live station-local yaw and normalized relative speed
to its independently seeded base sample. During outbound spool the craft does
not integrate; cancellation retimes the unchanged relative state to the current
universe tick. This keeps moving-station ephemeris, craft physics, and jump
alignment deterministic without consuming a shared random stream.

## Fresh-universe flow

A universe with zero discoveries begins docked at the origin station. The
station is known home infrastructure, not a discovery entry. Exactly one
bounded `FIRST SIGNAL RUN` offer is visible.

The onboarding sequence is:

1. **Offered, docked:** the briefing identifies the first objective and the
   origin station as the return destination.
2. **Active, docked:** explicit acceptance selects the objective and enables
   launch.
3. **Active, in flight:** launch enters the existing orbital-to-surface loop.
4. **Completed, in flight:** the signal interaction has completed, but the run
   is not finished until the player returns.
5. **Completed, docked:** the player has returned to the origin station and the
   first run is complete.

Launch is rejected until the offer is accepted. Completion is rejected unless
the objective is active in flight. Return is rejected until the objective is
complete. Repeated or out-of-order actions do not change state.

For the first slice, `return_to_origin` is a presentation-independent arrival
signal emitted after the craft has navigated back to the tick-resolved station
rendezvous and the player confirms return. Traffic, collision/damage, orbital
decay, and walkable interiors remain outside this contract.

## Signal Run handoff

The single offer is a narrow bridge into the milestone work:

- #21 supplies its deterministic signal target and stable target identity;
- #22 presents bearing, distance, strength, and target selection in flight;
- #23 changes the active objective to completed and emits exactly one world
  delta;
- #24 drives the complete launch, descent, scan, return, save, and resume
  acceptance path.

No job-board collection, reward economy, shops, crafting, NPC simulation, or
general mission type is implied. If later work demonstrates those needs, it
must introduce them behind a separate boundary.

## Save implications

Save format 14 separates the generated recipe from mutable state:

| Concern | Ownership and compatibility rule |
| --- | --- |
| Universe seed and generator versions | Saved recipe; regenerates the origin system, tutorial home, and station identity. |
| Home and station orbit | Saved IDs and integer orbit recipe, validated exactly against regeneration. |
| Origin station ID | Saved stable reference and validated against regeneration. A mismatch is an incompatible/corrupt save, not a reason to move the player. |
| Active origin craft | Saved as station-relative position and velocity during outbound free flight, its frozen spool, and the return approach. |
| Docked or in-flight location | Mutable session state. Loading must restore it exactly. |
| First objective status and target ID | Mutable mission state. A target ID is required once #21 binds the offer. |
| Discoveries and collected/completed deltas | Mutable sparse journal state; never folded into station generation. |

There are no released Apsis Drift save files yet. Existing Planetfall fixtures
and interactive starts remain in flight and are not reinterpreted as docked
state. A future compatibility loader handling a pre-onboarding representation
must preserve that in-flight/no-objective behavior rather than silently
teleporting the craft or synthesizing mission progress.

The format-14 field encodings and validation behavior are specified in the
[Save Format and Compatibility](SAVE_FORMAT.md) contract.

## Presentation decision

The first docked presentation is a shared semantic cell panel used by both
negotiated Kitty graphics and supported truecolor ANSI paths. It must expose,
without relying on color alone:

- the `ORIGIN STATION` role and stable station ID;
- `FIRST SIGNAL RUN` with offered, active, or completed text;
- separate `ACCEPT BRIEFING`, `LAUNCH`, and `RETURN COMPLETE` cues as they
  become valid;
- the station as the return destination before launch.

Kitty may additionally show a station silhouette in the exterior pixel region.
The smallest useful representation is an opaque, code-authored primitive or
voxel silhouette rendered through the existing application-owned pixel path;
the ANSI half-block path can present the same pixels. This does not require
encoded media, RasterForge, transparent layers, named terminal images, or new
TermForge protocol features. Static art and richer docking presentation should
wait until the gameplay loop demonstrates their value.

The later first intersystem mission keeps this same station and identity as its
offer, return, and turn-in anchor. Its target system is a separate generated
child, and neither its system-inertial waypoint nor its mission seed changes
the station's established derivation. See the
[Deterministic Intersystem Mission and Travel Contract](INTERSYSTEM_CONTRACT.md).

As of v0.4.7, fresh careers present that bounded contract through the shared
[Origin Station Mission Board](MISSION_BOARD.md). Format 14 does not silently
assign the live origin craft to older alpha saves; formats 1 through 13 are rejected
without modifying their source files.
