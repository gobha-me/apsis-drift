# Assets

Ordinary native play uses the source-verified packages in `native/`. The
[native launcher](../tools/run_godot_native.sh) prepares their decoded outputs
under its selected build directory. Contributors can run the game without an
authoring checkout, provider account or Blender installation; see
[native setup](../godot/README.md).

| Location | Role and instructions |
| --- | --- |
| `native/freedom-starter-01/` | Selected Wayfarer and station exports, packaged losslessly with metadata and licenses. See [starter preparation and verification](../docs/NATIVE_STARTER_ASSETS.md). |
| `native/wayfarer-operating-02/`, `native/wayfarer-stowed-01/` | Matched operating and stowed companions. See [operating assets](../docs/WAYFARER_OPERATING_ASSETS.md), [stowed assets](../docs/WAYFARER_STOWED_ASSETS.md) and [saved assembly selection](../docs/NATIVE_STARTING_ASSEMBLY.md). |
| `native/origin-crew-01/` | Selected pixel-art attendant idle views with reference clearance, provenance and a closed byte roster. See [station crew](../docs/STATION_CREW.md). |
| Other `native/` packages | Source-bound walking, contact, motion and material inputs. Some support retained boarding research; consult the package metadata and [grouped documentation index](../docs/README.md) before changing them. |
| `visual/` | Retained Blender masters, exchange tiers, render studies and historical preview deliveries. See [visual design history](../docs/VISUAL_DESIGN_KIT.md), [hero study history](../docs/HERO_ASSET_REVIEW.md) and [preview provenance](../docs/FREEDOM_PREVIEW_01.md). These are separate from the selected native starter package. |
| `music/`, `sfx/` | Retained First Light audio assets and MIDI research fixtures. See [audio contracts](../docs/AUDIO.md) and [provenance](../docs/ASSET_PROVENANCE.md). Native ship audio uses the separate [optional recorded-audio path](../docs/NATIVE_SHIP_AUDIO.md). |
| `provenance.json` | Versioned media provenance manifest, with source, license and transformation evidence. Its [validation contract](../docs/ASSET_PROVENANCE.md) complements each native package's own roster and provenance. |

Keep package manifests, source hashes and license records together. Historical
source filenames in lineage records are evidence pointers; ordinary asset
preparation does not require those authoring files. Moving or removing a file
requires checking its package roster, provenance parents and code/tool consumers.
Large compressed payloads are required content rather than generated build output.

Put decoded models, imports, renders and temporary compression caches beneath
an ignored build directory. Reuse prepared output when its verified inputs are
unchanged. Do not commit private recordings, provider responses, local account
details or generated output without the required provenance and redistribution
evidence. [Asset tools](../tools/README.md) distinguish preparation from authoring
and historical review.
