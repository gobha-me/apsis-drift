# Native start screen

Run `tools/run_godot_native.sh` to open the title. Choose **New Game** with an
unsigned 64-bit universe seed, **Continue…** with a JSON save, **Flight basics**
or **Quit**. New Game starts the actor on Origin Station; the Wayfarer waits
at D1. A seed is a whole decimal value from zero through 18446744073709551615,
without signs, spaces or leading zeros. The default field contains 42.

Opening the title or its read-only reference generates no C++ world. The
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
return. Its exclusive confirmation defaults to Cancel and warns that unsaved
progress will be discarded. The current view and children remain suspended;
Resume, Load and mode handoffs cannot run beneath it. Cancel or focus loss
retains the same paused journey and returns focus to Title. Confirmed acceptance
retires the old active view/bridge and opens a title with no generated world.
A later New Game or Continue still uses complete C++ staging. Optional ship audio
stops for the title, retains its single playback owner and mix preferences, and
rebinds to the accepted next journey. Recorded-loss recovery and the historical
frozen docked shell keep their existing workflows.

This is title work under #136. Profile catalog browsing, active-slot
Save/metadata and broader Settings remain separate work. The title makes no
automatic save and Quit never autosaves.
