# Native constant-level ship sound, study 25

2026-09-19, issue #254. Ports the human-accepted direction 24 into the existing
native audio node. The subtle range is accepted for now; this is not permission
to exaggerate it or to claim the final sound design is complete.

## Runtime behavior

One powered voice crossfades idle, lighter detail, the preferred resonant core
and a denser component. Commanded main/retro demand changes the smoothed blend,
not a throttle-volume multiplier. A 21-point static correction curve compensates
the actual correlated sum. It does not follow waveform amplitude like an AGC.
The old separate machinery bed is removed so idle is not doubled.

Zero thrust in an active cockpit retains the powered idle character. Paused,
muted, unfocused, stale or invalid telemetry still targets silence through the
existing 60 ms safety fade. Exterior vacuum remains silent. Atmospheric
airflow remains a separate pressure-dependent component; the constant-level
claim applies to the healthy engine voice, not every combined sound or future
warning. No damage state or condition cues are invented.

The runtime retains the approved carrier/body coefficients, blend and level
curve. Bounded causal private-noise filters replace the offline Fourier-shaped
textures, and the runtime uses its safety fade instead of the audition's longer
intro/outro fade. Therefore it is not bit-identical to the approved WAV. No
world-generation randomness, physics state, music or legacy audio is changed.

Queue capacity and per-process synthesis work remain bounded as in study 19.
The port does not add an audio thread, looping sample library or engine-specific
acoustic propagation model. Main-thread stalls and actual output-device quality
remain separate concerns.

## Verification

- GCC and Clang builds pass all ten selected native/landing-envelope contracts.
- CPU synthesis, invalid input, continuous idle, safe silence, mono compatibility,
  chunk independence across phase wraps and Dummy playback lifecycle checks pass.
- `ship_audio_level_test.gd` measures all 21 curve knots and 20 midpoint loads
  on the actual runtime filters: about -25.997 to -25.965 dBFS, peak below 0.128.
  This qualifies numerical level behavior, not equal perceived loudness.
- Actual main/bridge menu, mute, pause and focus integration passes. Neutral
  resume checks current zero demand with the single powered voice active, not
  the obsolete separate-idle-bed/zero-engine expectation.
- Controlled fine-grained consumption remains within queue/work limits at
  20 fps and above; the local 15 fps case also passes, while 10 fps underruns.
  Bursty Dummy-driver underruns remain and are not real-device qualification.

The native 40-second capture's measured idle/light/medium/high levels differ
from the approved offline comparison by less than 0.02 dB. That is useful port
evidence, not a substitute for listening. Real-device comfort, native playback
under sustained GPU load, and audible transitions remain human checkpoints.

## Repeat the checks

Run the selected Godot executable with `--headless --audio-driver Dummy`,
`--path experiments/godot-freedom`, and each of:

- `--script res://ship_audio_test.gd`
- `--script res://ship_audio_level_test.gd`
- `--script res://ship_audio_playback_test.gd`
- `--script res://ship_audio_integration_test.gd -- --snapshot=ABSOLUTE_SNAPSHOT`

The updated `ship_audio_capture.gd` writes a native 16-second load sweep,
40-second comfort fixture and separate atmospheric example, with source/output
hashes and BSD-3-Clause provenance. Pass a new absolute output directory after
the `--` separator. Capture uses CPU samples without opening audible output.

For a fresh editor import, use the separately documented
[editor timing workaround](GODOT_EDITOR_IMPORT.md), never that delay in a
runtime/performance test. The existing `--ship-audio=true` playtest option and
controller-accessible mute control remain unchanged.
