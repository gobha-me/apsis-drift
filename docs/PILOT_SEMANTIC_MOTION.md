# Seated semantic pilot motion — bounded native prototype

Issue #277. This adds a presentation-only driver for the seated proof rigs,
not walking/EVA animation, a generic IK engine or replacement gameplay art.
It solves the arms for the requested grip positions instead of scrubbing the
previous prerecorded whole-body sweep.

## Input and ownership

The caller first establishes the authored seated neutral pose, pauses its
AnimationPlayer, and configures the driver. Skeletal rest pose is not the seated
reference. The exact named five-bone chains and two provisional control pivots
are validated; missing/ambiguous, scaled, malformed or competing rigs refuse.
Torso/shoulder ancestors must retain their configured pose. Moving the complete
ship/scene or camera is not a new arm demand.

`update_thrust_axes` consumes the existing resolved seven-axis flight command,
not keyboard codes or controller axes. It validates every channel before mapping:

| Semantic demand | Index | Visible control |
| --- | --- | --- |
| Pitch | 2 | Left grip fore/aft |
| Roll | 4 | Left grip lateral |
| Yaw | 3 | Right grip lateral |
| Heave | 6 | Right grip fore/aft |

Main/retro and strafe remain validated but have no finger/button animation in
this slice. Head-look suppression and remapping are already resolved by the
existing input owner. Assistance's automatic corrections are not invented pilot
gestures. No physics, camera state, random stream or simulation time is written.

The provisional travel is eight degrees per control axis, not a final authored
control range. A bounded rate limit settles a unit demand change in 150 ms;
full -1 to +1 reversal takes 300 ms. This changes presentation only and never
delays the C++ command. Delta must be finite in [0, 0.25] seconds. Inactive calls
target neutral; the caller must keep updating through the return or explicitly
call `reset_neutral` before stopping updates. Invalid input safely resets the
presentation and reports refusal rather than retaining a stale demand.

## Deformation and limits

A two-segment geometric solve retains authored arm lengths and elbow bend side.
Both helper bones within each physical upper/lower segment move together, with
neutral orientation/offset preserved. Wrist orientation follows the control's
cached neutral grip relation; fingers inherit the wrist. Both sides are checked
before pose updates. Unreachable targets refuse rather than stretching limbs.

This certifies neither finger-surface contact nor garment/seat/canopy collision.
The borrowed proof meshes and the developing original suit still need their
own native surface/visual checks. First-person head hiding, gameplay asset
installation, complete cockpit action ranges and discrete reach/press/return
interactions are not implemented by this driver.

## Tests

`pilot_motion_test.gd` uses a synthetic rig by default and optionally accepts
`--pilot-glb=/absolute/proof.glb` for actual native rig checks. It covers single
axes, all combined corners, grip transforms, unstretched segments, unchanged
non-arm/rest data, input bounds, neutral return, root/camera independence,
presentation chunking and invalidated/competing rig behavior. Female study 29
and male study 30 are tested separately; they are not distributed by this code.

`pilot_motion_input_test.gd` exercises real input resolution with synthetic
keyboard/joypad events and remapping, then feeds the same resolved commands to
two native C++ sessions, one with and one without the pose consumer. Their
complete flight states agree. It also checks head-look suppression and the
inactive/disconnect return. This uses a synthetic rig, not physical controller
or final character acceptance. Both tests are included in the isolated native
runner; the suite now contains 26 contracts.
