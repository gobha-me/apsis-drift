# Local geological exposures

Selected journeys can encounter small pale, ochre or dark geological exposures
on the existing planetary ground. C++ generates their identities, positions,
radii and appearance from the selected world. They are flat color treatments
of the actual terrain, with broad visible bands; no raised rock, new collision,
resource deposit or change to landing and walking support is implied.
The first material pass uses conspicuous bands and a soft circular footprint;
it still needs natural variation and art refinement after the interaction is
playtested. It is not final planetary art.

New Game explicitly selects local landmarks. A historical journey keeps its
original selection until **Enable local landmarks** is chosen in paused
Controls. Reading the scene or opening menus never adopts the feature. Enabling
does not advance ship time, consume resources or grant sensor facts.

On foot, the compact card identifies only the nearest nearby exposure. It does
not list distant generated sites. Approach within eight metres of its centre
on supported ground and use **E**, captured **left click**, or the selected
controller's **A/Cross** while walking and look controls are neutral. The compact
action offers inspection nearby; a reachable ground return takes priority.
Inspection pauses and the existing surface-reference reader shows the label
and description. The first
inspection records it; repeating the action reopens the description without
adding history. Back returns to paused controls, and Resume retains the normal
focus and neutral-input gate. **Inspect exposure** remains in paused Controls;
that paused action still permits held movement. Both paths are zero-time
metadata actions, preserve the personal marker and use the same C++ proximity
and support checks. The direct on-foot E action does not fire its flight strafe
binding or bypass the separate neutral gate before resuming seated flight.

Command results and refusals retain priority. Exposure prompts do not replace
RETURN, SHIP, HOME or the optional MARK cue. A personal marker remains a separate
manual choice; inspection never overwrites it or supplies a safe route.

The native bridge supplies at most eight sites per current-body query. The
presentation helper validates the complete optional packet, matching system,
body and tick, before changing any accepted terrain material. It updates the
existing and newly admitted materials from C++ native-tangent positions, rather
than subtracting planet-scale floats. Site radii are 28–52 metres. The shader
uses the same Euclidean footprint as proximity and suppresses tint wherever
the existing wetness attribute is positive. The entire exposure fades with its
centre's camera distance between 300 and 360 metres; the C++ observer query is
bounded to 384 metres. Disabled, unselected and nonplanet materials retain their
previous appearance. There is no displacement, independent placement noise,
shader clock or presentation-owned survey.

The UI uses the existing bool bridge commands
`enable_freedom_surface_landmarks()` and
`inspect_freedom_surface_landmark(id)`. Refusals use the ordinary command
message; the surface-reference reader supplies the inspection description.
Native material, interaction, save/provenance and compiler qualification are
separate from physical-controller and display acceptance.

C++ recipe1 appends seed domain15 without changing existing domains. Reference
sphere cells are 512 metres wide with signed16 coordinates. A cell's independently
derived middle-half point is projected onto the reference sphere and retained
only if it still belongs to that cell. The selected contact recipe supplies its
actual ground height; submerged, mixed and excessively steep centres are omitted.
A centre check certifies neither the whole colored region nor the route to it.
Queries examine at most27 cells and retain at most64 memoized cells and four
terrain tiles; none of these caches are saved or change generated identities.

Knowledge stores optional version1 `surface_landmarks` metadata: at most64
inspections, sorted by system, planet and site ID, containing the cell, selected
terrain policy and authoritative first tick. Historical absence remains absent.
Inspection authenticates the current site and supported actor, then checks the
complete save byte limit before committing. Standard recovery retains these
records independently of the replacement craft's lineage.

## Moon walks

The origin Moon has its own seeded exposures on the existing lunar ground.
Choose **Enable local landmarks** in paused Controls while on that Moon;
historical journeys and New Game do not silently adopt this separate selection.
Land, leave the Wayfarer and use the same nearby card, eight-metre inspection
action and paused reader. A reachable hatch keeps priority over an exposure.
The personal marker remains an independent navigation aid.

Lunar sites use genuine Moon identity and the selected lunar contact top. Dry
basins below the reference radius remain eligible ground. The tint adds no rock,
collision, composition measurement or terrain change. Nearby positions come
from C++; the lunar material only applies the same bounded bands and fade as
planetary exposures.

Optional version1 `moon_surface_landmarks` metadata retains typed body targets,
cells, site IDs, terrain recipe and first-inspection ticks. The combined Moon
and planetary inspection history is bounded to64 records; historical planetary
JSON and seed domains remain unchanged. Enabling and inspecting are zero-time,
transactional metadata actions; Continue and recovery retain both histories.


## Hand specimens

After inspecting an exposure, choose **Collect hand specimen** in paused
Controls while still within eight metres on supported ground. The same action
works at planetary and Moon exposures. Collection adds one specimen to a
four-slot pouch and records its source in your field notes. It leaves the clock,
resources and personal marker unchanged. An already-carried specimen or a full
pouch refuses collection; no specimen is silently replaced.

**Sample pouch** opens a read-only list of carried specimens and first collection
notes. Back returns to paused Controls. The **Discard** actions free a pouch
slot and retain its first collection note. You can collect again from that site;
the new specimen records the current collection, while its first note stays
unchanged. The journal holds up to64 distinct sites. If full, it still permits
recollection from a previously recorded site.

The same retained notes are also pages in **Recorded observations**, available
at the station and in planet/Moon/asteroid controls. Each page shows its source
and first-collection ship time, and separately states whether that specimen is
carried now. It does not infer whether an absent specimen was
discarded or lost. Opening the archive neither selects a pouch nor collects
or analyzes anything.

Save and Continue preserve the pouch and notes through walking, boarding and
travel. Standard craft replacement clears the carried specimens, binds the
empty pouch to the replacement craft and keeps the notes. The retired voyage
snapshot retains its old pouch; this does not imply a rendered wreck or a
retrieval mechanic. This increment has no specimen sale, assay, deposit,
extraction, flight-mass change or collection animation.

C++ owns collection, discard and validation. Optional version1 `field_samples`
metadata in voyage knowledge stores the current carrier, up to four carried
records and up to64 first notes. Each record names the typed body target, cell,
site, terrain recipe, appearance kind, collecting craft and authoritative tick.
The validator authenticates generated sites, prior inspections, collector
lineage, ordering, capacity and clocks. Commands validate the complete Save
before committing; refusal leaves the session unchanged. Historical absence
stays absent until a successful collection. The feature adds no seed domain or
navigation knowledge.

From a retained specimen page in **Recorded observations**, choose **Mark site**
to navigate back to that exposure on the currently loaded planet or Moon.
**Replace marker** explicitly replaces your existing personal point. Discarded
or lost specimens keep this option through their first-collection note. Notes
from other bodies remain readable; return to that body before marking. Enable
**Show personal marker** in flight or surface controls to display its bearing.

C++ resolves only the recorded cell under its original terrain recipe and
checks the generated identity before using the existing marker format and
bearing. It does not scan nearby cells, discover new sites or certify a route
or landing. Reading pages is unchanged; only the explicit action changes the
personal marker, without advancing time or spending resources.
