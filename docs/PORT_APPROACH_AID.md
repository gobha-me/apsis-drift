# Close-range port approach

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
long-distance navigation remain manual.

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
