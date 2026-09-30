# Origin station exterior and ports

2026-09-30, #218, geometry version **1**. This C++ contract binds the existing
generated Origin Station to one physical exterior frame and two measured
Wayfarer interfaces. D1 supplies the initial boarding/release path; D2 retains
the authored second datum. It does not change station generation, the existing
90–120 minute orbit, save meanings or spacecraft mass/thrust ratings.

Station walking and boarding are active in the owner's 2026-09-30 #245 minimum.
This geometry provider supplies their anchors; walking, capture/release, doors,
pressure/interlocks and actual asset admission remain consumer work.

## Frame and authored lineage

All distances are metres. Local +X is right, +Y up and +Z back. The origin is
the authored central hub floor at Blender `(0.97,0.978,0)`. Subtract that datum,
then convert Blender `(x,y,z)` to `(x,z,-y)`. An admitted asset must use that
translation/conversion once, without scaling the station to a planet silhouette.

Version1 deliberately uses **nonrotating system-aligned station axes**, matching
the existing `station_relative_inertial` frame. The C++ physical station ephemeris
at the authoritative tick supplies system position/velocity. Attitude is fixed;
no orbital-frame spin or extra `Omega × offset` is invented. A future rotating
station frame requires an explicit version and velocity/attitude contract.
The orientation is not a claim about the station's artificial gravity system.

The closed authoring handoff supplied these measurements:

| Reference | SHA-256 |
| --- | --- |
| `station-reference.blend` | `6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6` |
| `station-docking-lock-09.blend` | `f07ecfaa0c6607f7ebbd81312c0645028c60714629f44b4eb125363b95af4820` |
| Compatible `hopper-craft-09.blend` | `87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677` |

The docking-lock interface and station18 roof check identified Blender collar
positions D1 `(-22.15,0.978,-1.35)` and D2 `(22.15,0.978,-1.35)`; measured mesh
fit error was about 1.91 micrometres. Runtime geometry uses the intended decimal
design datums, not incidental binary32 mesh noise. Source yaw is +90° for D1,
-90° for D2. The Wayfarer collar is `(0,-5.2,2.907)` with source normal +Z.
These are measured interface dimensions, not a new physics frame recipe.

This record borrows numerical evidence only. The authoring files may remain
outside the published engine checkout until admitted with provenance/license
metadata. In particular, the whole reference includes visiting spacecraft and
must not be imported as a station plus duplicated live ships.

## Port, approach and clearance contract

Stable port identity is `(OriginStationId, ordinal)`, with ordinals 1/D1 and
2/D2. It is not a globally unqualified string or a freshly randomized ID.

| Quantity | D1 | D2 |
| --- | --- | --- |
| Local station collar | `(-23.12,-1.35,0)` | `(21.18,-1.35,0)` |
| Station outward normal | `(0,-1,0)` | `(0,-1,0)` |
| Aligned craft yaw about +Y | +90° | -90° |
| Compatible craft collar in body axes | `(0,2.907,5.2)` | Same |
| Compatible craft collar normal | `(0,1,0)` | Same |
| Clear aperture | 0.9 m circular bore | Same |
| Qualified straight withdrawal | 12 m outward | Same |

At zero separation the aligned craft origin is the station collar minus the
rotated craft collar. Increasing separation moves its collar along station -Y.
`resolve_origin_port_pose` accepts the closed interval **[0,12] m** and returns
the reference collar plus craft state in station-relative, system and named
planet-relative frames. It uses existing same-tick handoffs through the system
frame; it does not invent a direct station/planet shortcut. It retains the exact
station co-motion, craft attitude and zero relative spin. Reference separation
does not command velocity or integrate a departure.

Each port reserves local X from collar X ±12 m, Y **[-18,-1.30] m**, and Z
**[-5,5] m** for the aligned measured craft and withdrawal column. The two
reservations are disjoint. This is an interface-specific design reservation,
not compatibility for arbitrary ships or a swept-collision proof for rotated
arrivals. The runtime collider owner must qualify actual geometry there.

The occupied exterior bound is X **[-43,34]**, Y **[-7,15]**, Z **[-30,60] m**.
It conservatively includes the authored occupied configuration. It is a
broad-phase/navigation bound: treating it as a solid collision box would fill
the boarding route and obstruct the fitted ports. Detailed structural colliders
and aperture/door state must remain separate. The reservations legitimately
intersect the broad-phase box; that is not a structural collision exemption.

New gameplay capture tolerances are explicit tuning choices, not measurements
or proof of magnetic retention strength:

- Collar separation at most **0.15 m**, full craft alignment at most **3°**.
- Lateral relative speed at most **0.2 m/s**, inward closure in **[0,0.3] m/s**.
- Relative body angular speed at most **0.02 rad/s**.

Thresholds are inclusive. A matching collar normal alone does not establish
roll/yaw alignment or safe capture. #220 must assess all conditions against
actual state and collide/stop before treating a reference pose as captured.
HCD-90 remains a fictional compatible magnetic-face interface; no rated load,
power, pressure or mechanical under-flange latch is inferred from the mesh.

## Shared presentation and refusal cues

Rendering, navigation and docking consume the same station identity, geometry
version, port datums and tick. The visual floating origin may subtract a shared
camera/station reference for GPU precision, but never feed a scaled position
back into C++ state. D1 uses an amber numbered marker; D2 uses a blue numbered
marker. Labels and arrow direction remain readable without those colors.
They are presentation landmarks, not simulated illumination or pressure status.

Near fixtures are contact, 0.15, 1, 6 and 12 m collar separation; inspect both
authored headings and a deliberately misaligned arrival. Exterior views at 30,
100 and 500 m use the same physical size. At 640×360 and 1920×1080, labels must
retain station/port identity, distance, approach side and refusal reason without
covering the craft. If off-screen or unavailable, show a bearing/identity cue
and text such as wrong side, excessive closure, incompatible interface or
geometry unavailable. No raster fallback may invent a capture or new station.

## Validation

The geometry is immutable for version1. A caller-modified datum, quaternion,
bound, tolerance or interface dimension refuses rather than letting different
consumers choose different transforms. Unknown versions/owners, duplicate
ports, nonfinite/invalid dimensions, inside-out/intersecting approach volumes,
invalid port indices/distances and invalid ephemeris clocks refuse before any
pose is returned. No live state is mutated by these queries.

`station-geometry-contract` tests those boundaries first, then seeds 0, 42 and
maximum u64 at zero, progressed, wrap and largest valid clocks. Independent
quaternion-matrix and authored-datum oracles check collar coincidence, opposing
normals/headings, exact physical ephemeris translation/velocity and existing
planet-origin subtraction. Physical v3 projection and repeated queries retain
state/clock/identity. Earlier orbit, frame, save and dynamics goldens remain
separate regression gates. This evidence is geometry qualification, not a
visual walkthrough or a completed station-to-flight trip.
