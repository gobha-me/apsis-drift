# Deterministic MIDI score spike — 2026-08-31

## Decision

**Technical decision: PROCEED WITH MIDI.** Use the Drift-owned bounded SMF
parser and pinned TinySoundFont commit
`853a0a171759f1ddba0de1442133a75912bbeffa`. Do not adopt `midifile`,
FluidSynth, TinyMidiLoader, host MIDI, or tracker/module playback for the first
production implementation.

This is a research result, not runtime integration. `APSIS_DRIFT_MIDI_SPIKE`
is off by default. Production work remains a separately reviewed issue, and
the First Light inventory, normalization, packaged-byte total, and authored
music remain owned by #29.

Production status (2026-09-01): #29 adopted this boundary for the First Light
pack. The parser and TinySoundFont renderer are now part of the ordinary game
library; `APSIS_DRIFT_MIDI_SPIKE` controls only offline evidence and audition
executables. The measurements below remain the source-faithful spike record.

The required listening owner is the repository owner. The final audition
verdict must be recorded here before #230 is merged.

## Selected boundary

- Parse at most 64 KiB from memory before commit: format 0/1, 16 tracks,
  16,384 channel events, 960 PPQ, 100,000,000 absolute ticks, printable
  64-byte track names/markers, explicit end-of-track, and bounded chunk/VLQ
  arithmetic. SMPTE timing, malformed running status, duplicate tempo/meter
  points, trailing track bytes, truncation, and overflow fail closed.
- Project events, tempo, meter, markers, and source-track identity into an
  immutable schedule. Integer MIDI ticks are the musical clock; callback or
  wall-clock duration never defines a beat, measure, phrase, or loop.
- Validate the SoundFont RIFF/sample dimensions, preload it from memory,
  preallocate 64 voices total and all 16 channels, and give each semantic layer
  callback-owned synth/cursor/gain state. The callback performs no parsing,
  filesystem access, allocation, logging,
  application traversal, or blocking synchronization.
- Transfer only fixed SPSC commands for play, pause, stop, loop, bounded master
  volume, or a layer target with `immediate`, `next_beat`, `next_measure`, or
  `next_phrase`. Queue overflow rejects the newest command without changing
  playback or simulation.
- Keep `MusicState`, `MusicDirector`, game-facing control integration,
  production diagnostics, hot score swaps, and game-state mapping for the
  follow-up.
  Raw MIDI messages, channels, synth handles, paths, and callback clocks remain
  internal.

The production sidecar direction is JSON schema version 1. It identifies the
score and bank by provenance asset ID, maps stable `ambient`, `pulse`,
`percussion`, and `tension` layer IDs to MIDI track names and default gains,
names `loop-start`/`loop-end` plus `phrase-*` markers, and lists the allowed
transition boundaries. It does not expose channels or duplicate MIDI timing.

## Parser and synth assessment

Compressed source-archive bytes below were measured from GitHub archives for
the exact revisions. Runtime rows are measured only for the implemented MIDI
path: producing comparable libxmp evidence would require authoring a second
composition, and FluidSynth was not installed on the evidence host. Adding
either solely to manufacture a benchmark would broaden this spike without
testing a remaining risk; the selected path is already more than 220 times
inside the callback p99 budget.

| Candidate | Pin / license | Source archive | 48 kHz CPU, memory, latency | Asset and authoring | Layer/transition fit | Decision |
| --- | --- | ---: | --- | --- | --- | --- |
| TinySoundFont + Drift parser | `853a0a171759f1ddba0de1442133a75912bbeffa`, MIT / BSD-3-Clause | 757,978 bytes; 92,719-byte synth header | Measured below: 0 overruns, 4.5 MiB peak RSS or less, p99 0.019 ms or less | 52,960 committed bytes; ordinary MIDI editor plus stable track/marker convention | Direct immutable ticks, tracks, tempo/meter, phrase markers, and semantic fades | Selected. No upstream source was modified; callback exclusively owns synth operations. |
| `midifile` | `98917df5b1bf0d6e8d4c0e5fff86d6b05343e793`, BSD-2-Clause | 182,112 bytes | Parser-only; rendering metrics unchanged | MIDI workflow, but projection is still required | Reserves 10,000 events per track, trusts hostile dimensions too far, and emits parser errors to stderr | Rejected for the smaller fail-closed Drift parser. |
| FluidSynth | `e0b9a9ddc5fd30d0745fca4dbb74369455030eb7` (2.6.0), LGPL-2.1-or-later | 2,271,322 bytes | Not run: no host package; mature float rendering but a materially larger unmeasured runtime surface | Reuses the 52,178-byte bank and MIDI authoring | Supports sequencing, but its player/threading surface exceeds the four-layer boundary | Fallback only if a future bank requires unsupported SoundFont features. |
| libxmp | `a13276d27feabcf9ee4f982913f718ee05a65cb7` (4.7.2), MIT | 20,851,737 bytes | Not run: no equivalent module asset; fixed-buffer rendering is available | Requires a second tracker composition and embedded samples, so asset size is not comparable without changing the authored content | Pattern/order control does not retain the selected MIDI track, tempo-map, and marker contract | No demonstrated production advantage; do not add a parallel representation. |

TinyMidiLoader is also rejected because its millisecond event projection loses
the track and musical-time structure needed for layer and boundary decisions.

## Assets and licensing

| Item | License / distribution result |
| --- | --- |
| Drift SMF parser, scheduler, and authored MIDI | Repository BSD-3-Clause; source and MIDI bytes may ship under the project license. |
| TinySoundFont | MIT notice retained in `THIRD_PARTY_NOTICES.md`; source is fetched at the exact pin and compiled only when the spike is enabled. |
| Drift tonal prototype bank | Repository BSD-3-Clause; every waveform and envelope is code-authored and may ship under the project license. |
| SF2cute `3c5fc83b6ba3d1feb377f9c86021fd77499eb7c0` | zlib/libpng license; recipe-only build tool whose code is not incorporated into the bank or shipped by this repository. |

The code-authored format-1 score is 782 bytes, has four named tracks, 132
channel events, two tempo points, one 4/4 meter point, two phrase markers, and
explicit loop markers. Its SHA-256 is
`3d8ca137e9cd1ee13dcb92e844416e22b6333b2f7eb5f30f045c1a6d004f004e`.

The first audition failed because quarter-note machinery clips left 1.5-second
gaps and sounded like harsh noise. The replacement bank is code-authored from
three seamless low-harmonic waveforms and one decaying sine percussion voice;
the score sustains ambient harmony continuously and adds a soft arpeggio,
percussion pulse, and counterline through the layer transitions. The committed
bank is 52,178 bytes, contains a 51,248-byte decoded `smpl` chunk, and has
SHA-256
`511ebfa80fef166156faba2878cdcd0b9a066ab2ce367596cf4ab32ee15a9e2e`.
The exact recipe is `tools/regenerate_issue230_assets.sh`; the manifest records
its code-authored construction and license.

The spike implementation and tests are about 1,900 lines of C++ plus a 325-line
one-shot asset recipe/builder. On this glibc host the standalone spike adds no
dynamic dependency beyond the ordinary C/C++ runtime libraries. TinySoundFont
is compiled into the production game library and the optional evidence tools.

## Measurements

Both measurements used a Release build, RtAudio off, 48 kHz stereo float,
400-frame blocks, 100,000 consecutive offline callbacks, the same assets, and
no competing build. The 400-frame deadline is 8.333 ms and the approved p99
gate is 4.166 ms.

| Evidence | GCC 14.2 | Clang 20.1.8 |
| --- | ---: | ---: |
| Schedule checksum | `394862525842229051` | `394862525842229051` |
| PCM checksum | `4437640985499822839` | `4437640985499822839` |
| Callback average | 0.008554 ms | 0.009772 ms |
| Callback p99 | 0.016250 ms | 0.018189 ms |
| Callback maximum | 0.094642 ms | 0.342707 ms |
| Deadline overruns | 0 | 0 |
| Peak RSS | 4,584 KiB | 4,556 KiB |
| Spike executable | 252,272 bytes | 225,408 bytes |
| Peak PCM | 0.135068 | 0.135068 |
| RMS PCM | 0.040876 | 0.040876 |
| Maximum adjacent-sample delta | 0.010416 | 0.010416 |

The schedule checksum is required to match across compilers, render cadences,
and callback partitions. A repeated render with the same toolchain and
partition is bit-identical. Different callback partitions preserve the event
schedule and bounded PCM properties, but TinySoundFont's floating-point block
processing is not specified as bit-identical. The matching GCC/Clang PCM hash
above is evidence for this host, not a portability promise across CPUs,
standard libraries, compiler versions, or floating-point modes.

## Verification and remaining work

- Valid and malformed SMF tests cover byte/track/event/PPQ limits, headers,
  chunks, truncation, VLQs, running status, meta lengths, names, loop markers,
  and transactional SoundFont initialization.
- Render tests cover finite stereo PCM, buffer dimensions, schedule stability,
  repeated-render identity, callback partitioning, all transition boundaries,
  gain ramps below the 0.1 adjacent-sample threshold, queue capacity, stop,
  loop-end transitions, looping, and stale-note reset. The 16.7-second
  replacement audition has no silence interval at least 80 ms below -45 dB.
- Provenance validation covers both committed assets; normal builds remain
  dependency-free with respect to TinySoundFont and MIDI assets.
- The evidence host exposed no usable physical output device. GCC 13 and Clang
  20 builds pass with RtAudio both enabled and disabled, and the offline driver
  renders through the #25 `AudioRenderSource` seam; physical output and
  device-loss playback could not be exercised locally and remain CI/runtime
  integration evidence for the production follow-up.
- Production adoption in #29 added the sidecar validator, `MusicDirector`,
  runtime lifecycle integration, final score bank, and authored Signal Run
  traces. Persistent player audio preferences remain separately scoped to #30.

Manual audition: **PASS — repository owner, 2026-08-31**. The initial machinery
fixture failed for long pauses and harsh noise; the replacement tonal fixture
was accepted. Evidence file: `build-issue230-gcc/issue230-audition.wav`,
SHA-256
`330f9393e68b2e45e3170fdf7e718d7424116c6ac28d5ed1f762e1293f3ea2de`.
