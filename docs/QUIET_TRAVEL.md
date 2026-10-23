# Optional quiet travel

Pause seated flight and open **Flight instruments & navigation**. The time
acceleration selector offers **Normal**, **Up to 2×**, and **Up to 4×**. It is
available for eligible primary-planet free flight in clear space. Resume with
pilot controls released. Camera free look can continue during acceleration.

Assistance and orbit hold continue normally and can burn fuel even without
pilot input. Fuel, surveys, drive recovery, NPCs, rent and submission deadlines
all follow actual game time. Pausing stops the simulation. This option supplies
no fuel, orbit insertion or automatic piloting.

Thrust or steering returns to Normal, including opposing mapped keys that
cancel each other. Pause entry, focus loss, Save, accepted ship commands and
scene changes also cancel the request. A request explicitly chosen while
already paused remains selected until Resume. Continue always starts at Normal;
the selector is not stored in the voyage.

The prototype requires clearance above the atmospheric space boundary with a
100 km margin and beyond 10 km from Origin station. Landed/walking/boarding
states, local Moon/minor routes, active port aids, jumps, recovery and active
fuel-responder operations use Normal. These are acceleration guards; ordinary
flight stays available. They do not assess every secondary body or environmental
hazard. The page gives the current reason when acceleration is unavailable.

## Scheduling and qualification boundary

C++ retains one existing real-time accumulator and the fixed 120 Hz simulation
step. It reserves every ordinarily scheduled tick, then interleaves bonus ticks
within the same limit of 15 total ticks per call. Extra work that does not fit is
not banked. Lower frame rates can therefore deliver less than the requested
multiplier. Godot does not scale elapsed time or run another universe clock.

The default 1× path retains existing save meanings and physical rules. The
selector is transient scheduling state. Contract checks compare complete
voyages with independent ordinary ticks and retain native UI witnesses. Some
boundary fixtures initialize valid space/constraint states explicitly; those
are separate from flown journeys. Requested pace is not a hardware throughput,
manual-control or GPU certification. Quiet travel does not accelerate the
manual powered portions of the paid-survey reference fourfold.
