# Native follow-through — studies 31 and 32

This batch continues #200, #267 and #277. It does not replace the accepted
recorded audio, frozen playtest, canonical gameplay pilots or legacy saves.

## Station frame boundary

The [rigid frame handoff provider](RIGID_FRAME_HANDOFF.md) now transfers a
canonical state between system and origin-station-relative coordinates at the
same authoritative tick. It preserves orientation and body spin exactly,
validates ownership and numeric bounds, and refuses unsupported planetary
transitions. Both compiler builds and all 17 focused C++ contracts pass; new
handoff checksum fixtures agree across GCC and Clang. Independent review found
no blocker. Pinned formatting and focused clang-tidy 20 pass; complete pinned
lint remains a hosted CI gate because the local standard command lacks its
required system compiler/tool names.

This is not live docking or completion of #200. The inconsistent legacy
ten-minute lighting day versus 24-hour flight spin still needs an explicit
unified rotation decision under #213, without reinterpreting old generations.

## Arms driven by resolved flight commands

The [semantic motion prototype](PILOT_SEMANTIC_MOTION.md) drives two grips and
the seated arm chains from resolved pitch, roll, yaw and heave. It does not play
a prerecorded whole-body sweep or modify C++ flight state. Keyboard, synthetic
controller events and remapped bindings produce equivalent poses. Head-look
suppression and inactive return are checked. The isolated native runner now
contains 26 contracts, passing against both compiler builds.

Separate six-second GPU clips show female and male proof rigs following a
twelve-phase demand sequence with the source animation paused. Recorded maximum
bone-relative grip drift is below 0.00031 mm and segment-length error below
0.00033 mm; actual wrist movement is approximately 19–21 mm. These are transform
measurements, not fingertip/surface, garment collision or performance guarantees.
Capture receipts pin the exact source version used; later validation-only code
changes do not retroactively alter those receipts.

The driver is not wired into gameplay yet. First-person head/body partition,
clean cockpit/seat/control integration, discrete reach/press/return and final
art remain unfinished. #277 stays open.

## Original suit construction

Study 31 replaces the temporary borrowed bib garment on the female fit rig with
original petrol/slate tailoring, graphite articulation and an offset sand zip.
Refinement 2 cleans up collar/cuff edges, narrows the zip, improves seam
attachment and replaces circumferential knee bands with rear gussets. The native
central seat sample measures approximately 0.500 mm clearance over 90 frames
after an upholstery-only adjustment; eye, grip, pedal and anatomy anchors stay
unchanged. This is sampled vertex clearance, not full mesh collision proof.

The complete native fixture is 14,613,492 bytes, 44 mesh nodes and 169,968
triangles, including seat, controls, pedals and studio floor. Five original
textile maps and five temporary CC0 skin/eye/footwear maps have source receipts.
Editable masters, native PNGs, a three-second clip and output hashes are retained
in ignored authoring output. Prior studies are preserved.

This remains early garment construction: skin, hair, gloves, pressure-suit
hardware, restraint fit and cinematic material quality need further work.
Neither the approved concept nor the final character acceptance is replaced by
this study. #267 stays open.
