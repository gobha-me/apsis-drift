# Asset provenance

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

The title remains source code rather than a media file. Future generated or
third-party visual, music, or audio assets must add their own origin, tooling,
and license entries before publication.

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
