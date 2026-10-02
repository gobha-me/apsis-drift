# Complete upper cabin corridor

Issue [#359](https://github.com/gobha-me/apsis-drift/issues/359) extends the
[first seam](ORIGIN_CABIN_SEAM.md) to the complete upper cabin centerline.
It consumes the immutable [lower cockpit contact provider](LOWER_COCKPIT_CONTACT.md),
including the original craft geometry and all added obstacles. It creates no
new world, art derivative, actor action or saved phase.

## Declared route

The craft-local foot center stays at X=0, Y=-100000 micrometres, with Z from
3.84 to -0.35 metres. Reverse translations, equal endpoints and subintervals
use the same geometric policy. Hardware stays at `OperatingProgress{1,1,1,0}`:
roof transfer open, inner door open, seat in its boarding pose, station closure
zero. The route is 4.19 metres long.

The technical standing reservation retains the first seam's 0.64-metre body
width and depth, 1.93-metre height and body bottom 45mm above the foot datum.
Both boots remain 120mm wide, 280mm long and 100mm high, centered at X=±140mm.
Each sole requires 75% finite source contact area; the projected load needs
10mm of support-hull margin. These flat-route limits do not define stair gait.

## Actual support

All selected top faces belong to the original `craft_fixed` buffer. Triangle
indices are group-local contact indices, not Blender evaluated face indices.

| Walking tile | Source object index | Top triangles | Z extent, metres |
| --- | ---: | --- | --- |
| 00-1 | 178 | 85129, 85130 | 3.6645 to 4.2255 |
| 01-1 | 181 | 85537, 85538 | 3.0695 to 3.6305 |
| 02-1 | 184 | 85945, 85946 | 2.4745 to 3.0355 |
| 03-1 | 187 | 86353, 86354 | 1.8795 to 2.4405 |
| 04-1 | 190 | 86761, 86762 | 1.2845 to 1.8455 |
| 05-1 | 193 | 87211, 87212, 87219, 87220 | 0.6895 to 1.2505 |
| 06-1 | 196 | 87645, 87646 | 0.0945 to 0.6555 |
| 07-1 | 199 | 88053, 88054 | -0.5005 to 0.0605 |

Tile 05 has a real 10.5mm coplanar sliver. Its two extra triangles close the
source partition down to Z=0.6895. The seven gaps between tiles are 34mm.
The tapered outer edges of tiles 05–07 remain part of their actual geometry;
rectangular object bounds cannot supply missing support. Within the fixed
sole bands X=[-0.20,-0.08] and [0.08,0.20], the actual top partitions cover
the complete tile intervals. Shared triangle edges carry zero area.

## Continuous qualification

Support is computed from clipped source polygons and their projected hull.
A complete partition at sole-edge/top-edge events bounds each translation;
checking a regular time grid cannot establish this result. Independent exact
rational clipping predicts minimum sole area fraction 123/140 and load margin
106mm. Binary64 source coordinates differ slightly from their ideal decimal
values. Production bounds account for that representation and arithmetic
rounding conservatively; the rational values provide independent comparisons.
This reporting bound never permits collision penetration or changes the exact
floor-contact requirement.

The bound is `64 × binary64 epsilon × 8 metres` (about 1.14e-13 metres),
covering the bounded interval arithmetic; dividing it by the 0.28-metre sole
length bounds the area fraction. Certificate limits deduct these amounts and
round downward. Production geometry uses binary64 arithmetic; independent test
clipping uses a separate extended-precision oracle.

The conservative body and boot endpoint-union boxes cover the complete
straight translation. Every original and halo obstacle remains in the
collision query, including tile bevels and posed mechanisms. Any allowed
boundary contact must be on the sole underside and belong to the actual
source support partition after clipping to the swept boot band. An object
name or bounding box does not waive a contact. Interior penetration has no
tolerance allowance.

## Integration boundary

`make_origin_cabin_corridor_geometry` retains the immutable lower/original
geometry handles and verifies the selected source partition. Its read-only
top-face view preserves actual coordinates and object attribution.
`assess_origin_cabin_corridor_proxy_support` reports source-clipped support
for diagnostic poses; `assess_origin_cabin_corridor_support` restricts that
query to the declared centerline and range. Only
`certify_origin_cabin_corridor_translation` supplies the bounded continuous
translation certificate. Moved-from handles refuse queries.

The certificate retains mathematical source events in micrometres and every
distinct floating arithmetic event. Adjacent representable events are not
merged by an epsilon. Explicit below/at/above support limits account for a
tile entering or disappearing from the support hull; a zero-area touch cannot
create an extremal load-bearing point.

This craft-local provider is a prerequisite for the boarding route in
[#352](https://github.com/gobha-me/apsis-drift/issues/352). The lower steps,
pod seal clearance, articulated transfer and occupied seat still require
qualification. Station mount, ladder reach, actor actions, mechanism motion
and the composed First Flight handoff remain separate acceptance work.
Existing support, motion, asset identities and save formats 16–20 are retained.
