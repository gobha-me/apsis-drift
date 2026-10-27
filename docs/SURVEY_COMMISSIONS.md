# Survey commissions

Optional survey work pays for a fresh orbital reference measurement or a
newly collected physical specimen. It is available in the existing
**planetary-chart**, **four charted + two uncharted systems** and
**Outer frontier: four charted systems** journeys. Older journeys keep their
charts, fuel service and movement rules.

## Orbital commissions

At Origin, pause and open **Survey commissions**. Review the already-charted
remote planet and select **Accept commission**. Nearby's primary remains the
ordinary offer. If the primary planet of **Waypoint A** is already known by
identity and location, the office also lets you review that destination. Choosing
it displays its own full terms and named Origin → target → Origin route; it
changes no navigation selection, observations or saved work. Choose the matching
system separately in Navigation chart.

The displayed two-charge plan and current shortfall are information. Zero or one
carried charge does not block acceptance; existing free replenishment or an
eligible paid retainer remains available. This jump budget does not promise
flight fuel, orbital insertion, landing, deadline completion or profit.

Fly to the accepted planet and maintain a stable bound orbit above its
atmosphere, within two planet radii of its centre. Thirty seconds of new
qualifying flight acquires the measurement.
The scanner does not steer or establish an orbit. Partial progress holds when
conditions become unsuitable. During flight, the HUD shows progress in seconds
and the actual submission time remaining, including when the report is ready.
For unfinished orbital work it also shows the current C++ eligibility or held
reason, such as travelling to the accepted planet, approaching within two radii,
or establishing a bound orbit above the atmosphere. “Survey eligible” describes
current conditions; the counter advances only through committed qualifying
flight intervals. A paused eligible readout earns no work. Ready reports and
expired deadlines take precedence over acquisition status.
Pausing freezes the countdown with the simulation; the display runs no separate
clock.

Return to Origin, attach to a port and explicitly **Submit report** in the same
page. Submission pays **100 prototype credits** once. Another commission needs
a new acceptance and thirty new seconds of qualifying flight, including when
the planet's terrain survey is already complete. Station walking and seated
attached flight use the same account and office.

These are provisional earnings, not a balanced operating economy. Flight/jump
fuel service remains free unless the player separately enables the
[paid operating prototype](PAID_OPERATIONS.md). The account starts with zero earned credits. The broader
paid-operation contract remains [#293](https://github.com/gobha-me/apsis-drift/issues/293).

## Geological commissions

With no active commission, choose **Geological specimen** in the office and
review the request for Origin's already-charted planet. Accepting reserves
**100 prototype credits** for **four game hours**. The orbital and geological
offers share one active slot; abandon the current job before choosing another
kind. Existing orbital work keeps its own measurement rules.

Land and walk to an existing exposure, inspect it, and collect a specimen
**after acceptance**. The existing [specimen pouch](SPECIMEN_SALES.md) carries
the physical item separately from its discovery note. A sample collected
earlier, or a retained note without the item, does not fulfill fresh work.
Revisiting a previously noted exposure can provide a new physical specimen
under the existing collection rules.

Return and attach at Origin. The office names the exact pouch slot and sample
that **Deliver named specimen** will consume. Confirming removes that one
item, retains all notes and other cargo, and pays 100 once. This is the total
commission payment; the delivered item also earns no ordinary buyer payment.
Selling or discarding it first invalidates the delivery quote. Abandonment and
expiry consume nothing, so any retained sample can still use the ordinary buyer.

Standard replacement retains the accepted terms and notes while losing carried
specimens. Collect a new item before the original deadline or abandon the job;
a note cannot recreate lost goods. Completed orbital reports retain their
existing recovery behavior. Paid delivery uses the same insurance-first and
operating-tab settlement as orbital income. Existing operating retainers fund
only fresh orbital work; geological acceptance adds no retainer eligibility.

The first geological acceptance selects survey-account version 2. Its kind,
delivered-record proof and geological payment counter distinguish physical
deliveries from orbital acquisition in strict saves and recovery. Existing
version-1 accounts keep their exact encoding and meaning. The reward is a
provisional prototype value, not evidence of profitable landing/ascent across
seeds or final price balance. This bounded scope is
[#713](https://github.com/gobha-me/apsis-drift/issues/713).

## Terms and time

The standing offer rolls over at the next absolute game-hour boundary. An
explicit acceptance reserves its quoted reward for **four game hours** from
acceptance. Submission at or after the deadline pays nothing. An expired
orbital commission can be explicitly renewed for the same target at Origin,
or abandoned there without payment. Any existing job, expired or not, locks its
destination and kind; abandon it before choosing a different target or kind. Existing exploration observations remain; replacing the job discards
that job's expired measurement. Another fresh commission is available after
payout or abandonment, including within the same offer hour.

Reading terms, opening menus, pausing and offline time advance no clock and
issue no work or money. Docking, acquiring the measurement and recovery do not
submit a report. The existing attendant's stellar lead remains separate,
nonexpiring hearsay. No simulated competitor or market is added here.

## State ownership and compatibility

C++ owns target admission, orbital eligibility, fixed flight intervals, absolute
terms and atomic payouts. Godot presents bounded packets and explicit actions.
The shared physical eligibility law is reused from
[orbital terrain surveys](FREEDOM_ORBITAL_SURVEY.md). Those once-per-planet
records are never reset or sold again: commissioned reference measurements
have their own counter and source/time provenance and grant no sensor facts.

The optional version1 `survey_commissions` personal record is absent until
explicit acceptance. It travels in the existing knowledge save envelope as
separate account metadata, outside its sensor-fact entries. It retains one
active job, a monotonic sequence, a bounded lifetime paid count and the latest
paid receipt. Earned credits are exactly the count multiplied by the version1
reward; no separate mutable wallet or unbounded payment history is required.
Version 1 retains the fixed Nearby primary target. Geological work uses version
2. Explicit acceptance of the admitted Waypoint primary selects version 3, with
an explicit per-job target policy; later Nearby or geological work retains that
version. Merely browsing, reading or loading promotes nothing. Existing absent,
version-1 and version-2 bytes and meanings remain unchanged. Mixed receipt
history retains its literal target and proof. An unknown planet is never
revealed to manufacture a commission.

Ordinary jumps retain the job. Save/Continue retains its terms, progress,
earnings and receipt. Standard replacement preserves the account and accepted
terms, discards unfinished acquisition, and retains a completed report with
its original craft provenance. Replacement grants neither measurement nor
payment. Loading an older save rewinds the whole account with that voyage.
Historical absent JSON, supported recipes and generated-world identities keep
their meanings. Counter/clock exhaustion refuses another commission without
blocking ordinary exploration.

## Qualification boundary

Provider and save contracts cover invalid schemas, unsupported policies,
nonfinite/invalid flight input, wrong targets and craft lineage, clock/counter
overflow, stale actions, expiry equality, duplicate payout and recovery.
Session fixtures retain real boarding, charged outward/return jumps,
3599/3600-tick acquisition, capture and submission. Their explicitly initialized
Home-space, circular-orbit and return-port poses are test boundaries, not a
claim of uninterrupted manual ascent, orbital handling or docking.

Native checks use the actual paused office in station and flight, retained
complete-save comparisons and Continue. Automated checks do not establish
manual handling, final scanner pacing or typical procedural-start viability.
The additional [destination-choice scope](https://github.com/gobha-me/apsis-drift/issues/720)
uses labelled transaction/orbit boundaries for its alternative-target checks;
those checks do not establish an uninterrupted Waypoint earning trip.
The separate [ordinary paid-survey reference](FREEDOM_ENDURANCE.md#ordinary-paid-survey-reference)
measures a fully flown seed 42 Nearby paid trip with a positive operating cash
change after restoration. It is a C++ test-pilot reference; broader balance,
failed/delayed work and player review remain open.

The [incomplete-work reference](research/incomplete-paid-survey-reference-v1.json)
separately flies outward and home without completing the measurement. It records
zero payment, actual restoration costs and acceptance of fresh work after
explicit abandonment. It establishes that one next action remains viable for
that candidate; expiry, sparse starts and player pacing remain separate.

The separate [Waypoint A paid-survey reference](FREEDOM_ENDURANCE.md#waypoint-a-paid-survey-reference)
now completes one alternative-target voyage through actual commands, including
orbital acquisition, return, submission and full restoration. Its seed-42
candidate takes 22.493 game minutes and finishes with 246 credits after a
34-credit replenishment. It preserves the earlier initialized-boundary tests
and Nearby references; it does not establish a seed distribution or price balance.
