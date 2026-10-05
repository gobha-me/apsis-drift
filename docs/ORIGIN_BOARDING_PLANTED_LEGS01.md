# Exact planted leg closure recipe01

Registered for #425 on 2026-10-05 before implementation or fixture evaluation.
This source-free expression recipe follows the mixed-plane seam under #361.

#361 needs continuous mixed-height load transfer. #420/#422 now provide static contact and surface checkpoints, but rounded Body02 forward joints do not impose the exact identity between two boot bottoms and different source planes. Switching anchors without that identity can move the entire body discontinuously. Deliver the missing constrained kinematics before ship fitting.

Plan:
- Add one separately versioned, purpose-specific paired planted-leg expression recipe. Preserve nominal .47285/.47478m bones, original hip offsets, boot dimensions and existing hip/knee/ankle/abduction limits. Keep Body02/self04 and their static records untouched. No generic IK, expression engine or caller-created route/geometry authority.
- Define hip/ankle/boot coordinates as exact-real expressions with bounded outward evaluation. A planted ankle uses its plane +100mm and a boot center its plane +50mm, with halfheight50mm. Retain those expressions and one common stance placement, so both boot bottoms meet their distinct planes algebraically. Reporting points never supply that identity.
- Use the analytic two-link sphere intersection in the leg's vertical sagittal plane, deriving that plane from hip/ankle X/Y with a fixed forward knee branch. Preserve both bone lengths algebraically; validate domain, reach, plane and actual joint limits with complete bounds. Do not lerp joint points, snap feet, silently normalize a stored legacy frame or increase limits.
- Freeze one source-free mixed-plane lateral-transfer fixture before evaluation: rootX=.32*t²*(3-2*t), rootY=.72, rootZ=-.16; hip X offsets -.14/+.14; port planted ankle X .02/Z-.35 at plane0; starboard X .34/Z-.65 at plane-.16. Y is the exact plane+boot-height expression. t is in[0,1], duration12s. Fixed branch and recipe constants cannot be tuned after refusal. This is a constrained leg observation, not an inherited supported whole-body route.
- Provide complete interval/subinterval/reverse diagnostics with bounded subdivision and explicit capacity/domain refusals. Compare angle limits using sound bounds/sector inequalities, rather than rounded inverse-angle reports. Register numeric workspace/depth/node limits before code. Continuous plane/link identities and interval angle evidence must cover the complete requested interval, with genuine leaf coverage.
- If derivative evidence is included, bound it over the interval and keep it distinct from root duration arithmetic. Do not claim timing/acceleration or full trajectory readiness from samples. A first narrow closure/limit delivery may leave dynamics clearly deferred.

Acceptance:
- The frozen mixed-plane closure preserves both exact plane identities and both fixed link lengths throughout the interval where domain/limits certify. An unresolved interval is reported honestly, without fixture changes.
- Independent synthetic controls cover exact source differences, common placement, report rounding, fixed knee branch, reach/singularity, invalid/nonfinite parameters, nextafter bounds, every enforced joint limit and exhausted subdivision. Independent point reconstruction corroborates enclosures; samples are not the interval certificate.
- Full/subinterval/reverse coverage has no missing leaf, hidden endpoint, partial-success flag or history-dependent result. Source-free private numeric adversaries may not become public geometry admission.
- Full GCC/Clang native builds/tests, pinned format20/tidy20 and current boot/self/world/save/walk/flight regressions.

This only delivers source-free exact planted-leg kinematics. Whole-body expression/self ownership, finite load, source/crop sweeps, free-foot swing, pan-supported seating, actor/save integration and First Flight remain open under #361/#352. No asset, master/capture/material-proof replay, source-floor expansion, contact epsilon or gameplay teleport. Publication depends on #422 merge.


## Numeric and ownership registration

The public query accepts only finite parameter interval endpoints in [0,1].
Reverse endpoints request the same closed geometric interval in reverse;
reporting direction is retained. No caller geometry, joint-angle override,
source table, supplied diagnostic or editable identity flag is admitted.
The fixed recipe uses the forward knee branch in each derived vertical leg
plane. Its plane normal is an exact normalized expression of hip/ankle X/Y;
it never normalizes an existing stored Body02 or source frame.

Register maximum subdivision depth12, maximum8191 examined nodes and maximum4096
retained leaves. Every accepted leaf owns its interval bounds and certificates;
only a complete gap-free cover can grant the source-free interval result.
Unresolved domain, arithmetic, reach, branch or joint limits and exhausted
capacity retain explicit refusal without a partial-success flag. A midpoint
that cannot strictly subdivide also refuses. Private numeric adversaries can
exercise smaller budgets or synthetic degeneracy without public admission.

Keep outward arithmetic bounded and finite. Prove the paired planted plane
and link identities from the analytic expressions; rounded report endpoints
can have discrepancies and do not replace those expressions. Fixed limits are
hip [-20,65], knee [0,135], flat ankle pitch/roll +/-30/+/-15 and hip abduction
+/-35 degrees with upright pelvis. Abduction is derived from the leg plane;
flat-roll15 is the tighter bound. Report inverse angles separately from limit
certificates. Root curve monotonicity follows 6*t*(1-t)>=0; enclosing endpoints
may tighten that fixed polynomial without sample-based permission.

The12-second duration is recorded for the fixture. This first delivery does
not certify joint speed, acceleration, load, self, world surfaces or a gait.
Those require the actual complete-body expression and trajectory model.
No source-free result grants route, actor, seat, saved-phase or source-volume
qualification. Body02/self04 and their existing static checkpoints remain
unchanged and separately versioned.
