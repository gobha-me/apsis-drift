# Known-system diagram

Pause seated flight, open **Flight instruments & navigation**, and expand the
navigation chart. The diagram plots systems whose identities and locations are
recorded. A ring marks the current system; a box marks the selected system.
Numbers follow the chart list. Use the existing destination buttons to select a
system, and its jump preview to assess distance and readiness.

This is an oblique position diagram. Its spacing is not a distance scale or a
jump-clearance assessment. The current reference region supports the original
pair, four charted systems, or four plus genuinely acquired frontier fixes.
The original discoverable profile admits up to six systems; the separate
[outer-frontier profile](FREEDOM_TOPOLOGY.md#outer-frontier) admits up to eight.
An unverified station lead adds no marker. The view does not set a permanent
universe size or reveal unknown slots. Existing markers keep their diagram
positions when a further system is resolved.

C++ admits system identity and location before projecting the actual integer
universe anchors. Local planet/orbit positions are separate. Current system
ownership grants no visit record; a typed planet or companion selection does
not highlight an old system slot. Empty fuel or an attached ship does not hide
recorded chart information.

Godot draws cached normalized points. Opening the chart or selecting an existing
destination refreshes the view; resizing redraws its cache. The diagram captures
neither focus nor pointer input. Existing keyboard/controller navigation stays
on the destination controls. Reads, opening, resize and Back advance no time,
spend no resources and preserve the complete Save. Selecting a destination has
the same saved result as the ordinary selection command.

The original version1 presentation packet retains its six-node bound. Only the
outer-frontier profile uses version2 with an eight-node bound and a taller
minimum plot to keep its markers distinct. Both use the same admitted anchor
coordinates and preserve marker positions as discoveries are added.

The diagram creates no world, route edges or travel action and queries no future
arrival. The original presentation increment is
[#703](https://github.com/gobha-me/apsis-drift/issues/703), extended by
[#709](https://github.com/gobha-me/apsis-drift/issues/709);
the wider [#207](https://github.com/gobha-me/apsis-drift/issues/207) horizon and
manual/controller/GPU acceptance remain separate.
