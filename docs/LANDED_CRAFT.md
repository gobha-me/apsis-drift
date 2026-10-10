# Landed craft lifecycle

C++ owns actual gear deployment, touchdown, surface constraint, idle time and
liftoff. The terrain provider described in [TERRAIN_TOUCHDOWN.md](TERRAIN_TOUCHDOWN.md)
remains a read-only query. A ready pad assessment alone does not create a landing.

The native Landing action follows the selected assistance mode. Manual mode
deploys gear and leaves flying to the pilot. Assisted mode requests a bounded
thruster maneuver over certified dry ground: within 25 m, speed below 5 m/s,
alignment within 2.5 degrees, angular speed at most 0.002 rad/s and pad height
spread at most 0.20 m. This small local aid does not search for a site or orient
a poorly aligned craft. It refuses unresolved geometry, excessive slope,
pressure, gravity or insufficient vertical thrust. It lasts at most 60 simulation
seconds. Enabling assistance alone neither deploys gear nor requests landing.
Manual intervention cancels the maneuver and retains deployed gear.

A safe contact requires the unchanged pad envelope plus conservative clearance
of the registered hull box against the actual finest top mesh. The hull query
bounds all eight rotated corners in one cube face, encloses their projected
cell range with a one-cell numerical halo, and evaluates at most 128 triangles.
A triangle's body-Y maximum is bounded by its vertices. Requiring all covered
terrain below the box bottom certifies clearance of the whole box. Face seams,
excessive work and nonpositive bounds refuse certification; a refusal does not
prove collision. This is endpoint clearance, not a swept collision or damage
system. Unsafe impact remains separately scoped.

Touchdown is an atomic transition from the current state checksum. Terrain,
material, motion, deployment and hull checks all qualify before commit. The
anchor retains planet/system and craft identity, fixed position/orientation,
landing tick, terrain policy and rotation versions. The accepted small relative
landing motion is constrained to zero. It awards no discovery, service,
resources, mission progress, repair or fuel.

While landed, neutral ticks advance the shared clock and resolve the inertial
pose from the unchanged planet-fixed anchor. Ordinary thrust is refused at the
C++ boundary; the native view sends neutral demand while landed. No hidden
velocity accumulates. Retraction, station capture and repeat touchdown refuse.

Explicit Liftoff revalidates the terrain, support, hull and rated vertical
thrust. Removing the constraint preserves the exact same-tick derived pose and
velocity, including planet rotation once. The transient aid uses rated thrust
until every pad is at least 3 m clear, then returns control with gear deployed.
Its deadline is 10 simulation seconds. The pilot can interrupt it. Stow gear
before approaching or capturing a station port. The existing atmospheric and
orbital dynamics continue to own flight. Actual gross propulsion consumes the
shared [Freedom flight quantity](FREEDOM_RESOURCES.md); atmospheric drag and
rotating-ground constraints do not become fuel channels.

## Save compatibility

Format 23 is an explicit surface wrapper around a complete existing flight,
docking, journey, starting-assembly or boarding document (formats 18–22). The
base remains the sole vehicle, history and clock owner. Surface state stores
actual deployment and the optional versioned fixed anchor. No terrain catalog,
controller command or extra vehicle is serialized. Existing saves keep their
original format until a surface action activates this layer.

Continue validates the base, owner, terrain support, hull and exact constrained
flight pose before opening. Version mismatches, contradictory gear/attachment,
changed identities, corrupt/nonfinite anchors, clock overflow, duplicate keys,
excessive nesting and document size refuse. Pending maneuvers do not resume
from a save. Native Continue stages the selected model and gear state while
paused, using the normal model-readiness transaction.

## Qualification boundaries

Run `landed-craft-contract` under GCC and Clang. Its generated dry-bedrock fixture
covers immutable idle, Save As/Continue, continuous release, assisted touchdown,
real ascent to a stable orbit, invalid and repeated commands, and independent long-double
rays across the hull underside. The opt-in `--fixtures DIRECTORY` mode emits a
near-ground boarding save and exact lifecycle checkpoints for the native
`native_surface` integration test. This is an explicitly initialized test site,
not a recorded station-to-site flight. The existing uninterrupted voyage remains
separate regression coverage for station departure, atmosphere, terrain cruise
and return.

The existing main and station-withdrawal exhaust remain animated from applied
thrust. Dedicated downward liftoff/upward-force exhaust is still a visual gap.
Gear presentation currently selects the authored stowed/deployed calibration
endpoints immediately. It adds no new asset or mechanism study. Compression
animation, swept impact response, EVA/hatch exit, taxiing, terrain detail and
manual pilot acceptance remain separate work.

The paused **Surface conditions** action exposes the shared
[starter environmental reference](CRAFT_ENVIRONMENT_ASSESSMENT.md) through
observed knowledge. Unknown conditions remain UNKNOWN. This reference query
does not replace the actual terrain/motion/hull qualification above; saved
environmental commitment policy remains part of #102.

The software Compatibility captures currently expose close-ground depth artifacts.
These images establish composed state/gear integration, not finished terrain
materials, contact shadows or manual landing acceptance. Keep this visual
limitation separate from the exact C++ lifecycle and save comparisons.
