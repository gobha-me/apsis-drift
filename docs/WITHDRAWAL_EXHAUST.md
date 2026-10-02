# Native withdrawal exhaust

2026-10-02, #374. The ordinary saved-flight view now presents the actual gross
negative body-Y force used to withdraw from an Origin Station port. The existing
two aft main plumes still consume negative-Z force independently. C++ remains
the sole owner of propulsion, flight, station constraints, time and saves.

## Source and visual interface

The retained lower lift outlets face negative Y and therefore represent positive-Y
reaction; they cannot honestly show the opposite withdrawal channel. A separate
trial using the upper internal lifeboat attitude fittings found the assembled
exterior hull crossing both plume axes. That refusal remains preserved.

This change explicitly authors two simplified assembled-craft exterior cosmetic
apertures on the retained coated structural skin, with upward plumes. It does
not claim historical nozzle meaning for the lifeboat fittings. The original
master, imported model, contact geometry and thrust allocator remain intact.
An internal manufacturing throat or fluid simulation is outside this visual
interface.

The selected source is Craft09 SHA256
`87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677`.
The fixed skin has identical POSITION, NORMAL, UV and index bytes in operating02
model `a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c`
primitive14 and the actual saved-flight model
`12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8`
primitive18. Material and resolved texture bytes match too. Exported triangle
IDs belong to these material-batched exports, not original authoring polygons.

| Mouth | Fixed seed triangle | Body X/Z (m) | Exact-datum display Y (m) | Render anchor Y (m) |
|---|---:|---|---:|---:|
| Port | 150829 | -1.309999942779541 / -1.4500000476837158 | 2.0551589514498816 | 2.055159091949463 |
| Starboard | 149833 | 1.309999942779541 / -1.4500000476837158 | 2.0589798322994657 | 2.0589799880981445 |

The [registered interface](https://github.com/gobha-me/apsis-drift/issues/374#issuecomment-5961580859)
fixes each X/Z anchor and a 36-mm square before evaluation. Its complete unique
connected exterior chart uses four actual triangles at port and two at
starboard. Exact clipped projection covers the square without overlapping
positive-area pieces. The datum is the exact maximum clipped skin Y plus
10 mm; the render anchor is its outward float32 conversion. The plume tapers
within an 18-mm radial envelope over 600 mm, pointing +Y.

The complete operating craft at REST and BOARDING and the complete actual static
consumer were checked for finite chart exposure, access from skin to start,
plume clearance and outward visibility. Only initial contact on the declared
skin is intentional; no whole object was skipped. This static effect check
does not establish actor boarding, continuous occupied hardware motion or
departure collision clearance.

The aperture is an extra procedural material pass on the exact skin surface.
It reuses existing vertices and marks only the two projected disks within their
qualified height ranges. The original coated material remains outside those
disks. Other same-material layers are below the height gates. No raised mesh,
glass override, hull hole or new collider is introduced. Mesh centres are
rounded outward after accounting for the actual float32 height, and an exact
diagonal basis flips the cylinder's bright base toward the skin without a
trigonometric axis tilt.

The loader retains the qualified skin surface without generated LODs or lossy
attribute compression, so the tiny conformed mark stays on the qualified
triangles at every viewing distance. Other surfaces retain their existing
compression and generated LODs. This keeps more skin triangles resident;
hardware performance remains a separate measurement. The native contract hashes
the actual imported positions and complete triangle indices, allowing only
Godot's fixed winding conversion.

## Applied force and verification

Both effects validate the selected Wayfarer flight owner, attachment flag, all
four complete positive/negative force and rating arrays and elapsed time.
Malformed input hides both groups without advancing phase. Pause and attachment
also hide both groups and freeze phase. Intensity reads gross negative force
divided by its own channel rating. Positive opposing firing, gravity, drag and
coasting velocity cannot erase or invent a nozzle firing.

Terminal flight-view errors also hide both groups immediately. Exhaust refresh
follows terrain validation, so a failed presentation frame freezes the prior
visual phase. A valid C++ flight batch committed before a terrain error remains
committed; the view does not roll back simulation time.

The real native saved-flight and port tests exercise independent main/vertical
commands, the first withdrawal tick, zero net vertical force with opposing
firings, pause, attachment, coast and invalid ownership/buffers. Existing
independent C++ save-byte and cadence comparisons remain intact. Actual generated
mesh vertices are recorded for a separate numeric envelope check.

The opt-in port capture adds a withdrawal image after the first 120 of the
existing 720 departure ticks. The remaining 600 ticks preserve the original
final trace, including its three-second delay before forward thrust. Captures
record both intensities, applied gross force/rating arrays and phase. Headless
contracts alone do not qualify visible rendering or hardware performance.

`native_withdrawal_capture.gd` provides an additional opt-in close inspection
using the same three arguments as the port capture. It performs a real port
capture, release and 120 withdrawal ticks. Four images compare a paused effect
with three animated visual phases at one unchanged committed C++ state. Its
caption identifies this inspection, and its manifest records state, force,
renderer and script hashes. It is separate from the gameplay camera.

The procedural plume and aperture shaders are original BSD-3-Clause code under
the repository license. The craft retains its existing
[asset provenance and license records](NATIVE_STARTER_ASSETS.md). No paid
generation or source-master edit was used.
