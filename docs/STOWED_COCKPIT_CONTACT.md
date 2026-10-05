# Matched static stowed cockpit contact

The [static stowed package](WAYFARER_STOWED_ASSETS.md) provides the parked
restraints' render and contact inputs. The existing C++ lower-cockpit handle can
select its replacement contact through
`make_origin_stowed_lower_cockpit_contact(base, replacement_contact, frame)`.
This factory supplies effective static queries; it does not change ordinary
game startup or enable boarding actions.

The selected derivatives are
[`assets/native/stowed-cockpit-contact-01`](../assets/native/stowed-cockpit-contact-01).
They reproduce the offline preparer's exact `replacement-contact.json` and
`frame.json`. CMake independently checks their SHA-256 identities and compiles
the approved bytes into the native/headless core. The factory validates closed
schemas, dimensions, indices, source attribution and frame/range ownership
before requiring those exact approved bytes. Verification needs no authoring
checkout, GLB importer, provider account or numerical-owner replay.

The immutable result owns the original geometry through its existing sharing
handle, retains the additive halo, and binds the
[eight-range partition](ORIGIN_STOWED_CONTACT_PARTITION.md) to that same source
catalog. Failed construction leaves the base unchanged; selecting replacements
twice refuses.

The three triangle-key namespaces remain distinct:

| Buffer | Meaning |
| --- | --- |
| `original` | Original group/triangle keys, excluding the eight removed ranges |
| `halo` | Existing additive lower-cockpit geometry; group zero |
| `replacement` | Thirteen stowed objects, 1,508 faces; group zero |

Removed original keys refuse lookup without redirecting to new faces. Every
other original obstacle and all 8,100 halo faces remain unchanged. Replacement
faces retain their explicit evaluated source-face IDs. Equal coordinates do
not merge separate occurrences. `objects()` keeps its halo-inventory meaning;
`replacement_objects()` exposes the separate replacement inventory.

Prepared points are direct REST, canonical metres, +Y up/-Z forward and already
world-baked. The complete existing ancestor-composed `craft_seat_lift` delta
applies once at `kCabinSeamHardware`: operating progress `{1,1,1,0}`. Original
catalog group11 binds motion index10 `seat_lift`. Source matrices are provenance
and are not reapplied. Arbitrary-progress overlays require subsequent
qualification against a matching retained base.

Existing lower-cockpit lookup, surface-point and reservation queries consume
this effective selection. Reservation traversal skips the eight original
ranges, retains unrelated originals and halo, and visits each replacement once.
It preserves the existing fixed reservation shapes, intersection predicates
and declared coverage domain. The cabin corridor already uses these effective
queries; its walking-tile keys remain original. `original_geometry()` describes
original-source provenance and must not be used as the effective replacement
collision view.

This selection does not prove that an actor fits or can reach the seat. Matched
Godot presentation must replace the complete old seat-lift renderer once on the
verified operating base, preserving eye, screens, gear and exterior/exhaust
bindings. The [saved starting assembly](NATIVE_STARTING_ASSEMBLY.md) connects
that matched presentation and contact selection. The articulated transfer and
occupied hardware motion remain under
[First Flight #245](https://github.com/gobha-me/apsis-drift/issues/245).
