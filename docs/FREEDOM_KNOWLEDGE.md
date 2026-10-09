# Persistent Freedom knowledge

The minimal #175 implementation provides a sparse, versioned C++ ledger for
native sessions. It records what is known about existing system, planet and
station identities, while generated truth remains independent. The minimal
physical local triggers are documented under #189; broader equipment and
sensor/render presentation remains #171/#215. This ledger does not implement
a scan minigame or a new body hierarchy.

## Explicit starting chart

Knowledge recipe 1 / starting-chart policy 1 grants these **RESOLVED** facts:

- Identity and anchor of the [origin/nearby pair](FREEDOM_TOPOLOGY.md).
- Identity and orbit of the actual home planet and station host (one entry when
  they are the same planet).
- Identity, orbit/host relation and supported port count of Origin Station.

Every grant names `starting_chart`, policy ID 1, tick zero and no related flight.
It grants no physical properties, atmosphere, hazards, landing survey, visits,
signals, objectives, rewards or jump history. Missing or altered mandatory grants
are corrupt saves, not an invitation to reconstruct observations during load.
The preview sky does not populate this ledger.

The recipe explicitly selects navigation 1, physical origin catalog/ephemeris
2/2 and ambient envelopes 1. These selections must match the saved voyage.
Existing `SystemId`, `PlanetId` and `OriginStationId` have tagged subjects;
future generalized BodyId/hierarchy #206 must add a versioned interpretation,
not silently reinterpret these identities. The current subject domain is the
two systems, actual origin planets and Origin Station. #214 moons/minor bodies
and a generated galaxy are not prerequisites or hidden new subjects.

## Per-fact evidence and redaction

Identity/type, location/orbit, physical properties, atmosphere, hazards, landing
overview, station ports and physical presence are separate facts. CONTACT and
PROBABLE expose confidence/provenance without exact generated values. RESOLVED
facts expose only their own typed value; an atmosphere reading cannot unlock
hazards or landing data. Hazard readings contain the selected immutable surface
envelopes, without leaking recipe seeds or unrelated gravity/atmosphere fields.
Landing overview is terrain character/water coverage, not a qualified landing
pad or a complete surface map.

Read-only subject lists come from known identity records, never the generated
catalog. An unknown valid subject and a nonexistent subject both query absent;
no generated names or catalog rows appear in errors or sort order. The chart
keeps both granted anchors visible at zero charges and reports real affordability.
VISITED appears only from a separate physical-presence record, not from focus,
selection, current-system context or knowledge of other facts.

Local ship evidence identifies the seed-derived active starter craft as source
and related flight owner, with an authoritative tick at or before the live clock.
It may resolve a particular fact directly; confidence is evidence quality, not
an experience-point ladder. Only a physical-arrival source can record presence
as VISITED. Property facts cannot become VISITED. No mission, probe, relay or
replacement-craft source is accepted by version 1; those owners must extend the
recipe explicitly. #247 replacement identity integration remains necessary.

Recipe 1 has no automatic observations. New native games now explicitly select
recipe 2 / observation policy 1 through [local ship observations](FREEDOM_OBSERVATIONS.md).
Its starting chart and fact/provenance meanings are unchanged; the additional
policy selects authoritative local triggers. Continue preserves either recipe
without upgrade, and the original recipe-1 codec/ledger goldens stay exact.

Each fact retains up to four strictly increasing confidence transitions with
source ID, tick and flight owner. The ledger is canonically ordered and capped
at 128 facts / 64 KiB of encoded knowledge. Repeated or weaker evidence changes
nothing. Older evidence for an existing fact is ignored; local observations are
processed synchronously. Contradictory stronger evidence for the same tick
refuses. Delayed cross-system reports need a separately versioned #97 policy.
Missing identity records, unsupported subjects/facts/sources, backward or
duplicate persisted transitions, future ticks and overflow clocks refuse before
replacement. The writer returns a complete candidate; presentation has no writer.

## Native persistence and presentation

New Game explicitly selects format 25: the existing resource/voyage document
plus the knowledge recipe and sparse ledger. Continue and Save As preserve
knowledge, flight, quantities, world history, actor, boarding, attachment and
surface state together. Closed JSON, duplicate keys, byte/nesting/array limits
and integer narrowing are checked before authoritative state is replaced.
The existing atomic file owner preserves the destination on a rejected write.
Formats 17–24 remain knowledge-unselected and are not migrated during load.

The cockpit provides a controller-focusable, collapsible **Starting chart**
readout of the granted pair, confidence, anchor separation and charge availability.
Reading it cannot advance time, burn/refill fuel or award evidence. Physical
neighboring travel, route selection controls and committed-jump save/resume
remain #193/#176/#194. Existing local flight telemetry still measures the current
ship; this change does not qualify all future camera/sensor disclosure behavior.

## Verification boundary

The focused contract uses three fixed seeds including zero and UINT64_MAX,
ordered ledger goldens, per-fact redaction, real origin ambient values and
save/resume around confidence transitions. It rejects corrupt recipes,
subjects, facts, sources, clocks, JSON and forged native owners while preserving
saved bytes. Native startup, boarding and resource-only compatibility remain
covered independently. Headless chart text/focus tests do not imply GPU visual,
controller hardware, actual survey-trigger or neighboring-flight acceptance.
