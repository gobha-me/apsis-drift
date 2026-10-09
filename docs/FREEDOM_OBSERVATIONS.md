# Local ship observations

For the explicit active-world/travel extension, see [Native First Jump](FREEDOM_NATIVE_TRAVEL.md). The historical recipes described below retain their meanings.


The minimal #189 implementation selects knowledge recipe **2** with explicit
`observation_policy=1` for new native games. Format 25 retains the same outer
knowledge/resource/voyage ownership; another wrapper is unnecessary. Recipe 1
remains exact and has no automatic sensors. Continue never upgrades a recipe,
samples a scene or invents an arrival. Existing formats 17–24 remain unchanged.

## Starter sensor model 1

This is a compact game abstraction: an omnidirectional ship sensor infers coarse
planet models from authoritative geometry. It has no camera, framebuffer,
viewport, render time, target focus, progression, inventory or unsaved dwell
counter. Later equipment/spectral/occlusion fiction needs a new explicit policy.
It does not claim a physically accurate observation simulator.

The cockpit chart shows the number of locally recorded findings, excluding
granted chart facts. Its rows mark an actually visited system independently.
This is a ledger summary, not a complete survey archive or body renderer.

The pilot must be aboard. Committed simulation ticks divisible by 120 sample
at one-second intervals; tick zero does not sample. Time compression executes
the same integer ticks and cannot skip or duplicate these samples. Walking or
boarding on the station does not operate an unmanned ship scanner. Queries,
chart browsing, Save As and Continue have no observation side effects.

For each existing origin planet, actual ship-to-center distance is measured
using continuous same-tick ephemerides. Calculations stay relative to the home
planet, preserving its close-range precision. Body radius sets inclusive bands:

| Distance | Identity/orbit/physical/atmosphere | Hazard/landing model |
| --- | --- | --- |
| Greater than 100 radii | No observation | No observation |
| At most 100 radii | CONTACT | CONTACT |
| At most 10 radii | PROBABLE | CONTACT |
| At most 2 radii | RESOLVED | PROBABLE |
| At most 1.02 radii | RESOLVED | RESOLVED |

Only the strongest reading for a fact is emitted at a sample. These provisional
design bands are selected by policy 1, not implicit changes to older saves.
Confidence can skip levels when a close approach supplies stronger evidence.
Each reading retains the actual starter craft/source identity and committed tick.
Unknown distant planets remain absent, while CONTACT/PROBABLE measurements
remain redacted through #175. An inefficient approach can discover another
existing nearby planet without moving it or improving its generated conditions.

Resolved atmosphere is the model's surface pressure/class, not instantaneous
local pressure. Resolved hazards are the immutable coarse ambient envelopes;
resolved landing is terrain character/water coverage. These are inferred
planet models, not live weather, certified engineering safety, a complete survey,
qualified landing pads or a guaranteed route. #102/#202/#253 retain those
assessment/contact/consequence responsibilities. Sample collection and detailed
survey operations remain future scope.

## Actual presence and transactions

Free-flight samples record physical presence in the actual origin system.
A validated landed anchor records presence on its planet; a validated supported
port attachment records presence at Origin Station. Touchdown and port capture
sample immediately through their committed action, even between periodic ticks.
Knowing properties or setting a current-system context never grants presence.
Repeated samples, landed idle, ascent and returning to the same station retain
the original presence tick and confidence history.

The pure provider validates the complete voyage, selected physical/knowledge
owners, clock and event before returning a ledger candidate. Native flight,
resources and knowledge commit together; a refused pose, stale touchdown or
capture cannot leave behind discoveries. Safe landing uses actual certified
terrain/support geometry rather than the nominal planet-radius sphere. This
observer adds no collision clamp, death, repair, resource grant or damage law.

There are at most six existing planets. Sparse knowledge stays bounded by the
existing 128 facts/four transitions/64 KiB contract. No sensor-local cursor or
generated catalog is saved. Save/resume immediately around a sample converges
on the same ledger and source ticks.

## Scope and verification

The local provider covers the implemented origin bodies and actual starter
flight/landing/attachment. Native neighboring arrival waits for #193/#176.
Signals, probes, delayed relays, replacement-craft lineage and recovery stay
with their owners. Full knowledge-filtered body rendering remains deferred
#215; this change does not certify every existing telemetry/render consumer.

`freedom-local-observations-contract` covers fixed seeds, confidence/redaction,
proximity edges, independent facts, off-route discovery, cadence, repeat and
save/resume, invalid state/owners/versions/clocks and native flight transactions.
`freedom-observed-surface-lifecycle` uses real generated contact geometry for
touchdown, landed idle, thrust liftoff and Save As/Continue. Retained recipe-1
knowledge goldens and historical physics/resource traces remain independent.
