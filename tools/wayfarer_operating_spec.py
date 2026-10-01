"""Source-bound operating Wayfarer identities; metres, BSD-3-Clause.

A source object's ancestry is ordered self, parent, grandparent, ... .
Both contact extraction and the render producer use the closest listed rig.
"""
from collections.abc import Sequence

SOURCE_SHA256 = "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677"
BASE_EXPORTER_SHA256 = "762499e37fe13a33bf297fc58801e711590cd9d56058a6462b3467ff6b7102fe"
MOTION_RIGS = {
    "AFT01 | dock hatch hinge -1": "roof_port",
    "AFT01 | dock hatch hinge 1": "roof_starboard",
    "AFT01 | inner sliding leaf rig -1 0": "inner_port_outer",
    "AFT01 | inner sliding leaf rig -1 1": "inner_port_inner",
    "AFT01 | inner sliding leaf rig 1 0": "inner_starboard_outer",
    "AFT01 | inner sliding leaf rig 1 1": "inner_starboard_inner",
    "AFT01 | swing-away dock ladder": "ladder_base",
    "AFT01 | telescoping ladder upper": "ladder_upper",
    "RIG | fore-aft carriage": "seat_carriage",
    "RIG | 90 degree boarding swivel": "seat_swivel",
    "RIG | seat height adjustment": "seat_lift",
    "RIG | left entry arm": "seat_entry_arm",
    "RIG | positive swivel lock": "seat_lock",
}
RUNTIME_NODES = {
    "roof_port": "WFOpRoofPort",
    "roof_starboard": "WFOpRoofStarboard",
    "inner_port_outer": "WFOpInnerPortOuter",
    "inner_port_inner": "WFOpInnerPortInner",
    "inner_starboard_outer": "WFOpInnerStarboardOuter",
    "inner_starboard_inner": "WFOpInnerStarboardInner",
    "ladder_base": "WFOpLadderBase",
    "ladder_upper": "WFOpLadderUpper",
    "seat_carriage": "WFOpSeatCarriage",
    "seat_swivel": "WFOpSeatSwivel",
    "seat_lift": "WFOpSeatLift",
    "seat_entry_arm": "WFOpSeatEntryArm",
    "seat_lock": "WFOpSeatLock",
}
CONTACT_ROLES = {
    group: "roof_assembly" if group.startswith("roof_") else
    "inner_pressure_leaf" if group.startswith("inner_") else
    "dock_ladder" if group.startswith("ladder_") else group
    for group in RUNTIME_NODES
}
POSE_IDS = ("rest", "roof_open", "transfer_deployed", "inner_open",
            "seat_boarding", "transfer_cabin")
CHANNEL_IDS = ("roof_transfer", "inner_door", "seat_boarding")


def classify_motion_group(ancestry: Sequence[str]) -> str | None:
    """Return one closest source-rig identity, never a name substring guess."""
    for name in ancestry:
        if name in MOTION_RIGS:
            return MOTION_RIGS[name]
    return None
