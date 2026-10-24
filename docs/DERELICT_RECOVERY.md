# Derelict recovery prototype

At New Game, choose **Four charted + two uncharted systems** and explicitly
enable **Derelict recovery · prototype**. The choice starts off. It composes with
the existing drive and Origin fuel-service choices. Continue keeps the original
selection; older journeys gain no recovery policy or cargo automatically.

Use the existing [derelict contact and exterior inspection](FREEDOM_TOPOLOGY.md#derelict-inspection).
After inspecting the hull, approach to **100–150 m** and match its velocity to
**2 m/s or less**. Pause, choose **Retrieve sealed canister · 30 seconds**, then Resume and
maintain that envelope for **30 seconds of game time**. The progress readout
comes from actual C++ simulation ticks. Pilot assistance can still use fuel;
retrieval adds no autopilot. The earned local contact shows **Closing** speed:
positive means approaching; negative means separating. **Sideways** speed is
motion across the line of sight. Both are current C++ observations, in m/s,
alongside range and total relative speed. They are not stopping clearance or an
automatic rendezvous. Unknown/remote contacts and undefined zero separation
have no motion cue. The same values and sign legend appear in paused flight
instruments. This first transfer uses a shipboard retrieval abstraction; a
drone, cable and transfer animation remain asset work.

Pause stops progress. Save/Continue retains actual progress. Leaving the
supported body, stand-off or speed envelope, starting a jump, explicit Cancel
or actual loss cancels unfinished work. A missed interval is never backfilled.
A cancelled attempt leaves the item unclaimed and permits an explicit retry.
[Quiet travel](QUIET_TRAVEL.md), when otherwise available, executes the same
ordinary ticks and the same per-tick physical checks.

Completion claims **one sealed service canister** in one carried slot, separate
from the four-specimen pouch. Its source stays depleted after a sale or loss.
Inspection and recovery notes remain distinct from cargo: a note cannot be
sold, and replacement loses the carried canister while retaining the claim and
history. An unfinished retrieval has not claimed its source. Loading an older
save rewinds the whole voyage, as with other saved progress.

At Origin, attach to a port and open **Survey commissions**, then
**Sell recovered canister**. Review the quote and confirm. The provisional
price is **100 credits**. Reading, docking, Continue and duplicate requests
produce no payment. Sales retain income before paid enrollment; enrolling later
carries it into the same account without another opening grant.

In [paid operations](PAID_OPERATIONS.md), the quote shows insurance excess,
operating-tab repayment, net proceeds and resulting cash. Insurance settles
first, then at most half the remaining income repays the tab. For example,
100 gross with 60 insurance due and an 85-credit tab pays 60 insurance and 20
tab, leaving 20 net and a 65-credit tab. This example describes settlement,
not the profitability of a voyage. Existing fuel prices, berth bills and
replacement excess keep their current rules.

C++ owns the selected policy, one finite claim, actual retrieval intervals,
physical guard checks, historical craft provenance and atomic sales. Godot
reads bounded packets and submits commands. The absent-by-default knowledge
record selects recovery policy1; it introduces no world generator or random
stream. The existing hull remains unpowered and has no boarding, hull collision,
repair, towing or ship-ownership interaction.

Paid-v1 keeps its nine-field, survey-only account. Paid-v2 keeps its ten-field
specimen account. An actual canister sale in an enrolled journey, or explicit
enrollment with prior canister income, selects paid-v3 with an additional settled
canister counter. Paid-v3 carries the specimen counter even when zero; its single
canister counter is one. Selecting recovery, reading and loading never promote
an account. There is one cash balance and one existing settlement policy.

Qualification distinguishes initialized boundaries from flown play. The shared
fixture uses actual station walking, boarding, stellar acquisition, charged
travel, scan and inspection, with disclosed Home-space and close-approach poses.
Retrieval thereafter uses actual committed ticks. The Origin attachment and
debt examples are separately initialized boundaries; their sales are actual
commands. They establish neither an uninterrupted return nor profit, balance,
transfer animation, manual controller feel or GPU appearance.
