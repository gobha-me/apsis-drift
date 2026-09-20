# Recorded ship audio and player mix controls — study 28

2026-09-20; advances issue #254 without closing sound design or asset licensing.

## Current direction

The build 25 synthesized voice failed human listening review. It remains an
explicit historical regression path (`--ship-audio=true`), not the accepted
voice or a fallback when recordings are missing. Study 28 uses native looping
players with `--ship-audio=recorded`, `--audio-hum=/absolute/loop.wav` and
`--audio-propulsion=/absolute/loop.wav`. This opt-in requires live presentation.
Missing or rejected files disable this audio path visibly in the log.

Private prepared loops follow the approved comparison 27 balance: background
hum at -28 LUFS and propulsion at -26 LUFS, with fixed offline calibration.
They are 18-second, 48 kHz stereo PCM16 loops made from the supplied references,
with a two-second endpoint overlap. Native playback does not pitch-shift,
normalize, decode MP3s or generate recording samples in the frame loop.
Requested thrust drives a smoothed equal-power timbre blend, not speed-based
fictional RPM. Coast can retain velocity while returning to powered idle.

Start/Escape offers controller-focusable sliders for ship master, background
machinery, propulsion and atmospheric airflow, plus mute and restore defaults.
Each category runs from 0–100% of its prepared default. Versioned preferences
are saved separately from world state in `user://freedom_ship_audio_v1.json`;
bad, oversized or unknown-schema input retains the current/default mix.
Writes replace a temporary file atomically. Save failures remain session-only
and are shown in the menu. `--audio-persist=false` disables disk writes;
otherwise persistence follows `--controls-persist`, defaulting to enabled.

## Boundaries and safety

- C++ telemetry remains authoritative. Audio and settings never advance or
  mutate simulation, world seeds, controls or save generation.
- WAV loading is bounded to 16 MiB per file and 1–60 seconds. RIFF extents,
  alignment, canonical PCM format, peak, DC and endpoint continuity are checked
  before activation. Failed replacement preserves a previously valid pair.
- Paused, unfocused, muted, stale, invalid or unavailable flight state fades
  toward silence. The 80 ms presentation envelope is not a hard real-time
  silence deadline: frame cadence, device buffers and scheduling also matter.
- A 250 ms telemetry watchdog is checked on presentation ticks. Native players
  can continue during a completely blocked main thread until it resumes;
  resumed long frames stop playback rather than queue catch-up samples.
- External vacuum presentation is silent. Cockpit structure carries engine
  sound; atmospheric density transmits exterior sound, and actual dynamic
  pressure adds a quiet, startup-generated mono airflow loop. That procedural
  layer is original BSD-3-Clause code, not a physical acoustics simulation.
- Stereo is the present target. No spatial hardware, compartment propagation,
  wear, damage tones, fault pitch or long-session comfort is qualified here.

## Source rights and acceptance

User-supplied Pixabay-named MP3s and their derivatives remain ignored private
assets. Matching creator CC0 listings are recorded with their hashes and
processing receipts, but byte equivalence to original creator downloads is not
established. Public distribution requires acquiring the original cleared
sources and retaining their receipts before regenerating distributable loops.
No third-party recording is relabeled BSD or committed with this adapter.

The preference test covers invalid/non-finite/bounded input, transactional
loads, persistence and reset. The recording test uses generated fixtures for
WAV corruption, envelope and native Dummy-player lifecycle checks. The real
bridge/menu integration test covers sliders, synthetic keyboard/D-pad focus,
mute/reset, pause/focus/stale-state safety and unchanged native state.
These are not physical-controller or listening acceptance.

Run the scripts with the project's Godot executable and
`--headless --audio-driver Dummy --path experiments/godot-freedom --script`:

- `res://audio_preferences_test.gd`
- `res://recorded_ship_audio_test.gd`
- `res://recorded_audio_integration_test.gd`, followed by
  `-- --snapshot=/absolute/native-snapshot.json` (requires the native bridge).

The next human gate is native stereo listening: idle versus sustained thrust,
several loop boundaries, coast, atmosphere/vacuum, and saved slider comfort.
