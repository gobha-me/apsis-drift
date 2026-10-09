# Native First Jump

The bounded Freedom origin/neighbor route consumes the existing C++ world,
[knowledge](FREEDOM_KNOWLEDGE.md), [resources](FREEDOM_RESOURCES.md) and
[targeting policy](FREEDOM_JUMP_TARGETING.md). Godot presents commands and the
selected physical world. No mission, currency, mastery or upgrade is required.
New Game still starts walking on Origin Station with the authored Wayfarer.

Open **Starting chart**, select the other system, then **Spool jump**. Selection
is allowed while docked; departure requires an aboard pilot in free space and
one charge. Resume flight explicitly if paused. The three-second spool runs
ordinary physics and propulsion bills. Cancel before commitment without spending
a jump charge or advancing the arrival attempt. At commitment, recompute actual
geometry, freeze one point and debit one charge atomically. Two seconds of transit
advance the shared clock without propulsion burn. Controls cannot change the
committed destination, assistance profile or attachment. Pause freezes simulation;
Save As does not cancel spool or transit.

At arrival, install the destination's actual seeded physical system, planet frame
and rotation. Convert the frozen barycentric point/velocity to that reference;
retain orientation and angular velocity. The bubble holds the commitment pose
and attitude during transit. This and adopting the destination reference velocity
are explicit drive fiction, not a conservation-of-momentum claim. There is one
live craft/voyage, never a second home-flight checkpoint to restore.

Godot replaces the terrain stream and retires the old GPU cover on owner change.
The neighboring system has no Origin Station model, HOME marker, port selection
or free station service. Returning restores the actual home-world station cues;
arrival is 40 km radially outside the station, not docked. Flight, interception,
capture and attached replenishment remain physical actions. Two legs spend two
of the initial three charges. Knowledge of home is independent of charge
availability; this is not a promise of free rescue or a guaranteed fuel margin.

The chart uses the current C++ system and granted positions. Preview reports
actual distance, heading/drift and envelope size without exposing the sampled
point, destination planet ID or hidden catalog. Selection/focus/Save/Continue
award no observations. A committed physical arrival records system presence,
then the existing local sensor policy samples that actual world. CONTACT and
PROBABLE property readings retain their existing redaction.

## Explicit persistence

Only an explicit destination selection opts an existing compatible native session
into travel. Continue and ordinary Save As do not upgrade old saves.

- Flight format **26**, active-world selection **1**, names the current system and
  reference planet with canonical decimal IDs. Physical catalog/ephemeris **2/2**
  regenerate only the existing origin/neighbor pair; unknown owners refuse.
  The unchanged origin recipe remains history, not the active-world selector.
- Knowledge recipe **3**, `world_domain=1`, retains sensor policy **1** and all
  existing evidence. Its subject domain adds the actual neighbor planets. Recipes
  1 and 2 preserve their original domains and bytes.
- Travel format **27**, state **1**, wraps one format-25 resource/knowledge/voyage
  and the pinned craft binding. A completed seated route retains the actual pilot
  and station entry while no port is selected; a nested boarding voyage owns that
  record when a port is selected, without a duplicate pilot or neighbor port.
  It retains selected destination, spool start,
  next attempt and the latest immutable commitment/source bill. Earlier completed
  commitments need no unbounded log; visited provenance remains in the ledger.
- Docking/journey projections with an explicit `world_owner` delegate to format
  26 rather than assuming format 18. Port ownership must match the actual home
  system and station host. Historical projections without that selector are exact.

The frozen save stores full source pose/request, attempt, independent sample seed,
point, scheduled arrival and envelope radius. Decode regenerates the policy-1
solution and requires exact agreement. Transit additionally binds source pose,
phase clocks and the one-charge resource delta. Invalid/nonfinite state, corrupt
fields, owners, pins, versions, oversized/deep documents and unsafe clock bounds
refuse before replacing the live session. Every phase uses the existing atomic
file writer. Save/Continue retains the authored craft and actual seated pilot
through jumps. Returning and physically docking permits disembarking through the
original entry route; a jump never manufactures a fresh pilot or station actor.

## Qualification boundary

This starter implementation commits in-range, free-space departures with arrival
volumes clear of the tested star/planet spheres. That is not a general safety
certificate. Atmospheric jumps, extended reach, wrong-system arrivals, condition
stress and early repeat consequences require the unselected policies in #248/#253.
Standard replacement-craft recovery remains unimplemented in #247. No hard
cooldown or invented damage/death law is added here. Full body-render redaction remains #215.
The composed departure, surface visit, physical return docking and fuel-margin
acceptance remains #194.

`native-first-jump-contract` tests actual C++ round trips for three seeds in both
piloting profiles, cancellation, every saved boundary, malformed saves and station
selection. `first_jump_test.gd` compares the real bridge/UI route with independent
C++ phase saves, checks neighbor asset staging, craft continuity, chart presence,
terrain retirement and removal/restoration of station cues. Headless checks do
not establish GPU appearance, hardware controls or play balance.
