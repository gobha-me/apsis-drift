# Native vacuum dynamics provider

Issue #192, provider version 1, 2026-09-19. This is a standalone authoritative
C++ force/torque provider over the immutable craft frame and canonical native
rigid-body state. It does not replace the experimental thrust lab, modify the
reviewed controller layout, migrate legacy save format 16, or install new live
handling. There is no gravity, atmosphere, contact, damage, fuel consumption,
external-force model, actuator spool, or hidden integration state here.

The user explicitly selected **rotational as well as translational coasting
when runtime assistance is off**. This runtime stabilization choice is separate
from the permanent legacy career penalty profiles described in
[RULE_PROFILES.md](RULE_PROFILES.md). It does not alter their saved meanings.

## Boundary and semantic input

`advance_vacuum_dynamics(context, state, intent, step)` advances one 120 Hz tick
and returns applied-actuation telemetry. It validates everything before
replacing the caller's state. Failure leaves that state untouched. The only
accepted step is `kSimulationStep`; rendering supplies no variable physics step.

Only `system_inertial` coordinates are accepted. Rotating planet-fixed and
accelerating station-relative frames require the #200/#238 composition work;
valid identities in those frames are not permission to apply incorrect inertial
equations. In particular the lab's planet-centered nonrotating coordinates must
not be relabeled `planet_fixed`.

Input contains independent positive and negative translation/rotation fractions
for each body axis, each finite and in [0,1], plus a runtime assistance flag.
Body axes remain right-handed +X right, +Y up, +Z back. Main propulsion is -Z;
retro is +Z. Positive torque follows the right-hand rule, not a controller's
notion of "turn right." Keyboard/controller sign mapping remains outside this
provider and unchanged in the existing game.

Fractions multiply the corresponding immutable frame force/torque ratings.
Equal physical opposing forces cancel, while equal main/retro fractions do not:
the starter's main engine has five times its retro authority. Opposing torque
fractions cancel because each axis has symmetric torque bounds. Independent
channels remain visible even when their net wrench is zero.

## Assistance and actual authority

With assistance OFF, net commanded force and torque are applied directly. No
linear or angular damping, gyro compensation, velocity hold, or automatic
speed clipping is added. Releasing controls leaves momentum rather than
silently braking it. Unassisted torque input can spin the craft beyond its
assisted commanded-rate rating; that rating is not a momentum clamp.

With assistance ON:

- A neutral lateral or vertical axis requests `-mass * 0.65 * body_velocity`
  newtons on that axis. Either nonzero directional command suppresses its
  automatic damping. There is **no longitudinal speed hold**.
- Rotation requests a target body angular rate equal to the signed difference
  of directional fractions times that axis's frame rate rating. A neutral
  rotation request targets zero rate through actual bounded torque.
- Requested stabilized torque is
  `I * (target_omega - omega) / dt + omega × (I * omega)`.
  The second term compensates gyroscopic dynamics; it is not free stabilization.
- Desired forces/torques are allocated within real directional capacities.
  Excess requests saturate authority, not pose, velocity, or momentum.

For each axis, explicitly requested opposing firing `c = min(positive,negative)`
is retained. The permitted net range is `[-negative_limit+c, positive_limit-c]`.
The allocated directional outputs are `c+max(net,0)` and `c+max(-net,0)`.
Without a correction, original channels are retained exactly. With assistance,
the remaining capacity realizes the stabilized net request; assistance can
reduce, cancel, or reverse the requested net, but cannot invent authority.

The returned report separates requested manual net wrench, actual assist delta,
actual applied net wrench, positive/negative gross channels, saturation flags,
and integrated world linear/angular impulses. Requested manual torque is an
input diagnostic, not an additional force to apply after the assisted result.
Future #133/#251 fuel accounting must use **actual gross channels**, including
assistance and opposing firings, not net impulse or controller position alone.
Assist deltas are algebraic `applied - requested` diagnostics, not additional
independent actuators to bill on top of those gross channels; charge each actual
channel once.
No fuel unit or torque-to-propellant conversion is invented here.

## Integration and angular momentum

The integrator evolves position `p`, velocity `v`, orientation quaternion `q`,
and world angular momentum `L`. At each RK stage:

```
omega_body = inverse(I) * R(q)^T * L
p_dot     = v
v_dot     = R(q) * force_body / mass
q_dot     = 0.5 * q * (0, omega_body)
L_dot     = R(q) * torque_body
```

Mass and diagonal principal inertia come from the validated immutable craft
recipe. Unequal inertia is respected: torque-free body angular velocity changes
as the body tumbles, even though world angular momentum remains constant.
The formulation includes Euler's gyroscopic term through world-momentum
evolution; it is not a constant-body-angular-velocity approximation.

Classical four-stage RK4 uses fixed stages at 0, dt/2, dt/2 and dt with weights
1,2,2,1 divided by six. Assistance and actuator allocation are evaluated at tick
start; the resulting body force/torque are held over that tick. Their world
directions change with each stage's attitude. Position and velocity use those
same stages, and impulse telemetry integrates the same world wrench.

Stage rotations evaluate `q * vector * conjugate(q) / norm_squared(q)`, so a
nonunit intermediate quaternion does not accidentally scale thrust or momentum.
No stage quaternion is normalized. The final quaternion is explicitly
normalized **once per successful tick** using the #191 helper. Final body omega
is recovered from final attitude and integrated world angular momentum, then
signed-zero/quaternion-sign canonicalization validates the complete candidate.

Arithmetic order is explicit, with source-local `-ffp-contract=off`; no trigonometric
functions, `hypot`, renderer cadence, camera direction, or interpolation values
enter the integrator. The temporary world momentum is derived from saved `q`
and body omega at the start of each tick; there is no unsaved accumulator.
Save/resume therefore needs the canonical physical state and the same subsequent
semantic input trace, including its explicit assistance choices, not a hidden
actuator history. This is not a new gameplay save menu or a persisted preference.

## Numerical safety and qualification

All #191 finite representation bounds remain enforced, including the 100 rad/s
**per-component** angular limit. That is a refusal boundary, not a promise of
accurate high-speed tumbling throughout the entire representable range.
Likewise, no speed cap is imposed by braking or rewriting an accepted state.
An unrepresentable next state rejects the entire tick.

Each RK intermediate must be finite, with quaternion squared norm in [0.125,8].
The final normalization has its existing [0.5,2] squared-norm safety envelope.
Invalid intent, unsupported frame, invalid craft/state/step, unsafe intermediates,
or final validation failure all reject. A tick at `UINT64_MAX-1` cannot advance
because #191 reserves `UINT64_MAX` as invalid; it is refused before arithmetic.

The numerical qualification must distinguish two kinds of evidence:

- Exact GCC/Clang trace checkpoints, canonical checksums, and interrupted versus
  uninterrupted save/resume establish deterministic execution.
- Analytic single-axis acceleration, unpowered principal-axis/mixed-axis coast,
  and measured energy/world-angular-momentum error establish bounded physical
  integration accuracy at the documented tested rates and durations.

High-spin stress cases near representation bounds test safe finite handling and
transactional refusal, not orbital/attitude fidelity. They must not be presented
as equivalent to the rated-range accuracy checks. Continuous production flight
and atmosphere/gravity composition remain separate qualification steps.

The headless contract fixes two 600-second (72,000-tick) torque-free tumble
fixtures. It checks maximum error throughout each trace, not just its endpoint:

| Initial body omega (rad/s) | Relative energy-error bound | Relative world-angular-momentum-error bound |
| --- | --- | --- |
| `(1.15, 0.9, 1.7)` | `< 1e-7` | `< 1e-8` |
| `(3, 3, 3)` | `< 2e-6` | `< 1e-8` |

Both GCC and Clang passed with matching observed maxima: approximately
`1.6386e-9` energy / `3.25059e-12` momentum for the first fixture, and
`1.03939e-6` energy / `3.50447e-12` momentum for the second. All four replay
traces also matched exact state/checksum goldens under both compilers; a separate
one-tick analytic fixture checks the integration independently of those traces.

Momentum error is the norm of the change in world momentum divided by its
initial norm. These are qualification vectors, not universal guarantees for
every initial condition within a rectangular angular-rate range. Separate
principal-axis spin tests verify that already-existing spin above the assisted
target rate is not clipped. Substantially faster mixed-axis rotation needs
further fidelity work before a production feature can rely on it.

## Deliberate omissions

There are no coordinate-frame handoffs, planetary rotation corrections, gravity calls,
atmospheric damping, collisions, hard surface stops, speed teleportation, fuel
debit, engine spool, boost, camera code, or profile migration. Additional force
layers must compose through an explicit future interface, not apply their own
second position/orientation integration. The currently running Godot lab remains
its separately versioned implementation until an explicit adapter and playtest.
