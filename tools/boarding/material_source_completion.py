#!/usr/bin/env python3
"""Capture complete current Wayfarer material inputs, without selecting occupancy.

Blender --background --python tools/boarding/material_source_completion.py --
--source exact-craft09.blend --inventory inventory.json --output NEW_DIRECTORY.
The read-only source load is a separate explicitly registered operation. Import
and pure buffer helpers do not load Blender, masters, or material classifiers.
"""

import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

# Blender's --python execution does not add the script directory to sys.path.
# Resolve the pinned companion from this tool's own directory before importing.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import material_inventory as inventory

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
SCHEMA = "apsis-drift-complete-material-source/1"
MAX_INVENTORY_BYTES = 16 * 1024 * 1024
MAX_RECORDS = 4096
MAX_BINARY_BYTES = 24 * 1024 * 1024
MAX_NUMERIC_SCRATCH_BYTES = 64 * 1024 * 1024
MAX_CAPTURE_VERTICES = 262144
MAX_CAPTURE_TRIANGLES = 524288
EXPECTED_SELECTED_VERTICES = 187755
EXPECTED_SELECTED_TRIANGLES = 347579
EXPECTED_MOVING_VERTICES = 29674
EXPECTED_MOVING_TRIANGLES = 50132
INVENTORY_SHA256 = "a78800c6f5223323855aae1c37e1b40f7f83af76c709696b4eb0f08f3a9f55d8"
MAX_PACKET_VERTICES = 256
MAX_PACKET_POLYGONS = 512
MAX_SPLINES = 8
MAX_SPLINE_POINTS = 128
MAX_MODIFIERS = 32
MAX_METADATA_BYTES = 8 * 1024 * 1024
BLOCK_ROWS = 32768
SHELL = "CRAFT | main pressure-shell exterior"
CURVES = {
    "CABIN | main life-support feed": (6, .027),
    "CABIN | life-support return": (5, .021),
    "CABIN | protected cockpit power data loom": (6, .025),
}
SUPPORTS = ("CABIN | cockpit transition step", "CABIN | lift-out walking tile 07-1")
STOWED_DIR = ROOT / "assets/native/stowed-cockpit-contact-01"
STOWED_PINS = {
    "frame.json": (494, "f98c50f69d71ecd38ca1d43d010f7b43ca300178dc25d40e170fd45fe5bc88a6"),
    "replacement-contact.json": (72208, "62b4d493f2d74f59b81fd089873360b99e3bae9d17a6d90a00066e9440e44c4a"),
    "provenance.json": (818, "8925bc3634f14a2c1a6c42d03947faf878af913a9992ead9ce5671374191133d"),
}


def checked_json(path, cap, expected_sha=None, exact_bytes=None):
    path = Path(path)
    size = path.stat().st_size
    if not 0 < size <= cap or (exact_bytes is not None and size != exact_bytes):
        raise ValueError("JSON input byte capacity")
    raw = path.read_bytes()
    if len(raw) != size or (expected_sha is not None and hashlib.sha256(raw).hexdigest() != expected_sha):
        raise ValueError("JSON input identity")
    result = json.loads(raw)
    return result, hashlib.sha256(raw).hexdigest()


def validate_inventory(document):
    rows = document.get("objects", [])
    if (document.get("schema") != "apsis-drift-wayfarer-material-containing-inventory"
            or document.get("version") != 1 or document.get("source_sha256") != inventory.SOURCE_SHA
            or document.get("operating_metadata_sha256") != inventory.METADATA_SHA
            or document.get("original_contact_sha256") != inventory.CONTACT_SHA
            or document.get("boarding_support_sha256") != inventory.SUPPORT_SHA
            or document.get("source_helper_sha256") != inventory.HELPER_HASHES
            or document.get("master_unchanged") is not True
            or document.get("material_permission") is not False
            or not 0 < len(rows) <= MAX_RECORDS
            or document.get("selected_object_count") != len(rows)):
        raise ValueError("Complete original inventory binding")
    names = [inventory.checked_name(row["source_object"]) for row in rows]
    if len(set(names)) != len(names):
        raise ValueError("Duplicate complete original object")
    fixed = sum(row["motion_group"] is None for row in rows)
    if document.get("fixed_object_count") != fixed:
        raise ValueError("Fixed inventory count")
    return {row["source_object"]: row for row in rows}


def verify_summary(summary, original):
    if any(summary[key] != original.get(key) for key in summary):
        raise ValueError("Full original geometry fingerprint changed")


class PayloadWriter:
    """Bound aggregate storage before writes; never retain the whole scene."""
    def __init__(self, stream):
        self.stream = stream
        self.bytes = 0
        self.vertices = 0
        self.triangles = 0
        self.records = 0

    def array(self, array, dtype):
        if dtype not in ("<f8", "<i8", "<i4"):
            raise ValueError("Closed binary codec")
        if array.ndim != 2 or array.shape[1] != 3:
            raise ValueError("Binary row dimensions")
        size = array.shape[0] * 3 * np.dtype(dtype).itemsize
        if self.bytes + size > MAX_BINARY_BYTES:
            raise ValueError("Aggregate binary byte capacity")
        result = {"offset": self.bytes, "bytes": size, "rows": len(array), "columns": 3, "dtype": dtype}
        digest = hashlib.sha256()
        for start in range(0, len(array), BLOCK_ROWS):
            block = np.ascontiguousarray(array[start:start + BLOCK_ROWS], dtype=dtype).tobytes()
            if self.stream.write(block) != len(block):
                raise ValueError("Incomplete binary write")
            digest.update(block)
        self.bytes += size
        result["sha256"] = digest.hexdigest()
        return result

    def mesh(self, vertices, faces):
        self._reserve(vertices, faces)
        summary = capture_summary(vertices, faces)
        return self._write_captured(vertices, faces, summary)

    def _reserve(self, vertices, faces):
        if (vertices.ndim != 2 or vertices.shape[1] != 3 or faces.ndim != 2
                or faces.shape[1] != 3 or len(vertices) > MAX_CAPTURE_VERTICES
                or len(faces) > MAX_CAPTURE_TRIANGLES):
            raise ValueError("Capture dimensions/capacity before arithmetic")
        if (self.records + 1 > MAX_RECORDS
                or self.vertices + len(vertices) > MAX_CAPTURE_VERTICES
                or self.triangles + len(faces) > MAX_CAPTURE_TRIANGLES):
            raise ValueError("Aggregate complete geometry capacity")
        required = len(vertices) * 48 + len(faces) * 12
        if self.bytes + required > MAX_BINARY_BYTES:
            raise ValueError("Complete mesh byte capacity before arithmetic/write")

    def _write_captured(self, vertices, faces, summary):
        # Main supplies only the just-captured and authenticated original summary;
        # this private phase shares it without repeating whole-mesh fingerprints.
        self._reserve(vertices, faces)
        quantized = np.rint(vertices * 1000000).astype(np.int64)
        packet = {
            "vertices_binary64": self.array(vertices, "<f8"),
            "vertices_quantized_micrometres": self.array(quantized, "<i8"),
            "triangle_indices": self.array(faces, "<i4"),
            **summary,
        }
        self.records += 1
        self.vertices += len(vertices)
        self.triangles += len(faces)
        return packet


def capture_summary(vertices, faces):
    if (vertices.ndim != 2 or vertices.shape[1] != 3 or faces.ndim != 2
            or faces.shape[1] != 3 or len(vertices) > MAX_CAPTURE_VERTICES
            or len(faces) > MAX_CAPTURE_TRIANGLES):
        raise ValueError("Capture dimensions/capacity before arithmetic")
    # Raw + scale + rint + cast overlap, faces, and conservatively four complete
    # indexed/conversion/hash block copies. Blender host and mesh conversion are
    # separate; no whole-scene numeric buffer is retained here.
    scratch = vertices.nbytes * 4 + faces.nbytes + BLOCK_ROWS * (9 * 8 * 4 + 3 * 4)
    if scratch > MAX_NUMERIC_SCRATCH_BYTES:
        raise ValueError("Numeric scratch capacity before fingerprint arithmetic")
    return inventory.summarize_mesh(vertices, faces)


def modifier_packet(obj):
    if len(obj.modifiers) > MAX_MODIFIERS:
        raise ValueError("Constructor modifier capacity")
    result = []
    keys = ("width", "segments", "affect", "offset_type", "limit_method", "angle_limit",
            "profile", "use_clamp_overlap", "loop_slide", "miter_outer", "miter_inner",
            "harden_normals", "mark_seam", "mark_sharp", "face_strength_mode", "vmesh_method",
            "keep_sharp", "weight", "thresh", "mode", "use_face_influence",
            "thickness", "offset", "use_even_offset", "use_rim", "use_rim_only")
    for mod in obj.modifiers:
        row = {"name": inventory.checked_name(mod.name), "type": mod.type,
               "show_viewport": bool(mod.show_viewport), "show_render": bool(mod.show_render)}
        for key in keys:
            if hasattr(mod, key):
                value = getattr(mod, key)
                if isinstance(value, (str, int, float, bool)):
                    if isinstance(value, float) and not np.isfinite(value):
                        raise ValueError("Nonfinite modifier parameter")
                    row[key] = value
        result.append(row)
    return result


def constructor_packet(obj, columns, convert):
    base = {"source_object": obj.name, "source_type": obj.type,
            "corrected_world_columns": columns(convert @ obj.matrix_world),
            "modifiers": modifier_packet(obj), "certified_material_membership": False}
    if obj.name == SHELL:
        return {**base, "source_kind": "retained_cut_mesh_skin",
                "existing_material_policy": "Unchanged surface boundaries enclosing cabin air, not filled solids.",
                "authored_wall_thickness_metres": None,
                "added_caps_or_thickness": False,
                "all_original_evaluated_triangles_required": True}
    if obj.name in CURVES:
        data = obj.data
        count, radius = CURVES[obj.name]
        if obj.type != "CURVE" or len(data.splines) != 1 or len(data.splines) > MAX_SPLINES:
            raise ValueError("Actual service POLY constructor")
        spline = data.splines[0]
        if (spline.type != "POLY" or len(spline.points) != count
                or len(spline.points) > MAX_SPLINE_POINTS
                or data.bevel_depth != float(np.float32(radius))):
            raise ValueError("Selected service curve structure/radius changed")
        if data.bevel_object is not None or data.taper_object is not None:
            raise ValueError("Unexpected service profile dependency")
        points = []
        for point in spline.points:
            co = list(point.co)
            if not all(np.isfinite(v) for v in co) or not np.isfinite(point.radius) or not np.isfinite(point.tilt):
                raise ValueError("Nonfinite service constructor")
            points.append({"co_homogeneous": co, "radius_factor": point.radius, "tilt": point.tilt})
        settings = {key: getattr(data, key) for key in (
            "dimensions", "resolution_u", "render_resolution_u", "bevel_depth", "bevel_resolution",
            "bevel_mode", "fill_mode", "use_fill_caps", "twist_mode", "twist_smooth",
            "bevel_factor_start", "bevel_factor_end", "bevel_factor_mapping_start", "bevel_factor_mapping_end")}
        return {**base, "source_kind": "actual_POLY_bevel_sweep", "curve_settings": settings,
                "spline": {"type": spline.type, "use_cyclic_u": spline.use_cyclic_u,
                           "resolution_u": spline.resolution_u, "points": points},
                "endpoint_caps_added": False, "bore_or_filled_volume_inferred": False}
    if obj.name in SUPPORTS:
        if (obj.type != "MESH" or not 0 < len(obj.data.vertices) <= MAX_PACKET_VERTICES
                or not 0 < len(obj.data.polygons) <= MAX_PACKET_POLYGONS):
            raise ValueError("Bounded actual support premodifier geometry")
        vertices = [list(v.co) for v in obj.data.vertices]
        polygons = [list(p.vertices) for p in obj.data.polygons]
        if not np.isfinite(np.asarray(vertices)).all() or any(not 3 <= len(p) <= 32 for p in polygons):
            raise ValueError("Invalid support constructor geometry")
        return {**base, "source_kind": "tapered_primitive_cube_with_current_modifiers",
                "premodifier_vertices_local": vertices, "premodifier_polygons": polygons,
                "untapered_box_substitution_allowed": False,
                "bevel_containment_or_convexity_inferred": False}
    return None


def checked_stowed_inputs(existing):
    sys.path.insert(0, str(TOOLS))
    import stowed_asset_identity as identity
    documents = {}
    for name, (size, pin) in STOWED_PINS.items():
        documents[name], _ = checked_json(STOWED_DIR / name, size, pin, size)
    frame = documents["frame.json"]
    contact = documents["replacement-contact.json"]
    if (frame != identity.FRAME or contact["source_sha256"] != inventory.SOURCE_SHA
            or contact["frame_sha256"] != STOWED_PINS["frame.json"][1]
            or contact["model_sha256"] != identity.PAYLOAD_PINS["model.glb"]["sha256"]
            or contact["runtime_actor_admitted"] is not False
            or contact["retained_originals_and_halo_added_again"] is not False
            or [row["source_object"] for row in contact["objects"]] != identity.PROFILE["replacement_objects"]):
        raise ValueError("Selected stowed source/frame binding")
    removals = identity.ORIGINAL_RANGES
    for removal in removals:
        original = existing[removal["source_object"]]["retained_original_contact_range"]
        if original != {"group": "craft_seat_lift", "object": removal["original_object"],
                        "triangle_start": removal["start"], "triangle_count": removal["count"]}:
            raise ValueError("Complete-original stowed removal identity")
    return frame, contact, removals


def stowed_records(writer, existing):
    frame, contact, removals = checked_stowed_inputs(existing)
    rows = []
    for row in contact["objects"]:
        vertices = np.asarray(row["vertices"], dtype=np.float64)
        faces = np.asarray(row["triangles"], dtype=np.int32)
        # These records are already REST-world baked. No source transform is reapplied.
        rows.append({"source_object": row["source_object"], "frame": "stowed_REST_world_baked",
                     "runtime_transform": "existing seat_lift group delta exactly once",
                     "original_catalog_range": row["original_catalog_range"],
                     "introduced_connector": row["introduced_connector"],
                     "geometry": writer.mesh(vertices, faces)})
    return {"frame": frame, "input_pins": STOWED_PINS,
            "original_removal_ranges": removals, "replacement_objects": rows,
            "posed_membership_qualified": False, "closed_material_proof_replayed": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    if args.output.exists():
        raise ValueError("Completion output must be new")
    document, inventory_sha = checked_json(args.inventory, MAX_INVENTORY_BYTES, INVENTORY_SHA256)
    expected = validate_inventory(document)
    # Authenticate these small existing packets before the expensive master load.
    checked_stowed_inputs(expected)
    for name, pin in inventory.HELPER_HASHES.items():
        if inventory.sha(TOOLS / name) != pin:
            raise ValueError("Changed original source helper")
    for path, pin in ((inventory.METADATA, inventory.METADATA_SHA),
                      (inventory.CONTACT, inventory.CONTACT_SHA),
                      (inventory.SUPPORT, inventory.SUPPORT_SHA)):
        if inventory.sha(path) != pin:
            raise ValueError("Changed original admitted source input")
    if not 0 < args.source.stat().st_size <= inventory.MAX_SOURCE_BYTES or inventory.sha(args.source) != inventory.SOURCE_SHA:
        raise ValueError("Exact bounded original master required")
    # Only this explicit main operation loads Blender. No source save or factory reset.
    sys.path.insert(0, str(TOOLS))
    import bpy
    from export_boarding_contact import ancestors, columns, craft_keep, mesh_data, CONVERT
    from wayfarer_operating_blender import OperatingPoseController
    from wayfarer_operating_spec import classify_motion_group
    controller = OperatingPoseController.prepare(args.source).freeze()
    if not controller.frozen or len(bpy.context.scene.objects) > inventory.MAX_SCENE_OBJECTS:
        raise ValueError("Bounded genuine original source state")
    objects = sorted((o for o in bpy.context.scene.objects if craft_keep(o)), key=lambda o: o.name)
    if {o.name for o in objects} != set(expected) or len(objects) != len(expected):
        raise ValueError("Complete original roster changed")
    args.output.mkdir()
    selected_rows, posed_rows, constructors = [], [], []
    selected = {SHELL, *CURVES, *SUPPORTS}
    selected_vertices = selected_triangles = moving_vertices = moving_triangles = 0
    with (args.output / "geometry.bin").open("xb") as stream:
        writer = PayloadWriter(stream)
        for obj in objects:
            source = expected[obj.name]
            moving = classify_motion_group(ancestors(obj))
            if obj.type != source["source_type"] or moving != source["motion_group"]:
                raise ValueError("Current original object binding")
            if (obj.parent.name if obj.parent else None) != source["source_parent"]:
                raise ValueError("Current original parent binding")
            if obj.name not in selected:
                continue
            # Current inventory identities bound each mesh before evaluation.
            if (source["evaluated_vertex_count"] > MAX_CAPTURE_VERTICES
                    or source["evaluated_triangle_count"] > MAX_CAPTURE_TRIANGLES):
                raise ValueError("Selected geometry capacity before evaluation")
            vertices, faces = mesh_data(obj, "craft")
            summary = capture_summary(vertices, faces)
            verify_summary(summary, source)
            selected_rows.append({"source_object": obj.name, "motion_group": moving,
                                  "source_type": obj.type, "source_parent": source["source_parent"],
                                  "rest_corrected_world_columns": columns(CONVERT @ obj.matrix_world),
                                  "geometry": writer._write_captured(vertices, faces, summary)})
            constructors.append(constructor_packet(obj, columns, CONVERT))
            selected_vertices += len(vertices)
            selected_triangles += len(faces)
            del vertices, faces
        if (selected_vertices != EXPECTED_SELECTED_VERTICES
                or selected_triangles != EXPECTED_SELECTED_TRIANGLES):
            raise ValueError("Frozen selected complete geometry work")
        if {row["source_object"] for row in constructors} != {SHELL, *CURVES, *SUPPORTS}:
            raise ValueError("Complete six constructor packets")
        controller.reset()
        for channel in ("roof_transfer", "inner_door", "seat_boarding"):
            controller.apply(channel, 1, reset=False)
        posed_deltas = controller.matrices()
        for obj in objects:
            moving = expected[obj.name]["motion_group"]
            if moving is None:
                continue
            if (expected[obj.name]["evaluated_vertex_count"] > EXPECTED_MOVING_VERTICES
                    or expected[obj.name]["evaluated_triangle_count"] > EXPECTED_MOVING_TRIANGLES):
                raise ValueError("Moving geometry capacity before evaluation")
            vertices, faces = mesh_data(obj, "craft")
            summary = capture_summary(vertices, faces)
            if (len(vertices) != expected[obj.name]["evaluated_vertex_count"]
                    or len(faces) != expected[obj.name]["evaluated_triangle_count"]):
                raise ValueError("Rigid moving topology changed")
            moving_vertices += len(vertices)
            moving_triangles += len(faces)
            if (moving_vertices > EXPECTED_MOVING_VERTICES
                    or moving_triangles > EXPECTED_MOVING_TRIANGLES):
                raise ValueError("Moving aggregate work capacity")
            posed_rows.append({"source_object": obj.name, "motion_group": moving,
                               "operating_tuple": [1, 1, 1, 0],
                               "posed_corrected_world_columns": columns(CONVERT @ obj.matrix_world),
                               "source_group_delta_columns": posed_deltas[moving],
                               "complete_posed_bounds_and_fingerprints": summary,
                               "authoritative_cpp_pose_equivalence_claimed": False})
            del vertices, faces
        if (len(posed_rows) != 165 or moving_vertices != EXPECTED_MOVING_VERTICES
                or moving_triangles != EXPECTED_MOVING_TRIANGLES):
            raise ValueError("Complete selected-pose moving work")
        stowed = stowed_records(writer, expected)
    if inventory.sha(args.source) != inventory.SOURCE_SHA:
        raise ValueError("Master changed during read-only completion")
    result = {"schema": SCHEMA, "version": 1, "source_sha256": inventory.SOURCE_SHA,
              "extractor_sha256": inventory.sha(Path(__file__)),
              "inventory_sha256": inventory_sha,
              "inventory_extractor_sha256": document["extractor_sha256"],
              "source_helper_sha256": inventory.HELPER_HASHES,
              "original_input_pins": {"operating_metadata": inventory.METADATA_SHA,
                                      "original_contact": inventory.CONTACT_SHA,
                                      "boarding_support": inventory.SUPPORT_SHA},
              "coordinate_contract": document["coordinate_contract"],
              "blender_version": bpy.app.version_string,
              "full_original_inventory": document,
              "selected_original_geometry": selected_rows,
              "actual_source_posed_moving_objects": posed_rows,
              "constructor_packets": constructors, "selected_stowed_source": stowed,
              "binary_payload": {"path": "geometry.bin", "bytes": writer.bytes,
                                  "sha256": inventory.sha(args.output / "geometry.bin"),
                                  "records": writer.records, "vertices": writer.vertices,
                                  "triangles": writer.triangles},
              "caps": {"records": MAX_RECORDS, "binary_bytes": MAX_BINARY_BYTES,
                       "capture_vertices": MAX_CAPTURE_VERTICES,
                       "capture_triangles": MAX_CAPTURE_TRIANGLES,
                       "numeric_scratch_bytes": MAX_NUMERIC_SCRATCH_BYTES,
                       "metadata_bytes": MAX_METADATA_BYTES},
              "work": {"roster_bindings": len(objects), "rest_mesh_captures": len(selected_rows),
                       "posed_moving_summaries": len(posed_rows), "replacement_captures": 13,
                       "selected_vertices": selected_vertices, "selected_triangles": selected_triangles,
                       "moving_vertices": moving_vertices, "moving_triangles": moving_triangles,
                       "whole_mesh_summary_visits": len(selected_rows) + len(posed_rows) + 13,
                       "ordered_triangle_hash_stream_visits": 2 * (len(selected_rows) + len(posed_rows) + 13)},
              "master_unchanged": True, "source_save_count": 0,
              "topology_implies_filled_solid": False, "added_wall_thickness": False,
              "material_membership_qualified": False, "runtime_cpp_pose_bound": False,
              "actor_permission": False}
    text = (json.dumps(result, indent=2, allow_nan=False) + "\n").encode()
    if len(text) > MAX_METADATA_BYTES:
        raise ValueError("Completion metadata byte capacity")
    (args.output / "completion.json").write_bytes(text)
    print("MATERIAL_SOURCE_COMPLETE", writer.records, writer.bytes,
          "master unchanged; material membership and actor permission false", flush=True)


if __name__ == "__main__":
    main()
