# Station scene readiness for tonight's film

This is an integration assessment for issue [#349](https://github.com/gobha-me/apsis-drift/issues/349), not a boarding or NPC implementation. The film now stages the ready character resources inside the registered station and renders those scenes in Godot. Earlier review movies are readiness evidence and are not the character construction method.

## Available physical stage

The selected Origin Station and Wayfarer are loaded by the existing operating consumer from the approved starter/operating packages. Fresh C++ seed 42 supplies station identity, station basis/position, attached D1 craft, player foot/heading and the shared simulation clock. Local coordinates below are **Godot station metres, +Y up**, not Blender source coordinates or an invented second world. Project positions through the current C++ station transform; keep character roots at scale one.

The present application-owned standing actor has a 0.64 m wide, 1.93 m high reservation. Its geometry package covers the westward corridor crop `x [-24.5, 0.45], y [-0.05, 2.1], z [-0.875, 0.875]`; the complete standing box must fit inside that domain. The rendered hub is broader than this crop, and the model shows commons/habitation/arrival doors. That does **not** make every adjoining room a supported player route. Keep the actual player on the existing supported route for this film.

A read-only probe of the existing C++ geometry confirmed an actual fixed-step route: 24 rightward steps at heading π/2 move the player from `(0,0,0)` to `(0,0,-0.4)`; 1,320 forward steps then reach `(-22,0,-0.4)` without obstruction. This is 120 Hz input, not a reposition. Independent standing-box samples `(-1,0,0.4)`, `(-2,0,0.4)`, `(-16,0,-0.4)`, `(-20,0,-0.4)` and `(-22,0,-0.4)` are clear and supported. A positive-z side lane is obstructed around x=-12; do not assume two side-by-side lanes stay open throughout the workshop passage. These probes qualify source geometry for staging; they do not create a second simulated actor or implement NPC collision.

## Useful blocking and cameras

| Place | Character feet / route | Editorial camera and target | Scope |
| --- | --- | --- | --- |
| Hub / west passage entrance | Fresh player `(0,0,0)`; after real sidestep/walk `(-1,0,-0.4)`. Clerk `(-1,0,0.4)` or `(-2,0,0.4)`. | Camera `(-4,1.55,0.2)`, target `(-0.6,0.92,0)`; closer comparison camera `(-3.6,1.4,-0.25)`. | Both identities, floor registration and metre scale are visible. Nearby characters genuinely occlude one another. |
| Workshop passage | Player follows the supported negative-z lane toward `(-16,0,-0.4)`. Keep an accompanying clerk at least 1.2 m ahead/behind when using the centre lane; source-check any lateral transition. | Camera about `(-13.5,1.65,0.1)`, target `(-16,1.0,-0.2)`; final framing needs a short render. | C++ player motion is ready. Clerk travel is explicit cinematic choreography, not NPC AI or collision. |
| D1 approach | Stop player at `(-20,0,-0.4)` or the demonstrated `(-22,0,-0.4)`; keep another figure inboard, with separate standing reservations. | Camera about `(-18.9,1.65,0)`, target `(-22,1.1,0)`; established deck study view `(-21.9,1.7,0)` toward `(-24,0.1,0)`. | Supported standing approach is ready. Shaft descent, ladder use and boarding are unfinished. For closure studies, keep characters well inboard rather than assuming an animated guard and actor have a committed interaction. |

Characters can remain together through a continuous choreographed route, or the film can cut between explicitly staged scenes. The player must never be moved to a new foot position by the camera/character renderer. A cinematic clerk can use a finite authored root path; check floor/geometry and avoid body overlap, but do not describe this as production NPC navigation.

## Character integration contract

The frozen character resource delivery is `d932d4939d87a9d6551bcfc407141eafca5aba26`: 49 runtime files / 30 PNGs, preserving the original `res://` paths, with the complete source/provenance roster in `character-resources.json`. Both selected casual identities are available: female 1.78 m and male 1.85 m. Import caches belong only to disposable film projects; authoring masters and source textures remain unchanged.

Female movement uses `trials/walk-04/pilot_presentation.gd.present_movement(heading,to_camera,distance,moving)`. C++ heading zero faces -Z; this art API's heading zero faces +Z, so the adapter uses `wrap(C++heading + π)`. Player root translation is the actual C++ station-local foot. Gait phase derives from actual travelled distance. The caller converts the camera vector into the same station-local frame.

The stock fixed-Y billboards assume global up. Disable that mode in the **runtime instance** and turn the card about station-local Y toward the camera; retain shading and depth testing. Contact art uses its fixed source plane instead of locomotion billboarding. Both source-sized figures fit the demonstrated hub view without resizing or lifting them to disguise a mismatch.

The casual male supports four idle directions, side/axial walking and two held greeting drawings. The newer casual-actions adapter also exposes a held service key and grip anchor. These are ready drawings, not a continuous male service cycle. Female press/touch/service presentation supplies fingertip/grip anchors for supported source frames. Anchors are fitting aids: they do not implement pickup, inventory, a switch result, repair or tool success. Use the same male identity for clerk/technician scenes; a different role does not justify an unexplained third clone.

## Ready details and remaining gaps

- The registered station has visible source cabinets, door frames, wall controls, workshop approach and D1 hardware. Existing D1 closure and Wayfarer roof/ladder/inner/seat previews are source-bound. Craft channels accept only admitted 21 sample knots; no continuous baked-delta interpolation is claimed.
- The source actor APIs can show an approach, stop, walk, greeting and supported held contact drawing. Dialogue and NPC choreography are cinematic. They do not add a game conversation system, repair logic or a new simulation actor.
- The newer station workshop instrument and passage-door assemblies have authored metadata and reusable source rigs. Their isolated readiness does not automatically place them in this C++ Origin Station or implement a controller. A workshop integration must explicitly bind the physical fixture and character grip in the same scene, and label unqualified contact/action outcomes.
- General Origin Station door handlers, switch events, terminal outcomes, collision-aware NPC paths and inventory/tool pickup are missing. A nearby wall plaque is not an invented contact/grip point. Do not press a hand through a source fixture or claim a door opened because of a production interaction.
- Player descent into the compact shaft, the existing threshold step, cabin admission, seat attachment and departure as one continuous actor journey remain unfinished. The later independently loaded flight chapter must have a visible editorial cut.
- Whole-pose sprite gait is coarse and may slide. There is no dedicated strafe drawing: the proof's initial real sidestep necessarily reuses the available gait art. Source cards have no cast shadow, which can make correct feet feel less planted. Preserve real foot/height registration; do not offset figures or add artificial scaling to conceal that limitation.
- Character art retains its inherited private-study redistribution limitation. Tonight's owner-review use does not clear a public release.

## Evidence so far

The actual integration proof renders both characters inside the same physical station with a real C++ short input trace, from tick 0 to tick 95. Invalid pose requests preserve prior visual state; the player and body share the same tick, and the saved checkpoint is format 20. Source hashes, character resource receipt, bridge hash, metre heights, foot positions and card transforms accompany the images.

An independent Hero-resource review confirms distinct identities, plausible source-sized bodies against the door/deck, visually seated soles without obvious floor clipping, working inter-character depth occlusion and upright station-local planes. The first attempted wall-control camera was inside the broad hub and therefore did **not** prove wall occlusion; it is retained as honest development evidence. The corrected outside-corridor camera `(-8,1.2,2)` is blocked by the actual opaque source wall: neither character draws through it with depth testing retained. Final dialogue needs a dark caption plate where the ceiling is bright.

## Integrated narrative capture

The selected integrated scene consists of three 40-second chapters at 1080p / 24 fps: `hub-meeting` (frames 0–959), `go-west` (960–1919), and `d1-approach` (1920–2879). The same female pilot and male clerk remain in the actual registered station. The clerk moves aside near the hub before the pilot centers in the corridor. His finite choreography is presentation-only; no NPC simulation or collision certification is implied.

The C++ pilot takes the measured hub approach to `(-1,0,-0.4)`, returns to the corridor center, walks west to `(-16,0,0)`, then approaches D1 to `(-22,0,0)`. Every displayed output frame advances exactly five shared 120 Hz ticks, including zero-input dialogue holds. Each chapter advances 4,800 ticks; the full scene reaches tick 14,400. Chapter boundaries use real format-20 Save As and reopen with unchanged actor/body snapshots. No player position teleport, craft boarding, ladder descent or production dialogue action is added.

The follow camera blends beside the source card before reaching the actual C++ first-person eye. During that final approach, its existing station-local card plane is retained; the avatar is hidden only when the camera intersects the card's near-plane rectangle. A coincident horizontal eye/card direction keeps the last valid artwork view instead of submitting an invalid request to the strict character API. Camera motion remains editorial.

The upper truth caption has a dark plate; the lower 220 pixels are reserved for final dialogue. Conversation cameras aim slightly lower so both full-height figures clear that lower region. The west-follow shot deliberately uses shoulder framing rather than claiming a complete foot shot. D1 observations stop on the supported platform and show the shaft without claiming entry.

The frozen 29-image review passes with both GCC and Clang: all images and all six save documents are byte-identical, including actor/body snapshots, camera transforms and NPC samples. Five invalid CLI controls reject without changing outputs. Footage is an offline software-rendered 24-fps delivery with a 120-Hz authoritative timeline; its encoding rate is not an interactive performance claim.

The completed source is exactly 120 seconds / 2,880 frames, with 960 byte-distinct PNGs per chapter. All 29 full-capture review samples match the frozen GCC/Clang review; all six format-20 save documents and boundary snapshots match. The H.264 CRF18 source is 1080p / 24 fps, BT.709 limited, silent, and passes counted-frame probing plus a complete decode. Original PNGs, model/source receipts, checkpoint bytes, encode command and movie hash remain together in the ignored owner-review output.
