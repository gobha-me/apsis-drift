# Survey commissions

The first optional earning prototype pays for a fresh orbital reference
measurement. It is available in the existing **planetary-chart** and
**four charted + two uncharted systems** journeys. Older journeys keep their
charts, fuel service and movement rules.

At Origin, pause and open **Survey commissions**. Review the already-charted
remote planet and select **Accept commission**. Fly to that planet and maintain
a stable bound orbit above its atmosphere, within two planet radii of its
centre. The prototype target is the first planet in **Nearby system**; select
that system in Navigation chart to arrive at the commissioned planet. Thirty seconds of new qualifying flight acquires the measurement.
The scanner does not steer or establish an orbit. Partial progress holds when
conditions become unsuitable; the flight HUD shows the counter.

Return to Origin, attach to a port and explicitly **Submit report** in the same
page. Submission pays **100 prototype credits** once. Another commission needs
a new acceptance and thirty new seconds of qualifying flight, including when
the planet's terrain survey is already complete. Station walking and seated
attached flight use the same account and office.

These are provisional earnings, not a balanced operating economy. Flight/jump
fuel service remains free; rent, purchases, debt and spending are not selected
by this increment. The account starts with zero earned credits. The broader
paid-operation contract remains [#293](https://github.com/gobha-me/apsis-drift/issues/293).

## Terms and time

The standing offer rolls over at the next absolute game-hour boundary. An
explicit acceptance reserves its quoted reward for **four game hours** from
acceptance. Submission at or after the deadline pays nothing. An expired
commission can be explicitly replaced at Origin, or abandoned there without
payment. Existing exploration observations remain; replacing the job discards
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
The target is the existing first charted neighboring planet. An unknown planet
is never revealed to manufacture a commission.

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
manual handling, final scanner pacing, profitable paid operation or typical
procedural-start viability. Those require subsequent measured scenarios and
player review.
