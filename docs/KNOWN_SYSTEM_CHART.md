# Known-system diagram

Pause seated flight, open **Flight instruments & navigation**, and expand the
navigation chart. The diagram plots systems whose identities and locations are
recorded. A ring marks the current system; a box marks the selected system.
Numbers follow the chart list. Use the existing destination buttons to select a
system, and its jump preview to assess distance and readiness.

This is an oblique position diagram. Its spacing is not a distance scale or a
jump-clearance assessment. The current reference region supports the original
pair, four charted systems, or four plus up to two genuinely resolved systems.
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

The view adds no world, generation, Save or request version. It creates no
route edges or travel action and queries no future arrival. This is the bounded
presentation increment in [#703](https://github.com/gobha-me/apsis-drift/issues/703);
the wider [#207](https://github.com/gobha-me/apsis-drift/issues/207) horizon and
manual/controller/GPU acceptance remain separate.
