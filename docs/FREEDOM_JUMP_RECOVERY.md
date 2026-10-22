# Freedom jump recovery

The first bounded increment of #248 is tracked in #664. At New Game, **Jump drive** independently selects Standard, Recovery, or Recovery + temporary strain below starting charts. Nearby systems, four systems and planetary charts keep their existing generated worlds and knowledge grants. Existing saves keep their selected drive rules.

Recovery mode starts ready. Every successful system, ordinary-planet, Moon or minor-body jump starts 45 seconds of recovery at its actual arrival. Early jumps remain possible for the normal one charge. They add up to 2 km of arrival uncertainty, fading as the drive recovers. The ordinary three-second spool and two-second transit remain; physical spool time reduces the previous recovery. There is no recovery lockout, extra charge, damage or maintenance bill.

C++ owns 5,400 ticks on the existing 120 Hz simulation clock. Flight, supported walking and attached idle age recovery. Pause, reading the chart, Save and Continue do not. Canceling a spool creates no use stamp or charge; time already simulated still counts. Replenishing fuel and charges preserves recovery. Standard replacement starts the new craft ready and retains the retired craft's saved state.

## Temporary drive strain

The owner selected temporary drive strain next on 2026-10-22 (#667), with actual
living-pilot rescue afterward. **Recovery + temporary strain** retains the same
one-charge early jump and arrival-uncertainty rules. The cooled baseline is
45 seconds. At commitment, unfinished recovery carries into the next arrival's
cycle: 45 seconds plus the remaining recovery, capped at90 seconds. A prompt repeat
after the ordinary3-second spool therefore starts87 seconds of recovery on arrival;
another prompt repeat reaches the90-second cap. These are initial playtest values.

Flight, supported walking and attached idle cool the drive on the same C++ clock.
Pause, menus and Continue preserve it. Waiting out recovery restores a45-second
next cycle; there is no permanent damage or repair requirement. Docked replenishment
keeps current strain, while standard replacement gives the new craft a ready drive.
Canceling a spool adds no cycle; time already simulated still cools the prior one.

The HUD shows time left, the current cycle and the projected next-arrival recovery.
Before commitment that projection is an upper bound from the displayed tick;
physical spooling can shorten it. During transit the new full cycle is frozen and
starts on actual arrival. The extra arrival spread still fades from at most 2 km,
normalized over that drive's current cycle. No recovery lockout or extra charge
is introduced.

## Placement and consequences

The additional radius uses an integer millimetre smoothstep of remaining recovery. The independent `jump_recovery_uncertainty` seed domain is 20; existing domain 14 navigation scatter is unchanged. Sampling uses three bounded signed components in an inscribed cube; it is not a uniform spherical distribution. The sampled displacement is frozen at commitment, authenticated with the actual source, destination, craft, lineage, attempt, clock and prior drive state. Repeated queries, rendering cadence and reload cannot draw another result.

The enlarged envelope must pass the route provider's existing clearance policy before commitment. No redraw, moved destination or reduced penalty hides a refusal. The system provider retains its existing star/planet sphere checks and documented environmental omissions. Local and planetary providers retain their fuller hull and environment checks. This feature does not guarantee safety around all universe objects. At maximum added spread, the new planetary policy's nominal 100 km stand-off can be reduced by at most 2 km; the historical planetary policy remains unchanged.

The cockpit shows recovery time and its current extra-spread bound. The actual commitment occurs after spooling, so the final bound may be smaller. Before commitment the UI exposes bounds rather than the hidden sampled point. During transit it reports that the new recovery begins on arrival. Ready arrivals use the exact baseline placement without adding floating-point zero offsets.

## Compatibility and authority

Resource version 2 owns drive state version 1; historical resources remain version 1 with no drive state. New travel format 57 / logical state 6 reuses the existing bounded composed-route representation. No unbounded voyage log or second clock is added. Chart choice stays independent: pair knowledge promotes through its existing travel selection, network charts remain four-system charts, and only planetary charts grant ordinary-planet routes.

New typed provider policies are system 3, ordinary planet 2, Moon 3 and minor 4. Historical policies, save formats, seed recipes and golden outputs remain unchanged. Pre-debit bills retain the prior drive state. The current drive stamp must match the latest authenticated typed receipt, including the single pending arrival during transit. Continue cannot silently enroll historical travel or clear debt by selecting another route.

Strain uses explicit resource version 3 / drive version 2 and composed travel
format 58 / logical state 7. A version 2 use stamp includes its authenticated recovery
duration. Current and retained commitments derive that duration from their actual
prior drive state. Version 2 sampling includes its version/duration; the existing
version 1 seed words, JSON and fixed45-second behavior stay unchanged. The same
four typed route families carry the explicitly versioned input and frozen result;
there is no new world, random domain or unbounded use log.

Permanent damage, maintenance-dependent recovery, extended reach, wrong-system arrivals, atmospheric escape and economy remain separate design decisions under #248. This increment does not close that broader issue.
