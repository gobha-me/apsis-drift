# Planetary scale and surface fidelity

Contract checkpoint: 2026-10-10, active Freedom scope of #208. This defines what
future presentation checks must measure. It neither changes a saved recipe nor
claims every view below is implemented or qualified. The existing playable
prototype can continue while missing rendering/detail work remains recorded.

## Units and ownership

C++ owns selected universe/system/body identity, generator family and versions,
reference radius, orbital/spin state, terrain geometry, craft/actor pose and
the 120 Hz clock. Retain the complete owner: a numeric PlanetId cannot distinguish
authored origin terrain from its procedural namesake. A new view, LOD, cache,
viewport or graphics setting must not change those values or consume generation
streams. Historical worlds retain their selected recipes.

| Quantity | Meaning and unit |
| --- | --- |
| Reference radius R | C++ descriptor radius, metres; never a screen-fit parameter |
| Reference altitude h | Radial camera/craft distance minus R, metres; can differ from terrain clearance |
| Terrain elevation | Generated top geometry relative to its reference datum, metres |
| Clearance | Signed distance/gap to the selected generated geometry using the owning contact convention; not h |
| Local positions and dimensions | Physical metres after the C++ frame transform; no craft/terrain scale factor |
| Time, velocity and angles | Shared simulation ticks/seconds, metres/second and radians; convert explicitly for displayed kilometres/degrees |
| Camera | Actual eye, orientation, projection, aspect policy and viewport; cockpit/chase are different physical camera poses |
| Pixel error | Output-frame pixels after mapping internal pixels to output; separate from logical UI coordinates |

Subtract world/body positions in binary64 before conversion to Godot vectors.
The current local presentation maps East/Up/negative North to X/Y/Z. Reframing
must preserve the physical point, not simply reuse its old local coordinates.
Use same-tick planet rotation, star geometry and station motion. Paused capture
must preserve the complete C++ save, including resources and observations.
Exact replay still has its separately recorded cross-libm limitation (#255);
an image tolerance never excuses an authoritative mismatch.

## Regimes and handoffs

Regimes describe overlapping measurements, not new gameplay altitude limits.
Use actual descriptor/atmosphere boundaries rather than a universal entry height.

| Regime / fixture | Authoritative inputs | Visible invariant / acceptance target |
| --- | --- | --- |
| System approach, h = 3R calibration | Selected body centre/R, observer pose, current spin/star | Projected centre and spherical limb agree with the reference projection within one output pixel; clipping is permitted, shrinking the body to fit is not |
| Orbital approach, h = 0.1R calibration | Same body, frame and physical camera | Same projection rule; known surface anchors rotate with that body; orbit/system frame switches cannot relocate them |
| Entry and terrain flight, h = 30 km / 50 m calibration | Same descriptor plus versioned terrain owner, exact camera clearance | Horizon and matched physical landmarks remain within one output pixel across representation/LOD swaps at a frozen camera/tick |
| Landed cockpit / exterior | Saved landed pose, deployed gear, certified top triangles | Craft, feet/pads and terrain retain metre scale; the measured support gap agrees with the C++ certificate rather than a camera-derived floor |
| Suited ground, illustrative eye h = 2 m | Actual foot/eye/basis and same landed world | Surface anchor and nearby craft retain identity and metre scale; eye height comes from the actor, not a generic 2 m snap |

One pixel is a representation-change target allowing raster edge rounding,
not a measured current error or physics tolerance. Compare the same physical
camera and primitive/landmark in both representations. Changing to chase,
moving the actor or advancing the clock invalidates that comparison. A silhouette
comparison uses unlit geometry; atmosphere, clouds, shadows, texture and exposure
may legitimately change colour without moving the physical limb. At occluded
landmarks record occlusion rather than inventing a visible match.

No transition may select another body, alter R/pose/terrain owner, reset an
anchor, fit the globe to the viewport, or flatten a landing patch. Exact ownership
failures have zero permitted tolerance. Where a distant representation lacks a
particular terrain landmark, mark that continuity check unavailable until its
consumer exists; a coloured sphere is insufficient evidence for local fidelity.

## Projection calibration

For a spherical datum and a camera outside it, centre distance d = R + h,
angular radius alpha = asin(R/d), tangent horizon range =
sqrt(h * (2R + h)), and horizon surface arc = R * acos(R/d).
These follow from the right triangle between centre, eye and tangent point.
They exclude refraction and generated relief. Use the actual terrain ray for
the rendered top surface; do not treat the reference-sphere formula as collision.

For vertical perspective FOV v and frame height H, focal length in pixels is
f = H / (2 tan(v/2)). A centred sphere has projected diameter
2 f tan(alpha); portions outside the frame remain clipped. For off-centre
objects and oblique features use the complete camera projection, not that
centred formula. The native 75-degree default is vertical under Godot's
default aspect policy; record the actual policy in every capture.
[Godot Camera3D documentation](https://docs.godotengine.org/en/stable/classes/class_camera3d.html)
defines the projection/aspect behaviour.

An analytic 6,000,000 m datum gives approximately 4,898.98 m tangent range
at h = 2 m, 24,494.95 m at 50 m and 600,749.53 m at 30 km.
At h = 3R its angular radius is 14.477512 degrees. At 1080 output rows and
75 degrees vertical FOV, f is 703.741701 pixels. These are calibration cases,
not forged procedural worlds or the assertion that every home planet has R = 6 Mm.
The [fixture specification](../test/fixtures/planetary-scale-contract.json)
records the cases and measurement matrix.

## Recognizable relief and sampling

Measure a real feature's physical wavelength/width, crest-to-trough height,
surface sample spacing and projected size at the chosen eye/lighting. Keep the
terrain vertices, normal/material passes and final image distinct. Shader
colour, grain or a stable checksum alone cannot prove geometric relief.

The initial local-detail acceptance target is at least four samples across
the feature width and four output pixels across its projected width. The sample
target provides margin over the two-sample aliasing limit; the pixel target
requires more than an isolated pixel flicker. These are explicit engineering
targets for a chosen feature, not a new generation rule, a claim about current
LOD quality or a requirement that a naturally smooth planet grow ridges.
Use exact point projection for oblique features; f * width / camera-depth is
only a small-feature estimate. Report the projected crest/trough separation too.
If geometry or height quantization erases the selected relief, fail its fidelity
check even when colour suggests a ridge.

The current support policy remains terrain generator1, source LOD8, relief0,
mesh LOD13, 32 intervals and its existing anti-diagonal. Relief0 retains base
elevation; it is not a declaration of a perfectly smooth sphere. Historical
source tiles have 65 samples / 64 intervals. Do not confuse source samples with
render/contact triangles or infer spacing from the LOD number alone: measure
physical edges at the actual body/face/location. Exact shared source samples,
seams and recipe identity retain [their contract](TERRAIN_TILES.md).
New local detail remains #211; recognizable terrain implementation remains #212.

The current two depth layers share camera transform, FOV and viewport. Their
different clip planes may improve depth conditioning, but cannot change scale
or render close terrain over the foreground craft. Their existing bounds and
rough edges are documented in [saved flight](SAVED_NATIVE_FLIGHT.md).

## Capture and failure protocol

Use the ordinary seed42 station-to-surface journey and seed43 startup as
distinct generated owners. Include the neighbouring-system round trip to detect
stale tile ownership. The legacy seed4 airless snapshot is an explicitly legacy
case, not evidence of a native seed4 arrival. Analytic small/large datum cases
test projection without fabricating saved worlds.

Capture nadir, tangent/horizon and oblique views; cockpit, exterior and actual
suited eyes; day, near-terminator and night at retained same-tick star directions.
Freeze each pair rather than advancing a separate sunlight clock. Record the
body/recipe, complete Save hash, camera transform/projection, output and actual
internal dimensions, render method, clip planes, visible tile keys/spacing,
feature anchors, primitive coverage and image. Name unavailable combinations.

The output matrix is compact 800×450, wide 1920×1080 and 3840×2160, with native
and half-width/half-height internal resolutions. This is a qualification matrix,
not a claim of a shipped dynamic-resolution setting or 4K GPU acceptance.
Map internal measurements to output pixels explicitly. Reuse completed fixtures
and keep compact logs/source/results; retain imported test assets only for a
declared replay/capture, then remove that owned copy after process reaping.

Reject zero/negative/unsupported dimensions, nonfinite or degenerate pose,
nonpositive radius, missing/wrong owner and unsupported versions before staging
the replacement view. A negative reference altitude can be valid in a basin;
an unsupported/intersecting camera must not be rescued by a fabricated floor.
Use the owning terrain/contact refusal. Retain the active paused session and
files on failed selection. An unavailable target stays unavailable.

Report simulation correctness, renderer frame time/VRAM, preparation/cache size,
installation size and display pacing separately through #244. No 48 KiB cap,
fixed frame-rate promise or headless-to-GPU throughput inference enters this
contract. Retained terminal output must report the same units/identity/state
through its existing information fallback; new terminal image parity is not a
native gate. These target definitions complete the research boundary of #208;
their full visual/hardware execution remains consumer and #217/#244/#245 work.
