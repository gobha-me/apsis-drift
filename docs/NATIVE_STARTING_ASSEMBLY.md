# Saved native Wayfarer assembly

#414 connects the fixed parked starter to the C++ session, save and native
presentation. This selection keeps the visible restraints and effective cockpit
contact geometry together. It does not add boarding, seating or departure.

## State and compatibility

New Game retains the existing seed, Origin world, station actor, attached D1
craft and shared clock. Format21 wraps the unchanged format20 journey with a
closed, versioned `wayfarer-stowed-01` starting selection. The selection pins the
operating base, replacement model, frame and contact bytes and the supported
fixed hardware pose. C++ validates and owns the selection and immutable contact
view; Godot consumes its model and transform binding.

Formats16–20 preserve their existing encodings and support limits. Historical
saves retain the original presentation and unknown hardware state; loading does
not infer a parked pose or migrate equipment. Save As retains the selected
format and assembly. Unsupported poses, identities or journey phases refuse
before changing the source save or active session.

## Native handoff

The bridge stages one unstepped candidate. Its pending dictionaries come from
that C++ candidate, with an exact commit/discard token. Reading the candidate
does not advance either clock or replace the active session. A second stage or
a wrong token refuses without replacing the pending candidate.

The native shell loads and validates the complete selected model and view
off-tree before committing. The operating base's whole `WFOpSeatLift` is removed
once; fourteen flat replacement roots are installed as siblings. All receive
the complete C++ seat delta once. The other operating groups, calibrated eye,
screens, gear and glass retain their bindings. The thirteen replacement contact
objects are a separate roster; retained render objects are not extra collision
obstacles.

The operating exterior atlas is surface14; historical starter presentation uses
surface18. The selected atlas binding drives both its LOD/compression exemption
and main-exhaust attachment. Model presence or a preparation receipt cannot
choose equipment.

Asset, geometry, material and exhaust checks finish before the exact candidate
commit. The ready view then replaces the previous view synchronously. A late
loading failure discards only the candidate and preserves the previous complete
view/session. Streaming resources are prepared with the C++ candidate, so
activation does not introduce another fallible initialization after commit.

## Assets and verification

`tools/prepare_freedom_native_assets.py` composes the existing authenticated
starter package with fixed `operating/` and `stowed/` companion directories.
It publishes a complete new directory atomically and validates all files on
reuse. Changed or incomplete existing outputs refuse without repair. Existing
provenance and license records accompany each constituent. The native launcher
uses this preparation; the standalone constituent preparers retain their
original closed-directory contracts.

The C++ save/session contract checks exact wrapped journey preservation,
historical formats, typed identities and fixed pose, finite state, source-save
immutability and deterministic continuation. Native assembly and staged-start
contracts exercise the actual selected loader, once-applied transforms, atlas
binding and late-failure preservation. These are headless integration checks;
they do not establish real-time performance or complete the First Flight route.

See [static asset delivery](WAYFARER_STOWED_ASSETS.md),
[effective contact](STOWED_COCKPIT_CONTACT.md),
[station walking](SAVED_STATION_WALK.md) and
[Godot ownership](GODOT_ADOPTION.md).
