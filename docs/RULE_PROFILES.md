# Deterministic Rule Profiles

Version 1 defines exactly two rule profiles for the intersystem career:
`ASSISTED` and `PILOT`. “Rule profile” is the player-facing term; neither
profile claims high-fidelity simulation, changes generated-world truth, or
selects a different mission.

## Selection and authority

Fresh and migrated careers use `ASSISTED`. Left and Right select either
profile at the Origin Station while the first contract is offered or accepted.
Launch locks the selected profile for the active mission. Returning, changing
terminal driver, resizing, changing render cadence, or loading the save cannot
change it.

The selection is a tick-addressed intersystem contract command and save format
8 stores it as authoritative state. Formats 1 through 7 migrate without a
profile field and therefore select `ASSISTED`. Unknown profile values are
invalid rather than silently downgraded.

This version establishes and persists the profile boundary. The existing
Assisted jump and entry mechanics remain the playable implementation until the
separate Pilot thermal and alignment systems consume the selection.

## Assisted

Assisted remains the default complete-loop experience:

- descent speed and flight-path angle cannot make atmospheric entry fail;
- thermal feedback may teach the envelope but cannot reject or damage the
  craft;
- outbound FTL commitment uses the existing ten-radius, matched-velocity
  approach corridor;
- the current keyboard-only controls can complete the full contract.

## Pilot thermal contract

Pilot uses the same generated planet, atmosphere, terrain, objective, and
flight controls. The follow-up thermal implementation derives heating from
authoritative atmosphere, altitude, speed, flight-path angle, and fixed-step
time.

Accumulated thermal load and an over-limit abort latch are authoritative and
must survive save/resume. Load trend, safe/unsafe cues, warnings, and suggested
correction are derived cockpit presentation. Reaching the limit triggers one
bounded consequence: a deterministic forced skip-out inhibits further descent
until the craft returns to the orbital regime. It does not create permanent
damage, consume fuel, change the objective, or mutate generated truth. The
player may cool and retry.

## Pilot FTL contract

Pilot commitment grades a deterministic alignment sample into three arrival
quality bands:

- `ALIGNED` uses the Assisted ten-radius matched-velocity corridor;
- `OFFSET` places the craft in a deterministic off-axis approach sector;
- `OPPOSED` may place the craft across the target planet's orbital phase while
  remaining in the correct generated system.

The sampled alignment score, selected band, and immutable arrival solution are
authoritative at commitment. Pre-commit aiming cues and projected quality are
derived presentation. The band geometry must keep the worst generated first-
route direct-assist recovery within five authoritative hours of additional
system travel, which is less than nineteen minutes at 16x compression. The
alignment implementation must test that bound across the generated first-route
catalog before publication.

Poor alignment never changes the target system, planet identity, ephemeris,
mission, or objective and never requires fuel, repair, or permanent damage.

## Deferred systems

Thermal integration and reentry guidance belong to #93. Alignment scoring and
band-specific arrival placement belong to #94. Fuel, repair, permanent ship
damage, installed hardware, additional profiles, sliders, and a generic
difficulty framework remain outside this contract.
