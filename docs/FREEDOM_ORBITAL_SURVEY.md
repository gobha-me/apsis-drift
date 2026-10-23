# Orbital terrain survey

In a stable orbit, pause and choose **Begin orbital terrain survey** in Controls.
Resume explicitly to operate the scanner. The starter equipment is available
without a mission, purchase or upgrade.

The first survey targets the **current identified planet**, using the existing
flight body's owner. It does not select a distant chart destination or scan a
Moon/asteroid. A suitable orbit must be bound, have periapsis above the selected
atmosphere boundary, and put the craft within **two planet radii from the
centre**. This is an orbital observation band, not two radii of altitude.

Fly for **30 seconds of qualifying ship time**. Actual committed 120 Hz flight
steps acquire the data; the HUD shows progress or the reason acquisition is
held. Leaving the observation band or a stable free orbit holds the partial
acquisition. Returning to suitable flight continues it. The scanner does not
steer, circularize, hover or spend a jump charge. Ordinary propulsion still
consumes fuel.

Pause, focus loss, Controls and reading records freeze ship time and scanning.
Save As/Continue retains the unfinished acquisition. **Stop orbital survey**
discards unfinished work; accepted jump commitment or a replacement craft also
discards it. Completed records remain. Starting or stopping stays paused until
you Resume.

Completion adds orbital survey provenance to **Recorded observations** and
resolves that planet's existing coarse terrain model: terrain character and
water coverage. A previously resolved model still receives a distinct survey
record. This is not a local landing-site scan, full surface map, live weather,
physical visit or guarantee that any patch is safe. Use actual terrain/support
checks for landing. The 30-second acquisition setting is an initial playable
choice, not final economic balance.

## Ownership and persistence

The optional `orbital_surveys` knowledge extension selects version 1 on the
first explicit Start. It stores one active target/source/start/counter and up
to 16 sorted unique completed target/source/start/completion records. Targets
retain typed system and planet identities; source craft identities follow the
existing lineage policy. Completion time is authoritative ship time. Version 1 fixes the 3,600-tick
acquisition and orbital eligibility law; future changes need an explicit
version/compatibility decision.

The extension is absent in historical and untouched new games. Continue does
not select it, grant a scan or change automatic observation policy 1. Completed
survey records and landing facts survive travel and standard recovery;
replacement abandons the old craft's unfinished acquisition. Historical recipe,
seed, physical state, generated terrain and passive sensor semantics are
unchanged.

C++ evaluates orbital eligibility and commits progress with flight. Only the
outer accepted flight transaction acquires an interval: recursive coordinate
handoffs, impact integration and flight aids cannot count it twice. A rejected
flight command cannot acquire time or partially commit physics. The completion
transaction coalesces a weaker passive reading staged on the same tick, then
retains the strongest landing evidence without rewriting committed history.
Godot displays the resulting state and sends explicit Start/Stop commands.

The focused C++ contracts cover the acquisition boundary, rejected ticks,
held progress, frame handoffs, save continuation, schema/provenance bounds and
recovery history. The native `orbital_survey` check exercises the real paused
controls, a completing bridge tick, full-save parity and retained reader
provenance. These checks do not establish final scanner pacing, manual orbital
handling or GPU appearance/performance.

[Survey commissions](SURVEY_COMMISSIONS.md) reuse the same physical orbital
eligibility for separately accepted fresh reference measurements. They never
reset these completed terrain surveys or turn their facts into repeatable sales.
