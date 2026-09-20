# Contact-patch and asset-fit follow-up — study 29

Build 28 received positive native audio feedback and its seated fit/motion was
accepted as a promising start, not finished character art. PR #278 merged after
all five hosted checks passed. The separate local historical replay failures
remain tracked under #255; neither these changes nor hosted CI resolve them.

## Preserve sound; retire playback on exit

The recording mix, prepared loop levels and player settings stay unchanged.
An observed six-object exit warning was reproduced using generated audio on
the Dummy backend: three WAV streams and their playback objects were retained
when the application exited immediately. Calling stop alone was insufficient.
Godot's [audio-server implementation](https://github.com/godotengine/godot/blob/4.7.2-stable/servers/audio/audio_server.cpp)
retires playback through audio-thread processing and main-thread cleanup.

Normal menu quit and window close now share an idempotent exit path. It freezes
flight presentation/input, stops and releases native players, and checks weak
references across frames before exiting. The maximum cooperative wait is
500 ms; an unretired backend produces a warning and cannot hold the app open
indefinitely. Scheduling or a completely blocked main thread can delay deadline
observation, so this is not a hard real-time guarantee. No audio node means no
drain wait. Legacy synthesis keeps its existing mute/exit behavior.

This is normal-shutdown cleanup, not recovery from forced termination, a new
sound mix or a reason to relaunch an active playtest. Tests use generated
fixtures, real native flight state and Dummy playback, with no speaker output.

## Geometry-only contact prerequisite

The [one-triangle patch experiment](EXPERIMENTAL_CONTACT_PATCH.md) derives a
registered landing-pad rectangle from canonical state and tests its entire
normal projection against a single native terrain triangle. Affine extrema
provide plane-gap bounds; crossing or uncertain boundaries are refused rather
than guessed from a few terrain samples. This is intentionally narrower than a
complete surface provider: it cannot certify bearing, first-hit obstruction,
multi-triangle support, hull clearance or safe landing. The playable lab keeps
its existing floor guard; no contact or damage behavior is enabled here.
The specially authored origin-home terrain variant is explicitly refused by
the current point-provider contract. Supporting it needs a context-aware
extension, not replacement with an ordinary generated planet.

## Seat construction and arm pose

The visible cushion protrusions came from both lift cylinders terminating
inside the cushion. The isolated study retains the cylinders, shortens them
beneath a new seat pan, and checks actual mesh intersections: both connect to
the carriage/pan and neither intersects the cushion. They are not simply hidden.

The revised elbow bend plane tucks the arms while retaining the authored hand,
eye and pedal positions without bone stretching. Volume-preserving deformation
is limited to anatomical skin; applying it indiscriminately to the borrowed
garment worsened seated fit and was rejected. Pose, deformation and material
quality remain separate concerns; this is not cinematic-art completion.

The refined female pose has been rechecked through the same provisional
49-frame +/-8-degree grip sweep. Actual grip/wrist tracking, unchanged seat and
pedal transforms, and return to the revised neutral elbow pose pass. Rendered
extremes were inspected. This is a Blender motion proof only; male motion and
actual Godot semantic control animation remain open.

Measurement correction: earlier cushion-gap receipts evaluated the coarse
viewport garment, while their PNGs used render subdivision. The new study
explicitly measures render-evaluated meshes. The female garment has about
2.03 mm clearance above the cushion; the male still has an unresolved 23.71 mm
gap. These do not supersede older numbers as though the evaluated geometry were
identical. Prior proofs and receipts are retained unchanged.

These remain isolated Blender studies with borrowed CC0 fit clothing, source
receipts and editable masters. They are not replacements for the original suit
design, final skin/hair/gloves, restraint fit, runtime export or Godot review.
The running/frozen build 28 assets are not modified.

## Validation scope

GCC and Clang full builds and all 16 focused craft/state/vacuum, touchdown,
native/contact and asset-manifest contracts pass. New contact output hashes
match between compilers; old flight/terrain reference checksums are unchanged.
Pinned clang-format 20 and focused clang-tidy 20.1.0 pass; full pinned static
analysis and the complete compiler matrix remain hosted-CI publication gates.

Godot preference, recorded-player, recorded bridge/menu and preserved synthesis
integration checks pass. The new shutdown test verifies actual native Dummy
resource retirement, repeated/menu/window/no-audio/legacy exits, unchanged
flight state, and a deliberately held resource exercising the 500 ms fallback.
That fixture intentionally logs the deadline warning. Hidden native controller
flight smoke passes with the unchanged private recording loops. No physical
controller, speaker or new character-runtime acceptance is inferred from those
automated checks.
