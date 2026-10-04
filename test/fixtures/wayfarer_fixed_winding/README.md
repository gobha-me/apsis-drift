# Exact winding test references

These two unchanged source files preserve the previous exact classifier as a
reference for invented-shape differential tests of the bounded fixed-ray
classifier. Their relative layout preserves the reference loader. They contain
no ship, character or authoring asset buffers and do not admit a game asset.

- `stowed-embeddedness-proof-preparation/embeddedness_certificate.py`:
  10644 bytes, SHA256 `09b7433c3e58ac854f64ce64e03eff396ba44cf710fbdcf3586ed334bfbd3fe8`.
- `stowed-winding-containment-proof-preparation/winding_containment_certificate.py`:
  15130 bytes, SHA256 `748c307f3395f7c31751866871c23cdda895829cdaba8ec6c1677605aac04127`.

They are Apsis Drift source under the repository BSD-3-Clause license. Preserve
their bytes when updating the classifier; use an explicitly versioned reference
if its mathematics or authenticated handle contract changes. The test creates
only procedural fixtures and requires Python's standard library.
