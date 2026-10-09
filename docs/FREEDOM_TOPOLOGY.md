# Freedom starting chart and nearby topology

Decision recorded 2026-10-09 for #174, using its approved 2026-09-17 Freedom
amendment. Reuse the completed navigation-version-1 recipe from #130 unchanged:
exactly the origin and system ordinal one, separated by 172,800–345,600 integral
light-seconds (48–96 light-hours). This is the first round-trip test scope.

## Representation and compatibility

`FirstUniverseRoute` owns both stable system identities and their integer-metre
anchors. `navigation=12`, route ordinal one, and independent direction/distance
substreams remain permanent. The origin is `(0,0,0)`; the neighbor lies on one
of six signed cardinal axes. One light-second is exactly 299,792,458 metres.
The origin coordinate is a frame origin, not a claim about a galactic center or
the corporation's core worlds. Local inertial frames share axes and translate
by these anchors; their existing boundary is 100,000,000,000 metres.

The seed-42 reference is unchanged: origin `system-09683d79dbc20b52`, neighbor
`system-28630482e6b15573`, direction `-z`, separation 321,457 light-seconds /
96,370,384,171,306 metres. Maximum recipe separation is
103,608,273,484,800 metres, comfortably inside signed 64-bit coordinates.
Validation regenerates the complete route before accepting it. Altered IDs,
seeds, axes, anchors or distances, including overflow-sized values, refuse
without subtracting or squaring unchecked coordinates.

No generator version changes. Existing saves and historical onboarding retain
their selected semantics. This contract adds no save fields: a starting chart
is a read-only grant, not a visit ledger. #175 must explicitly select and
persist the chart/provenance policy when it installs mutable native knowledge;
no current save is silently awarded observations. #229 owns later spatial
topology versions. Stable identities and local/universe frame translation are
the extension boundary, without assuming later systems follow this cardinal
recipe. The seeded preview sky remains a preview, not this route catalog.

## Knowledge and selection

The Freedom starting chart explicitly grants **RESOLVED system anchors** for
both endpoints. It grants no visit, planet inventory, environmental reading,
surface map, mission, reward or earned mastery. Merely setting the current
system to the neighbor does not turn either entry into VISITED. #175/#189 own
provenance from actual observations and arrivals.

`resolve_freedom_starting_chart` consumes the valid route, current endpoint,
and actual versioned starter resource ledger. It checks the seed-derived craft
owner, keeps both known rows even when fuel is exhausted, and exposes exactly
one other-system candidate. The starter drive is installed; authorization has
no mission/onboarding dependency. One or more jump charges makes one leg
affordable. Zero charges disables it. Flight fuel does not change jump charge
affordability; a depleted craft can still face unsafe arrival or station access.
The caller separately closes selection during an active travel phase.

This is the baseline chart projection, not a general discovery query. Consumers
with CONTACT/PROBABLE knowledge use the existing redacted destination resolver,
which withholds exact anchors and distances. Unknown identities refuse without
returning any generated rows or catalogs. Focus/selection changes only pending
UI selection, never fuel, time, topology or discovery.

## Outbound, return and recovery

The installed starter's bounded in-range contract must cover this existing
pair in both directions. Each committed valid leg costs one charge: full
departure 3 → outward arrival 2 → return arrival 1. Selection, canceled spool
and refused commitment cost zero. A last charge can complete its committed
arrival, after which the known return is shown as unaffordable. Three charges
are a reserve, not free return or immunity from navigation hazards.

#193 owns rated reach, the actual ship-position distance, alignment-dependent
arrival envelope, and validity versus disclosed risk; #248 owns environment,
condition and beyond-optimum risk. This topology decision does not introduce
a universal 96-light-hour drive limit or a new hotrod/drive class. Native
commitment must validate actual endpoint offsets against its chosen reach;
anchor separation alone is not proof that every local departure pose is safe.
The retained jump/resource adapter proves charge timing and outbound/return
transitions; it is not native neighboring flight or a committed-jump save.

The origin remains known after arrival. Returning to it is subject to actual
charges and physical access. Free refill requires actual supported station
attachment under #246; map focus cannot refill or move a craft. Default loss
recovery #247 must preserve the same universe/knowledge while replacing the
lost craft at a valid safe station with the selected starter resources.
It does not move the endpoint anchors or turn route knowledge into a visit.
Living-pilot rescue, finite air and permadeath remain their separately owned
contracts; this decision does not promise an instant universal tow.

## Implementation owners and evidence

- #175: sparse persistent knowledge and explicit starting-chart provenance.
- #193/#248: mission-free physical reach, frozen arrival and disclosed risk.
- #176/#194: native chart/cockpit selection, physical handoff, and exact
  selection/spool/commit/transit/arrival/return save-resume.
- #246/#247/#251: repeatable attached service, recovery and resource coupling.

The `freedom-starting-chart-contract` test covers fixed route goldens and
bounded seed samples; both endpoints and 0–3 charges; unchanged resources and
truth; current/charge/travel disabled reasons; redaction and unknown identities;
selection without effects; and invalid owners/versions/geometry/overflow.
The retained navigation acceptance and resource round-trip tests remain
independent evidence. Native travel, risk outcomes and recovery are subsequent
implementation, not completion claims of this research decision.
