# Origin station crew

Selected in [#641](https://github.com/gobha-me/apsis-drift/issues/641): one
optional bay attendant, short text conversations and occasional passing remarks.
Conversations introduce no jobs, fees, rewards or required departure steps.

The attendant stands on the far side of the existing arrival guard. Approach
the guard, face the attendant and release movement before interacting. Boarding
keeps priority near the Wayfarer. The conversation reader pauses the journey;
closing it returns to paused controls, with an explicit neutral Resume.

C++ supplies the station-qualified crew identity, fixed pose, interaction
eligibility, authored topics and clock-derived remark selection. Godot presents
the four-view pixel artwork, text and input. The crew has no independent
simulation, navigation, schedule or dialogue progression. Conversation display
and chatter cooldown are transient; no new save format or generated-world
recipe is introduced.

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
