# Recorded audio in saved Freedom play

The native launcher can reuse the recorded ship-audio adapter with a
user-provided pair of prepared hum and propulsion WAV loops. The accepted
[recording mix and source-rights boundary](RECORDED_SHIP_AUDIO_28.md) still apply.
The current private recording derivatives are not cleared public game assets.
Recording paths and playback state are separate from C++ world saves.

```sh
tools/run_godot_native.sh --new-game=42 \
  --audio-hum=/absolute/path/to/hum.wav \
  --audio-propulsion=/absolute/path/to/propulsion.wav
```

The paired flags also work with Continue. Add `--audio-persist=false` for
session-only mix settings. Relative, unpaired, duplicate or malformed options
refuse before launch. Headless validation uses Dummy audio and does not load
recordings. Ordinary play uses Godot's normal system driver. Without a pair,
the adapter is inactive.

WAV validation retains the existing 16 MiB/file, 1–60 second, canonical PCM,
peak/DC and loop-seam checks. Rejected files disable playback with a diagnostic.

## Telemetry and lifecycle

One playback owner follows the current view across boarding, disembarking,
world changes and explicit recovery. Walking and unavailable views are silent.
The flight pause menu exposes the existing four mix sliders and mute, using
the existing versioned audio preferences independently of world saves.

Negative-Z main thrust and lateral/vertical channels feed main demand from the
strongest applied force fraction; positive-Z feeds the existing retro profile.
Opposing gross firings remain distinct. Torque-only audio remains future work.
Density and dynamic pressure come directly from C++. Audio does not infer
propulsion from keys, velocity or gravity.

Cockpit machinery and conduction can remain audible in vacuum. Exterior sound
follows density; dynamic pressure drives the existing quiet airflow layer.
Transit supplies no propulsion or airflow, while cockpit machinery can continue.
These are presentation rules rather than physical acoustics or damage laws.

Pause, focus loss, dialogs/menus, errors, malformed buffers and stale telemetry
silence the existing envelope/watchdog. View handoffs reuse the same three
players. Window/menu exit uses the bounded cooperative drain, including stopped
players.

## Verification

`native_audio_test.gd` uses generated BSD-3-Clause test WAVs with Dummy playback.
It checks malformed options, nonfinite/short telemetry, actual propulsion and
opposing channels, transit presentation, pause/focus/staleness, volume controls,
walking/refused Continue, one-owner handoffs and window-close drain. Playback
and mix changes preserve complete Save As bytes.

Run through `tools/test_godot_native.py --test native_audio` with the usual
explicit Godot/build arguments. Listening comfort, physical devices, device
loss, spatial compartments, wear/damage tones and cleared public recording
sources remain #254; silent-driver contracts do not qualify these outcomes.
