# Origin Station Mission Board

Version 1 presents exactly one deterministic first intersystem contract at the
origin station. It is a bounded onboarding system, not a general mission
generator, economy, shop, faction, or NPC framework.

## Stable offer

The offer binds independent generated identities from the universe seed:

```text
system/0 -> settlement/0 -> mission/0
system/1 -> star/0
         -> planet/0 -> encounter/0 objective
```

The objective ID is the existing first surface-signal identity on the target
planet. Binding it does not generate terrain, consume catalog state, or change
planet, terrain, signal, station, or mission streams. Loading validates every
stored identity against deterministic regeneration; a mismatch is corrupt or
incompatible state.

## Semantic presentation

`MissionBoardSnapshot` supplies the origin station, mission, destination
system, target planet, objective, return destination, textual status, primary
action, launch authorization, rule profile, profile explanation, and selection
lock. Kitty and ANSI render that same snapshot, so no mission state or warning
depends on pixels or color alone.

A fresh career begins `OFFERED` with `ACCEPT CONTRACT`. Acceptance is an atomic
`offered -> accepted` transition at the authoritative universe tick. Repeated,
out-of-order, wrong-tick, or corrupt-reference actions are rejected without
mutation. The accepted board reports `LAUNCH ROUTE AUTHORIZED` and exposes
`LAUNCH`; the flight deck then owns the explicit profile-aware jump command.
Sub-light system-space craft movement and the target-planet orbital handoff are
implemented by the versioned system-flight path.

Left and Right switch between `ASSISTED` and `PILOT` while the mission is
offered or accepted at the station. Launch locks the selection for the active
mission. Assisted is the fresh-career default; the descriptions
remain textual and information-complete on Kitty and ANSI. The complete
profile contract is recorded in [Deterministic Rule Profiles](RULE_PROFILES.md).

The same semantic model names active, objective-complete, returned, and
turned-in states for later loop stages. A returned contract exposes
`TURN IN CONTRACT`; turning it in remains a distinct explicit transition.

## Persistence and compatibility

Fresh careers use save format 15 and persist the high-level
`IntersystemContractState`. Formats 1 through 13 are unsupported alpha saves
and are rejected before mission state is decoded or applied.

The save excludes terminal capabilities, render profiles, menu selection,
camera state, ephemeris caches, and animation progress. See
[Save Format and Compatibility](SAVE_FORMAT.md) for the wire contract.
