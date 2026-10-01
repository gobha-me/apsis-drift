"""Closed, bounded source-specific boarding candidate metadata; no actor authority."""
import json
import math

import prepare_native_assets as base
from boarding_support_identity import COUNTS, GROUPS, IDENTITIES

SCHEMA = "apsis.boarding-support/1"
MAX_RUNTIME = 2 * 1024 * 1024
MAX_SOURCE = 2 * 1024 * 1024
MAX_CONTACT = 32 * 1024 * 1024
MAX_TOTAL = 6 * 1024 * 1024
MAX_DEPTH = 16
ROLES = {"hand_grasp": "hand/fingers", "boot_support": "boot sole",
         "elbow_rest": "elbow/forearm", "head_rest": "helmet/head",
         "back_rest": "back", "seat_pan": "pelvis/thigh"}
LABELS = ("unclassified_obstacle", "forbidden_structure_or_mechanism", "rung",
          "handhold", "control_grip", "cushion")
PATCH_KEYS = ("id", "object", "role", "body_contact", "triangle_ranges", "triangle_count",
              "selected_faces", "surface_area_m2", "representative_triangle",
              "representative_barycentric", "representative_contact_point_rest_m",
              "bounds_rest_m", "contact_side_direction_rest", "opposite_winding_triangles",
              "required_point_predicate")
SOURCE_PATCH_KEYS = ("id", "role", "body_contact", "triangle_ranges", "triangle_count",
                     "surface_area_m2", "representative_triangle", "representative_barycentric",
                     "representative_contact_point_rest_m", "bounds_rest_m", "status")
CROP = {"station": [[-24.8, -1.8, -1.65], [-18.4, 3.5, 1.65]],
        "craft": [[-1.05, -0.2, -3.5], [1.05, 2.97, 6.35]]}
COORDINATES = {"station": "Blender(x,y,z)->(x-.97,z,-y+.978)",
               "craft": "Blender(x,y,z)->(x,z,-y); source-rest world baked"}


def decode(raw, maximum):
    base.require(type(raw) is bytes and 0 < len(raw) <= maximum, "document byte bound")
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
            base.require(depth <= MAX_DEPTH, "JSON depth bound")
        elif byte in (93, 125):
            depth -= 1
            base.require(depth >= 0, "unbalanced JSON")
    def number(text):
        value = float(text)
        base.require(math.isfinite(value), "nonfinite JSON number")
        return value
    def invalid(text):
        raise ValueError("nonfinite JSON constant: " + text)
    return json.loads(raw.decode("utf-8"), object_pairs_hook=base.pairs_unique,
                      parse_float=number, parse_constant=invalid)


def encode(value):
    return (json.dumps(value, indent=2, allow_nan=False) + "\n").encode("utf-8")


def integer(value, low, high):
    base.require(type(value) is int and low <= value <= high, "invalid integer/index")


def scalar(value, low=-100.0, high=100.0):
    base.require(type(value) in (int, float) and math.isfinite(value) and low <= value <= high,
                 "nonfinite/out-of-range scalar")


def vector(value):
    base.require(type(value) is list and len(value) == 3, "invalid vector dimension")
    for component in value:
        scalar(component)


def text(value, maximum=256):
    base.require(type(value) is str and 0 < len(value) <= maximum, "invalid string")


def array(value, maximum, count=None):
    base.require(type(value) is list and len(value) <= maximum and
                 (count is None or len(value) == count), "invalid array count")


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]]


def unit(value):
    vector(value)
    base.require(abs(dot(value, value) - 1.0) <= 2e-9, "nonunit direction")


def bounds(value):
    array(value, 2, 2)
    for row in value:
        vector(row)
    base.require(all(a <= b for a, b in zip(*value)), "reversed bounds")


def close(a, b, tolerance=1e-10):
    base.require(abs(a - b) <= tolerance, "derived geometry differs")


def validate_runtime(value):
    base.keys(value, ("schema", "version", "identities", "quantization_metres", "crop_bounds_metres",
                      "counts", "groups", "objects", "vertices", "faces", "patches"))
    integer(value["version"], 1, 1)
    base.require(value["schema"] == SCHEMA, "unsupported schema")
    base.keys(value["identities"], IDENTITIES)
    base.require(value["identities"] == IDENTITIES, "source/model/motion identity changed")
    scalar(value["quantization_metres"], 1e-6, 1e-6)
    base.keys(value["crop_bounds_metres"], CROP)
    for owner in CROP:
        bounds(value["crop_bounds_metres"][owner])
    base.require(value["crop_bounds_metres"] == CROP, "contact crop changed")
    base.keys(value["counts"], COUNTS)
    for key, count in COUNTS.items():
        integer(value["counts"][key], count, count)
    groups, objects = value["groups"], value["objects"]
    vertices, faces, patches = value["vertices"], value["faces"], value["patches"]
    for table, count in ((groups, 32), (objects, 1087), (vertices, 1217), (faces, 1322), (patches, 42)):
        array(table, count, count)
    object_cursor = 0
    for gi, (group, selected) in enumerate(zip(groups, GROUPS)):
        base.keys(group, selected)
        for key in ("vertex_count", "triangle_count", "object_start", "object_count"):
            integer(group[key], 0, 475212)
        base.require(group == selected and group["object_start"] == object_cursor, "group roster changed")
        triangle_cursor, names = 0, set()
        for obj in objects[object_cursor:object_cursor + group["object_count"]]:
            base.keys(obj, ("group", "source_object", "triangle_start", "triangle_count",
                            "semantic_label", "default_collision"))
            integer(obj["group"], gi, gi)
            integer(obj["triangle_start"], 0, group["triangle_count"])
            integer(obj["triangle_count"], 1, group["triangle_count"])
            text(obj["source_object"])
            base.require(obj["source_object"] not in names, "duplicate object name")
            names.add(obj["source_object"])
            base.require(obj["triangle_start"] == triangle_cursor and
                         obj["semantic_label"] in LABELS and obj["default_collision"] == "obstacle",
                         "object partition/policy changed")
            triangle_cursor += obj["triangle_count"]
            base.require(triangle_cursor <= group["triangle_count"], "object range overflow")
        base.require(triangle_cursor == group["triangle_count"], "object partition gap")
        object_cursor += group["object_count"]
    base.require(object_cursor == len(objects), "object roster mismatch")
    previous = (-1, -1)
    for vertex in vertices:
        base.keys(vertex, ("group", "source_vertex", "position_micrometres"))
        integer(vertex["group"], 0, 31)
        integer(vertex["source_vertex"], 0, groups[vertex["group"]]["vertex_count"] - 1)
        key = (vertex["group"], vertex["source_vertex"])
        base.require(key > previous, "duplicate/nonordered vertex identity")
        previous = key
        array(vertex["position_micrometres"], 3, 3)
        for component in vertex["position_micrometres"]:
            integer(component, -100000000, 100000000)
    previous, face_points, normals = (-1, -1), [], []
    used_vertices = set()
    for face in faces:
        base.keys(face, ("group", "source_triangle", "vertices"))
        integer(face["group"], 0, 31)
        integer(face["source_triangle"], 0, groups[face["group"]]["triangle_count"] - 1)
        key = (face["group"], face["source_triangle"])
        base.require(key > previous, "duplicate/nonordered face identity")
        previous = key
        array(face["vertices"], 3, 3)
        points = []
        for vi in face["vertices"]:
            integer(vi, 0, len(vertices) - 1)
            base.require(vertices[vi]["group"] == face["group"], "cross-group vertex")
            used_vertices.add(vi)
            points.append([v * 1e-6 for v in vertices[vi]["position_micrometres"]])
        normal = cross(sub(points[1], points[0]), sub(points[2], points[0]))
        base.require(dot(normal, normal) > 1e-24, "degenerate candidate face")
        face_points.append(points)
        normals.append(normal)
    base.require(len(used_vertices) == len(vertices), "unreferenced selected vertex")
    seen_ids, used_faces, references, winding = set(), set(), 0, 0
    previous_object = -1
    for patch in patches:
        base.keys(patch, PATCH_KEYS)
        text(patch["id"], 512)
        base.require(patch["id"] not in seen_ids, "duplicate patch ID")
        seen_ids.add(patch["id"])
        integer(patch["object"], 0, len(objects) - 1)
        base.require(patch["object"] >= previous_object, "nonordered patch owner")
        previous_object = patch["object"]
        obj = objects[patch["object"]]
        role = patch["role"]
        base.require(type(role) is str and role in ROLES and patch["body_contact"] == ROLES[role],
                     "unknown/incompatible body role")
        expected_label = "rung" if role == "boot_support" else (
            ("rung", "handhold") if role == "hand_grasp" else "cushion")
        base.require(obj["semantic_label"] in expected_label if isinstance(expected_label, tuple)
                     else obj["semantic_label"] == expected_label, "incompatible object role")
        base.require(patch["id"] == groups[obj["group"]]["id"] + "/" + obj["source_object"] + "/" + role,
                     "patch identity differs from owner/role")
        array(patch["triangle_ranges"], 76)
        base.require(patch["triangle_ranges"], "empty patch ranges")
        selected = []
        last = obj["triangle_start"]
        end = last + obj["triangle_count"]
        for pair in patch["triangle_ranges"]:
            array(pair, 2, 2)
            first, count = pair
            integer(first, obj["triangle_start"], end - 1)
            integer(count, 1, obj["triangle_count"])
            base.require(first >= last and count <= end - first, "patch overlap/range overflow")
            selected.extend(range(first, first + count))
            last = first + count
        integer(patch["triangle_count"], 1, 1474)
        base.require(len(selected) == patch["triangle_count"], "patch count mismatch")
        array(patch["selected_faces"], 1474, len(selected))
        points, area, reversed_count = [], 0.0, 0
        side = patch["contact_side_direction_rest"]
        if role == "hand_grasp":
            base.require(side is None and patch["opposite_winding_triangles"] is None,
                         "invented grasp side")
        else:
            unit(side)
            integer(patch["opposite_winding_triangles"], 0, len(selected))
        for ti, fi in zip(selected, patch["selected_faces"]):
            integer(fi, 0, len(faces) - 1)
            base.require(faces[fi]["group"] == obj["group"] and faces[fi]["source_triangle"] == ti,
                         "selected face does not match source triangle")
            used_faces.add(fi)
            points.extend(face_points[fi])
            area += math.sqrt(dot(normals[fi], normals[fi])) / 2
            if side is not None and dot(normals[fi], side) < 0:
                reversed_count += 1
        scalar(patch["surface_area_m2"], 1e-15, 100)
        close(area, patch["surface_area_m2"])
        bounds(patch["bounds_rest_m"])
        for axis in range(3):
            close(min(p[axis] for p in points), patch["bounds_rest_m"][0][axis])
            close(max(p[axis] for p in points), patch["bounds_rest_m"][1][axis])
        integer(patch["representative_triangle"], 0, groups[obj["group"]]["triangle_count"] - 1)
        base.require(patch["representative_triangle"] in selected, "unselected representative")
        bary = patch["representative_barycentric"]
        vector(bary)
        base.require(all(0 <= b <= 1 for b in bary), "invalid barycentric coordinate")
        close(sum(bary), 1.0, 1e-12)
        representative = face_points[patch["selected_faces"][selected.index(patch["representative_triangle"])]]
        vector(patch["representative_contact_point_rest_m"])
        for axis in range(3):
            close(sum(bary[i] * representative[i][axis] for i in range(3)),
                  patch["representative_contact_point_rest_m"][axis])
        if side is not None:
            base.require(patch["opposite_winding_triangles"] == reversed_count, "winding count mismatch")
            winding += reversed_count
        predicate = patch["required_point_predicate"]
        if role in ("hand_grasp", "boot_support"):
            base.keys(predicate, ("type", "origin_rest_m", "unit_axis_rest", "inclusive_interval_m", "end_margin_m"))
            base.require(predicate["type"] == "rest_axis_interval", "unknown point predicate")
            vector(predicate["origin_rest_m"])
            unit(predicate["unit_axis_rest"])
            array(predicate["inclusive_interval_m"], 2, 2)
            for endpoint in predicate["inclusive_interval_m"]:
                scalar(endpoint)
            base.require(predicate["inclusive_interval_m"][0] < predicate["inclusive_interval_m"][1],
                         "reversed rod interval")
            scalar(predicate["end_margin_m"], .03, .03)
        else:
            base.require(predicate is None, "unexpected point predicate")
        references += len(selected)
    base.require(references == 1474 and len(used_faces) == len(faces) and winding == 233,
                 "selected geometry roster mismatch")
    return value


def construct_runtime(support, contact):
    """Validate raw object/patch variants, select unchanged integer source geometry."""
    base.keys(support, ("schema_version", "id", "status", "contact_sha256", "models", "sources",
                        "coordinate_contracts", "quantization_metres", "triangle_numbering", "source_rest",
                        "motion_contract", "contact_policy", "patch_probe", "checks", "missing", "objects"))
    base.keys(contact, ("schema_version", "sources", "coordinate_contracts", "quantization_metres",
                        "crop_bounds_metres", "groups"))
    integer(support["schema_version"], 1, 1)
    integer(contact["schema_version"], 1, 1)
    base.require(support["id"] == "wayfarer-boarding-support-01" and
                 support["contact_sha256"] == IDENTITIES["contact_sha256"], "source sidecar identity changed")
    for key in ("status", "triangle_numbering", "motion_contract", "contact_policy", "patch_probe"):
        text(support[key], 2048)
    base.keys(support["source_rest"], ("craft", "station"))
    for description in support["source_rest"].values():
        text(description, 2048)
    array(support["missing"], 7, 7)
    for description in support["missing"]:
        text(description, 2048)
    base.keys(support["checks"], ("groups", "objects", "triangles", "vertices", "exact_all_vertices_triangles_and_object_order", "candidate_patches"))
    for name, expected in (("groups", 32), ("objects", 1087), ("triangles", 475212), ("vertices", 253078), ("candidate_patches", 42)):
        integer(support["checks"][name], expected, expected)
    base.require(support["checks"]["exact_all_vertices_triangles_and_object_order"] is True,
                 "source replay evidence absent")
    scalar(support["quantization_metres"], 1e-6, 1e-6)
    scalar(contact["quantization_metres"], 1e-6, 1e-6)
    expected_sources = {"station_reference_sha256": IDENTITIES["station_source_sha256"],
                        "wayfarer_sha256": IDENTITIES["craft_source_sha256"],
                        "station_closure_sha256": IDENTITIES["closure_source_sha256"]}
    base.require(support["sources"] == contact["sources"] == expected_sources and
                 support["coordinate_contracts"] == contact["coordinate_contracts"] == COORDINATES, "source coordinate mismatch")
    base.keys(support["models"], ("model", "station_model"))
    for model in support["models"].values():
        base.keys(model, ("output", "bytes", "sha256", "source_sha256", "license", "chunks"))
        text(model["output"])
        integer(model["bytes"], 1, 64 * 1024 * 1024)
        base.digest(model["sha256"])
        base.digest(model["source_sha256"])
        text(model["license"])
        array(model["chunks"], 1, 1)
        base.relative(model["chunks"][0])
    base.require(support["models"]["model"]["sha256"] == IDENTITIES["craft_model_sha256"] and
                 support["models"]["station_model"]["sha256"] == IDENTITIES["station_clearance_model_sha256"],
                 "source model identity changed")
    array(contact["groups"], 32, 32)
    source_groups = {}
    for g, selected in zip(contact["groups"], GROUPS):
        base.keys(g, ("id", "owner", "motion_group", "source_objects", "vertices_micrometres", "triangles"))
        base.require(all(g[key] == selected[key] for key in ("id", "owner", "motion_group")), "contact group mismatch")
        array(g["source_objects"], selected["object_count"], selected["object_count"])
        for name in g["source_objects"]:
            text(name)
        base.require(len(set(g["source_objects"])) == len(g["source_objects"]), "duplicate contact object")
        array(g["vertices_micrometres"], selected["vertex_count"], selected["vertex_count"])
        array(g["triangles"], selected["triangle_count"], selected["triangle_count"])
        for v in g["vertices_micrometres"]:
            array(v, 3, 3)
            for component in v:
                integer(component, -100000000, 100000000)
        for tri in g["triangles"]:
            array(tri, 3, 3)
            for vi in tri:
                integer(vi, 0, selected["vertex_count"] - 1)
        source_groups[g["id"]] = g
    base.require(contact["crop_bounds_metres"] == CROP, "source crop changed")
    array(support["objects"], 1087, 1087)
    source_objects, face_keys = {}, set()
    source_rosters = {name: [] for name in source_groups}
    common = ("source_object", "group", "owner", "motion_group", "triangle_start", "triangle_count",
              "semantic_label", "default_collision", "source_evaluated_triangles", "crop_retained_triangles",
              "quantized_degenerate_triangles_removed", "bounds_rest_m", "source_corrected_world_rows", "candidate_patches")
    for obj in support["objects"]:
        base.keys(obj, common + (("candidate_omission",) if obj.get("semantic_label") == "control_grip" else ()))
        text(obj["source_object"])
        base.require(type(obj["group"]) is str and obj["group"] in source_groups, "unknown source group")
        g = source_groups[obj["group"]]
        base.require(obj["owner"] == g["owner"] and obj["motion_group"] == g["motion_group"], "source owner mismatch")
        key = (obj["group"], obj["source_object"])
        base.require(key not in source_objects, "duplicate source object")
        source_objects[key] = obj
        source_rosters[obj["group"]].append(obj["source_object"])
        for field in ("triangle_start", "triangle_count", "source_evaluated_triangles", "crop_retained_triangles", "quantized_degenerate_triangles_removed"):
            integer(obj[field], 0, 1000000)
        base.require(obj["triangle_count"] == obj["crop_retained_triangles"] - obj["quantized_degenerate_triangles_removed"],
                     "source crop/degenerate count mismatch")
        base.require(obj["crop_retained_triangles"] <= obj["source_evaluated_triangles"], "invalid source crop count")
        bounds(obj["bounds_rest_m"])
        matrix = obj["source_corrected_world_rows"]
        array(matrix, 4, 4)
        for row in matrix:
            array(row, 4, 4)
            for component in row:
                scalar(component)
        base.require(matrix[3] == [0, 0, 0, 1], "non-affine source provenance")
        base.require(abs(dot(matrix[0][:3], cross(matrix[1][:3], matrix[2][:3]))) > 1e-8,
                     "singular source provenance")
        # These are authored affine provenance, including two legitimate scales.
        # They never deform the already corrected integer contact again.
        array(obj["candidate_patches"], 2)
        for p in obj["candidate_patches"]:
            role = p.get("role")
            base.require(type(role) is str and role in ROLES, "unknown source patch role")
            extra = () if role == "hand_grasp" else ("contact_side_direction_rest", "opposite_winding_triangles")
            if role in ("hand_grasp", "boot_support"):
                extra += ("required_point_predicate",)
                base.keys(p["required_point_predicate"], ("type", "origin_rest_m", "unit_axis_rest", "inclusive_interval_m", "end_margin_m", "rule"))
                text(p["required_point_predicate"]["rule"], 1024)
            base.keys(p, SOURCE_PATCH_KEYS + extra)
            text(p["status"], 1024)
            array(p["triangle_ranges"], 76)
            for pair in p["triangle_ranges"]:
                array(pair, 2, 2)
                start, count = pair
                integer(start, 0, len(g["triangles"]) - 1)
                integer(count, 1, 1474)
                base.require(count <= len(g["triangles"]) - start, "source patch range overflow")
                face_keys.update((g["id"], ti) for ti in range(start, start + count))
        if obj["semantic_label"] == "control_grip":
            text(obj["candidate_omission"], 1024)
            base.require(not obj["candidate_patches"], "invented control grip patch")
    base.require(all(source_rosters[name] == group["source_objects"] for name, group in source_groups.items()),
                 "source object order mismatch")
    groups, objects, vertices, faces, patches = [], [], [], [], []
    vertex_map, face_map = {}, {}
    for gi, g in enumerate(contact["groups"]):
        triangles = sorted(ti for name, ti in face_keys if name == g["id"])
        for vi in sorted({vi for ti in triangles for vi in g["triangles"][ti]}):
            vertex_map[(g["id"], vi)] = len(vertices)
            vertices.append({"group": gi, "source_vertex": vi, "position_micrometres": g["vertices_micrometres"][vi]})
        for ti in triangles:
            face_map[(g["id"], ti)] = len(faces)
            faces.append({"group": gi, "source_triangle": ti,
                          "vertices": [vertex_map[(g["id"], vi)] for vi in g["triangles"][ti]]})
    for gi, g in enumerate(contact["groups"]):
        first = len(objects)
        for name in g["source_objects"]:
            base.require((g["id"], name) in source_objects, "missing source object")
            obj = source_objects[(g["id"], name)]
            oi = len(objects)
            objects.append({"group": gi, **{key: obj[key] for key in ("source_object", "triangle_start", "triangle_count", "semantic_label", "default_collision")}})
            for p in obj["candidate_patches"]:
                patch = {"id": p["id"], "object": oi, "role": p["role"], "body_contact": p["body_contact"],
                         "triangle_ranges": p["triangle_ranges"], "triangle_count": p["triangle_count"],
                         "selected_faces": [face_map[(g["id"], ti)] for start, count in p["triangle_ranges"] for ti in range(start, start + count)],
                         **{key: p[key] for key in ("surface_area_m2", "representative_triangle", "representative_barycentric", "representative_contact_point_rest_m", "bounds_rest_m")},
                         "contact_side_direction_rest": p.get("contact_side_direction_rest"),
                         "opposite_winding_triangles": p.get("opposite_winding_triangles"), "required_point_predicate": None}
                if "required_point_predicate" in p:
                    patch["required_point_predicate"] = {key: p["required_point_predicate"][key] for key in ("type", "origin_rest_m", "unit_axis_rest", "inclusive_interval_m", "end_margin_m")}
                patches.append(patch)
        groups.append({"id": g["id"], "owner": g["owner"], "motion_group": g["motion_group"],
                       "vertex_count": len(g["vertices_micrometres"]), "triangle_count": len(g["triangles"]),
                       "object_start": first, "object_count": len(objects) - first})
    base.require(len(source_objects) == len(objects), "extra source object")
    return validate_runtime({"schema": SCHEMA, "version": 1, "identities": IDENTITIES,
                             "quantization_metres": contact["quantization_metres"], "crop_bounds_metres": contact["crop_bounds_metres"],
                             "counts": COUNTS, "groups": groups, "objects": objects, "vertices": vertices, "faces": faces, "patches": patches})
