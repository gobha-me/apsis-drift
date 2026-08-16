# Origin Station and New-Game Contract

Version 1 gives every fresh universe one deterministic home without making
that station the physical or procedural center of unrelated content. This is
the onboarding contract for the v0.4 Signal Run milestone; it is not a station
simulation, mission framework, or save-file schema.

## Deterministic identity

The origin station is derived through one fixed hierarchy:

```text
universe seed
`-- system domain, ordinal 0       (origin system)
    `-- settlement domain, ordinal 0  (origin station)
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
signal emitted after the craft has navigated back to the origin-station
rendezvous and the player confirms return. The rendezvous position and arrival
predicate belong to the later navigation implementation. They must use an
explicit station waypoint and must not alias the system barycenter. Docking
physics, approach animation, traffic, and walkable interiors are outside this
contract.

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

The save contract in #18 should separate the generated recipe from mutable
state:

| Concern | Ownership and compatibility rule |
| --- | --- |
| Universe seed and generator versions | Saved recipe; regenerates the origin system and station identity. |
| Origin station ID | Saved stable reference and validated against regeneration. A mismatch is an incompatible/corrupt save, not a reason to move the player. |
| Docked or in-flight location | Mutable session state. Loading must restore it exactly. |
| First objective status and target ID | Mutable mission state. A target ID is required once #21 binds the offer. |
| Discoveries and collected/completed deltas | Mutable sparse journal state; never folded into station generation. |

There are no released Apsis Drift save files yet. Existing Planetfall fixtures
and interactive starts remain in flight and are not reinterpreted as docked
state. A future compatibility loader handling a pre-onboarding representation
must preserve that in-flight/no-objective behavior rather than silently
teleporting the craft or synthesizing mission progress.

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
