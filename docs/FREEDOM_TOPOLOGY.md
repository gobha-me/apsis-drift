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

## Optional four-system region

New Game offers **Four charted systems** under **Starting charts**; **Nearby
systems** preserves the original pair. The separate
[planetary-chart choice](FREEDOM_PLANET_TRAVEL.md) adds navigation facts over
these same generated systems. Continue always retains the saved selection. The
new finite network recipe 1 keeps Home and the original neighbor exactly, then
adds Waypoint A and Waypoint B on the same integer light-second coordinate grid.
It uses the appended seed domain 19; existing streams and catalogs do not change.

For legacy separation D, let h=floor(D/2), q=floor(D/4), e be its signed cardinal
axis and p the next positive cyclic axis. Home is 0, the neighbor D×e, A=h×e+q×p,
and B=h×e−q×p. Every leg is within the existing starter drive rating. The
application authenticates the entire regenerated network before lookup. Chart
distances use an exact integer ceiling in metres; actual jump targeting also
includes the craft's source position and destination arrival offset.

The starting grant supplies four resolved anchors, with no remote planetary
catalog, visit or sensor reading. Travel remains one selected leg at a time.
Three charges support Home → A → B → Home; a tour of all four systems returning Home
requires four jumps. Origin remains the only station and free replenishment
requires physical attachment. There are no extra services, missions or upgrades.

Further region expansion and automatic routing remain separate
[scope decisions](https://github.com/gobha-me/apsis-drift/issues/229).

## Discoverable systems

The explicit **Four charted + two uncharted systems** New Game choice adds two
initially unknown generated systems to the existing four-node region. Continue
retains its saved choice. Recipe2 embeds the original four nodes exactly, adds
two nodes through independent seed domain22, and preserves every prior recipe.
The added anchors use the remaining perpendicular axis, h×e±q×r. All fifteen
links retain the integer light-second grid and existing starter range bound;
actual jump eligibility still uses the craft's physical source and arrival.

**Controls → Search nearby stars** starts a ten-second acquisition during
ordinary space flight above the atmosphere. Resume explicitly. Menus freeze
the shared clock; unsuitable flight holds progress. Stop, committed jump or
craft replacement discards unfinished acquisition. Save/Continue retains partial
progress. The next unknown contact is chosen deterministically from the current
system by anchor distance, with system identity breaking ties; reopening a menu
cannot reroll it.

Completion records one resolved stellar navigation fix with source craft,
source system and acquisition ticks. That system then appears in the chart.
It grants no visit, remote planets or surface findings. Actual arrival and the
existing local observer earn those facts. Completed fixes survive ordinary
recovery. A second search can find the other contact; an exhausted region
reports no further detections rather than inventing more systems or resources.

Knowledge recipe6/chart4 grants the original four systems and their planetary
navigation facts, while the extra systems remain hidden. C++ retains the
private generated catalogs and acquisition targets; Godot receives only known
chart rows and bounded progress. Guessed system IDs cannot authorize a native
jump. Closed knowledge persistence binds the region, craft and source world;
completed search provenance and learned anchor facts must agree.

Travel uses the current drive, one charge per committed leg, spool/transit and
selected recovery/strain. Home → Frontier A → Frontier B → Home uses three
charges. Fuel service still requires physical Origin docking; no extra station,
free return, probe, relay, mission or economy is added. This finite prototype
does not define the eventual universe's size or exploration ceiling.

## Derelict inspection

New **Four charted + two uncharted systems** journeys explicitly select one
unpowered Wayfarer in the initially unknown region. Continue preserves absence
in older journeys. Independent seed domain23 determines its identity and
circular orbit around the first ordinary planet in the first added system;
the existing catalog, clock and gravity own that orbit. No asset or world
position is invented by the presentation layer.

During ordinary seated flight above an ordinary planet's atmosphere, open
**Controls → Scan local space**. The scan acquires a contact only in its actual
parent-planet frame, within 100,000 km. Unsuccessful scans reveal no remote
identity or coordinates. An earned contact shows a bearing, distance and relative
speed; fly there manually. **Inspect derelict exterior** becomes available at
100–500 m with relative speed at most 20 m/s. These instantaneous observation
actions add no elapsed time, fuel or navigation facts. Resume explicitly to fly.

The short exterior condition note persists through Save/Continue and ordinary
craft recovery. The hull has no collision response, boarding route, repair,
towing or ship sale. The separately selected [recovery prototype](DERELICT_RECOVERY.md)
can retrieve one service canister; ordinary inspection and older journeys have
no cargo or sale interaction. Observe
the indicated stand-off. The admitted Wayfarer model has retracted gear and no
pilot or exhaust; its pose comes from C++ at the committed tick. Retained notes
do not expose a moving remote pose when visiting another body.

**Recorded observations** also keeps an acquired contact and its earned exterior
history readable at the station and in all flight contexts. These are retained
notes, without a live pose, distance or new scan. An unknown hull contributes
no archive entry; reading never performs the inspection.
