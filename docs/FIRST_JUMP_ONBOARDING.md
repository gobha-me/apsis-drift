# First-Jump Onboarding

Version 1 reframes the existing first intersystem mission as Guided contract
three. The physical jump, target-system approach, Planetfall objective, return,
station rendezvous, and turn-in remain the established application-owned
systems.

## Availability and completion

A Guided career receives the offer only while its onboarding chapter is
`contract_three`. Normal Guided progression carries the explicitly turned-in
home Signal Run and origin-system transfer histories into that chapter; legacy
saves that predate contract-two history remain compatible. A Skipped career
keeps its post-onboarding access and may accept the same authored offer
voluntarily without gaining synthetic tutorial history.

Objective completion and arrival at the Origin Station do not complete
onboarding. The station-board turn-in atomically changes the mission to
`turned_in`, changes Guided onboarding to `completed`, and clears its
chapter. No follow-up contract is accepted automatically.

## Universe-view interaction

`U` opens or closes the bounded universe view without pausing authoritative
flight. Tab and Shift-Tab focus rows, including disabled rows whose stable
reason remains visible. Enter selects only an available row; Escape closes the
view before it can open the pause menu.

The outbound view contains the current Origin and the one resolved Target.
The return view contains the visited Target and the known Origin. The panel
shows knowledge, route availability, FTL readiness, cardinal heading, exact
distance, and the informational continuous-flight ETA. No unknown generated
system is rendered.

Focus and pending selection are presentation state: they do not advance time,
move the craft, change mission progress, enter save JSON, or affect checksums.
The selected `SystemId` is validated when J begins a spool. Cancellation
retains the selection and restores the same live craft pose and velocity,
retimed to the current authoritative tick. Commitment binds the destination
and immutable arrival through the existing intersystem contract.

## Contextual guidance

Before outbound commitment the cockpit introduces opening the view, selecting
Target, starting the spool, the cancel command, and either Assisted automatic
alignment or Pilot correction and projected arrival quality. At the target it
retains the existing physical approach, objective, ascent, and departure
guidance, then requires selecting Origin before the return spool. Arrival still
requires physical station rendezvous, docking, and explicit turn-in.

Kitty and ANSI consume the same application Screen and semantic status.
Encoder byte totals remain driver evidence only and are not claims about
terminal, PTY, proxy, decoder, network, compositor, or display throughput.

## Persistence boundary

Save format 16 is unchanged. It already records authoritative onboarding,
mission/travel phases, universe tick, Pilot alignment, committed destination,
arrival solution, craft state, discoveries, and world deltas. Loading resets
the transient universe-view focus and selection, while every authoritative
phase resumes unchanged.

Continuous non-FTL travel remains an informational heading and ETA in this
version. Activating direct cruise, multiple destinations, procedural missions,
fuel, encounters, and a generic navigation or tutorial framework remain
separate work.
