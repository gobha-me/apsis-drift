#!/usr/bin/env python3
"""Read-only, registered two-object capture for checkpoint material extension01.

This tool deliberately does not run the broad material capture. Blender loads
the master only in main, after the exact invocation/helper/input pins pass.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import material_source_completion as completion
import material_inventory as inventory
import prepare_material_source as preparation

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "apsis-drift-checkpoint-material-extension/1"
HISTORY_SHA = "1ff8c7de423eeddf100f5a36e1f494225263c342e745361075dbef88c7215db1"
HELPERS = {
    "material_inventory.py": "df430228aef4e1527076353e20e03bda518a3f63abaa131d7a2f51e8953fdf22",
    "material_source_completion.py": "8fbb8a8bed5abcca57a2e8626966466d6cd4f5e8b89c0f2c0ee122dfc60eaceb",
    "prepare_material_source.py": "13a64f8a8fe440db369622df8093fdb7c4b487f9c43a7c9e5fb229b5c4d51987",
}
# Source index, exact current name, evaluated dimensions and retained crop.
SELECTED = (
    (1436, "WF02 | CABIN emergency pressure frame", 256, 512, 650, 188765, 296),
    (1574, "WF02 | retained nose joint backing", 3152, 5187, 733, 341614, 2898),
)
MAX_METADATA = 65536
MAX_PROPERTIES = 64
MAX_PACKET_JSON = 4096
EXPECTED_BINARY_BYTES = 231972


def canonical(packet):
    result = json.dumps(packet, sort_keys=True, separators=(",", ":"),
                        ensure_ascii=True, allow_nan=False)
    if len(result.encode()) > MAX_PACKET_JSON:
        raise ValueError("Captured property/modifier packet capacity")
    return result


def properties(obj):
    """Capture all available scalar properties, without inventing lineage."""
    keys = sorted(obj.keys())
    if len(keys) > MAX_PROPERTIES:
        raise ValueError("Current property count capacity")
    result = {}
    for key in keys:
        if not isinstance(key, str) or len(key.encode()) > 512:
            raise ValueError("Current property key capacity")
        value = obj[key]
        # Blender's UI descriptor is editor metadata, not a constructor property.
        if key == "_RNA_UI":
            continue
        if not isinstance(value, (str, int, float, bool)):
            raise ValueError("Unregistered nonscalar current property")
        if isinstance(value, float) and not math.isfinite(value):
            raise ValueError("Nonfinite current property")
        if isinstance(value, str) and len(value.encode()) > 2048:
            raise ValueError("Current property value capacity")
        result[key] = value
    canonical(result)
    return result


def bounded_vectors(values):
    if any(len(v) != 3 or any(not math.isfinite(x) or abs(x) > 8 for x in v)
           for v in values):
        raise ValueError("Registered finite eight-metre workspace")


def check_source_row(row, selected):
    index, name, vertices, triangles, oid, start, count = selected
    if (row["source_object"] != name or row["source_type"] != "MESH"
            or row["source_parent"] is not None or row["motion_group"] is not None
            or row["evaluated_vertex_count"] != vertices
            or row["evaluated_triangle_count"] != triangles
            or row["retained_contact_group"] != "craft_fixed"
            or row["retained_original_contact_range"] != {
                "object": oid, "group": "craft_fixed", "triangle_start": start,
                "triangle_count": count}
            or row["retained_quantized_triangles_verified"] != count
            or row["absent_from_original_contact_crop"] is not False):
        raise ValueError("Selected original inventory/crop attribution changed")
    return index


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--history-helper", type=Path, required=True)
    parser.add_argument("--capture-sha256", required=True)
    parser.add_argument("--base-completion", type=Path,
                        default=ROOT / "assets/native/wayfarer-material-source-01/completion.json")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    capture_sha = inventory.sha(Path(__file__))
    if args.capture_sha256 != capture_sha or len(capture_sha) != 64:
        raise ValueError("Exact registered capture helper required")
    if args.output.exists():
        raise ValueError("Capture output must be new")
    for name, pin in HELPERS.items():
        if inventory.sha(Path(__file__).parent / name) != pin:
            raise ValueError("Changed retained numeric capture helper")
    if (not 0 < args.history_helper.stat().st_size <= 1024 * 1024
            or inventory.sha(args.history_helper) != HISTORY_SHA):
        raise ValueError("Changed retained historical constructor")
    base, base_sha = completion.checked_json(args.base_completion,
        completion.MAX_METADATA_BYTES, preparation.COMPLETION_SHA)
    document = base["full_original_inventory"]
    completion.validate_inventory(document)
    if base["inventory_sha256"] != completion.INVENTORY_SHA256:
        raise ValueError("Original inventory fingerprint changed")
    rows = document["objects"]
    for selected in SELECTED:
        check_source_row(rows[selected[0]], selected)
    for name, pin in inventory.HELPER_HASHES.items():
        if inventory.sha(ROOT / "tools" / name) != pin:
            raise ValueError("Changed original source helper")
    for path, pin in ((inventory.METADATA, inventory.METADATA_SHA),
                      (inventory.CONTACT, inventory.CONTACT_SHA),
                      (inventory.SUPPORT, inventory.SUPPORT_SHA)):
        if inventory.sha(path) != pin:
            raise ValueError("Changed admitted original source input")
    if (not 0 < args.source.stat().st_size <= inventory.MAX_SOURCE_BYTES
            or inventory.sha(args.source) != inventory.SOURCE_SHA):
        raise ValueError("Exact bounded original master required")

    sys.path.insert(0, str(ROOT / "tools"))
    import bpy
    from export_boarding_contact import ancestors, columns, mesh_data, CONVERT
    from wayfarer_operating_blender import OperatingPoseController
    from wayfarer_operating_spec import classify_motion_group
    if bpy.app.version_string != "5.2.2 LTS":
        raise ValueError("Registered Blender version required")
    controller = OperatingPoseController.prepare(args.source).freeze()
    if not controller.frozen or len(bpy.context.scene.objects) > inventory.MAX_SCENE_OBJECTS:
        raise ValueError("Bounded genuine original source state")
    args.output.mkdir(parents=True)
    captured = []
    # Tighten the reused writer's aggregate bounds for this named capture only.
    completion.MAX_BINARY_BYTES = EXPECTED_BINARY_BYTES
    completion.MAX_CAPTURE_VERTICES = 3408
    completion.MAX_CAPTURE_TRIANGLES = 5699
    completion.MAX_RECORDS = 2
    with (args.output / "geometry.bin").open("xb") as stream:
        writer = completion.PayloadWriter(stream)
        for selected in SELECTED:
            index, name, n, t, *_ = selected
            row = rows[index]
            obj = bpy.data.objects.get(name)
            if (obj is None or obj.type != "MESH" or obj.parent is not None
                    or classify_motion_group(ancestors(obj)) is not None):
                raise ValueError("Exact fixed current source object required")
            matrix = columns(CONVERT @ obj.matrix_world)
            bounded_vectors(matrix)
            if matrix != row["source_local_blender_to_corrected_world_columns"]:
                raise ValueError("Current source affine frame changed")
            mods, props = completion.modifier_packet(obj), properties(obj)
            canonical(mods)
            packet = {"source_index": index, "identity": row,
                      "properties": props, "modifiers": mods,
                      "corrected_world_columns": matrix}
            if index == 1436:
                if len(obj.data.vertices) != 32 or len(obj.data.polygons) != 32:
                    raise ValueError("Registered frame base dimensions")
                local = [list(v.co) for v in obj.data.vertices]
                polygons = [list(p.vertices) for p in obj.data.polygons]
                bounded_vectors(local)
                if any(len(p) != 4 or len(set(p)) != 4
                       or any(not 0 <= i < 32 for i in p) for p in polygons):
                    raise ValueError("Registered base quad topology")
                packet["frame_base"] = {"vertices_local": local, "polygons": polygons}
            elif mods:
                raise ValueError("Retained nose current modifier state changed")
            vertices, faces = mesh_data(obj, "craft")
            if (len(vertices), len(faces)) != (n, t):
                raise ValueError("Exact evaluated source dimensions changed")
            if not completion.np.isfinite(vertices).all() or completion.np.max(completion.np.abs(vertices)) > 8:
                raise ValueError("Evaluated source outside registered workspace")
            summary = completion.capture_summary(vertices, faces)
            completion.verify_summary(summary, row)
            packet["geometry"] = writer._write_captured(vertices, faces, summary)
            captured.append(packet)
            del vertices, faces, summary
    if (writer.records, writer.vertices, writer.triangles, writer.bytes) != (2, 3408, 5699, EXPECTED_BINARY_BYTES):
        raise ValueError("Registered aggregate geometry changed")
    if inventory.sha(args.source) != inventory.SOURCE_SHA:
        raise ValueError("Original master changed during read-only capture")
    result = {"schema": SCHEMA, "source_sha256": inventory.SOURCE_SHA,
              "inventory_sha256": completion.INVENTORY_SHA256,
              "base_completion_sha256": base_sha, "history_helper_sha256": HISTORY_SHA,
              "capture_helper_sha256": capture_sha, "retained_helper_sha256": HELPERS,
              "source_helper_sha256": inventory.HELPER_HASHES,
              "original_input_sha256": {"operating_metadata": inventory.METADATA_SHA,
                  "original_contact": inventory.CONTACT_SHA, "boarding_support": inventory.SUPPORT_SHA},
              "blender_version": bpy.app.version_string, "objects": captured,
              "binary_payload": {"path": "geometry.bin", "bytes": writer.bytes,
                  "sha256": inventory.sha(args.output / "geometry.bin"),
                  "vertices": writer.vertices, "triangles": writer.triangles},
              "work": {"full_mesh_summaries": 2, "ordered_triangle_hash_streams": 4,
                  "inherited_verified_crop_faces": [296, 2898], "new_crop_face_replays": 0},
              "caps": {"decoded_source_bytes": 262144, "metadata_bytes": MAX_METADATA,
                  "numeric_scratch_bytes": completion.MAX_NUMERIC_SCRATCH_BYTES},
              "master_unchanged": True, "source_save_count": 0,
              "material_permission": False, "actor_permission": False}
    raw = (json.dumps(result, indent=2, allow_nan=False) + "\n").encode()
    if len(raw) > MAX_METADATA:
        raise ValueError("Capture metadata capacity")
    (args.output / "completion.json").write_bytes(raw)
    provenance = {"schema": "apsis-drift-checkpoint-material-extension-provenance/1",
        "source_sha256": inventory.SOURCE_SHA,
        "source_package": "../wayfarer-operating-02",
        "base_material_package": "../wayfarer-material-source-01",
        "source_license": "LicenseRef-Apsis-Hopper-Meshy-Output",
        "licenses": ["../wayfarer-operating-02/licenses"],
        "files": {"completion.json": {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()},
                  "geometry.bin": result["binary_payload"]},
        "capture_tool": "tools/boarding/checkpoint_material_extension.py",
        "capture_helper_sha256": capture_sha, "history_helper_sha256": HISTORY_SHA,
        "master_unchanged": True, "source_save_count": 0,
        "material_permission": False, "actor_permission": False}
    (args.output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    (args.output / "README.md").write_text(
        "# Wayfarer checkpoint material extension01\n\n"
        "Read-only two-source capture for the registered checkpoint material completion. "
        "The frame base and full evaluated meshes are preserved without remodeling. "
        "Geometry is in corrected WORLD coordinates; full evaluated face ordinals "
        "remain distinct from inherited native crop keys. Material and actor admission "
        "require the named C++ guards. See provenance.json and the linked source licenses.\n")
    print("CHECKPOINT_MATERIAL_CAPTURE_COMPLETE", writer.records, writer.bytes,
          "master unchanged; no material/actor authority", flush=True)


if __name__ == "__main__":
    main()
