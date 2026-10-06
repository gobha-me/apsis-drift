# Documentation

Start with the [project overview](../README.md). The current product direction
is native Freedom; study results and static certificates do not complete the
player's station-to-flight journey.

## Run and develop

- [Native project setup](../godot/README.md)
- [Development, headless tests and quality checks](DEVELOPMENT.md)
- [Retained terminal build and run](TERMINAL.md)
- [Godot editor import limitation](GODOT_EDITOR_IMPORT.md)

## Direction and ownership

- [Freedom roadmap](ROADMAP.md)
- [C++ / Godot / TermForge ownership](GODOT_ADOPTION.md)
- [Concept](CONCEPT.md)
- [Native flight session](NATIVE_FLIGHT_SESSION.md)
- [Freedom save format](FREEDOM_SAVE_FORMAT.md)

## Current native play and presentation

- [Saved station walking](SAVED_STATION_WALK.md)
- [Saved flight](SAVED_NATIVE_FLIGHT.md) and [port lifecycle](NATIVE_PORT_LIFECYCLE.md)
- [Starting assembly](NATIVE_STARTING_ASSEMBLY.md) and [station view](NATIVE_STATION_VIEW.md)
- [Flight controls/reference](NATIVE_FLIGHT_REFERENCE.md)
- [Starter assets](NATIVE_STARTER_ASSETS.md), [operating assets](WAYFARER_OPERATING_ASSETS.md)
  and [static stowed assets](WAYFARER_STOWED_ASSETS.md)

## Simulation, assets and bounded contracts

- [Seed derivation](SEED_DERIVATION.md) and [planet generation](PLANET_GENERATION.md)
- [Audio](AUDIO.md) and [asset provenance](ASSET_PROVENANCE.md)
- [Boarding body](ORIGIN_BOARDING_BODY.md), [support](ORIGIN_BOARDING_SUPPORT.md)
  and [lower cockpit contact](LOWER_COCKPIT_CONTACT.md)
- [Actual foot sites](ORIGIN_BOARDING_FOOT_SITES02.md),
  [fixed body endpoint](ORIGIN_BOARDING_SOURCE_ENDPOINT01.md) and
  [complete static self check](ORIGIN_BOARDING_SOURCE_ENDPOINT_SELF01.md)
- [Fixed endpoint vertical load](ORIGIN_BOARDING_SOURCE_ENDPOINT_LOAD01.md)
- [Fixed endpoint surface checkpoint](ORIGIN_BOARDING_SOURCE_ENDPOINT_SURFACE_CHECKPOINT01.md)
- [Partial lower-foot transfer01](ORIGIN_BOARDING_LOWER_FOOT_TRANSFER01.md): continuous kinematic/self/nominal support preparation qualifies; movement remains open; the separate surface consumer is below.
- [Partial transfer surface sweep01](ORIGIN_BOARDING_LOWER_FOOT_TRANSFER_SURFACE_SWEEP01.md): continuous source-triangle separation qualifies using the same owned body/source/cells; material volume and actor movement remain open.
- [Grouped supported route](ORIGIN_BOARDING_SUPPORTED_ROUTE.md) and
  [independent-foot phase contract](ORIGIN_BOARDING_ROUTE_FOOT_PHASE01.md):
  current development, fixed body geometry, source completion and remaining
  supported movement checks.
- [Initial complete material assessment](ORIGIN_BOARDING_INITIAL_MATERIAL01.md):
  full source envelopes, selected complete geometry and bounded material relations.
- [Supported port unload](ORIGIN_BOARDING_ROUTE_PORT_UNLOAD01.md):
  clear supported endpoints with a refused joint sector between them.
- [Checkpoint unload](ORIGIN_BOARDING_ROUTE_CHECKPOINT_UNLOAD01.md):
  supported preparation and a retained unresolved hip-ownership interval.
- [Checkpoint self02](ORIGIN_BOARDING_ROUTE_CHECKPOINT_UNLOAD_SELF02.md):
  complement certificate for the same finite hip slab and supported path.
- [Checkpoint WORLD/material01](ORIGIN_BOARDING_ROUTE_CHECKPOINT_WORLD_MATERIAL01.md):
  complete preparation clearance and named remaining material gaps.
- [Hatch-seal material SealSweep01](ORIGIN_BOARDING_HATCH_SEAL_MATERIAL01.md):
  admitted eight-capsule seal and its historical load-ring material finding.
- [Separation load-ring material SeparationRingSweep01](ORIGIN_BOARDING_SEPARATION_RING_MATERIAL01.md):
  admitted ring material and complete WORLD04 prefix clearance.
- [IntermediateStep01 preflight](ORIGIN_BOARDING_ROUTE_INTERMEDIATE_STEP01.md):
  frozen moving-foot candidate, shared kinematic cover and retained joint-sector refusal.
- [SectorAttribution01 diagnostic](ORIGIN_BOARDING_ROUTE_INTERMEDIATE_STEP_SECTOR_ATTRIBUTION01.md):
  eight fixed replays identify port ankle-pitch violations in the unchanged motion.
- [Intermediate pause finite support](ORIGIN_BOARDING_INTERMEDIATE_PAUSE_SUPPORT01.md):
  registered source contact and nominal load gate for the unchanged two-second pause.
- [Intermediate pause fixed fore/aft envelope01](ORIGIN_BOARDING_INTERMEDIATE_PAUSE_ENVELOPE01.md):
  one necessary support bound at the unchanged load split.
- [Constructive intermediate pause support02](ORIGIN_BOARDING_INTERMEDIATE_PAUSE_SUPPORT02.md):
  registered lighter-load static witness, preserving the quarter-load refusal.
- [Intermediate load acquisition01](ORIGIN_BOARDING_ROUTE_INTERMEDIATE_LOAD01.md):
  registered continuous lighter-load segment and held endpoint.
- [IntermediateStep02 preflight](ORIGIN_BOARDING_ROUTE_INTERMEDIATE_STEP02.md):
  registered foot-preposition sequencing hypothesis under unchanged shared limits.
- [Checkpoint material Boundary03](ORIGIN_BOARDING_CHECKPOINT_MATERIAL_BOUNDARY03.md):
  admitted frame/nose relations; remaining hatch seal material gap.
- [Checkpoint material Union02](ORIGIN_BOARDING_CHECKPOINT_MATERIAL_UNION02.md):
  registered adjacent-band containment after the retained single-band refusal.
- [Checkpoint material extension01](ORIGIN_BOARDING_CHECKPOINT_MATERIAL_EXTENSION01.md):
  registered two-source completion and same-cover WORLD02 consumer.

- [System-flight validation cost](SYSTEM_FLIGHT_VALIDATION.md): per-call catalog authentication preserves flight behavior and removes repeated work.

## Studies and history

- [Native study 01](GODOT_STUDY_01.md) and [streaming study 04](GODOT_STUDY_04.md)
- [Flight handling lab](THRUST_FLIGHT_LAB_09.md)
- [Technical preview](FREEDOM_PREVIEW_01.md) and [offline reel](GODOT_DEMO_REEL.md)
- [Historical detailed project reference](PROJECT_REFERENCE.md)
- [Release checklist](RELEASING.md)

These are grouped entry points, not a complete chronological proof index. Each
contract records its own scope, version and remaining qualifications.
