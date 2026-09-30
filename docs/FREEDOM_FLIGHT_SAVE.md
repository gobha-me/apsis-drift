# Explicit Freedom flight save format

2026-09-30, #328, format **18**. This is application-owned canonical flight
persistence and C++ selection. Existing career format16 and docked Freedom
format17 retain their exact meanings and encoders. New Game still creates a
docked format17 document. The current Godot station shell explicitly refuses
format18 flight until its separate live consumer is connected; this provider
alone does not close #291 or implement an undock/landing/jump transition.

## Persisted authority

`FreedomFlightSaveDocument` retains the unchanged origin recipe, starter craft
instance identity, discovery/world-delta history and the canonical flight state.
The serialized location is **planetary_flight**, never docked_at_origin.
`state.station_id` retains the original station identity, not docking status.
The origin-history clock and rigid-state clock must match exactly. A flying
clock must remain advanceable; max-u64-minus-one and max-u64 refuse.

The first domain is the generated **physical origin home planet**, on its
nonrotating planet-relative frame. The recipe's active body remains that home
planet. Other bodies, station-relative/rotating/system frames and unsupported
catalog families refuse; no state is converted by matching a numeric seed.
A future interbody/system transition requires its own explicit state contract.

The document retains these model selections, without silently defaulting an
omitted field to current behavior:

| Selection | Current supported value |
| --- | --- |
| Physical catalog generator | 1 |
| Physical ephemeris | 1 |
| Physical rotation owner | 1 |
| Physical rotation generator | 1 |
| Central-body dynamics | 1 |
| Atmospheric flight | 1 |
| ORBIT HOLD controller | 1, optional explicit target |
| Assistance | Explicit boolean runtime choice |

The saved physical catalog/rotation versions plus exact universe/home identity
rederive their immutable generated recipes. The nested rigid **v3** projection
also preserves and qualifies its complete physical owner. No arbitrary caller
spin/tilt or current-default regeneration is admitted. Every generator/owner
selection must match its supported recipe. Atmosphere derives the shared space
boundary from that qualified body; a second arbitrary boundary is not saved.

The optional hold target retains planet identity, finite radius and signed unit
plane normal. Loading validates it with a **pure** C++ hold qualification query;
it never integrates a tick merely to validate a target. Assistance and hold
selection remain independent: Advanced may retain a paused target. No last input,
filter, controller accumulator or hidden rail/capture state is required.

## Strict codec and file behavior

The outer object has exactly eight fields: application, application_version,
format_version, mode, recipe, state, flight and flight_model. Identity/history
validation reuses the existing strict format17 owner internally; the original
format18 location and mandatory flight/model objects are independently checked.
The nested rigid projection preserves its exact decimal binary64 representation.
Hold numerical values round-trip through the existing JSON library's binary64
encoding, then require the same finite/canonical target contract.

Unknown/missing/duplicate fields, bad types, unsupported versions, mismatched
identity/owner/clock, noncanonical signed zero, non-finite state and unsafe
geometry refuse. Document size retains the existing 1 MiB limit. A damaged
format18 document cannot fall back to format17, a career or a study universe.
Explicit format dispatch leaves supported v16/v17 to their existing decoders.

`write_freedom_flight_file_atomically` validates/encodes before using the **same
existing C++ temporary-file, sync, atomic replacement and directory-sync owner**.
A refused encode cannot replace destination bytes or leave a temporary file.
`load_native_save_file` returns a distinct flight variant; `native_continue`
retains all selected state and the actual source path without rewriting it.
The current station bootstrap returns an actionable unsupported-flight error,
preserving any existing bridge session. It never treats the origin record as a
docked craft or replaces flight with a zero-clock fixture.

This codec does not authorize or construct a port release, teleport spawn,
physical capture, free propulsion, movement gate or mission/economy state.
Those transitions belong to a later persistent session and docking/contact owner.

## Qualification

Strict malformed/version/type/duplicate tests cover outer and nested selections,
all 13 non-finite rigid scalars, invalid hold radius/normal/body, stale clocks and
unsupported frames. Existing destination bytes survive rejected writes; source
bytes survive Continue/refusal. Actual atomic files retain supported legacy16,
docked17 and flight18 as distinct variants.

Physical home seeds 0, 42 and maximum u64 exercise three uninterrupted/resumed
360-tick traces: atmospheric flight, gravity/coasting and explicit bounded hold.
Files are written and continued at ticks within mixed assistance, translation
and rotation inputs. Full identity/history/model/state, exact binary64 state
bits, environmental/propulsive impulses, hold status and orbital observations
match the uninterrupted traces. Provider replay goldens remain independent.

The native runner builds an actual C++ flight-save fixture, then verifies that
the current docked bridge/shell refuses it without replacing the selected
session or altering its source. This is an unsupported-consumer boundary test,
not a claim that ordinary Godot play now flies or saves a completed Freedom trip.
