# Healthy ship sound: listening direction

2026-09-19, issue #254. This records human audition choices, not completed
runtime integration or an acoustic simulation. The existing native adapter and
safety boundaries remain in [study 19](NATIVE_SHIP_AUDIO_19.md); the C++ audio
contract, MIDI and other existing audio work are preserved.

## Latest human checkpoint: recording-based direction

The native build 25 audition did not pass: the user found the continuous sound
annoying and could not clearly distinguish idle from load. Earlier offline
acceptance of direction 24 does not override that native listening result.
Numerical level stability alone is not sound-design acceptance.

The user subsequently narrowed references to Low Engine Hum (kaboose102) and
Space Flight 10 (Bret Bernhoft), and preferred the private recording-based
comparison. Low Engine Hum should sit as slightly quieter background machinery;
Space Flight's level in comparison 26 is accepted as the propulsion reference.
Comparison 27 lowers only the hum by 2 dB; the user accepted that comparison
("perfect"). The numeric offset was our implementation of the requested modest
reduction, not a number specified by the user. This background/propulsion difference
amends the earlier strict single-level direction; it is not permission to make
thrust drive an unbounded volume crescendo.

Plan player-adjustable audio levels rather than treating this default mix as
universal. Keep source character/load response, player category gains, and
listener position/transmission as separate concerns. A compact shuttle has
machinery near its pilot; future larger ships can have a quiet bridge and
audible machinery spaces. Ship size alone is not a master-volume multiplier:
placement and compartment isolation matter. Do not implement autopilot,
walkable large-ship interiors or generic acoustic simulation under this note.
Safety mute/pause/focus gates and existing music/audio remain preserved.

The supplied references are Pixabay-named MP3s. Matching creator CC0 originals
have been located, but the private auditions are not a public-redistribution
clearance or a claim that imported audio is original BSD synthesis. Preserve
source/license records and establish the distribution source chain before
shipping samples. The opt-in recording adapter and saved category controls are
implemented in [study 28](RECORDED_SHIP_AUDIO_28.md); native listening and loop
comfort still require a human audition. Offline acceptance is not that gate.

## Historical synthesis direction, not current sound acceptance

Human checkpoint: direction 24's constant-level dynamic blend is accepted for
the next implementation pass. The variation is subtle and may be difficult to
notice; retain that as a known future refinement, not permission to exaggerate
the voice now. This accepts the offline sound direction for now, not the
unheard runtime port or long-session/device qualification.

The engine is a recurring companion during play. Its normal sound should be
comfortable and almost pleasurable to hear over time, not merely impressive
for an eight-second demonstration. Direction 22's second audition (heavier
structural resonance) is the preferred starting point. The next comparison
adds a denser component and blends all three complementary characters:

- Direction 22 / 02: the principal engine identity and resonant weight.
- Direction 22 / 01: lighter articulation and detail within that identity.
- New dense layer: additional body and texture under heavier demand.

The user requested a range within one engine, not three unrelated engines or
a sequence of abruptly switched samples. Smoothly vary contributions with
load and spool response, preserving the preferred core. Do not equate density
with higher playback volume, add a permanent crescendo, or bring back a fan
wash. Near-frequency interaction and repetitive flutter require listening
review; a larger oscillator count alone is not an improvement.

### Confirmed correction: one overall level, dynamic character

After direction 23, the user clarified and confirmed that demand should change
the blend, **not make the healthy engine louder**. Matching the average level
of two complete clips is insufficient when each still swells with throttle.
The next audition must hold a comparable overall perceived level from powered
idle through high demand, smoothly replacing contributions rather than adding
more signal on top. The player's volume control sets playback loudness.

Use overlapping idle, light, core and dense regions, not hard percentage
switches. The idle voice is continuous while powered; zero commanded thrust
does not silence it. Engine-off and existing pause/mute/focus/invalid-state
safety gates still silence output. Fault cues remain separate future work.
This is a healthy-engine mixing target, not an instruction to flatten every
alarm or atmosphere sound through a new global compressor.

Calibrate the actual correlated mix at intermediate blend positions as well as
anchors. Prefer a smooth known gain curve over a reactive automatic gain
control that pumps with each waveform fluctuation. Equal RMS is a useful
diagnostic, not proof of equal perceived loudness; the sustained human audition
must confirm that the tonal change does not sound like a volume change.

Absolute flight speed must not drive fictional engine RPM. Thrust can cease
while velocity persists in vacuum. Powered idle and active propulsion remain
distinguishable by character rather than relying on loudness. This revises the
earlier engine-only audition's silent zero-throttle behavior, not the existing
running prototype or exterior-vacuum policy. Atmosphere remains an additional
pressure-dependent transmission/airflow layer around the same ship identity.

## Preserve room for condition

The user proposed later chirps or pitch changes to communicate damage,
overheating and other conditions. Reserve that attention-getting contrast;
do not bake warning-like effects into the healthy continuous sound. Specific
fault mappings, thresholds, cooldowns and localized wear cues remain future
work driven by actual simulation state. Audio must not fabricate damage.
Existing visual/status information must remain useful without sound.

## Listening and implementation gates

1. Compare the idle-to-dense dynamic blend on identical short throttle timing.
   Keep direction 22 / 02 as the character reference, not its throttle-driven
   gain envelope. Report individual steady-load levels, intermediate-position
   behavior and peaks, not only full-clip normalization. RMS is not perceived
   loudness or comfort.
2. Include a longer uninterrupted audition with steady light/heavy-load holds,
   transitions and a quiet end. Do not repeat a short clip and call that a
   sustained synthesis test. Human listening on ordinary stereo speakers is
   the deciding comfort check, with headphones useful as an additional check.
3. Keep source, private random seed, source/output hashes and BSD-3-Clause
   provenance with offline artifacts. Test finite samples, DC offset, clipping,
   channel compatibility and fade boundaries before the listening handoff.
4. Only after the direction is accepted, translate the selected synthesis and
   blend into the bounded native audio path. Recheck telemetry validity,
   silence/coast/atmosphere behavior, pause/mute/focus safety, device playback,
   and frame-time cost. An offline Fourier-shaped texture is not evidence of
   bounded runtime synthesis or a seam-free looping asset.

The offline comparisons do not replace the running playtest and do not qualify
spatial sound, long-session listening comfort or production-ready output.
