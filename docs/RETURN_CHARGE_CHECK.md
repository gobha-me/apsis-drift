# Return-charge check

Pause seated flight and open **Flight instruments & navigation**. Select a
known system in the chart to see the named trip **here → destination → Origin**.
Selecting Origin shows just the direct return leg. The card compares the one or
two required standard jump charges with the charges actually carried and shows
any shortfall. Spending the last charge remains allowed.

The flight-fuel percentage and reserve/empty notices are the existing tank
readings. Keep fuel for maneuvering: this check does not estimate that amount,
predict a future arrival or promise that either jump will be feasible. It assumes
no replenishment along the named trip. Drive recovery, strain, range, arrival
uncertainty and ordinary command validation continue to apply.

C++ admits recorded system identity and location before exposing each endpoint.
Unresolved destinations, journeys without selected chart/resource ledgers,
local planet/companion routes, an active jump and pending loss receive an
explanation instead of an itinerary. Older journeys with the required ledgers
remain supported. A valid system selection can be budgeted while docked or landed;
that is not departure clearance.

The getter is read-only and bounded to two legs. Godot requests it when the
instruments open or the player changes the relevant selection, rather than
querying the catalog each frame. Opening, refreshing and leaving the reader
advance no time and change no Save, resources, charge commitment or arrival
outcome. No new saved preference or generation version is introduced.

This is the bounded engineering follow-through in
[#701](https://github.com/gobha-me/apsis-drift/issues/701). Saved multi-leg routing,
automatic execution and broader risk comparisons remain separate
[#195](https://github.com/gobha-me/apsis-drift/issues/195) work.
