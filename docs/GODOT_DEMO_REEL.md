# Godot demo reel — Beyond the paperwork

Issue [#349](https://github.com/gobha-me/apsis-drift/issues/349) follows the
owner's request for a 5–10 minute short film by 20:00 America/New_York on
2026-10-01. The delivered film is **322 seconds (5:22)**, with a small narrative told through
Godot-rendered text: a pilot preparing to leave home and a station clerk with
opinions about the paperwork. The story and character roles are editorial.
They do not implement NPC dialogue, a mission, or a conversation system.

The film composes actual Hero character resources with metre-registered station
rooms in scripted Godot scenes, plus native walking, source-bound Wayfarer
hardware poses and seeded flight/world views. This tests scale, occlusion and
contact gaps instead of assembling character review movies. Chapter labels
distinguish C++ gameplay from editorial NPC blocking. A cut from station to a
selected saved orbit does not demonstrate continuous boarding, departure,
landing or return.
The underlying C++ state remains authoritative; film cameras and captions cannot
commit an action or advance a second game world. This is an owner-review film,
not a release or a runtime-performance benchmark.

[The storyboard](GODOT_REEL_STORYBOARD.md) records the planned shot order and
dialogue blocking. Actual captures and the final assembly receipt determine the
delivered duration.

## Integrated scenes and source resources

The owner clarified that ready assets should be placed together and scripted
in Godot, with multiple scenes stitched as needed. The actual Hero and Station
sessions supplied reusable modules, textures, rooms, fixtures and source clocks
in `docs/ASSET_COORDINATION_2026-10-01.md` in the original checkout. Their replies
are source handoffs; they do not certify the final integration or agree to
unfinished gameplay features.

The cast contains two identities: the female pilot and male clerk. The same
male appears in a workshop role using the newly ready casual service lean.
Scripted greetings, movements and the dialogue are editorial acting, without
production NPC AI, conversation state, inventory or economic gates. Native
walking and flight retain C++ authority. Do not disguise the missing boarding
transition with a seamless departure cut.

[Integration readiness and gaps](REEL_INTEGRATION_GAPS.md) records the selected
APIs, 1.78/1.85 m body references, fixed interaction plane, heading conversion,
provenance and actual runtime checks. `tools/stage_reel_characters.py` preserves
original `res://` paths, verifies runtime bytes against Hero checkpoint
`d932d4939d87a9d6551bcfc407141eafca5aba26`, and archives source metadata in
`character-resources.json`. The isolated film project keeps its own
`project.godot`; source masters and import caches are not modified or copied.

The actual room libraries and fitted station service-instrument scene remain
at their source scale. Workshop and personnel-door animation import uses an
explicit **24 Hz bake** with source-frame/24 seeks. Cinematic capture runs on
its own frame clock.
The separately delivered observation derivative is now tested through an
isolated [registered window scene](GODOT_REEL_OBSERVATION.md), showing the same
C++ world through source glazing.
Control and service-target fit must be measured against the real source assets,
not concealed by resizing actors or moving a panel per drawing.

| Scene / source-specific notes | Authority and demonstrated behavior | Retained gap |
| --- | --- | --- |
| [Registered station and characters](GODOT_REEL_STATION_READINESS.md) | C++ fresh seed42 supplies station, docked craft, player foot/heading and shared ticks; Hero artwork follows the real player. The clerk has explicit cinematic blocking. | General NPC navigation, interaction outcomes and unsupported room routes. |
| [Workshop inspection](GODOT_REEL_WORKSHOP.md) | Actual full room, retained instrument, same male identity, held key4 and an illustrative pointer fitted to the measured screen. C++ stays at tick0. | Screen contact is not an electrical service connector or repair; no male service cycle. |
| Wayfarer / D1 hardware | Existing operating consumer previews source-bound station controls and exact admitted craft knots. | Shaft traversal, threshold/cabin admission, boarding and seat ownership. |
| [Independent saved flight](GODOT_REEL_FLIGHT.md) | Actual C++ force/exhaust, coast and attitude; 80 seconds of ordinary fixed-step physics followed by 20 seconds of explicitly paused planet inspection. | The flight starts from a separate orbit save; it does not continue the station actor's journey. |
| [Observation window](GODOT_REEL_OBSERVATION.md) | Registered source bay, authored shutter opening and aligned near/far views of the same frozen C++ seed42 world. | Editorial camera only; no supported player route, pressure event or window gameplay. |

Use the scene-specific notes for reproduction and receipts, and
[the integration inventory](REEL_INTEGRATION_GAPS.md) for character APIs,
coordinate conventions, optional-asset loading and outstanding fit work.
Dialogue captions are Godot-rendered editorial text. They should leave faces,
hands, feet and the demonstrated contact visible, with a dark plate over bright
interiors. No lip-sync or implemented conversation system is asserted.

Inherited Hero reference/derived-art redistribution remains **uncleared**, as
its source receipts explicitly state. Hero authorizes the owner-requested local
integration/review, which is the scope here. The separate registered
`LicenseRef-Apsis-Station-Kit-Output` grants permit station copying and
derivatives under BSD-3-Clause. This does not change character-art clearance.

The prior clip selection is archived readiness evidence only: Hero suit/walk/
service captures at `49181768cad102207292d82164ad482a58b3221a`, casual-male at
`8a58368876800a8f1cbc7fb6f25e56f71fe72f97`, and the original Station clean movies
under `docs/media/station-assembly/reel/`. Their per-source hashes and receipts
were checked. The earlier ignored clip inventory and `build-reel/inputs`
copies describe that superseded movie plan, not the current
film's integrated asset resources or final duration.

## Existing music, without another provider job

Use the original checkout's `build-godot/film-first-light.wav`, with its
`film-first-light.json` report. It is an existing **60-second, 48 kHz stereo
PCM16 music-only production-path render**, 11,520,044 bytes, SHA-256:

```text
4f6ad100db755e79eb8e5a4f5e32d2fffbef9a28dc1264f2df4a988163fb97ce
```

The committed score and sidecar are code-authored under BSD-3-Clause. The bank
uses `LicenseRef-Apsis-First-Light-Generated-Output`, with redistribution and
derivatives allowed in [asset provenance](../assets/provenance.json). The
[First Light terms](licenses/FIRST_LIGHT_GENERATED_OUTPUT.md) expressly grant
use, copying, redistribution, public performance and derivatives under the
repository's BSD-3-Clause license for those selected outputs. This audio is
not subject to the character-art clearance limit. Its ambient source attribution
is **Apsis Drift contributors; generated with Stable Audio 2.5 through Venice.ai**.
The render uses the existing MIDI and SoundFont; no paid generation is needed.

The assembler overlaps repeats by four seconds using triangular crossfades.
That gives a 56-second offset between starts, exactly two 28-second score loops.
Measured head 0–4 s versus tail 56–60 s correlation is `0.999849`; their RMS
difference is `-59.28 dBFS`. A raw 60-second repeat has a discontinuity and does
not share that phase alignment. The source WAV stays unchanged; the final
crossfade, trim, fade and loudness filter is retained in the film receipt.
An audio-only execution of the actual assembler graph produced and fully decoded
120 seconds of 48 kHz stereo PCM, exactly 5,760,000 sample frames, including two
four-second crossfades. This checks the graph and clock, not final listening.

If the existing WAV is unavailable, generate the same 60-second audition from
the committed assets into a new disposable output directory. The audition
program's duration is fixed; it does not accept a longer-duration argument.

```sh
cmake -S . -B build-reel-audio -DAPSIS_DRIFT_TERMINAL=ON -DAPSIS_DRIFT_MIDI_SPIKE=ON
cmake --build build-reel-audio --target apsis-drift-audio-pack-audition -j 4
mkdir -p "$REEL_AUDIO_OUTPUT"
build-reel-audio/apsis-drift-audio-pack-audition assets \
  "$REEL_AUDIO_OUTPUT/first-light.wav" \
  "$REEL_AUDIO_OUTPUT/audio-source.json" --music-only
```

Set `REEL_AUDIO_OUTPUT` to a new directory first; this program opens its outputs
for writing. Prefer borrowing the existing verified WAV to rebuilding. Its
production trace changes musical layers over the minute; any film-length
repeats, crossfades and final gain/fades belong to the separate editorial mix and
should be recorded in the final receipt. First Light is a non-diegetic
soundtrack here, not proof that the current native start
already plays this music. The privately prepared recorded ship loops are not
required for this edit.

## Labels and credits

Use short feature labels at the relevant cuts:

- **Native station walking**: actual saved actor input; stop on supported space.
- **Wayfarer hardware study**: source-bound preview; craft uses exact recorded
  knots. No player climb, boarding or supported cabin route is asserted.
- **Scripted character scene**: actual staged actors in station geometry; cinematic NPC blocking.
- **Selected saved flight** or **Seeded world inspection**, according to the
  actual source. An inspection camera moving over terrain is not flight input.
- **Observation study / frozen C++ world**: source shutter and editorial camera
  reveal actual world geometry; the player does not walk into the bay.

Suggested short human credits, rendered over a moving final shot:

- Apsis Drift — design and direction by the project owner
- Engine, station, spacecraft and character studies — Apsis Drift contributors
- Pixel source art — built-in OpenAI image generation; archived requests/references
- First Light — Apsis Drift contributors; ambient source: Stable Audio 2.5 / Venice.ai
- Rendered in Godot 4.7.2 — local owner review

Keep the full source/license evidence in an accompanying receipt rather than
turning the closing shot into a dense license page. Character staging binds
scripts, frame manifests and textures; room/capture receipts bind physical
source assets and editorial scene scripts. The archived movie inventory remains
readiness evidence. Preserve the existing music render/report, MIDI/bank/sidecar
identities and the specific local-reuse responses alongside the final receipt.
The film does not edit a master, native save or another session's project.

## Final assembly and verification

The final edit contains sixteen excerpts from five newly scripted Godot
captures: station walking/hardware, integrated station characters, the
observation bay, workshop and independent saved flight. None is an asset
session's review movie. The frozen edit totals **322 seconds**, with **7,728
frames at 1920×1080 / 24 fps** and **48 kHz stereo AAC**. Both stream clocks
match the edit and the complete video/audio decode passed.

The final movie SHA-256 is:

```text
299776a775d6ad30112435a69ef07adb731f8eb571deeaa1307a2cac544cf6a2
```

Local delivery retains the MP4, dialogue SRT, edit/capture/card receipts,
source hashes and source/license evidence outside Git. The soundtrack graph and
encoded audio clock were checked; this is not a claim of a listening review.
Private character-art clearance still limits distribution to owner review.

`tools/reel_narrative.json` holds the editorial text.
`experiments/godot-freedom/reel_text_cards.gd` renders transparent 1080p plates
and records their hashes with the frozen narrative. The default dialogue panel
starts at y=860, preserving the feet in the integrated station framing. The
workshop wide view uses a left panel to preserve its contact demonstration.

The assembler takes a schema-1 JSON plan with a `music` path and SHA-256, and
`clips` containing source path/hash, start, duration, truthful scope label, and
optional card intervals. Use new output directories:

```sh
python3 tools/assemble_godot_reel.py "$REEL_PLAN" "$REEL_CARDS" "$REEL_OUTPUT"
```

Set those variables to the locally prepared plan, frozen Godot cards and a
nonexistent delivery directory. `--proof` permits a 1–60 second excerpt;
normal delivery requires 5–10 minutes. Invalid/non-finite or off-frame timings,
source overruns, changed cards/source hashes and existing outputs are refused.
The receipt binds the complete edit and source probes; it does not certify
continuous boarding or flight departure.
