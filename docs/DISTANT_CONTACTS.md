# Distant optical contacts

The optical-only contract below remains unchanged. Explicit local-drive routes
now separately support [Moon landing](AIRLESS_MOON.md) and
[origin asteroid inspection](ASTEROID_INSPECTION.md). Tracking CONTACT alone does
not select either route or imply that arbitrary contacts support close flight.

The dated Freedom increment in #214 adds one C++ seeded moon and one minor
body as optical contacts in the ordinary native sky. They share the existing
authoritative simulation tick. Their apparent silhouette, illumination and
rotation come from the authenticated secondary catalog; Godot adds no orbit,
clock, physical surface or gravity source.

Resolved eligible contacts use their supplied angular radius, without an
enlarged asteroid silhouette. Unresolved contacts can have a direction
annotation but no fabricated optical disc. Occluded and close-unavailable
contacts are not drawn. The C++ close-observation boundary is explicit:
this slice supports distant observation, not approaching a new solid body,
gravity switching, collision, terrain or moon/asteroid landing.

In paused Controls, **Track optical contact 1/2** selects a view-local CONTACT
annotation and explains its current visibility or limitation. Back returns to
Controls; Resume remains explicit and retains the existing neutral-input gate.
Selection itself needs no neutral input and issues no simulation command.
**Stop tracking optical contact** clears the annotation. No selection is
inherited by a new view or saved to the journey. HOME, RETURN and MARK keep
priority; an overlapping CONTACT annotation is hidden while its selected
status remains in the compact readout. Labels stay generic; no exact identity,
body classification, physical radius, distance or undisclosed catalog name is
introduced by this display.

`state.distant_contacts` is optional. Its closed version-1 packet contains
`version`, `selected`, `system_id`, `tick`, and at most two `contacts`. Each
contact has exactly `id`, `label`, `status`, `direction`, `angular_radius`,
`light_direction`, `tint`, `spin_radians`, and `reason`. Directions are unit
vectors in the existing native tangent axes. The same-tick owner, canonical
decimal identifiers, finite geometry/colors and all bounds are validated
before any sky uniform is changed. A malformed packet or destination leaves
accepted graphics unchanged. Historical absence or explicit unselection
disables contacts without adopting a recipe.

`native_distant_contacts.gd` exposes `valid(state)`,
`selected_contact(state, id)` and `apply(state, materials)`. The existing
environment transfer binds at most two visible contacts through
`distant_contact_count`, `distant_contact_directions`,
`distant_contact_angular_radii`, `distant_contact_light_directions`,
`distant_contact_tints` and `distant_contact_spins`. Packet order is
farther-first for optical composition. The sky shader adds phase shading and
coarse analytic albedo variation from C++ spin, beneath existing atmospheric
optics. These are background optical projections, not collisionless solid
meshes or flight destinations. Existing station-walk sky transfer consumes
the same packet; the suited surface view shares the ordinary flight view.

New Game explicitly selects `flight_model.companions: 1` in its existing
optical save envelope. The component is independently versioned; historical
absence remains zero internally and is omitted on write. Existing flight,
docking, journey and recovery wrappers retain the selection. Unsupported
values and its appearance in an older clear-envelope format are refused.
No generated descriptors or view-local tracked ID are added to the save.

The current catalog provides exactly one moon parented to the first planet
and one minor body parented to the star outside existing planetary annuli.
Circular periods derive from the existing parent gravity parameters. This
bounded population recipe does not model long-term orbital stability or new
forces. The minor body's spherical optical proxy and coarse analytic surface
variation are provisional art. The close boundary begins when apparent radius
would exceed 0.2 radians; its disc is suppressed and the tracking readout
states that close flight and landing are unavailable. Apparent radii below
0.00002 radians remain unresolved, with an optional annotation only.

Optical limits: occultation currently tests the centre sightline, so partial
moonrise can pop at the limb. Phase shading uses an orthographic sphere
approximation and becomes coarser near the close boundary. These are recorded
presentation limits for later review; the angular silhouette still uses the
physical radius and distance. No new solid surface is implied.
