# Native start screen

Run `tools/run_godot_native.sh` to open the title. Choose **New Game** with an
unsigned 64-bit universe seed, **Continue** for the newest valid catalog slot, **Load saved journey…**
to browse slots, **Open save file…** for an explicit JSON path, **Flight basics**,
[**Settings**](NATIVE_SETTINGS.md) or **Quit**. New Game starts the actor on Origin Station; the Wayfarer waits
at D1. The new journey is unsaved until Save As creates its first slot. A seed is a whole decimal value from zero through 18446744073709551615,
without signs, spaces or leading zeros. The default field contains 42.

**Starting charts** offers **Nearby systems**, **Four charted systems**,
**Four systems + planetary charts**, **Four charted + two uncharted systems**,
and **Outer frontier: four charted systems**. A visible, wrapped explanation
below the selector describes the current choice without requiring a mouse
tooltip. The first two keep their existing local reference facts but grant no
remote planetary charts or commission office. The last three add planetary
names and orbital locations for [manual planet trips](FREEDOM_PLANET_TRAVEL.md)
and support optional [survey commissions](SURVEY_COMMISSIONS.md) at Origin.
Additional navigation charts do not grant surface findings or visits. The two
search profiles retain their distinct regional and limited-reach
[discovery rules](FREEDOM_TOPOLOGY.md); neither promises an unlimited region.

**Jump drive** has its own visible explanation. Standard retains its ordinary
arrival rules. Recovery starts a 45-second cycle at arrival; temporary strain
carries unfinished recovery into a 45–90 second cycle. Valid early jumps remain
possible for one charge, with up to 2 km of additional arrival uncertainty that
fades as recovery clears. Pause freezes recovery; this is not a mandatory
cooldown or a new range/maintenance policy. See [jump recovery](FREEDOM_JUMP_RECOVERY.md).
Three starting charges and physical Origin docking for replenishment remain
unchanged. Exploration and technical fuel service remain free until explicit
[Paid operations](PAID_OPERATIONS.md) enrollment; accepting work does not enroll
you. Continue retains its saved chart and drive choices.

Opening the title, control Settings or its read-only reference selects no active C++ journey. The
reference explains the current Freedom mechanisms and control bindings without
claiming live observations. Escape/Start or Back returns to the title. Keyboard,
mouse and ordinary controller focus navigation select actions; the seed field
requires text entry to choose a different seed. Short windows scroll the menu
and reference while keeping readable text. Keyboard/controller focus reveals
each selector together with its explanation; the labels do not take focus or
change popup navigation. Reading or changing a choice creates no C++ journey
and leaves error/status feedback separate from the help.

Accepting New Game, Continue, a catalog slot or an explicit save file immediately
shows a static loading acknowledgement. It identifies whether the game is
preparing a new world or checking a saved journey and preparing its view. The
acknowledgement gets a complete frame before preparation starts; the window may
briefly stop updating during that work. It does not show a percentage or promise
continuous animation. Other title actions stay locked until the request succeeds
or returns a readable refusal. Losing focus before preparation starts cancels
that request; restore focus and choose again. Duplicate or stale requests cannot
open a second journey. Failed loading restores the menu without changing the
source Save. Successful loading retains the existing paused, neutral-control
handoff.

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

New Game and Continue retain the title's selected controller. Returning to the
title retains the journey's live selection, including keyboard-only ownership;
another connected pad does not silently take over. Fresh journey views keep
their existing focus, held-control neutralization and explicit Resume gates.
These presentation transfers do not change saved world state.
