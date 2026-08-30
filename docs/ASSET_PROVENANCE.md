# Asset provenance

`assets/provenance.json` is the versioned, machine-validated provenance record
for media committed to Apsis Drift. The prose below remains useful narrative,
but it does not replace a manifest entry. The current manifest represents the
code-native title and every existing acceptance capture without moving or
rewriting those files.

Validate it from a configured build with:

```sh
./build/apsis-drift-asset-validator --root . assets/provenance.json
```

The validator is publication tooling, not a runtime asset system. It resolves
manifest paths against the explicit repository root, rejects symlink traversal
and paths outside that root, and performs no network access.

## Version 1 contract

The root contains `schema_version: 1` and an `assets` array. Documents are
bounded to 1 MiB, 1,024 records, 32 levels of nesting, 256 entries per JSON
nested collection, and 16 KiB per string. Unknown fields, duplicate JSON keys,
unknown enumerators, and unsupported schema versions fail closed.

Every asset has:

- a globally unique ID of the form
  `<media-type>/<lower-kebab-name>`, where the media type is `visual`, `font`,
  `music`, `voice`, or `sfx`;
- one matching `source_kind`: `generated`, `third-party`, `code-authored`,
  `capture`, or `derived`;
- one or more normalized repository-relative `files`, a `purpose`, and an
  explicit `edits` array;
- license evidence naming an SPDX expression or `LicenseRef`, its local or
  HTTPS terms, permitted `source`/`documentation`/`runtime` uses,
  redistribution and derivative status, and attribution.

New encoded assets use
`assets/<media-type>/<lower-kebab-name>.<extension>`. Existing source-native
and documentation capture paths remain where their owning systems expect them.
A file belongs to exactly one manifest record. Files under `assets`, `docs`, or
source locations must respectively permit runtime, documentation, or source
use. Committed files must permit redistribution; a derived record is rejected
unless every named parent permits derivatives. Parent IDs must exist and the
complete relationship graph must be acyclic.

Each source kind adds only its relevant evidence:

- `generated` records provider, tool, model and version, prompt, generation
  date, source output, and either a seed value or why no seed was available;
- `third-party` records an author or publisher, retrieval date, upstream
  license, and either a canonical HTTPS URL or pinned package name/version;
- `code-authored` records author, date, construction method, and an explicit
  list of derived input asset IDs, which may be empty;
- `capture` records application, scenario, application version, date, tooling,
  and either structured deterministic inputs or why they do not apply;
- `derived` records parent asset IDs, transformation, tooling, and date.

Generated or upstream source files that are not selected for distribution do
not enter this manifest merely because they were considered. When a selected
asset is edited, keep the original generation/upstream record if its file is
committed, give the result its own `derived` record, and name the original ID
in `parents`. If only the selected result is committed, its source-kind record
must retain the external source output and complete edit history.

## Code-native title alphabet and palette

- Location: `src/title.cpp`
- Purpose: scalable `APSIS DRIFT` startup title treatment
- Origin: original, repository-authored bitmap glyphs and color palette created
  for Apsis Drift on 2026-08-15
- Tooling: authored directly as C++ bit rows and color constants; no generated
  image, external font, encoded media, or third-party source was used
- Glyph coverage: uppercase `A`, `D`, `F`, `I`, `P`, `R`, `S`, `T`, plus space
- Scaling: integer-only from the fixed 5x7 glyph grid
- License: BSD 3-Clause, under the repository [license](../LICENSE.md)

The title remains source code rather than an encoded media file. Its
`visual/title-alphabet` manifest record makes that intentional boundary
machine-checkable.

## Flight Deck acceptance captures

- Locations: `docs/media/flight-deck-kitty.png` and
  `docs/media/flight-deck-ansi.png`
- Purpose: representative README captures of the completed v0.2 cockpit and
  exterior viewport on both supported presentation paths
- Origin: captured from Apsis Drift's repository-authored canonical Flight
  Deck acceptance run on 2026-08-15; no generative model, external image,
  third-party art, or encoded source asset was used
- Scenario: `v0.2-flight-deck`, seed `12648430`, 18 fixed commands, final
  simulation tick 240
- Kitty capture: `--driver kitty --profile local`, logical viewport 640x480
- ANSI capture: `--driver ansi --profile remote`, logical viewport 320x240
- Tooling: rendered by Apsis Drift through TermForge in Kitty 0.48.2; captured
  as the active KWin window with KDE Spectacle 6.7.4; cropped to the decorated
  window bounds and metadata-stripped with ImageMagick 7.1.2-29
- Edits: lossless crop only; no color, content, compositing, or generative edits
- License: BSD 3-Clause, under the repository [license](../LICENSE.md)

## Orbiting-home acceptance captures

- Locations: `docs/media/origin-home-kitty.png` and
  `docs/media/origin-home-ansi.png`
- Purpose: deterministic visual evidence that the tutorial home and the
  tick-resolved Origin Station marker share one system-space view
- Origin: captured from Apsis Drift's repository-authored intersystem-return
  acceptance path on 2026-08-19; no generative model, external image,
  third-party art, or encoded source asset was used
- Scenario: `v0.4.12-intersystem-return`, universe seed `42`, authoritative
  origin-arrival tick `1260`, station `station-ce51e866ec4e032d`
- Kitty-scale capture: local profile, logical viewport 640x480, framebuffer
  checksum `11003545979043014705`
- ANSI-scale capture: remote profile, logical viewport 320x240, framebuffer
  checksum `7194522011593911474`
- Tooling: application framebuffer written as binary PPM by Apsis Drift and
  converted losslessly to stripped PNG with ImageMagick 6.9.12-98; the files
  are renderer evidence at the viewports consumed by the supported Kitty and
  ANSI paths, while encoder behavior is validated by the separate driver matrix
- Edits: deterministic PPM-to-PNG encoding and metadata removal only; no color,
  content, compositing, or generative edits
- License: BSD 3-Clause, under the repository [license](../LICENSE.md)
