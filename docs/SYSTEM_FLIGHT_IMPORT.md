# Same-tick system-frame flight import

Format 35 admits a complete, explicitly selected Freedom flight represented in
system-inertial coordinates. Continue authenticates its existing owner and
translates it once into the selected planet's nonrotating frame at the source
simulation tick. The existing native session, force model and tangent projection
then consume the canonical flight. Import does not advance time, neutralize or
invent controls, relocate to an arrival menu, or construct another universe.

This is a bounded system-to-planet consumer under #200. It does not add a live
reverse departure-selection policy, system-frame force integrator, automatic
altitude gate, player frame-debug control, FTL transform or terrain-contact rule.
The wider handoff issue remains open.

## Input contract

The root retains the complete ordinary flight fields: `application`,
`application_version`, `format_version`, `mode`, `recipe`, `state`, `flight`,
`flight_model`, and mandatory `world_owner`. Unknown fields and duplicate keys
refuse. `format_version` is 35 and `state.location` is `system_flight`.

- `recipe` and `state` carry the existing validated Freedom universe, individual
  starter identity, discoveries, world deltas and history clock.
- `world_owner` version 1 explicitly selects a system and planet in the existing
  bounded origin/neighbor pair. Physical catalog and ephemeris are both 2.
- `flight_model` retains existing central-body, atmosphere, rotation and hold
  versions, assistance, and a mandatory `terrain_policy` of 1 or 2. No default
  force body or terrain selection is inferred.
- `flight` is the existing physical rigid-v2 projection with `system_inertial`
  axes, null planet/station fields and Wayfarer frame 2/version 1. Its catalog
  identity and tick must match the authenticated world/history.

Bare rigid JSON has insufficient voyage identity and is refused. Format 35 is
not a nested voyage format: historical flight, docking, surface, resource,
knowledge, travel and recovery wrappers cannot silently opt into this import.
Replacement lineage requires its recovery owner and is not admitted by this
standalone input.

Closed fields, bounded JSON/nesting, finite canonical rigid values, all owner
and generator versions, advanceable matching ticks, the selected force body and
hold-target parameters are checked before translation. Complete post-conversion
atmosphere/orbit qualification uses unchanged ordinary flight hydration. Errors
leave source bytes and the accepted native selection unchanged.

## Translation and persistence

The existing `reframe_rigid_body` provider subtracts the selected planet's final
same-tick physical ephemeris position and velocity exactly once. The axes remain
system-aligned: full quaternion, body-resolved spin, craft recipe and tick retain
their bits. The completed destination is canonicalized and validated; arithmetic
failure never clamps or resets motion.

Distant-origin roundtrips can lose low coordinate bits. The existing translation
contract bounds component error by `4 * epsilon(binary64) *
max(1, abs(original), abs(ephemeris_offset))`; it does not promise bit identity for
small relative position/velocity after subtraction. Save/Continue starts from
the converted canonical state and subsequently reproduces exact controlled
steps under the qualified compiler environment. #255 retains wider cross-host
math-library compatibility ownership.

Import is read-only. Ordinary explicit Save As writes the current canonical
format 26 for terrain policy 1 or 31 for policy 2, with `planetary_flight` location.
It does not preserve format 35 as a second runtime frame or rewrite the source
file implicitly. Historical accepted bytes, defaults and strict refusal of
system-state relabelling in formats 18/26/31 remain unchanged.

## Verification

`freedom-flight-import-contract` checks malformed inputs first, old-format and
nested-import refusal, origin/neighbor ownership, both terrain policies,
independent final-ephemeris subtraction, attitude/spin/tick preservation,
scale-qualified cancellation bounds, real controlled 120 Hz stepping,
Save/Continue, and native body/star/station projection equivalence.

The `apsis-drift-freedom-flight-import-tests --fixtures DIRECTORY` exporter
writes a real complete `system-import.json`, independently constructed
`canonical-before.json`, a neutral 120 Hz `canonical-after.json`, and a rejected
`malformed-import.json`. These support both-bridge native Continue tests without
new asset generation or a presentation-owned world.
