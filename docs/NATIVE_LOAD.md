# Load a saved Freedom journey

Choose **Load…** on the station or inside **Esc / Start · Controls** during
saved flight or suited ground walking. Select a JSON save, then confirm
**Load journey**. Replacement discards unsaved progress; it makes no automatic
save. Cancel returns to the paused invoking action. Resume remains explicit and
requires focused, neutral movement controls.

The root presents a filesystem chooser, not a profile catalog. It uses the same
C++ Continue validation and detached, complete model staging as command-line
startup. A corrupt, incompatible or missing file, or a refused asset, leaves
the current journey, complete save bytes and source file intact. Refusal text
appears in the current paused interface. A successful load commits its C++
candidate before swapping views, starts paused, and rebinds the existing audio
owner. It does not refill fuel, refund a committed jump, alter saved actor
phases or invent knowledge.

While either chooser or confirmation is open, the current view is suspended;
Escape/Start cannot resume the world beneath it. Losing application focus keeps
the journey paused and prevents replacement confirmation until focus is restored.
Closing/canceling a nested dialog never resumes movement. The chooser and warning
keep readable text as the window changes size; the confirmation starts with
Cancel focused.

This is a bounded #136 increment for the station, saved-flight and suited views.
Recorded-loss recovery retains its own explicit overlay. The historical frozen
docked shell, title/catalog browsing, active-slot Save semantics and general
settings integration remain separate work. Existing **Save As** and explicit
command-line New Game/Continue remain available; quitting does not autosave.
