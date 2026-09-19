# Native ship sound study 19

2026-09-19; bounded prototype for #254. This is an opt-in presentation layer,
not a new authoritative audio policy, replacement score, or damage simulation.
Existing C++ procedural audio, MIDI work and First Light assets are unchanged.

## What should be heard

- Inside the cockpit, quiet machinery continues during a vacuum coast.
- Commanded main/retro thrust adds structure-conducted engine vibration;
  vacuum does not mute the interior of a powered craft.
- Airflow follows effective aerodynamic density and dynamic pressure, not raw
  orbital speed. It fades away in vacuum.
- Exterior vacuum is silent. An exterior camera is not an imaginary microphone
  wired to the hull; any later cinematic sound option must be explicit.

All inputs are read-only telemetry. The synthesizer cannot fire engines, change
assistance, advance a tick, alter flight checksums or consume world-generation
random streams. Malformed telemetry targets silence. Camera changes alter the
audible perspective only. Audio sample output is presentation, not a supported
save/replay checksum contract.

## Use and safety

Pass `--ship-audio=true` after Godot's `--` separator to enable the prototype.
It is off by default. Use the platform audio backend for listening; the Dummy
driver exercises behavior without speaker output. Begin at a comfortable low
system volume. This study does not change system volume or select another
device for the player.

The pause menu has a controller-accessible **Mute ship audio prototype** toggle.
Its value lasts for this process only; no existing controller profile or saved
setting is rewritten. Pause, focus loss, inspection modes and terrain loading
silence the ship layers. Returning focus does not resume flight automatically.
Unmuting while paused must remain silent. The audio node starts inactive.

Synthesis runs at 24 kHz. At most 1,920 authored frames (80 ms) are queued,
excluding backend/device latency; a process call produces at most 2,048 frames
in blocks of at most 1,024. Layer gains fade over 60 ms. A pause/mute request
therefore is not an instantaneous physical mute: queued sound and the fade
must drain. The node retires playback after the silence has passed through.
Telemetry older than 250 ms expires; a long frame stall discards the old queue.
This is a bounded main-thread prototype, not a real-time audio thread: sustained
frame intervals longer than the queue coverage can underrun and need a later
buffering/threading solution. No unbounded catch-up work is performed.

The local Dummy cadence probe reported underruns even at 60 requested fps:
its bursty consumption is not a substitute for an actual output backend.
Increasing the queue from 40 to 80 ms reduced those observed underruns but did
not eliminate them. The actual-generator test establishes bounded lifecycle
behavior, not glitch-free audio. Keep this distinction when evaluating timing
reports; sustained real-device playback under GPU load is still unqualified.

## Scope and future condition sounds

Three bounded layers cover machinery, propulsion and airflow. Local private
noise state and oscillators are original code-authored sources; there are no
downloaded samples, model-generated clips or new credentials. The source is
registered in `assets/provenance.json` under BSD-3-Clause. Offline audition
captures must retain their adjacent provenance metadata.

The telemetry adapter is the future seam for localized condition. Actual
component identity, wear, damage and player-relative location must first come
from authoritative simulation. This prototype invents no leaks, rattles, crew,
damage percentage or repair sounds. It does not claim spatialized walking audio,
engine exhaust propagation, ship acoustics simulation or device hotplug
qualification. Godot owns physical output-device handling; disconnect behavior
still requires hardware testing.

## Acceptance boundary

Automated tests cover invalid values before playback, finite bounded samples,
quiet coast versus thrust, vacuum transmission, smooth transitions and
pause/mute/focus gates. Main/bridge integration must leave authoritative state
unchanged. Dummy playback and waveform checks cannot certify comfort, tone,
click-free physical output or controller listening quality: those need a human
audition on the actual playback device.

### Reproducible checks and audition

Run these scripts with the pinned Godot executable, `--headless`,
`--audio-driver Dummy`, and `--path experiments/godot-freedom`:

- `--script res://ship_audio_test.gd`: CPU synthesis, malformed inputs,
  bounded waveform transitions and independent local noise state.
- `--script res://ship_audio_playback_test.gd`: real generator playback through
  a test-only Dummy adapter, including queue bounds and drain/stop/resume.
- `--script res://ship_audio_cadence_test.gd`: short local Dummy timing probe at
  60/30/20/15/10 requested frames per second. Reports wall time, CPU cost and
  underruns; those timings are measurements, not portable performance promises.
  A separate controlled, fine-grained consumer asserts queue/work bounds and
  nominal scheduling capacity at 20 fps and above; it is not a hardware model.
- `--script res://ship_audio_integration_test.gd -- --snapshot=ABSOLUTE_PATH`:
  inherited main/menu/focus routing with actual native bridge telemetry. Only
  graphics bootstrap and the flight-frame body are omitted.
- `--script res://ship_audio_capture.gd -- ABSOLUTE_NEW_OUTPUT_DIRECTORY`:
  five four-second PCM16 stereo comparison clips plus a provenance sidecar.
  The output directory must not already exist. No speaker output is opened.

Compare cockpit idle in vacuum, cockpit thrust in vacuum, the same thrust in
atmosphere, exterior atmospheric flight, and exterior vacuum. The last clip is
deliberately silent. The others use authored telemetry fixtures, not a recorded
flight or a claim of realistic acoustics. Captures have source hashes, exact
fixture inputs, peak/RMS measurements and license metadata. No encoded audio
is added to the runtime package; synthesis source is the runtime asset.

Local evidence: GCC/Clang builds and nine targeted native contracts per compiler
passed. Existing provenance, MIDI and First Light contracts passed (five checks
in the GCC configuration, four in Clang; optional level target absent there).
CPU, actual-generator Dummy and main/bridge audio suites passed. Native GPU
controller regression and mute-menu inspection passed with Dummy audio; this
does not qualify speaker output, headphones, device disconnects or listening
quality. Hosted CI and human audition remain separate gates.
