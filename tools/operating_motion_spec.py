"""Bounded, source-specific operating-motion JSON; no scene or simulation clock."""
import json
import math

import prepare_native_assets as base
from operating_motion_identity import IDENTITIES
from wayfarer_operating_spec import MOTION_RIGS

SCHEMA = "apsis.operating-motion/1"
LAYOUT = "columns basis_x/basis_y/basis_z/origin; metres; XYZ radians"
MAX_RECIPE = 128 * 1024
MAX_SOURCE = 3 * 1024 * 1024
MAX_TOTAL = 6 * 1024 * 1024
MAX_DEPTH = 16
CHANNELS = ("roof_transfer", "inner_door", "seat_boarding", "station_closure")


def decode(raw, maximum):
    base.require(type(raw) is bytes and 0 < len(raw) <= maximum, "document bytes exceed bound")
    # Bound structural depth before the standard decoder creates nested values.
    depth, quoted, escaped = 0, False, False
    for byte in raw:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
        elif byte in (91, 123):
            depth += 1
            base.require(depth <= MAX_DEPTH, "JSON nesting exceeds bound")
        elif byte in (93, 125):
            depth -= 1
            base.require(depth >= 0, "unbalanced JSON structure")

    def number(text):
        value = float(text)
        base.require(math.isfinite(value), "nonfinite JSON number")
        return value

    def invalid(text):
        raise ValueError("nonfinite JSON constant: " + text)

    return json.loads(raw.decode("utf-8"), object_pairs_hook=base.pairs_unique,
                      parse_float=number, parse_constant=invalid)


def finite(value, low=-100.0, high=100.0):
    base.require(type(value) in (int, float) and low <= value <= high and
                 math.isfinite(value), "nonfinite/out-of-range scalar")


def vector(value):
    base.require(type(value) is list and len(value) == 3, "invalid vector dimension")
    for component in value:
        finite(component)


def transform(value):
    base.require(type(value) is list and len(value) == 4, "invalid matrix dimensions")
    for column in value:
        vector(column)
    for first in range(3):
        for second in range(3):
            dot = sum(a * b for a, b in zip(value[first], value[second]))
            base.require(abs(dot - (first == second)) <= 5e-6, "nonrigid basis")
    a, b, c = value[:3]
    det = (a[0] * (b[1] * c[2] - b[2] * c[1]) -
           b[0] * (a[1] * c[2] - a[2] * c[1]) +
           c[0] * (a[1] * b[2] - a[2] * b[1]))
    base.require(abs(det - 1.0) <= 5e-6, "reflected/singular basis")


def validate_recipe(value):
    base.keys(value, ("schema", "version", "matrix_layout", "identities", "craft", "station"))
    base.require(value["schema"] == SCHEMA and type(value["version"]) is int and
                 value["version"] == 1 and value["matrix_layout"] == LAYOUT,
                 "unsupported motion convention")
    base.keys(value["identities"], IDENTITIES)
    base.require(value["identities"] == IDENTITIES, "motion source/model identities changed")
    for owner, count, track_count, groups in (("craft", 15, 12, set(MOTION_RIGS.values())),
                                             ("station", 19, 51, {f"station_d1_{i:02d}" for i in range(17)})):
        section = value[owner]
        base.keys(section, ("nodes", "tracks"))
        nodes, tracks = section["nodes"], section["tracks"]
        base.require(type(nodes) is list and len(nodes) == count, "motion node roster changed")
        names, actual_groups = set(), set()
        for index, node in enumerate(nodes):
            base.keys(node, ("source_object", "parent", "group", "location_metres", "euler_xyz_radians", "rest_world"))
            name = node["source_object"]
            base.require(type(name) is str and 0 < len(name) <= 128 and name not in names,
                         "duplicate/invalid source node")
            names.add(name)
            parent = node["parent"]
            base.require(parent is None or type(parent) is int and 0 <= parent < index,
                         "non-parent-first/cyclic ancestry")
            group = node["group"]
            base.require(group is None or type(group) is str and group in groups and group not in actual_groups,
                         "duplicate/unknown motion group")
            if group is not None:
                actual_groups.add(group)
            vector(node["location_metres"])
            vector(node["euler_xyz_radians"])
            transform(node["rest_world"])
        base.require(actual_groups == groups, "missing motion group")
        base.require(type(tracks) is list and len(tracks) == track_count, "scalar-track roster changed")
        seen = set()
        for track in tracks:
            base.keys(track, ("channel", "node", "property", "axis", "knots"))
            base.require(track["channel"] in (CHANNELS[:3] if owner == "craft" else CHANNELS[3:]),
                         "unknown/misowned channel")
            node, axis, prop = track["node"], track["axis"], track["property"]
            base.require(type(node) is int and 0 <= node < count and type(axis) is int and
                         0 <= axis < 3 and prop in ("location", "rotation_euler"), "invalid scalar target")
            key = (node, prop, axis)
            base.require(key not in seen, "duplicate property assignment")
            seen.add(key)
            knots = track["knots"]
            base.require(type(knots) is list and 2 <= len(knots) <= 16, "invalid knot count")
            previous = -1.0
            for pair in knots:
                base.require(type(pair) is list and len(pair) == 2, "invalid scalar knot")
                finite(pair[0], 0.0, 1.0)
                finite(pair[1])
                base.require(pair[0] > previous, "duplicate/nonordered knot")
                previous = pair[0]
            base.require(knots[0][0] == 0 and knots[-1][0] == 1, "missing progress endpoints")
    return value


def construct_recipe(craft, station):
    """Lossless selected scalar extraction; no matrix evaluation or oracle synthesis."""
    controls = {item["source_object"]: item["id"] for item in station["controls"]}
    identity = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0],
                [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]]

    def section(source_nodes, is_station):
        name_key = "source_object" if is_station else "id"
        names = [node[name_key] for node in source_nodes]
        nodes = []
        for node in source_nodes:
            base.require(node["scale" if is_station else "rest_scale"] == [1, 1, 1] and
                         node["parent_inverse_rows"] == identity and
                         (not is_station or node["rotation_mode"] == "XYZ"),
                         "unsupported source scale/parent inverse/rotation mode")
            rows = node["world_rows" if is_station else "rest_world_rows"]
            nodes.append({"source_object": node[name_key],
                          "parent": names.index(node["parent"]) if node["parent"] is not None else None,
                          "group": controls.get(node[name_key]) if is_station else node["motion_group"],
                          "location_metres": node["location_m" if is_station else "rest_location_m"],
                          "euler_xyz_radians": node["euler_xyz_radians" if is_station else "rest_euler_xyz_radians"],
                          "rest_world": [[rows[r][c] for r in range(3)] for c in range(4)]})
        source_tracks = ([("station_closure", track) for track in station["scalar_tracks"]] if is_station
                         else [(channel, track) for channel, tracks in craft["channels"].items() for track in tracks])
        tracks = [{"channel": channel, "node": names.index(track["node"]), "property": track["property"],
                   "axis": track["axis" if is_station else "axis_index"], "knots": track["linear_knots"]}
                  for channel, track in source_tracks]
        return {"nodes": nodes, "tracks": tracks}

    return validate_recipe({"schema": SCHEMA, "version": 1, "matrix_layout": LAYOUT,
                            "identities": IDENTITIES, "craft": section(craft["nodes_parent_first"], False),
                            "station": section(station["nodes_parent_first"], True)})


def encode(value):
    return (json.dumps(value, indent=2, allow_nan=False) + "\n").encode("utf-8")
