# Repeatable native surface practice start

2026-09-19, issue #257. The native thrust lab now starts above its surrounding
ridges instead of only 60 metres above the terrain directly underneath it.
This is a development fixture, not a career spawn, landing site or collision
system. A later station-bunk opening must retain this surface fixture so that
departure, orbit, return and deterministic regeneration remain easy to test.

## Version 1 contract

The C++ bridge surveys a fixed 17-by-17 grid in the tangent plane of the
original snapshot latitude and longitude: 250-metre spacing, 2-kilometre square
half-extent (corners approximately 2.828 kilometres away). It uses the same
planet seed, terrain source LOD and relief version as the flight session.
There are no random draws, terrain edits or changes to generation versions.

Initial altitude is the greatest of those 289 sampled elevations plus 300
metres. The shuttle keeps the reference coordinates and authored 0.3-radian
heading, with zero linear/angular velocity and translation/gravity assist on.
Displayed ground clearance is measured against the **local** centre sample,
not the highest nearby ridge. Seed 42 with relief version 1 starts at about
3436.18 metres altitude and 1260.56 metres local clearance.

Initialization is explicit and restricted to a pristine tick-zero snapshot
session. An existing flight, inspection relocation or already enabled lab
cannot trigger the survey to teleport or repair the ship. Invalid requests
are rejected before sampling; the completed candidate is committed together
with its camera/terrain projection. Reset repeats the original survey. The
bridge exposes `surface_start_reference` metadata for inspection and tests.

This finite survey does not guarantee clearance between probes, beyond its
neighbourhood, for the entire hull, or after the player starts flying. The
existing floor guard is still a lab aid, not damage or landing physics.

Legacy snapshots, standalone `enable_thrust_flight()` fixtures and save format
16 remain unchanged. Only interactive native `--flight-model=thrust` startup
and reset use `enable_surface_practice()`.

## Verification

`godot-surface-start-contract` tests invalid inputs, version/seed guards,
sample count/margin, seam/pole cases, cache and traversal independence, both
relief modes, and repeatability after visiting other terrain. GCC and Clang
pass this and the existing focused native contracts.

`surface_start_test.gd` exercises the real extension: initialization, reset,
metadata, atomic refusal after malformed input or an ongoing/relocated flight,
and preservation of the standalone legacy fixture.

`surface_start_review.gd` captures paused before/after/cockpit views using real
streamed terrain. Each PNG has a JSON sidecar recording state, source and
license. These captures establish visible startup clearance, not continuous
collision safety or a performance benchmark. Generated review outputs remain
outside the source tree's tracked assets.
