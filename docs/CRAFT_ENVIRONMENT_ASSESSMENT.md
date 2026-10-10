# Fixed starter environmental assessment

Issue #102 uses the registered starter/Wayfarer frame and immutable
[ambient truth](PLANET_AMBIENT.md). This version adds a pure reference query
and a **Surface conditions** action in the native paused controls menu.
The query separates shielding/thermal, structure and propulsion. The player
sees only ratings supported by their existing observed knowledge.

## Version 1 envelope

These are fictional equipment ratings selected for the Freedom starter,
independent of future upgrade tiers. They describe conservative surface
exposure envelopes; they do not assert live weather or simulate material
failure. The frame's existing mass, thrust, pressure, gravity and support
ratings retain their exact definitions.

| Margin | Fixed starter rating | Axis |
| --- | --- | --- |
| Temperature | 180–450 K | Shielding/thermal |
| Unshielded radiation envelope | 1,000,000 nSv/hour | Shielding/thermal |
| Electrical field envelope | 10,000 V/m | Shielding/thermal |
| Pressure | Registered frame maximum, currently 2,500 mbar | Structure |
| Gust envelope | 30 m/s | Structure |
| Ground acceleration | 1 m/s² during touchdown | Structure |
| Support load | Minimum individual registered pad rating, currently 160,000 N | Structure |
| Gravity envelope | Registered frame maximum, currently 18 m/s² | Propulsion |
| Vertical thrust | Registered positive-Y thrust minus full dry weight | Propulsion |

Each upper limit subtracts the actual generated demand from rated capacity.
The lower temperature margin subtracts 180 K from the generated minimum.
Temperature headroom is measured against the 270 K rated span. Ground load
conservatively applies full dry weight plus the full ground-acceleration
envelope to each support, matching the existing touchdown convention.
Nominal milli-g converts to mm/s² using exact 9.80665 m/s² with upward integer
rounding; forces use millinewtons. No host math library or random draw enters
this query.

A negative margin is **INSUFFICIENT**; nonnegative headroom below ten percent
of its reference capacity is **MARGINAL**; ten percent or more is **SAFE**.
Equality with a maximum therefore remains within its rated envelope but is
MARGINAL. Vertical thrust must strictly exceed weight. The worst applicable
margin determines its axis and the aggregate. A known insufficient margin
remains INSUFFICIENT even when another component is UNKNOWN.

Orbital operation checks the registered operation bit without applying
surface climate. Entry, surface flight and ascent evaluate conservative
surface temperature, radiation, electricity, pressure, gust and propulsion
references. Touchdown also evaluates ground acceleration and support loading.
Actual entry heating, orientation, velocity, slopes, material, pad geometry,+hull clearance and fuel remain with their existing providers. A SAFE reference
query does not certify a present landing site or an achievable flight trace.

## Knowledge and state ownership

The simulation query first resolves the actual ambient world/body owner and
registered frame. A separate ledger projection validates world ownership,
versions and clock. Physical observations unlock gravity/thrust; atmospheric
observations unlock pressure; hazard observations unlock temperatures,
radiation, electrical field, gust and ground motion. Support load requires
both physical and hazard observations. CONTACT and PROBABLE readings retain
UNKNOWN; missing margins expose neither their values nor their signs.

The native controls menu queries touchdown and return-ascent ratings on
demand. It does not poll the hazard resolver every render frame. Opening the
survey preserves pause, controls, complete save state and the authoritative
clock. Historical saves without a selected knowledge/ambient owner report
that the survey is unavailable. Querying neither selects a new recipe nor
grants observations.

Existing save bytes, flight behavior, generated worlds and seeds stay intact.
Assessment version 1 is explicitly supplied to the pure API. Before enforcing
new commitment policy, a saved consumer must explicitly select that policy;
old saves must not acquire new operation restrictions through a load.

## Remaining #102 acceptance

This is the shared query and native warning foundation. #102 remains open for
explicit saved policy selection, composed Assisted commitment refusal,
instantaneous flight/terrain assessment and the actual safe reference flight.
Pilot overrides need a complete deterministic consequence before admission.
Declared-loss recovery alone does not authorize inventing damage or fatality
rules. Suit capability/endurance and surface locomotion remain #199.

`craft-environment-contract` checks owned and reference environments, missing
and resolved knowledge, invalid inputs and clock limits, independent axes,
threshold edges and a bounded generated matrix under both compilers. Native
`freedom_start` checks redaction, invalid-operation refusal and unchanged
flight state through the real bridge.
