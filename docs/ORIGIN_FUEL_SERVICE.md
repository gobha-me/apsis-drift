# Origin fuel service

Selected under [#669](https://github.com/gobha-me/apsis-drift/issues/669), after
[temporary drive strain](FREEDOM_JUMP_RECOVERY.md#temporary-drive-strain).
This is living-pilot fuel assistance. [Standard recovery](FREEDOM_RECOVERY.md)
remains the separate operation that retires a lost ship and supplies a replacement.

## Using the prototype

Enable **Origin fuel service** when starting a new game. Existing journeys retain
their original rules. One service Wayfarer starts at D2; that berth stays reserved
while it is away. The player still starts at D1 with the selected charts and drive.

Request fuel while seated in a capable ship supported on the home airless Moon,
with Origin's contact known, room in the flight tank and at least one jump charge.
A zero flight-fuel tank is allowed. The ship must retain its landing and liftoff
capability; cosmetic forebody damage does not disqualify it. The current powered
cabin has no finite oxygen/battery deadline.

The service craft leaves its actual berth, jumps to a beacon above the supported
ship, approaches under thrust and transfers up to one-quarter tank. Cargo and
propulsion fuel are separate finite stores. The short-range link requires real
hull separation, clearance and stable relative motion within 40 metres; the
initial transfer takes 10 simulation seconds for a complete quarter tank.
No hose or physical fuel-mass simulation is claimed.

Stay supported during delivery. Walking outside the same parked ship preserves
the request. Liftoff, leaving the body, loss or replacement closes the link and
sends the craft home. Canceling before departure spends nothing; after departure,
Cancel requests a real return and preserves spent charges and delivered fuel.
A committed jump finishes before withdrawal. The service craft spends two of its
three jump charges on a normal round trip. It returns through the existing
station approach and capture rules. Request and Cancel live in the paused
Controls menu; release movement controls, select an action, then Resume explicitly.
This first prototype offers one sortie;
finishing it does not refill or restart its supplies.

Only flight fuel is delivered. Player jump charges, drive recovery, craft
identity, condition, specimens and discovery records keep their current owners.
There are no fees, missions or equipment gates. General rescue coverage, repairs,
passenger pickup, suits and lifeboats remain later work.

## Authority and persistence

C++ owns the service identity, both resource ledgers, route phases, real rigid
states and arrival receipts. Player and service share each committed 120 Hz
world tick, including when the player changes body. Pause and menus freeze both;
Save/Continue retains pending jumps, partial delivery and the completed return.
Godot displays the supplied relative transform and actual applied propulsion.

Explicit save format 59 wraps one unchanged complete player voyage and one
versioned service actor/sortie. Historical save formats do not acquire an NPC.
The independent `origin_service_craft` seed domain is appended as 21; existing
world recipes and seed streams are unchanged. The service-only jump policy uses
the real landed beacon, with a nominal 500-metre stand-off; it does not change the
player's jump targeting or recovery rules.

If a valid route cannot advance, the session first attempts an existing real
return transition where applicable. An unrecoverable service failure retires
that contact with its exact last authenticated state and failure tick. The player
can continue; the UI says the service is unavailable. Its historical pose is not
rendered as a current ship or advanced by invented movement. Remaining cargo and
spent charges are retained without delivery, refund or replacement.

The service has finite reserves, clearance requirements and bounded route phases.
This is one purpose-specific responder, not a simulated fleet or a promise of
rescue at every generated site. Manual flight feel and visual acceptance remain
separate from the automated contracts.
