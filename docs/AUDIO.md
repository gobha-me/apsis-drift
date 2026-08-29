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

The render-source seam currently produces validated silence. Empty, odd-sized,
or over-4096-frame callback buffers are rejected without modifying the buffer
or consuming queued events.

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

An output backend must make `stop()` synchronous with its callback before the
runtime clears playback state. The render callback performs no allocation,
filesystem access, logging, application traversal, or mutex acquisition; it
only validates/fills the caller's fixed buffer and drains the lock-free event
queue.
