# Optional frame diagnostics

Pause station walking or saved flight, choose **Frame diagnostics: On**, then
Resume explicitly. A small noninteractive panel appears during focused play.
The choice follows ordinary boarding and body-view handoffs. It is local to
the current application journey; it is not stored in a world save or preference
file. Turn it off through the same paused control.

The panel shows:

- Median and nearest-rank 95th-percentile active frame intervals, in milliseconds,
  over at most the last 240 samples. These are elapsed presentation-frame
  intervals, not GPU rendering durations or a portable benchmark.
- The engine's `Performance.TIME_PROCESS` reading, converted from seconds to
  milliseconds. This is an engine process monitor, not an isolated GPU or C++
  simulation timer. Engine counters can lag the displayed frame.
- Total engine-reported draw calls and primitives, including the active render
  passes, rather than a count of source meshes or objects.
- The actual 3D render-target pixel dimensions supplied by the current view.
- The current authoritative tick and the bridge's cumulative dropped elapsed
  seconds. Dropped time belongs to its bounded fixed-step scheduler, not a count
  of discarded saves, discoveries or rendered frames. It resets with a new
  bridge/Continue rather than being written into the world document.

GPU timing is deliberately not sampled. Headless tests can exercise controls
and layout but cannot turn these counters into GPU performance evidence.
A high frame interval, draw count or dropped-time total is a clue, not a cause
or an automatic quality adjustment. Useful comparisons keep window size,
view direction, renderer, scene and workload consistent.

The display updates at most twice per second. Pause, focus loss, nested dialogs,
view replacement and invalid readings discard the sampling interval. The first
active delta afterward is excluded, so a pause is not counted as a slow frame.
Disabling clears samples and hides the panel; ordinary disabled play does not
query the diagnostic monitors. Invalid/nonfinite telemetry or target dimensions
are refused. The panel never owns controls, simulation time or world state.

There is no automatic logging, file creation, upload or hardware inventory.
If reporting a slowdown, note the route location and view direction along with
the visible measurements. Manual input feel and broader frontend qualification
remain separate under [#244](https://github.com/gobha-me/apsis-drift/issues/244)
and [#245](https://github.com/gobha-me/apsis-drift/issues/245).
