#!/usr/bin/env python3
"""One registered read-only source1441 capture, with no material admission.

Run only after Root freezes this helper hash and the exact Blender invocation.
Imports and pure metadata checks never load Blender or an authoring master.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import checkpoint_material_extension as retained

completion = retained.completion
inventory = retained.inventory
preparation = retained.preparation
ROOT = retained.ROOT
SCHEMA = "apsis-drift-hatch-seal-material/1"
SOURCE_INDEX = 1441
NAME = "WF02 | CABIN hatch perimeter seal"
LIFEBOAT_SHA = retained.HISTORY_SHA
FINISH_SHA = "8c9fe3208c3360c5d96d2fd38847674c5875289e171ea9ca232031b33d78f38f"
RETAINED_CAPTURE_SHA = "b4dbd4800be1122aaf7251adc829292ef5454aba4acde6f3f21bb302e094e898"
RAW_SHA = "1c46cd423627cbdf9f94bb2cbc74f23befe9f8e5ac903d9be195b79d8c346e40"
GRID_SHA = "21c6d9c5dd54af597e8e04a65d9fda80c56c3fa97c45f9ad315c86de9bfbfce1"
MAX_METADATA = 65536
BINARY_BYTES = 6240


def check_row(row):
    if (row["source_object"] != NAME or row["source_type"] != "CURVE"
            or row["source_parent"] is not None or row["motion_group"] is not None
            or row["evaluated_vertex_count"] != 90 or row["evaluated_triangle_count"] != 160
            or row["ordered_triangle_binary64_sha256"] != RAW_SHA
            or row["ordered_triangle_micrometre_sha256"] != GRID_SHA
            or row["retained_contact_group"] != "craft_fixed"
            or row["retained_original_contact_range"] != {"object": 655,
                "group": "craft_fixed", "triangle_start": 189501, "triangle_count": 140}
            or row["retained_quantized_triangles_verified"] != 140
            or row["absent_from_original_contact_crop"] is not False):
        raise ValueError("Selected original seal/crop identity changed")


def curve_packet(obj):
    data = obj.data
    if len(data.splines) != 1:
        raise ValueError("Registered single spline capture capacity")
    spline = data.splines[0]
    if spline.type != "POLY" or len(spline.points) != 9:
        raise ValueError("Registered nine-point POLY capture capacity")
    points = []
    for point in spline.points:
        co = list(point.co)
        if (len(co) != 4 or any(not math.isfinite(v) or abs(v) > 8 for v in co)
                or not math.isfinite(point.radius) or not math.isfinite(point.tilt)):
            raise ValueError("Finite bounded current spline point")
        points.append({"co_homogeneous": co, "radius_factor": point.radius, "tilt": point.tilt})
    fields = ("dimensions", "resolution_u", "render_resolution_u", "bevel_depth",
              "bevel_resolution", "bevel_mode", "fill_mode", "use_fill_caps",
              "twist_mode", "twist_smooth", "bevel_factor_start", "bevel_factor_end",
              "bevel_factor_mapping_start", "bevel_factor_mapping_end", "extrude", "offset")
    settings = {key: getattr(data, key) for key in fields}
    # These optional geometry switches are captured when exposed by the pinned
    # Blender version; the packet records presence rather than inventing defaults.
    for key in ("use_radius", "use_deform_bounds", "use_map_taper", "taper_radius_mode"):
        if hasattr(data, key):
            settings[key] = getattr(data, key)
    for value in settings.values():
        if not isinstance(value, (str, int, float, bool)):
            raise ValueError("Closed scalar curve setting packet")
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError("Nonfinite current curve setting")
    settings["bevel_object_absent"] = data.bevel_object is None
    settings["taper_object_absent"] = data.taper_object is None
    packet = {"type": spline.type, "use_cyclic_u": spline.use_cyclic_u,
              "resolution_u": spline.resolution_u, "points": points}
    retained.canonical(settings)
    retained.canonical(packet)
    return settings, packet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--history-lifeboat", type=Path, required=True)
    parser.add_argument("--history-finish", type=Path, required=True)
    parser.add_argument("--capture-sha256", required=True)
    parser.add_argument("--base-completion", type=Path,
        default=ROOT / "assets/native/wayfarer-material-source-01/completion.json")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    helper_sha = inventory.sha(Path(__file__))
    if args.capture_sha256 != helper_sha:
        raise ValueError("Exact frozen seal capture helper required")
    if args.output.exists():
        raise ValueError("Capture output must be new")
    for name, pin in {**retained.HELPERS,
                     "checkpoint_material_extension.py": RETAINED_CAPTURE_SHA}.items():
        if inventory.sha(Path(__file__).parent / name) != pin:
            raise ValueError("Changed retained numeric/capture helper")
    for path, pin in ((args.history_lifeboat, LIFEBOAT_SHA), (args.history_finish, FINISH_SHA)):
        if not 0 < path.stat().st_size <= 1024 * 1024 or inventory.sha(path) != pin:
            raise ValueError("Changed bounded historical constructor helper")
    base, base_sha = completion.checked_json(args.base_completion,
        completion.MAX_METADATA_BYTES, preparation.COMPLETION_SHA)
    document = base["full_original_inventory"]
    completion.validate_inventory(document)
    if base["inventory_sha256"] != completion.INVENTORY_SHA256:
        raise ValueError("Original inventory pin")
    row = document["objects"][SOURCE_INDEX]
    check_row(row)
    for name, pin in inventory.HELPER_HASHES.items():
        if inventory.sha(ROOT / "tools" / name) != pin:
            raise ValueError("Changed original operating/source helper")
    for path, pin in ((inventory.METADATA, inventory.METADATA_SHA),
                      (inventory.CONTACT, inventory.CONTACT_SHA),
                      (inventory.SUPPORT, inventory.SUPPORT_SHA)):
        if inventory.sha(path) != pin:
            raise ValueError("Changed admitted source input")
    if not 0 < args.source.stat().st_size <= inventory.MAX_SOURCE_BYTES or inventory.sha(args.source) != inventory.SOURCE_SHA:
        raise ValueError("Exact bounded source master required")

    sys.path.insert(0, str(ROOT / "tools"))
    import bpy
    from export_boarding_contact import ancestors, columns, mesh_data, CONVERT
    from wayfarer_operating_blender import OperatingPoseController
    from wayfarer_operating_spec import classify_motion_group
    if bpy.app.version_string != "5.2.2 LTS":
        raise ValueError("Registered Blender version required")
    controller = OperatingPoseController.prepare(args.source).freeze()
    if not controller.frozen or len(bpy.context.scene.objects) > inventory.MAX_SCENE_OBJECTS:
        raise ValueError("Bounded original scene state")
    obj = bpy.data.objects.get(NAME)
    if (obj is None or obj.type != "CURVE" or obj.parent is not None
            or classify_motion_group(ancestors(obj)) is not None):
        raise ValueError("Exact fixed original seal required")
    matrix = columns(CONVERT @ obj.matrix_world)
    retained.bounded_vectors(matrix)
    if matrix != row["source_local_blender_to_corrected_world_columns"]:
        raise ValueError("Current seal affine identity changed")
    settings, spline = curve_packet(obj)
    mods, props = completion.modifier_packet(obj), retained.properties(obj)
    retained.canonical(mods)
    # Capturing settings is not material admission: unsupported current modes
    # are preserved for the separately frozen C++ constructor to refuse.
    args.output.mkdir(parents=True)
    completion.MAX_BINARY_BYTES = BINARY_BYTES
    completion.MAX_CAPTURE_VERTICES = 90
    completion.MAX_CAPTURE_TRIANGLES = 160
    completion.MAX_RECORDS = 1
    with (args.output / "geometry.bin").open("xb") as stream:
        writer = completion.PayloadWriter(stream)
        vertices, faces = mesh_data(obj, "craft")
        if (len(vertices), len(faces)) != (90, 160) or not completion.np.isfinite(vertices).all() or completion.np.max(completion.np.abs(vertices)) > 8:
            raise ValueError("Registered finite evaluated seal dimensions/workspace")
        summary = completion.capture_summary(vertices, faces)
        completion.verify_summary(summary, row)
        geometry = writer._write_captured(vertices, faces, summary)
        del vertices, faces, summary
    if (writer.records, writer.vertices, writer.triangles, writer.bytes) != (1, 90, 160, BINARY_BYTES):
        raise ValueError("Exact aggregate seal geometry")
    if inventory.sha(args.source) != inventory.SOURCE_SHA:
        raise ValueError("Master changed during read-only capture")
    result = {"schema": SCHEMA, "source_sha256": inventory.SOURCE_SHA,
        "inventory_sha256": completion.INVENTORY_SHA256, "base_completion_sha256": base_sha,
        "history_lifeboat_sha256": LIFEBOAT_SHA, "history_finish_sha256": FINISH_SHA,
        "capture_helper_sha256": helper_sha,
        "retained_helper_sha256": {**retained.HELPERS, "checkpoint_material_extension.py": RETAINED_CAPTURE_SHA},
        "source_helper_sha256": inventory.HELPER_HASHES,
        "original_input_sha256": {"operating_metadata": inventory.METADATA_SHA,
            "original_contact": inventory.CONTACT_SHA, "boarding_support": inventory.SUPPORT_SHA},
        "blender_version": bpy.app.version_string,
        "object": {"source_index": SOURCE_INDEX, "identity": row,
            "corrected_world_columns": matrix, "curve_settings": settings,
            "spline": spline, "modifiers": mods, "properties": props, "geometry": geometry},
        "binary_payload": {"path": "geometry.bin", "bytes": BINARY_BYTES,
            "sha256": inventory.sha(args.output / "geometry.bin"), "vertices": 90, "triangles": 160},
        "work": {"full_mesh_summaries": 1, "ordered_triangle_hash_streams": 2,
            "inherited_verified_crop_faces": 140, "new_crop_face_replays": 0},
        "caps": {"decoded_source_bytes": 65536, "metadata_bytes": MAX_METADATA,
            "numeric_scratch_bytes": completion.MAX_NUMERIC_SCRATCH_BYTES,
            "splines": 1, "points": 9, "modifiers": 32, "properties": 64,
            "canonical_packet_bytes": 4096},
        "master_unchanged": True, "source_save_count": 0,
        "material_permission": False, "actor_permission": False}
    raw = (json.dumps(result, indent=2, allow_nan=False) + "\n").encode()
    if len(raw) > MAX_METADATA:
        raise ValueError("Seal metadata capacity")
    (args.output / "completion.json").write_bytes(raw)
    provenance = {"schema": "apsis-drift-hatch-seal-material-provenance/1",
        "source_sha256": inventory.SOURCE_SHA, "source_package": "../wayfarer-operating-02",
        "base_material_package": "../wayfarer-material-source-01",
        "source_license": "LicenseRef-Apsis-Hopper-Meshy-Output",
        "licenses": ["../wayfarer-operating-02/licenses"],
        "files": {"completion.json": {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()},
                  "geometry.bin": result["binary_payload"]},
        "capture_tool": "tools/boarding/hatch_seal_material.py", "capture_helper_sha256": helper_sha,
        "history_lifeboat_sha256": LIFEBOAT_SHA, "history_finish_sha256": FINISH_SHA,
        "master_unchanged": True, "source_save_count": 0,
        "material_permission": False, "actor_permission": False}
    (args.output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    (args.output / "README.md").write_text(
        "# Wayfarer hatch-seal material01\n\n"
        "Read-only capture of the complete current hatch perimeter seal, spline "
        "and setting packets. Full evaluated face ordinals remain distinct from "
        "inherited native crop keys. Separate named C++ admission is required; "
        "captured geometry grants no material or actor permission. See "
        "provenance.json and the linked existing source licenses.\n")
    print("HATCH_SEAL_CAPTURE_COMPLETE", writer.records, writer.bytes,
          "master unchanged; no material/actor authority", flush=True)


if __name__ == "__main__":
    main()
