# Home navigation in native flight

The cyan HOME diamond shows Origin Station's bearing when its projected position
is on screen. An edge arrow shows the direction when it is off screen; the text
also identifies a target behind the current view. Range stays visible without a
long numeric identifier. The marker follows the cockpit or exterior camera,
including free look, and hides while the craft is attached.

A reference-globe sightline test identifies home beyond the planetary horizon.
This is directional information for manual flight, not an orbital transfer
planner. The existing orbit forecast, atmosphere and physical controls still
matter. Terrain peaks and other occluders are not classified by this UI test.

Detailed collar offsets, port targets and capture/approach buttons appear within
one kilometre of the station or while attached. The full controller-accessible
pause menu retains these actions at every distance and uses the same C++ refusal
reasons. The distant flight HUD leaves room for the world; its scrollable backing
fits the remaining content. Continue remains paused until an explicit neutral
resume, and ordinary controls and applied exhaust keep their existing behavior.

Godot projects the existing C++ station position and reference-planet geometry.
It neither changes state nor generates a destination, chart, discovery or flight
command. Invalid dimensions, non-finite positions, coincident targets and
collapsed camera transforms refuse projection. Render-pixel and logical UI
sizes are handled separately, including narrow and stretched windows.

`home_navigation` compares markers with real Camera3D projection, tests edge and
behind-camera bearings, viewport scaling, rotated cameras, globe occlusion and
invalid inputs. Existing saved-flight tests check near/distant/attached HUDs,
controller actions, layout/scrolling and unchanged authoritative saved bytes.
The planetary checkpoint captures remain presentation evidence, separate from
continuous manual/controller play and display-performance qualification.
