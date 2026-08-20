# Deterministic Rule Profiles

Version 1 defines exactly two rule profiles for the intersystem career:
`ASSISTED` and `PILOT`. “Rule profile” is the player-facing term; neither
profile claims high-fidelity simulation, changes generated-world truth, or
selects a different mission.

## Selection and authority

Fresh careers use `ASSISTED`. Left and Right select either
profile at the Origin Station while the first contract is offered or accepted.
Launch locks the selected profile for the active mission. Returning, changing
terminal driver, resizing, changing render cadence, or loading the save cannot
change it.

The selection is a tick-addressed intersystem contract command; save format 14
stores it as authoritative state. Formats 1 through 13 are unsupported alpha
inputs. Unknown or missing profile values are invalid rather than silently
downgraded.

Pilot FTL alignment and thermal entry both consume this boundary.

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
flight controls. Thermal integration derives heating from
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

The fixed-point heading/velocity sample, selected band, and immutable arrival
solution are authoritative at commitment. Pre-commit aiming cues and projected
quality are derived presentation. ALIGNED is bounded by 3 degrees and 2%;
OFFSET is bounded by 45 degrees and 20%; OPPOSED covers the remaining valid
sample. OFFSET scales from 10 to 100 planet radii. OPPOSED approximates the
opposite orbital phase. Its bounded recovery route is to cancel while spooling
or use target-hold system flight after arrival; #95 owns propulsion-specific
direct travel-time measurements.

Poor alignment never changes the target system, planet identity, ephemeris,
mission, or objective and never requires fuel, repair, or permanent damage.

The cockpit presents `HEAT`, `TEMP`, the 100% limit, signed flight-path angle,
and an information-complete correction cue. See
[Pilot Thermal Reentry](PILOT_REENTRY.md) for the formula, acceptance route,
and save boundary.

## Deferred systems

Fuel, repair, permanent ship damage, installed hardware, additional profiles,
sliders, and a generic difficulty framework remain outside this contract.
