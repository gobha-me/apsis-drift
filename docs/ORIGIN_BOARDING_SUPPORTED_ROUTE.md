# Supported lower-seat route work

The complete reversible route in [#361](https://github.com/gobha-me/apsis-drift/issues/361)
is under development. The existing partial planted preparation and surface
sweep remain unchanged. They do not qualify boarding or First Flight.

The new policy3 body definition lets each flat boot turn through the existing
±45-degree hip axial freedom. With an upright pelvis, the thigh frame is
`Ry(root) * Ry(axial) * Rz(side*abduction) * Rx(hip)`; the shin adds
`Rx(-knee)`, and the ankle compensates with
`Rx(-(hip-knee)) * Rz(-side*abduction)`. Sole yaw is root yaw plus hip axial.
There is no independent ankle yaw or manual roll. A pitched pelvis uses the
original sagittal branch with zero axial rotation and abduction. Relative torso
lean remains available within the original limits.

This preserves all fifteen rigid world reservations, joint lengths, dimensions
and 1,200 mass weights. The new branch uses the parent policy's hip range
[-20,120] degrees; the old planted recipe retains its narrower [-20,65] range.
Zero-axial geometry retains the original evaluator's bytes. The new static
diagnostic does not copy stale self-pair results or grant support, surface,
material, motion or actor permission. Independent target-foot closure and
continuous support/self/world assessment still follow within this route work.

Finite pan support uses the actual selected patch30. The original rigid pelvis
and thigh cores remain strict: the permitted 5mm underside skin is **outside**
those cores and applies only to the selected seat faces. Adjacent shell, welt,
hardware and all nonselected geometry remain obstacles. Floor support must be
acquired before pan load is released on reversal.

The complete-source inventory tool, `tools/boarding/material_inventory.py`,
loads the exact Craft09 master once, read-only, with the existing operating
corrections. It records all original kept objects before cropping, authenticates
the full operating roster and reproduces every admitted original quantized
triangle. Full raw and quantized bounds and ordered triangle fingerprints stay
distinct. This supplies missing containing-bounds input, including objects absent
from the collision crop; it does not infer material closure or cabin air from a
bounding box. Tests reject malformed geometry, non-finite state, substitutions,
capacity overflow and incorrectly quantized crop decisions before source use.

The remaining grouped work is bounded pose/path discovery, independent foot
unloading and swing, intermediate contact, finite pan load transfer, one frozen
complete candidate and whole/reverse/subinterval evidence. Explicit material
membership remains a separate prerequisite for world and actor permission.
The chosen model is the existing bounded synthetic kinematic support model;
general articulated dynamics, engineering strength and an economy do not gate it.
