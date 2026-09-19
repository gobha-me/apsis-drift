# Healthy ship sound: listening direction

2026-09-19, issue #254. This records human audition choices, not completed
runtime integration or an acoustic simulation. The existing native adapter and
safety boundaries remain in [study 19](NATIVE_SHIP_AUDIO_19.md); the C++ audio
contract, MIDI and other existing audio work are preserved.

## Approved intent, not final sound acceptance

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

Absolute flight speed must not drive fictional engine RPM. Thrust can cease
while velocity persists in vacuum. Quiet powered machinery remains distinct
from active propulsion; this engine-only audition does not redefine the idle
cabin bed or exterior-vacuum policy. Atmosphere remains an additional
pressure-dependent transmission/airflow layer around the same ship identity.

## Preserve room for condition

The user proposed later chirps or pitch changes to communicate damage,
overheating and other conditions. Reserve that attention-getting contrast;
do not bake warning-like effects into the healthy continuous sound. Specific
fault mappings, thresholds, cooldowns and localized wear cues remain future
work driven by actual simulation state. Audio must not fabricate damage.
Existing visual/status information must remain useful without sound.

## Listening and implementation gates

1. Compare the new dense layer and the three-layer blend against the preferred
   direction 22 / 02 on identical short throttle timing. Report linear level
   matching and peaks honestly; RMS is not perceived loudness or comfort.
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
