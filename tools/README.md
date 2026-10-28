# Tools

Run commands from the repository root. Start with
[development and testing](../docs/DEVELOPMENT.md) for dependencies and compiler
checks. Ordinary game development does not require the Blender, film or asset
authoring tools below.

| Workflow | Entry points | Guide |
| --- | --- | --- |
| Play the native slice | `run_godot_native.sh` | [Native setup](../godot/README.md); builds the C++ bridge and prepares selected assets. |
| Native integration tests | `test_godot_native.py` | [Runtime contracts](../docs/DEVELOPMENT.md#native-runtime-contracts); use a finished matching build and explicit Godot executable. |
| Repository formatting and lint | `format.sh`, `lint.sh`, `check_nolint.sh` | [Pinned quality checks](../docs/DEVELOPMENT.md#format-and-lint). |
| Prepare selected native models | `prepare_freedom_native_assets.py`, `prepare_native_assets.py`, `prepare_operating_assets.py`, `prepare_wayfarer_stowed.py` | [Starter delivery](../docs/NATIVE_STARTER_ASSETS.md), [operating assets](../docs/WAYFARER_OPERATING_ASSETS.md), [stowed assets](../docs/WAYFARER_STOWED_ASSETS.md). Ordinary launch performs preparation automatically. |
| Prepare contact and motion inputs | `prepare_boarding_support.py`, `prepare_lower_cockpit_contact.py`, `prepare_operating_motion.py`, `boarding/prepare_*.py` | [Boarding support](../docs/ORIGIN_BOARDING_SUPPORT.md), [lower cockpit contact](../docs/LOWER_COCKPIT_CONTACT.md), [documentation index](../docs/README.md). These validate pinned source packages; they are also used by build/test consumers. |
| Package an authoring delivery | `package_native_starter.py`, `package_operating_assets.py`, `package_wayfarer_stowed.py` and other `package_*.py` | Follow the owning asset delivery document. Packaging needs the specified source checkout and receipts; it is separate from ordinary runtime preparation. |
| Historical Blender authoring and review | `build_visual_designs.py`, `build_hero_assets.py`, `export_hero_lods.py`, `render_hero_views.py`, `review_hero_lods.py`, `verify_hero_assets.py` | [Visual design history](../docs/VISUAL_DESIGN_KIT.md), [hero study history](../docs/HERO_ASSET_REVIEW.md). These can write masters or exports; read their owning instructions before running them. |
| Retained boarding qualification research | `build_boarding_qualification.py`, `qualify_boarding_*.py`, `qualify_station_clearance.py`, `boarding/` | [Deferred research index](../docs/README.md). Full anatomical certification is deferred and does not gate the [playable boarding prototype](../docs/PLAYABLE_BOARDING_PROTOTYPE.md). |
| Opt-in studies and offline film | `run_godot_study.sh`, `render_freedom_film.sh`, `verify_freedom_film.py`, `assemble_godot_reel.py`, `stage_reel_characters.py` | [Study guide](../docs/GODOT_STUDIES.md), [historical preview](../docs/FREEDOM_PREVIEW_01.md), [demo reel](../docs/GODOT_DEMO_REEL.md). Offline output does not qualify a playable journey or runtime frame rate. |
| Retained audio regeneration | `regenerate_first_light_assets.sh`, `regenerate_issue230_assets.sh`, `issue230_asset_builder.cpp` | [Provenance and source requirements](../docs/ASSET_PROVENANCE.md), [MIDI research decision](../docs/MIDI_SCORE_SPIKE_2026-08-31.md). |

`APSIS_DRIFT_TIDY_FILES` limits `lint.sh` to newline/space-separated repository-relative `.cpp` paths within its existing scope;
unset runs the full scope, and an empty value selects nothing.

Several scripts share identity/specification modules in this directory and
`boarding/`. Keep their paths stable unless the imports, CMake consumers and
documented commands are updated together. The [asset directory guide](../assets/README.md)
describes which inputs are runtime packages and which preserve historical work.

Reuse compiler build directories and put preparation/capture outputs beneath
ignored build directories. The native test runner removes its successful run's
generated asset copies by default and retains compact evidence; `--keep-work`
preserves them for a deliberate replay. Failed runs retain their stage for
diagnosis. Review those outputs before cleanup rather than deleting a source
package or another contributor's work.

The opt-in `apsis-drift-freedom-moon-fixture` target exports eight complete
phase saves from a continuous station-to-moon voyage using real native commands.
Build it with the native bridge configuration, then run
`build-native/src/godot/apsis-drift-freedom-moon-fixture OUTPUT_DIRECTORY`
with a new output directory beneath an existing parent. It is test support,
not a gameplay autopilot, and is deliberately absent from default CTest.
`tools/test_godot_native.py --test native_moon` prepares and consumes those
phases for the bounded [airless moon integration](../docs/AIRLESS_MOON.md).
