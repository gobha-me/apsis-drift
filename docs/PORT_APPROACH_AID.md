# Origin Station rendezvous and port approach

The optional **Rendezvous with Origin Station** action covers the bounded home
return region used by the local Moon and asteroid trips. Select D1 or D2 in
paused flight controls, request rendezvous, then resume explicitly. The flight
HUD also offers the action for the selected port. This uses real propulsion,
flight fuel and elapsed time. Allow several minutes; it does not shorten travel
or replenish reserves. Capture remains a separate player action.

Admission requires the genuine continuous Origin home system, a seated free
craft, stowed gear, idle jump, available flight fuel and no other surface or
orbit-hold maneuver. The craft must be above the modeled atmosphere, on the
approach side of the port, more than 150 metres and at most 50 kilometres from
its collar, at least 150 metres outward, within 5 kilometres laterally, aligned
within 0.35 radians, below 40 metres per second relative speed and at most
0.02 radians per second angular speed. Historical orbit recipes retain manual
navigation. These bounds describe this return corridor, not general navigation,
obstacle avoidance or a swept collision guarantee.

C++ follows the actual moving station, central gravity and the selected craft's
separate force and torque ratings. Attitude control respects saved assistance.
Commands update every 15 authoritative simulation ticks, independently of
presentation cadence. The station's authored outward normal is already in its
system-aligned inertial axes; the craft alignment quaternion does not rotate it
again. No craft position, velocity, clock or resource is injected. Initial tight
speed/alignment/lateral gates are not reapplied while the controller turns and
accelerates; the actual owner, mode, clear-space, finite-motion, approach-side
and 50-kilometre running bounds remain enforced.

On reaching the unchanged close-range admission gate, the aid hands over to
**Approach port with thrusters** with its full 60-second budget before the first
close-range tick. It never captures automatically. Valid manual demand, explicit
cancel, port reselection, assistance changes, pause, focus loss, Save As, a lost
eligible state, exhausted flight fuel or the 2,400-second attempt limit cancels
rendezvous. Invalid inputs and invalid fixed steps refuse atomically. Continue
retains the actual voyage and starts paused with both aids off; no save-format
change is needed.

Issue #635 explicitly extends the former manual long-distance boundary only for
this Origin return region. The C++ route tests fly a genuine charged asteroid
round trip and the complete rendezvous under both assistance modes. Native
contracts exercise the far action and a separately retained, genuinely flown
near handoff; they do not replay the entire several-minute route in Godot.

## Smooth docking

**Dock with thrusters** completes the final approach and attachment after an
explicit request. Select D1 or D2, enter its approach column, choose the action
in the HUD or paused controls, then Resume if paused. The compact action gives
this assist priority near an eligible port. The controller aligns while holding
clear, closes the collar gap and brakes before latching. The phase and remaining
attempt time appear with the live collar offsets.

Admission requires the continuous Origin orbit recipe, a seated free craft,
stowed gear, idle jump, available fuel and no orbit hold. Collar separation must
be at most 150 m on the outward approach side, attitude error at most 0.35 radians
(about 20 degrees), angular speed at most 0.02 rad/s and station-relative collar
speed at most 5 m/s. The complete stowed hull must fit the fixed port column,
extended downward to -170 m in station coordinates. These limits describe the
supported docking corridor; the action does not navigate around obstacles.

Alignment and translation request the craft's real torque and thrust ratings,
consume fuel and advance the ordinary C++ clock. The final latch requires the
existing physical capture assessment plus collar error at most 1 mm, attitude
error at most 0.1 mrad, relative collar speed at most 1 mm/s and angular speed at
most 0.1 mrad/s. Only that small residual is arrested when the mechanical
attachment establishes the canonical station pose. Remaining approach travel is
flown rather than teleported or hidden by a camera transition.

Manual input, pause, focus loss, Save As, target or mode changes, exhausted fuel,
lost running conditions or the 180-second attempt limit cancel the assist. A
canceled craft continues under ordinary flight physics. Invalid commands and
fixed steps refuse atomically. Continue retains the actual craft and attachment
with the assist off; no new save format or docking-offset field is introduced.
The older translation-only approach and manual capture remain available. A far
rendezvous still hands over to the older approach; choose Dock explicitly for
completion.

## Close-range approach

The optional **Approach port with thrusters** action helps with the last part
of returning to Origin Station. It is available in the flight HUD and the
controller-navigable pause menu. Select D1 or D2, approach from below and align
the craft first. Resume explicitly if the command was selected while paused.
New games use the continuous C++ orbit recipe; historical rounded-motion saves
retain manual approach. When the live assessment says ready, select **Capture port**. The aid uses
thrusters to remain near the collar while you choose; it never captures for you.

The command accepts a free selected port within 150 metres, attitude error at
most 0.005 radians (about 0.3 degrees), angular speed at most 0.002 radians per
second and station-relative speed at most 40 metres per second. The complete
stowed hull must fit the fixed port column extended downward to -170 metres
in station coordinates. These bounds are a small approach region, not obstacle
avoidance or a swept collision guarantee. Aligning from another direction and
navigation outside the bounded Origin rendezvous region remain manual.

C++ measures same-tick collar error and relative velocity, follows the existing
station ephemeris acceleration, accounts for central gravity, and projects its
translation demand through the actual craft orientation. Positive and negative
requests use their separate rated forces and the existing atmospheric flight
step. Actuator saturation stays physical. Godot sends the action and displays
its state; applied-force exhaust uses the existing channels. The aid changes
neither the station ephemeris nor craft pose, attitude, velocity or capture
thresholds directly. It does not rotate the craft or replace orbit hold.

A valid manual thrust or rotation command cancels the aid immediately and runs
through the ordinary flight path. A changed port, successful capture, explicit
cancel, entry into pause/focus-loss/Save As, a lost approach condition or the
60-second attempt limit ends it. Invalid inputs refuse without canceling or
advancing the command. Pause-menu selection can explicitly arm a new attempt;
it still waits for neutral controls and explicit resume.

Approach is a transient command. Save As retains the actual body, journey and
clock in their existing format; Continue starts paused with the aid off. The
player chooses another attempt. Historical save formats remain unchanged.

For departure, release the attachment and use the mapped **Fall** control to
withdraw downward. **Rise** brakes that withdrawal; opposed thruster ratings
are unequal, so identical timed burns do not retrace a path. Clear the
12-metre port column before forward thrust. The live port offsets are expressed
on craft right/up/back axes.

The composed New Game test now walks to D1, boards, withdraws under propulsion,
returns with the aid, captures explicitly, disembarks and resumes station
walking in one session. Separate seeded D1/D2 checks retain the original gate.
The broader First Flight planetary journey remains open on #245. Station
exterior collision, planetary touchdown and general autopilot are separate work.
