# Audio Contract

Apsis Drift owns gameplay audio policy, cue identity, mixing, and synthesis.
An output backend owns only device discovery and delivery of application-mixed
sample frames. TermForge remains responsible for terminal input and
presentation and is not an audio transport.

The fixed application format is 48 kHz, stereo, interleaved 32-bit floating
point. The authoritative simulation runs at 120 Hz, so one simulation tick
maps exactly to 400 sample frames. An audio event is identified by its
authoritative simulation tick and a 16-bit sequence assigned in emission order
within that tick. Playback clocks, queue position, callback timing, latency,
underruns, voice state, and completion are presentation diagnostics and never
enter gameplay state, saves, or deterministic checksums.

## Queue and overflow

The application-to-mixer event path is a fixed-capacity, single-producer,
single-consumer queue of 256 events. Emission never waits on playback. When the
queue is full, the newest event is dropped after receiving its deterministic
identity; events already queued retain their order. Invalid cue IDs, regressing
ticks, sequence exhaustion, and tick-to-sample overflow are rejected without
mutating the queue.

The initial event contains only a strongly typed cue ID. Flight telemetry,
procedural voices, asset playback, and parameter updates enter through later
working game systems rather than turning this boundary into a generic engine.

## Disabled and no-device operation

Disabled mode assigns no identities and performs no queue work. The default
no-device backend opens no device, starts no callback thread, and never asks
the render source for samples. The application services that backend by
discarding queued presentation events without affecting simulation.

Load, return-to-title, backend loss, and shutdown are playback epochs. They
stop an active backend before clearing queued/mixer state and reset the
within-tick sequence so stale events cannot replay in a new session. Backend
loss falls back to no-device operation. Queue depths, high-water marks, drops,
resets, and backend state are available only through `AudioDiagnostics`.

The queue also carries one bounded flight-parameter update per authoritative
tick. A repeated update for the same tick is coalesced without assigning a new
identity or consuming queue capacity. Parameter events are ordered before any
warning cue emitted for that tick. Invalid or non-finite parameters fail closed.

The application derives engine demand and normalized speed from the active
legacy, planetary, system, or station-flight state. Planetary flight also
derives bounded atmospheric density from the generated planet and altitude.
The existing 24 metre cockpit low-clearance boundary emits one cue when flight
crosses from safe to low clearance; remaining below the boundary does not
repeat it. These observations and the edge latch are presentation-only.
Leaving flight emits one stop cue so continuous voices ramp to silence without
adding a second parameter update to the final authoritative tick.

The callback-owned mixer uses fixed-point phase, envelope, and noise state to
produce a phase-continuous engine voice, atmosphere/wind, and a bounded
low-clearance alert. Parameter targets slew instead of stepping, output is
finite centered stereo clamped to the fixed float format, and every playback
epoch resets the mixer. Empty, odd-sized, or over-4096-frame callback buffers
are rejected without modifying the buffer or consuming queued events.

## RtAudio device output

`APSIS_DRIFT_RTAUDIO=ON` builds the application-owned RtAudio 6.0.1 output
backend. Ordinary interactive play attempts an explicitly selected ephemeral
device ID or the platform default. Device IDs and callback timing are
presentation-only and are not persisted in a save. External preferences and a
stable player-facing device selection policy remain owned by issue #30.
Linux builds use ALSA, macOS builds use CoreAudio, and Windows builds use
WASAPI; optional APIs discovered incidentally on the build host are not added.

Automated benchmark, capture, and acceptance modes always select the no-device
backend. They therefore measure the same application rendering, encoding, and
terminal submission boundaries whether or not RtAudio was compiled.

Discovery, missing-device, invalid-selection, open, start, callback, and
disconnect failures synchronously stop the stream, clear the playback epoch,
and fall back to no-device operation. The realtime callback reports failures
through atomics only. The main thread emits one diagnostic per backend-state or
failure-count transition; it does not repeat a message for every callback.
Output underflow is counted as a presentation diagnostic but is not by itself
a device-loss signal.

## Audio synthesis benchmark

`apsis-drift --audio-benchmark [TICKS] [--report PATH]` replays a fixed
procedural flight-parameter trace directly through the offline mixer. It
reports rendered sample frames, represented audio duration, measured wall
time, real-time factor, queue high-water/drop counts, and the waveform
checksum. This is synthesis CPU evidence only: it does not include renderer,
terminal encoding, proxy, decoder, compositor, device, or speaker throughput,
and it does not claim a portable hardware percentage.

An output backend must make `stop()` synchronous with its callback before the
runtime clears playback state. The render callback performs no allocation,
filesystem access, logging, application traversal, or mutex acquisition; it
only validates/fills the caller's fixed buffer and drains the lock-free event
queue.

## Deterministic MIDI score boundary

The production runtime validates a Standard MIDI File completely in bounded
memory, projects tracks and musical time into an immutable schedule,
preallocates an embedded SoundFont synth, and then transfers only fixed
semantic layer commands to callback-owned state. `APSIS_DRIFT_MIDI_SPIKE=ON`
adds the isolated offline research and First Light audition executables; it no
longer controls whether the production parser and synth are compiled.

The contract permits at most 64 KiB of MIDI, 16 tracks, 16,384 events, 960 PPQ,
64 voices, a 4 MiB packaged SoundFont, and 16 MiB decoded sample data. Tracks
are named `ambient`, `pulse`, `percussion`, and `tension`; MIDI channels remain
internal routing details. The callback performs no parsing, filesystem work,
allocation, logging, or blocking synchronization. Beat, measure, phrase, and
loop decisions derive from integer MIDI ticks plus tempo and meter maps rather
than callback wall time.

The dated [decision report](MIDI_SCORE_SPIKE_2026-08-31.md) owns the measured
MIDI-versus-module comparison and the boundary between repeatable event
scheduling and floating-point PCM.

## First Light production pack

Interactive runs load `assets/music/first-light-score.json` from the `assets`
root, or from the explicit `--audio-assets PATH`. The version-1 sidecar accepts
only the fixed score/bank IDs and files, four ordered semantic layer mappings,
finite gains from zero through one, the required loop and phrase markers, and
the four supported transition boundaries. Unknown or duplicate fields,
excessive nesting, unsafe path components, symlinks, missing files, malformed
media, and exceeded budgets reject the whole pack before playback. Failure
emits one diagnostic and preserves procedural audio without partially loading
the pack.

The SMF is limited to 64 KiB. The packaged SoundFont is limited to 4 MiB and
its decoded sample chunk to 16 MiB. Each effect must be mono 48 kHz PCM16 WAV,
at most two seconds; the six effects together are limited to 1 MiB packaged,
eight seconds of PCM, and 4 MiB of conservative stereo-float residency. The
committed pack uses 3,316,791 packaged audio bytes and 5,042,288 decoded bytes.
All parsing, filesystem access, validation, and allocation occurs before the
audio callback.

The callback owns an eight-voice fixed effect pool and drops the newest effect
when it is full. Stable cues cover UI navigation, confirmation, rejection,
communications notice, signal lock, and signal completion. The semantic music
states map Signal Run presentation edges as follows: docked keeps the ambient
bed, flight adds pulse, scanning adds percussion on the next beat, warnings add
tension immediately, and completion resolves on the next phrase. Pause/resume
retains musical position; every playback epoch resets score and transient
voices without entering simulation or save state.

The 60-second production-path evidence render measures -23.25 LUFS-I and
-12.01 dBTP for music alone. The six final effects each measure -20 LUFS-I
within 0.05 LU and remain below -3.6 dBTP. The score loop and embedded ambient
sample are both exactly 28 seconds; the rendered loop boundary changes by one
PCM unit. Use `apsis-drift-audio-pack-audition` from a spike-enabled build to
produce the repository-owner listening artifact.
