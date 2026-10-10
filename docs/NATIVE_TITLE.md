# Native start screen

Run `tools/run_godot_native.sh` to open the title. Choose **New Game** with an
unsigned 64-bit universe seed, **Continue** for the newest valid catalog slot, **Load saved journey…**
to browse slots, **Open save file…** for an explicit JSON path, **Flight basics**,
[**Settings**](NATIVE_SETTINGS.md) or **Quit**. New Game starts the actor on Origin Station; the Wayfarer waits
at D1. The new journey is unsaved until Save As creates its first slot. A seed is a whole decimal value from zero through 18446744073709551615,
without signs, spaces or leading zeros. The default field contains 42.

Opening the title, control Settings or its read-only reference selects no active C++ journey. The
reference explains the current Freedom mechanisms and control bindings without
claiming live observations. Escape/Start or Back returns to the title. Keyboard,
mouse and ordinary controller focus navigation select actions; the seed field
requires text entry to choose a different seed. Short windows scroll the menu
and reference while keeping readable text.

Continue uses the existing C++ save validation and complete detached model/view
staging. Cancel or refusal retains the title and leaves the source file intact;
no current world is committed. Losing focus prevents accepting a selection,
including one deferred from a button callback. Successful Continue starts
paused and requires focused, neutral controls before explicit Resume. It does
not refill resources or alter a committed jump.

Explicit `--new-game=SEED` and `--continue=ABSOLUTE_PATH` still bypass the title.
`--headless-validate` requires one such selection. Direct project launch can
open the title, but playable selections require prepared assets; the repository
launcher prepares them. Optional recorded-audio arguments are retained for the
accepted journey; the title does not start playback.

Choose **Title…** on the station or in paused saved-flight/suited controls to
return. When the journey is dirty, its exclusive confirmation defaults to Cancel and
warns that unsaved progress will be discarded. Clean journeys return directly. The current view and children remain suspended;
Resume, Load and mode handoffs cannot run beneath it. Cancel or focus loss
retains the same paused journey and returns focus to Title. Confirmed acceptance
retires the old active view/bridge and opens a title with no active journey.
A later New Game or Continue still uses complete C++ staging. Optional ship audio
stops for the title, retains its single playback owner and mix preferences, and
rebinds to the accepted next journey. Recorded-loss recovery and the historical
frozen docked shell keep their existing workflows.

The [native save catalog](NATIVE_PROFILES.md) supplies slot browsing, active-slot
Save/Save As and dirty-progress transitions under #136. The title makes no
automatic save and Quit never autosaves.
