# Lateral boarding body policy 02

Registered for [#417](https://github.com/gobha-me/apsis-drift/issues/417) before
source fitting. This extends the fixed policy01 dimensions and masses with a
bounded hip-abduction frame. It enables lateral load shift needed by #361;
it does not qualify a supported step, self clearance, source clearance or actor.

The policy01 evaluator remains unchanged. Its sagittal rotations retain each
part's transverse coordinate. Equal port/starboard mass shares place the load
projection between soles centered at ±0.14m. With 0.12m-wide boots, either sole
alone misses that projection by 0.08m even before the required 0.01m load margin.
Common yaw cannot resolve that limitation.

## Registered frames and limits

Canonical axes remain +X width, +Y up and -Z forward. Root yaw rotates about
+Y. Port side sign is -1; starboard side sign is +1. Positive hip abduction
moves a straight leg outward, with the signed roll angle `side_sign * abduction`.
At nonzero abduction, the thigh frame is:

```
root_yaw * Rz(side_sign * abduction) * Rx(hip_flex)
```

Knee flexion then rotates about the child's +X axis by `-knee_flex`. Hip origins
retain the existing pelvis offsets ±0.14m. This bounded branch requires pelvis
lean exactly zero whenever either hip abduction is nonzero. Torso lean remains
its existing sagittal rotation. Coupled pelvis pitch and abduction have no
registered frame here and refuse; this is not a complete anatomical model.

Flat boots retain the root yaw frame. Required ankle pitch is
`-(hip_flex - knee_flex)` in the lateral branch and the unchanged policy01 formula
when abduction is zero. Required ankle roll is `-side_sign * abduction`.
The ankle compensation applies pitch followed by roll, in that order, giving
`Rz(a) * Rx(b) * Rx(-b) * Rz(-a)` before root yaw. Manual ankle roll, hip axial,
shoulder abduction and the other undeclared freedoms still refuse.

Hip abduction stays within ±35 degrees. Derived flat-ankle roll stays within
±15 degrees, and pitch within ±30 degrees. Thus the flat-boot branch refuses
abduction beyond ±15 degrees even when the hip itself permits it. No angle is
clamped. Fixed limb lengths, rigid boot dimensions, mass fractions, body/eye
frames, suit-on and pack-detached requirements stay unchanged.

Zero-abduction poses use the original arithmetic path, including nonzero
pelvis lean. A named policy02 evaluator accepts only explicit version2 poses;
the policy01 entry point still refuses version2. Diagnostics identify version2
and expose derived ankle roll. Recipe03 continues to consume policy01 only.

## Declared source-free demonstration

Use zero yaw, upright pelvis/trunk, both arms straight, and signed hip rolls
of +10 degrees (port abduction -10, starboard +10). The port leg remains straight;
the starboard hip and knee flex by +30 degrees each. Set root hip Y to
`0.1 + (0.47285 + 0.47478) * cos(10 degrees)`.
This retains the port sole at Y=0, raises the starboard sole, and shifts the
surrogate COM into the port sole's finite horizontal rectangle with more than
10mm edge margin. The mirrored pose reverses the roll and loaded side.
These are kinematic demonstrations against an abstract plane, not source support,
self/collision approval or a continuous trajectory certificate. No mesh fitting
or body-dimension adjustment follows from them.
