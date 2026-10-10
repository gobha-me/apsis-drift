# Freedom resource state

Ordinary native New Game selects the [endurance recipe](FREEDOM_ENDURANCE.md):
a 90-minute full-main flight reserve shared by translation and attitude, plus
three discrete jump charges. C++ owns the quantities and transactions; Godot
reads them. There is no economy, passive time drain, field harvesting or third
suit fuel pool.

## Flight and depletion

The session first qualifies the existing candidate atmospheric/central-body
step and reads its realized gross propulsion channels. An affordable tick
commits that same body and the exact integer debit together. Braking, hover,
rotational stabilization, orbit hold and port/surface aids use the same budget;
external gravity, drag and lift do not. Walking and supported landed/docked
advances synchronize the ledger tick at zero cost. Time scheduling changes the
number of authoritative steps, not a frame-time bill.

If the whole requested tick is unaffordable, the candidate firing is discarded.
The same existing integrator advances from the original state with no commanded
propulsion, stabilization or hold correction. Momentum, gravity and passive
aerodynamics continue. Quantity is retained so a smaller affordable manual input
can still use it. Transient approach/landing/liftoff aids stop with a fuel notice;
saved assistance and hold preferences remain selected. Subsequent corrections
must independently be affordable. Empty fuel cannot create thrust or freeze a
falling ship. This adds no terrain impact/damage rules (#253).

The cockpit shows remaining fuel, 0–3 charges, reserve/empty text, the last
applied effort rate and remaining **full-main-equivalent minutes**. These minutes
are not travel range or an escape guarantee. Paused presentation can retain the
last applied sample; it does not imply continued burn. Zero propulsion reports
no burn. Historical resource-unselected sessions are labeled explicitly.

## Save ownership

Format24 contains the unchanged format23 surface voyage (which may have inactive
gear/contact) and one resource record: recipe version, seeded starter craft ID,
physical frame ID/version, shared tick, exact unsigned flight Q and jump charges.
Capacity comes from recipe1 for Wayfarer frame2/version1. No serialized capacity,
float remainder, generated catalog or pending/prepaid jump is accepted.

Closed JSON rejects extra, missing, duplicate, negative, fractional, overflowing
or incompatible fields, wrong owners, mismatched ticks and unsupported frames.
Nested duplicate keys are checked before parsing can flatten them. Loading
qualifies the entire existing voyage and ledger before live state replacement;
rejection leaves the source untouched. Save As retains the existing atomic file
writer and never silently modifies the selected source.

Formats17–23 retain their existing behavior on Continue. They have no resource
selection, receive no invented historical bill and are saved in their original
schema until an existing surface/boarding action explicitly selects its own
layer. Free resource recipe conversion for historical sessions is not offered
by this cut. There is no hidden mid-flight migration.

## Service and jump boundary

The cockpit's free replenishment action validates the actual attached supported
station port and matching craft/tick, then fills both pools exactly without
advancing time. A nearby free craft, elapsed wall time, pause or reload cannot
refill. Repeating service is idempotent. The provider supplies station service
#246; no market or generic inventory is involved.
[Explicit standard replacement recovery](FREEDOM_RECOVERY.md) uses the same
baseline reserves for a distinct replacement craft; depletion does not invent
an automatic rescue.

The resource adapter composes with the **retained historical** intersystem jump
owner: charge availability before spool, candidate arrival binding plus one
charge at actual commit, no charge for cancellation, no second debit on arrival
or repeated completion. Its historical mission transitions remain compatibility
behavior and are not a new Freedom movement gate. Native mission-free neighboring
travel now consumes this provider through its own
[saved travel owner](FREEDOM_NATIVE_TRAVEL.md). The
[continuous round trip](FREEDOM_NATIVE_ROUNDTRIP.md) spends two of three charges,
physically captures the home station and replenishes both pools. Complete saves
and independent continuation agree under both compilers, including commitment
and transit. This qualifies the selected #251/#246 consumers; wider manual,
controller and hardware acceptance remains #244/#245.

## Validation boundary

The resource contract test exercises actual thrust, exact affordability, dry
passive gravity/attitude with saved assistance and hold selected, residual use,
invalid input rollback, live checkpoint continuation, changed render scheduling,
pause, seed/craft/tick ownership, corrupt file refusal, historical saves, attached
service and the retained jump commit/cancel/last-charge sequence. The surface
fixture has an opt-in reserve-funded landing/liftoff/ascent path; it starts at an
explicit initialized near-ground site. The existing recorded station voyage
also checks every actual debit against the independent research meter. These
checks do not replace manual/controller/default-renderer acceptance in #245.

```sh
build/apsis-drift-freedom-resource-tests
build/apsis-drift-landed-craft-tests --resources
```

Use the existing development/native smoke commands for staged New Game and
Continue. No additional asset package, visual study or second simulation is
needed.
