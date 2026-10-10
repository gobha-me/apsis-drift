# Native control settings

Ordinary native startup exposes **Settings** before a journey is selected.
The saved-flight pause menu opens the same screen over its current input
provider, including while walking outside a landed craft. Station walking also
opens this pending screen from its Settings action. All routes use the existing
version-4 controls provider and
user://freedom-controls-v4.json; preferences stay outside C++ world/save state.

The screen offers stick/trigger dead zone, response curve, flight head-look
speed, vertical flight head-look inversion and automatic/Xbox/PlayStation text
prompts. Station walking consumes dead zone and curve; saved flight consumes
all five. Ground keys and station look speed/inversion retain their documented
fixed behavior. Flight remapping remains in the paused flight controls.

Edits and **Restore Defaults (pending)** change the pending choices only.
**Apply** validates and writes before adopting them, preserves supported unknown
root/settings fields and all bindings, and installs the current input mapping
with a neutral gate. Failed writes retain the prior live choices and destination
file; the pending choices remain available for correction. **Cancel / Back**,
Escape, controller B/Circle or Start returns to the invoking Settings action.
From paused gameplay, Back stays paused; only a later root Resume can resume
after the existing neutral gate. The flight reference uses the same one-level
Back behavior. Unsaved flight studies retain their previous controls.
Successful Apply stays on the screen; a later Cancel discards only subsequent
pending edits.

The title does not create or advance a world while Settings is open. Losing
application focus blocks Apply and Back. A missing controller does not disable
keyboard use or stored controller choices. With persistence explicitly disabled,
Apply changes the current input provider without writing a file. In paused
gameplay, the screen retains pending edits through a focus/device safety pause
and discards them when the session/menu is explicitly hidden. Preference edits
advance no C++ ticks and do not change the committed save.

Missing files use defaults. An unreadable, oversized, malformed or unsupported
version uses in-memory defaults with a diagnostic and disables this screen's
Apply; inspect or move the source file before restarting to use a fresh file.
It is not silently replaced. Writes retain the existing same-directory temporary
file/flush/rename mechanism; no new durability, multi-process conflict or
symlink-ownership guarantee is claimed.

Video quality, audio device selection, calibration and future-provider fields
are absent. Optional ship audio retains its existing paused-flight controls.
Station walking retains fixed movement keys and explicit controller selection;
its preference provider is disabled as a gameplay input source. Active-slot/catalog
Save and the wider pause program remain #136 work. This screen does not claim that composed program or
physical-controller qualification.

The public control_settings test covers invalid candidates/source files,
unwritable paths, failed replacement, extension/binding preservation, pending
defaults/Cancel, focus loss, keyboard/controller navigation and scrollable
640×450/800×450/1280×720 layouts. The native_title test also applies preferences
before a real C++ New Game, checks the empty owner during editing and verifies
the next station walker reads the saved preferences without rewriting them.
The saved_flight test also compares real C++ state and complete save bytes
around pending/Apply/defaults/Cancel, failed writes, nested Back and resizing.
The native_walk test checks station pending/Apply/defaults/Cancel, failed writes,
focus guards, resizing and complete C++ save immutability.
Both compiler bridges are used for publication checks; synthetic events do not
qualify physical hardware.
