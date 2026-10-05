# Original cockpit source partition

[Issue #409](https://github.com/gobha-me/apsis-drift/issues/409) supplies the
original-face selection needed by the parked restraint replacement in
[#372](https://github.com/gobha-me/apsis-drift/issues/372). The C++
`OriginStowedContactPartition` owns a sharing handle to the independently
admitted [boarding catalog](ORIGIN_BOARDING_SUPPORT.md).

`validate_stowed_contact_partition` accepts exactly eight complete source
objects. Every supplied object index, group, name, start and count must match
that catalog. The selected group is `craft_seat_lift` (catalog group 11), owned
by the craft and driven by `seat_lift` (motion index 10).

| Original object | Source name | Group-local half-open range |
| ---: | --- | --- |
| 846 | Anti-submarining strap | [108, 128) |
| 853 | Buckle release | [2720, 2828) |
| 886 | Five-point buckle | [13940, 14048) |
| 891 | Lap restraint | [14480, 14500) |
| 892 | Lap restraint.001 | [14500, 14520) |
| 904 | Seat service manifold | [20228, 20336) |
| 905 | Shoulder restraint | [20336, 20372) |
| 906 | Shoulder restraint.001 | [20372, 20408) |

The selected ranges contain 456 original triangles. The retained inventory
contains the other 56 seat-lift objects and their 22,148 triangles, together
with every object in other catalog groups. Both inventories follow catalog
order regardless of submission order. Names refer to catalog-owned strings;
copying a partition keeps its catalog and views alive. Moved-from handles
return empty inventories and refuse membership queries.

`stowed_contact_partition_contains_removed` accepts bounded original
`BoardingTriangleKey` values. These keys remain distinct from evaluated source
face indices, halo keys and future replacement keys. Missing, duplicate,
partial, changed-source, overlapping and out-of-range selections refuse before
constructing the partition.

The partition records selection only. Existing collision queries still retain
all original obstacles and the additive lower-cockpit halo. Applying this
selection requires the matched replacement package, render/contact attribution
and frame binding in #372. The current candidate has 14 flat render groups at
direct REST; older prospective eleven-group and posed diagnostic plans do not
select its runtime frame. Boarding and seating remain separate work.

The `stowed-contact-partition-contract` test checks malformed selections,
every original catalog triangle, retained ownership, caller lifetime and
shared/moved handles. Existing contact, walking, save and flight tests continue
to exercise the unchanged runtime behavior.
