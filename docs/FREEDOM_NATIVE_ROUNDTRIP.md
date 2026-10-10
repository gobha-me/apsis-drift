# Native neighboring-system round trip

The bounded normal-trip test for #194 starts from actual Freedom New Game. The
pilot walks across Origin Station, boards the authored Wayfarer, releases D1,
brakes with real thrust, selects the known neighbor and commits the installed
drive. After arrival and a bounded ship observation, the pilot jumps home,
intercepts the moving station, docks, replenishes and disembarks through the
original entry. No mission, earned capability, money or upgrade is required.

Two opt-in Pilot traces deliberately align both departures or hold a twenty-degree
heading offset. The test pilot brakes drift and rotates with real actuators before
each spool, including a physical quarter-turn for the return reversal. Commitment
must retain the selected grade, a clear actual arrival and its documented envelope:
the offset radius is ten times the aligned radius. Both traces still return to the
moving station, pay two charges, replenish and disembark.

Every move uses the public C++ session commands. A test controller supplies
signed actuator demands, not pose or velocity writes. Godot replays their exact
double values through the production walking/flight views, chart selection,
drive controls, port controls, station service and boarding handoffs. The same
staged spacecraft remains in the native session.

## Evidence and persistence

The separate schema-2 trace contains nineteen ordered checkpoints, including
both selections, spools, commitments, transits and arrivals; neighbor observation;
rendezvous; capture; replenishment; and return to walking. The existing
eleven-checkpoint planetary trace retains its schema, labels and command set.

An uninterrupted C++ session receives the same commands as a session continued
at every checkpoint. Their complete saved ownership, surface state, pilot,
attachment, resources, knowledge and travel must agree at each boundary and at
completion. GCC and Clang traces are compared as complete files, including the
recorded controls. The actual resource provider spends two of three jump charges,
leaving one before service; an independent gross-work meter verifies flight fuel.

The native replay checks whole-save bytes against the C++ checkpoints. At each
boundary an independent Continue performs a neutral tick and must produce the
next C++ save. Current-world station cues, charge/fuel readings and seated-pilot
presentation must agree. The neighboring world cannot grant home station service.

The runner rejects invalid CLI options before generating a world. Native manifest
and control validation bounds dimensions, IDs, non-finite values, phase ordering,
paths, row/byte counts and tick totals before model staging.

## Running the bounded tests

Use the prebuilt native extension and fixtures from [development setup](DEVELOPMENT.md)
and the selected [Godot executable](../godot/README.md):

```sh
python3 tools/test_godot_native.py --godot "$GODOT_BIN" \
  --build-dir build-native --output-parent build-native-contracts --timeout 600 \
  --test native_roundtrip --test native_roundtrip_pilot
```

For the deliberate Pilot grades, select `--test native_roundtrip_pilot_aligned`
and `--test native_roundtrip_pilot_offset`. Their committed heading, drift and
uncertainty display are checked against C++ evidence as well as complete saves.

These are accelerated semantic-input and headless integration checks. They do
not qualify manual controller handling, performance or fun. A separate graphical
review replays both deliberate Pilot trips with the Compatibility/llvmpipe software
renderer, captures all nineteen checkpoints per trip, and preserves the same full
saves during frame waits. The neighboring body, home direction, actual station and
displayed commitment consequences follow the authoritative world. Source, command,
save and image hashes accompany the retained evidence. This is rendered correctness
on one backend; broader hardware and manual/controller qualification remain
[#244](https://github.com/gobha-me/apsis-drift/issues/244) and
[#245](https://github.com/gobha-me/apsis-drift/issues/245).

The dense scrolling HUD and plain planetary materials remain refinements under
the existing cockpit/terrain owners. The approved
[standard recovery provider](FREEDOM_RECOVERY.md) separately checks declared losses
after actual neighboring travel. Hazardous and extended-jump consequences remain
the unselected #248/#253 policies. Planetary surface flight remains covered by the
existing separate voyage.
