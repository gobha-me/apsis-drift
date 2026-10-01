# Beyond the paperwork: integrated scene plan

Owner-directed private review, 2026-10-01, [#349](https://github.com/gobha-me/apsis-drift/issues/349).
The pilot wants to leave a comfortable station and discover what lies beyond
the mapped lanes. A clerk supplies paperwork, directions and unexpectedly useful
advice. Machinery gets the glamorous close-ups; people get the good lines.

The camera follows the pilot, becomes the pilot's eyes while walking, then
leaves that viewpoint to watch the next action. The story uses text dialogue.
The conversations are editorial fiction, rather than implemented NPC logic,
contracts, communications or rent collection.

| Scene | Planned duration | Action and camera | Actual integration boundary |
| --- | ---: | --- | --- |
| A place to return to | 28 s | Station and attached Wayfarer reveal; opening title over moving camera. | Existing C++ placement and admitted native models. |
| Eleven minutes | 40 s | Follow the pilot approaching the clerk, then frame their exchange. | Pilot movement uses actual C++ walking; clerk blocking uses staged presentation assets in the same physical station. |
| A quiet window | 20 s | Observation bay shutter and restrained camera move toward the glazing. | Authored camera/shutter over the actual frozen C++ world; no supported player route. |
| Expensive machinery | 24 s | Walk west; blend from following the pilot to the actual eye position; look toward the workshop. | Existing supported hub/workshop corridor. No control interaction is implied by looking. |
| Confidence pending | 36 s | Three short workshop views: room, display and technician. | Actual registered workshop and SD-01 with the authored held inspection pose. The display is not an electrical service connector. |
| Independent return capability | 32 s | Approach D1 on foot, stop at the supported platform, then inspect the craft and port. | Actual C++ walking ends before the unsupported shaft/boarding transition. |
| Better benefits | 28 s | Roof/ladder and seat hardware close-ups, with a final practical warning. | Read-only source-bound recorded mechanism poses; no actor climb or occupied seat is shown. |
| A little room to breathe | 100 s | Powered exterior, cockpit, coast, attitude and quiet planetary view. | An explicitly introduced independent C++ flight save, already in orbit. The final planetary view is an editorial camera over that same world. |
| Leave the light on | 14 s | Station walking shot and restrained closing credits. | A station chapter reprise, rather than a demonstrated flight return or landing. |

The frozen delivered edit is **322 seconds (5:22)**, including the 20-second
observation-window scene. The assembly receipt binds the exact excerpts and
card timings. Neutral holds in the westward walk and D1 approach were trimmed
after capture; no actor action was replaced with a camera transition.

## Dialogue blocking

The opening encounter uses `c01`–`c05` from
[the narrative](../tools/reel_narrative.json): berth renewal, eleven minutes,
an excellent renewal rate, the mapped lanes, and a longer walk home. Reserve
seconds 4–9, 10–15, 16–21, 24–29 and 30–36 of the encounter for those cards.

The westward walk uses `c06`–`c08`: directions, expensive machinery, and the
whole station. Reserve seconds 3–8, 9–14 and 16–23. Hold the remaining time for
actual walking and the change of viewpoint.

The workshop uses `c12`–`c14`, one exchange per selected view. The D1 approach
uses `c09`–`c11` at seconds 7–12, 13–20 and 22–28. The hardware study uses
`c15`–`c18`. The flight uses `c19`–`c28`; leave visible breathing room between
the exchanges rather than filling every shot with text.

The dark dialogue plate normally occupies the bottom 220 pixels of the
1920×1080 delivery. Cameras should preserve a clear view of the participants,
feet and inspected geometry outside that region. A left plate keeps the
workshop's wide-view face, instrument and feet clear. Source heights, floor datums and station orientation
remain fixed; framing is adjusted to the assets.

## Delivery constraints

Use rendered engine scenes assembled from ready assets. Other sessions'
review movies are evidence of asset readiness, not final-film inserts. Preserve
source hashes, character checkpoint and runtime resource receipts, scene-script
identities, C++ state/tick/save evidence and the selected movie excerpts.

Boarding, seating, atmo departure and a complete return flight remain game
implementation requirements. This film does not substitute for those controls
or their tests. Current visual and motion gaps are recorded in
[the integration inventory](REEL_INTEGRATION_GAPS.md).
