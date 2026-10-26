# Home navigation in native flight

The cyan HOME diamond shows Origin Station's bearing when its projected position
is on screen. An edge arrow shows the direction when it is off screen; the text
also identifies a target behind the current view. The marker carries its readable target name and live metre/kilometre range
while flying; the station range is also retained in expanded instruments. The marker follows the cockpit or exterior camera,
including free look, and hides while the craft is attached.

Within one kilometre of the station centre, a valid selected port changes this
same cue to **D1** or **D2** and points it at that collar. Its marker range is
from the craft reference point to the indicated target. The expanded Origin
Station line still measures the station centre. Neither is the own-collar
separation used for capture: use live collar offsets and assessment to align
and dock. The bearing does not certify a clear approach.
At distance or without valid selected-port geometry, the cue remains HOME.

Paused Controls offers **Track Origin Station**, **Track Origin D1/D2** for the
currently selected known port, and **Stop HUD tracking**. The station is tracked
by default. Explicit port tracking retains its registered target at distance;
changing the existing port selector changes which port it follows. Tracking
alone changes no selected jump, port, observation, craft or Save. It is a
view preference; opening a replacement view or Continue starts with the station
default. Unavailable local stations and invalid port geometry grant no target.
Ground MARK/SHIP/RETURN and optical-contact cues retain their own controls.
Edge bearings can overlap the compact status card; screen-space avoidance is
still a presentation refinement under #244.

On foot, a separate **SHIP** marker uses the same projection to point toward the
landed craft centre. HOME stays hidden during surface walking. The bearing helps
reacquire the ship; the existing aft-access reach check still controls reboarding.

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
For keyboard/mouse play, the live contextual action also displays its existing
captured-left-click binding. The captured-pointer shortcut stays inactive while
paused; the normal paused commands remain available.

Godot projects the existing C++ station position, registered port geometry and
reference-planet geometry. Close port bearings use the current station transform,
matching the station model's port markers; craft attitude and the imported mesh
offset do not move those registered targets. Geometry must match the current
station owner before a port is used.
It neither changes state nor generates a destination, chart, discovery or flight
command. Invalid dimensions, non-finite positions, coincident targets and
collapsed camera transforms refuse projection. Render-pixel and logical UI
sizes are handled separately, including narrow and stretched windows. Invalid ranges produce no number;
name, range and the reference-globe horizon warning share a clamped caption.

`home_navigation` compares markers with real Camera3D projection, tests edge and
behind-camera bearings, viewport scaling, rotated cameras, globe occlusion and
invalid inputs. Existing saved-flight tests check near/distant/attached HUDs,
controller actions, layout/scrolling and unchanged authoritative saved bytes.
The actual port tests also check D1/D2 selection against the model's registered
markers, invalid/foreign geometry fallback and view-only Save-byte invariance.
The planetary checkpoint captures remain presentation evidence, separate from
continuous manual/controller play and display-performance qualification.
