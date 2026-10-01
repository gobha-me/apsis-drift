# Saved native Origin port lifecycle

2026-10-01, #342. A selected Wayfarer flight can target D1 or D2, approach the
actual Origin Station, capture through the canonical C++ port gate, remain
attached while the station clock advances, and release into normal flight.
The gate evaluates the collar, full attitude, motion and complete stowed hull
against the [qualified port geometry](ORIGIN_DOCKING.md). Targeting a port does
not move the craft. Failed capture reports the applicable physical refusal.

## One owner and clock

`NativeFreedomFlightSession` retains the sole canonical body, flight model and
history. Its optional docking state stores the qualified target and attachment.
The existing 120-Hz presentation scheduler advances this same session, with
transactional batch refusal and no paused time. An attached step follows the
generated station ephemeris at the next tick and updates history to that tick.
The mechanical constraint supplies this motion; it is not flight propulsion,
gravity integration, a second Godot physics body or an orbit-hold controller.
Requested propulsion while attached refuses atomically. The native control
view submits neutral channels until the player explicitly releases the port.

Capture invokes the existing provider and arrests only its accepted residual
pose/rates. Release removes the constraint at the current tick, retaining the
same canonical pose, attitude, linear velocity and angular velocity. It does
not withdraw twelve metres, advance time, refill, charge currency, create a
second craft or grant history. Normal actuators perform the actual departure.
Hold/assistance selections remain explicit flight-model state.

## Format19 and compatibility

Format19 adds a closed `docking` object to the existing format18 physical-flight
payload. It contains geometry version 1, a target `{station_id, ordinal}` and
the `attached` boolean. The root location is `docked_at_origin_port` when attached
and `planetary_flight` otherwise. The canonical body remains in the named
nonrotating home-planet frame. Its frame recipe must be Wayfarer 2/version1.
Station and target ownership, model versions and shared history/body clocks are
validated through the existing C++ owners. An attached body must exactly equal
the constraint pose resolved from its target, recipe and tick. No redundant
attachment pose or clock is serialized.

Save As uses the existing atomic writer and preserves the selected source.
Continue restores the target and attachment before any presentation is created.
Malformed fields, duplicate keys, overflowing identities, stale ports, unknown
geometry, incorrect locations and fabricated attachments refuse; a damaged
format19 cannot fall back to a station or older flight save.

Formats16/17/18 retain their meanings and encoding. Continuing format18 without
selecting a port still writes format18. Selecting a qualified port explicitly
adds docking state; its next Save As writes format19. New Game remains format17
station inspection. It does not fabricate a port/pilot or skip the pending
station walk and boarding path.

## Native presentation

The flight view verifies the existing station model hash and C++ geometry,
then imports the actual station when within one kilometre. It uses the same
same-tick double-precision C++ station projection and orientation as the craft.
The shared close pass renders station geometry, D1/D2 markers and the Wayfarer
with depth occlusion. Its current 200-metre draw range is a bounded near-dock
presentation, not a distant station LOD. The home range cue remains a projection
without a line-of-sight claim. No inspection camera or alternate station world
is introduced into flight.

Target buttons, collar separation, full alignment, inward/lateral speeds and
capture readiness read the actual C++ assessment. Capture/release buttons invoke
C++ commands; UI availability grants no extra capture tolerance. Constrained
motion reports no applied propulsion, so the actual-force exhaust stays dark.
The source-bound station and ship packages retain their existing provenance
and licenses; this increment edits neither authoring master nor derivative.

## Qualification and remaining journey

The C++ contract rejects invalid state before exercising approach/capture,
attached clock motion, atomic Save As, ordinary Continue and same-tick release
at both ports in three seeded worlds. A native command trace must reproduce
independently generated complete C++ format19 save bytes, including an attached
reload and a second presentation cadence. Existing atmosphere, surface flight,
save18 and station-start contracts remain required in GCC and Clang.

This lifecycle does not qualify arbitrary swept station exterior contact or
dynamic obstacles. The canonical gate checks the fixed reserved column and
stowed hull. Walking, open-hatch boarding, sitting, surface contact and the
composed home-return handoff remain #245/#291/#220 work. The current flight
camera is a presentation choice, not a persisted pilot or boarding action.

The 2026-10-01 local qualification passed all 33 CTest contracts and all 40
native contracts with GCC and Clang. The affected native port/flight checks
passed again after the UI correction. Final software-renderer reviews produced
six byte-identical PNGs across compiler builds: ready approach, capture, cockpit,
constrained clock advance, release and actuator-driven departure. The captured
station and ship match their source-bound exports, and capture manifests verify
the staged script/bridge and image hashes. Release retains the tick/checksum;
the departure review first withdraws down the column, then applies forward
thrust, reaching a collar separation of about 84.84 m at tick 865. This is visible
registration/lifecycle evidence, not a collision or hardware performance result.

`tools/test_godot_native.py --test native_port` produces an isolated matching
project, prepared assets and C++ approach fixtures in its reported output
directory. With a working rendering display, absolute `CONTRACT_DIR` and an
existing absolute `CAPTURE_DIR`, review that same selected state with:

```sh
"$GODOT_BIN" --path "$CONTRACT_DIR/project" --audio-driver Dummy \
  --script res://native_port_capture.gd -- \
  "$CONTRACT_DIR/port-approach.json" "$CONTRACT_DIR/native-assets" "$CAPTURE_DIR"
```

The capture script refuses a headless display and records the engine/renderer.
