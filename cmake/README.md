# Build organization

The root `CMakeLists.txt` defines options, pinned dependencies, application
targets and source-specific floating-point settings. It includes these modules
in the original configuration order:

| Module | Responsibility |
| --- | --- |
| `NativeData.cmake` | Authenticate admitted contact, support and motion data; prepare generated headers and material translation units |
| `CoreTests.cmake` | Application and headless contract executables and test registrations |
| `AssetTests.cmake` | Asset manifest and provenance contracts |
| `TerminalTests.cmake` | Retained terminal, audio, benchmark and CLI contracts |

These files are included in the root source and binary directory context.
Relative source paths, generated outputs, directory dependencies, variables and
test properties rely on that context. Keep their ordering and enclosing build
conditions; they are not standalone subdirectories or function definitions.

`apsis-drift::core` owns authoritative world, flight, contact and save code.
`apsis-drift::boarding-research` contains deferred anatomical certification
sources and links core. The full test build includes its existing contracts;
the native bridge has no dependency on this optional research target.
`apsis-drift::lib` adds retained terminal presentation and audio, and the
`apsis-drift` executable uses it. The asset manifest library and validator
provide content metadata checks. The existing `src/godot/CMakeLists.txt` owns
native snapshot, fixture and bridge targets; it retains its separate output
directory. Historical `GODOT_SPIKE` option names remain compatibility switches
for the active native path.

See [development and testing](../docs/DEVELOPMENT.md) for build commands and
[native ownership](../docs/GODOT_ADOPTION.md) for the C++/Godot boundary.
