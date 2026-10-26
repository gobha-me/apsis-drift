# Origin station crew

Selected in [#641](https://github.com/gobha-me/apsis-drift/issues/641): one
optional bay attendant, short text conversations and occasional passing remarks.
Conversations introduce no jobs, fees, rewards or required departure steps.

The attendant stands on the far side of the existing arrival guard. Approach
the guard, face the attendant and release movement before interacting. Boarding
keeps priority near the Wayfarer. The compact card shows nearby Talk guidance
and the current refusal. Releasing movement and immediately pressing Talk queues
one request until the next ordinary walking step confirms that you stopped.
Renewed input, pause, focus loss, a dialog or an owner change cancels it; input
does not force an extra simulation tick or relax C++ eligibility.

The conversation reader pauses the journey;
closing it returns to paused controls, with an explicit neutral Resume.

**Survey office · work and sales** and **Origin hull repair** open the existing
station service screens directly from the conversation. The office contains
commission work, specimen and salvage sales, and paid-operation terms. Opening
a service only reviews it; accepting work, selling, enrolling or repairing still
requires its separate confirmation. Services unavailable in an older journey
are disabled with an explanation. Back returns to paused controls, and the
original pause-menu entries remain available.

C++ supplies the station-qualified crew identity, fixed pose, interaction
eligibility, authored topics and clock-derived remark selection. Godot presents
the four-view pixel artwork, text and input. The crew has no independent
simulation, navigation, schedule or dialogue progression. Conversation display
and chatter cooldown are transient; no new save format or generated-world
recipe is introduced.

The original three advice topics remain available. After you record a derelict
contact, you can choose **Can you look at my contact log?** to share that record.
Before inspection, the attendant points you toward the existing exterior
inspection action. After inspection, the response acknowledges the recorded
power, thrust and registry observations while leaving ownership and occupancy
unknown. This uses your retained notes, including after recovery and Continue;
the attendant supplies no unseen contacts, coordinates or extra discovery facts.
Reading either response leaves the save unchanged. There is no rumor timer,
reward, new relationship state or requirement to return before exploring further.

## One optional stellar lead

In a journey with the six-system uncharted region, the attendant may offer
**Heard anything beyond the chart?** Reading it remains save-neutral. Choose
**Keep this lead** to retain one report in **Recorded observations →
Exploration notes**. It records the source and ship time; it grants no navigation
fix or selectable destination.

Use the existing **Search nearby stars** action in ordinary space flight.
Its actual sensor acquisition can confirm the report; a physical arrival adds
a separate visited status. Search order remains unchanged. The original note
survives Continue and standard recovery, including its original craft provenance.
The report concerns a stable star and has no expiry or freshness promise. It
reserves no salvage claim, offers no reward and gates no departure or scanner.

C++ owns the independently versioned optional knowledge note and validates the
actual attendant context, offered tick and complete save before receipt. Godot
receives source/status text and a known-clock offer token, without the unknown
target's identity or position. Unsupported and historical journeys retain their
existing recipes and absent-note encodings.

The decorative bubble cooldown currently uses Godot's signed integer clock
conversion. Extreme unsigned save clocks can suppress chatter; C++ conversation
eligibility, saved time and the journey remain independent of that display limit.

The attendant's standing reservation is outside the player's existing walking
crop. The admitted station mesh supplies clear standing space and all five
floor samples. The bounded conversation area's head-height prism is free of
source triangles, so its range/heading checks do not allow conversation through
station walls. No station geometry, historical actor pose or collision boundary
is moved to fit the character. The package retains the source mesh identity
and placement measurements.

The [selected art package](../assets/native/origin-crew-01/README.md) contains one
unchanged generated sprite sheet and four idle frame definitions. The owner
cleared the reference chain and authorized BSD 3-Clause distribution on
2026-10-20. Original prompts, source hashes and historical local-only receipts
remain with the dated clearance. The stationary attendant does not imply
finished walking, greeting, working or seated animation.
