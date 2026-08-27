# Guided and Skipped Onboarding

Version 1 defines the authoritative career choice consumed by New Game. It is
application-owned gameplay state, not a presentation
preference, tutorial prompt setting, or generated-world input.

## Progression state

| State | Required chapter | Meaning |
| --- | --- | --- |
| `guided` | `contract_one`, `contract_two`, or `contract_three` | The authored onboarding sequence is active at exactly one chapter. |
| `skipped` | none | New Game deliberately bypassed authored onboarding. |
| `completed` | none | Contract three was explicitly completed and turned in. |

Guided advances only one chapter at a time. Completing contract three changes
the state to completed and clears the chapter atomically. Skipped and completed
states cannot be advanced by tutorial commands. Prompt visibility remains
presentation state and cannot change this progression.

## Starting knowledge and access

Every path knows the Origin Station, tutorial-safe home planet, and bounded
origin-system chart. Guided contracts one and two do not expose an interstellar
solution. Contract three exposes the one authored first-jump solution without
opening unrestricted exploration. Skipped and completed careers expose that
solution and the post-onboarding navigation baseline.

The bounded universe view derives these facts through the versioned
[universe-navigation contract](UNIVERSE_NAVIGATION.md). Guided contract one and
two omit the target row entirely. Contract three, Skipped, and Completed show
the origin plus the one resolved target; this projection is access, not an
earned discovery or visit.

These access facts are derived from onboarding progress. They do not consume a
random stream or add a discovery, visit, completed objective, mission turn-in,
reward, technology upgrade, probe report, or world delta. Guided and Skip with
the same universe seed therefore regenerate identical immutable systems,
bodies, station, terrain, signals, and mission identities.

## New Game and persistence

Guided is the default `NewGameOnboardingChoice`. Skip may be supplied only
while constructing a new career; no API converts an existing Guided career to
Skipped. The title requires a separate cancel-first confirmation before it
persists a skipped career.

Save format 16 stores the state and nullable chapter in every career
projection. Invalid enums, Guided without a chapter, Skipped/Completed with a
chapter, and Completed with an unfinished authored intersystem contract are
rejected before live state is replaced. Guided contract two additionally
requires the home Signal Run to be explicitly turned in. Formats 1 through 15
are unsupported alpha saves and remain untouched when rejected.

Contract one is the home-planet Signal Run owned by #144. Its contextual
flight-check observations are presentation-only and cannot advance this state;
only an explicit dock and turn-in advances Guided to contract two. The later
origin-system contract is the physical contract-two round trip documented in
[ORIGIN_SYSTEM_CONTRACT.md](ORIGIN_SYSTEM_CONTRACT.md); the first-jump contract
is the implemented bounded flow in
[FIRST_JUMP_ONBOARDING.md](FIRST_JUMP_ONBOARDING.md). This boundary is
deliberately not a generic tutorial, mission, knowledge, or campaign engine.

## Integrated acceptance

The [station-to-universe acceptance](ONBOARDING_ACCEPTANCE.md) carries the
same format-16 document through all three physical contracts for three bounded
Assisted seeds, compares each final career with its identical-seed Skip
baseline, and proves the completed career remains stable during idle open
exploration. It reuses the shipped Kitty/ANSI departure input trace and keeps
application state, framebuffer, encoder, and external terminal/proxy evidence
separate.
