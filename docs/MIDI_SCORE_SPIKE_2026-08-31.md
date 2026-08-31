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
testing a remaining risk; the selected path is already more than 350 times
inside the callback p99 budget.

| Candidate | Pin / license | Source archive | 48 kHz CPU, memory, latency | Asset and authoring | Layer/transition fit | Decision |
| --- | --- | ---: | --- | --- | --- | --- |
| TinySoundFont + Drift parser | `853a0a171759f1ddba0de1442133a75912bbeffa`, MIT / BSD-3-Clause | 757,978 bytes; 92,719-byte synth header | Measured below: 0 overruns, 6.8 MiB peak RSS or less, p99 0.012 ms or less | 551,861 committed bytes; ordinary MIDI editor plus stable track/marker convention | Direct immutable ticks, tracks, tempo/meter, phrase markers, and semantic fades | Selected. No upstream source was modified; callback exclusively owns synth operations. |
| `midifile` | `98917df5b1bf0d6e8d4c0e5fff86d6b05343e793`, BSD-2-Clause | 182,112 bytes | Parser-only; rendering metrics unchanged | MIDI workflow, but projection is still required | Reserves 10,000 events per track, trusts hostile dimensions too far, and emits parser errors to stderr | Rejected for the smaller fail-closed Drift parser. |
| FluidSynth | `e0b9a9ddc5fd30d0745fca4dbb74369455030eb7` (2.6.0), LGPL-2.1-or-later | 2,271,322 bytes | Not run: no host package; mature float rendering but a materially larger unmeasured runtime surface | Reuses the 551,354-byte bank and MIDI authoring | Supports sequencing, but its player/threading surface exceeds the four-layer boundary | Fallback only if a future bank requires unsupported SoundFont features. |
| libxmp | `a13276d27feabcf9ee4f982913f718ee05a65cb7` (4.7.2), MIT | 20,851,737 bytes | Not run: no equivalent module asset; fixed-buffer rendering is available | Requires a second tracker composition and embedded samples, so asset size is not comparable without changing the authored content | Pattern/order control does not retain the selected MIDI track, tempo-map, and marker contract | No demonstrated production advantage; do not add a parallel representation. |

TinyMidiLoader is also rejected because its millisecond event projection loses
the track and musical-time structure needed for layer and boundary decisions.

## Assets and licensing

| Item | License / distribution result |
| --- | --- |
| Drift SMF parser, scheduler, and authored MIDI | Repository BSD-3-Clause; source and MIDI bytes may ship under the project license. |
| TinySoundFont | MIT notice retained in `THIRD_PARTY_NOTICES.md`; source is fetched at the exact pin and compiled only when the spike is enabled. |
| MechSounds sample subset | CC0-1.0; redistribution and derivatives are allowed, with source and transformation provenance retained. |
| SF2cute `3c5fc83b6ba3d1feb377f9c86021fd77499eb7c0` | zlib/libpng license; recipe-only build tool whose code is not incorporated into the bank or shipped by this repository. |
| FFmpeg conversion | External recipe-only tool; no executable or library code is redistributed by this repository. |

The code-authored format-1 score is 507 bytes, has four named tracks, 68
channel events, two tempo points, one 4/4 meter point, two phrase markers, and
explicit loop markers. Its SHA-256 is
`c75fdc71afe720dfc9ccb7dda4417c21626283a2f6ef9cadeaaad7791cde7a08`.

The prototype bank selects `AMB-DARKFIRE`, `TNL-DATAPLUK`, `PRC-INDSTHIT`,
and `TNL-RUSTECHO` from John Oestmann's CC0 MechSounds archive dated
2026-03-01. The source archive SHA-256 is
`d0817d9c2c1f05cef0ea06c29c51e519df72a23a4e1e2fbdd3681024dec9a6c1`.
The samples were converted to mono 48 kHz signed 16-bit PCM and repacked as
four presets using SF2cute `3c5fc83`. The committed bank is 551,354 bytes,
contains 550,514 decoded sample bytes, and has SHA-256
`3ca303da85862557cbfd82a51458a34ffa2e3a1afc1017354c250f3e6011ba03`.
The exact recipe is `tools/regenerate_issue230_assets.sh`; manifest and notice
records preserve the source, edits, CC0 terms, and attribution.

The spike implementation and tests are about 1,900 lines of C++ plus a 237-line
one-shot asset recipe/builder. On this glibc host the standalone spike adds no
dynamic dependency beyond the ordinary C/C++ runtime libraries; TinySoundFont
is compiled into the default-off target.

## Measurements

Both measurements used a Release build, RtAudio off, 48 kHz stereo float,
400-frame blocks, 100,000 consecutive offline callbacks, the same assets, and
no competing build. The 400-frame deadline is 8.333 ms and the approved p99
gate is 4.166 ms.

| Evidence | GCC 14.2 | Clang 20.1.8 |
| --- | ---: | ---: |
| Schedule checksum | `4520889698453040989` | `4520889698453040989` |
| PCM checksum | `5400705655840410743` | `5400705655840410743` |
| Callback average | 0.003113 ms | 0.003374 ms |
| Callback p99 | 0.009562 ms | 0.011284 ms |
| Callback maximum | 0.032258 ms | 0.121746 ms |
| Deadline overruns | 0 | 0 |
| Peak RSS | 6,876 KiB | 6,356 KiB |
| Spike executable | 252,320 bytes | 225,368 bytes |
| Peak PCM | 0.199512 | 0.199512 |
| RMS PCM | 0.016665 | 0.016665 |
| Maximum adjacent-sample delta | 0.083139 | 0.083139 |

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
  loop, and stale-note reset.
- Provenance validation covers both committed assets; normal builds remain
  dependency-free with respect to TinySoundFont and MIDI assets.
- The evidence host exposed no usable physical output device. GCC 13 and Clang
  20 builds pass with RtAudio both enabled and disabled, and the offline driver
  renders through the #25 `AudioRenderSource` seam; physical output and
  device-loss playback could not be exercised locally and remain CI/runtime
  integration evidence for the production follow-up.
- Production adoption must add the sidecar validator, `MusicDirector`, runtime
  lifecycle integration, preferences, final score bank, and authored adaptive
  traces as separately scoped work.

Manual audition: **PENDING — repository owner**. Evidence file:
`build-issue230-gcc/issue230-audition.wav`.
