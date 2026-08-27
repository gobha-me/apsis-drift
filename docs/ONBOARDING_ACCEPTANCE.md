# Station-to-Universe Onboarding Acceptance

The v0.4.38 acceptance path composes the three focused contract replays into
one authoritative career lineage. Each contract consumes the exact format-16
document returned by the previous contract; the integration does not fabricate
chapter-two or chapter-three state between legs.

## Deterministic matrix

Assisted Guided careers use universe seeds `1`, `42`, and `12648430`
(`0xC0FFEE`). Every seed completes the home Signal Run, origin-system body
transfer, first intersystem jump, three station returns, and the final
open-exploration handoff. The existing Advanced/Pilot planetary recovery
scenario remains anchored to seed 42 and must observe its bounded thermal
abort/recovery consequence.

For each seed the suite also creates an explicit Skip career. Guided and Skip
must have identical recipes, generator versions, station, home planet, and
mission identities. Skip must expose open exploration without tutorial
completions, rewards, earned discoveries, or world deltas. Ten seconds of
post-onboarding authoritative idle time may advance the universe clock but may
not change mission, travel, onboarding, discovery, or world-delta state.

The composed report retains every focused save boundary. Contract-three
checkpoints additionally replay from each boundary to the same final document
and checksum. Formats 1 through 15 are exercised as non-destructive rejection
fixtures; format 16 remains the only supported alpha save format.

## Running the proof

```bash
./build/apsis-drift --onboarding-acceptance \
  --driver kitty --profile local --report onboarding-kitty.json
./build/apsis-drift --onboarding-acceptance \
  --driver ansi --profile remote --report onboarding-ansi.json
```

The command includes the driver-backed contract-one departure trace introduced
in v0.4.37. Its presentation, framebuffer checksum, encoded bytes, and frame
count prove the requested Kitty or ANSI application path. The three-contract
checksums are application-owned and must not change with the driver or render
profile.

Report timings label simulation and application rendering separately.
`terminal_proxy` remains `external-live-capture`: headless encoded bytes do not
claim PTY, proxy, terminal, or display throughput.

## Current boundary

This suite integrates established systems. It does not add a generic campaign
engine, redesign cockpit information hierarchy, synthesize speech, or expand
the current two-item pause menu. Interactive New/Continue/Load and Skip
confirmation remain covered by the local-profile contract; atomic save/load is
exercised at the authoritative checkpoints. Broader pause actions and COMMS
presentation remain separate follow-up work.
