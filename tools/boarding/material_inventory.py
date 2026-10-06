#!/usr/bin/env python3
"""Extract complete Wayfarer containing bounds without modifying its master.

Run Blender --background --python tools/boarding/material_inventory.py --
--source exact-craft09.blend --output NEW_DIRECTORY. This inventory completes
source coverage; bounds and topology fingerprints do not assign material or
grant body, support, collision, route, or actor permission.
"""

import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
SOURCE_SHA = "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677"
METADATA = ROOT / "assets/native/wayfarer-operating-02/metadata/wayfarer-operating-02.json"
CONTACT = METADATA.with_name("contact.json")
SUPPORT = ROOT / "assets/native/boarding-support-01/boarding-support-01.json"
SUPPORT_SHA = "58694e671102f27df5f405478af43d00fd05b3aeaef34c612939af6fa548a8d3"
HELPER_HASHES = {
    "export_boarding_contact.py": "20353bd2f3ef5182a70339fb4a5ad1ae1c99874640aa38c0c95b0a6520f5201c",
    "wayfarer_operating_blender.py": "00bd9327cea482518cf4bf5811f21a351b50309323d64a93c991d9356007e261",
    "wayfarer_operating_spec.py": "41bc77dd225a059427525113da3cef46db3b794937f433af818d31f3bb2f3113",
}
METADATA_SHA = "db7b0a2de2516adf9925abe74133a6c523bd334062884a1bfc403448c2b1e72e"
CONTACT_SHA = "109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a"
MAX_SOURCE_BYTES = 512 * 1024 * 1024
MAX_SCENE_OBJECTS = 16384
MAX_OBJECTS = 4096
MAX_OBJECT_VERTICES = 2097152
MAX_OBJECT_TRIANGLES = 4194304
MAX_TOTAL_VERTICES = 8388608
MAX_TOTAL_TRIANGLES = 8388608
MAX_DOCUMENT_BYTES = 16 * 1024 * 1024
MAX_NAME_BYTES = 512
TRIANGLE_CHUNK = 32768


def sha(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def checked_name(value):
    if not isinstance(value, str) or not value or len(value.encode()) > MAX_NAME_BYTES:
        raise ValueError("Invalid or excessive source name")
    return value


def summarize_mesh(vertices, faces):
    """Full pre-crop enclosures and ordered evaluated-triangle fingerprints.

    Extrema include unreferenced vertices, conservatively. Integer bounds refer
    to the separately quantized source, not raw binary64 geometry. This is not
    a closure test or a proof that an enclosed volume is occupied material.
    """
    if (vertices.ndim != 2 or vertices.shape[1] != 3 or faces.ndim != 2
            or faces.shape[1] != 3 or vertices.dtype != np.dtype("float64")
            or faces.dtype.kind not in "iu"):
        raise ValueError("Invalid evaluated array dimensions/types")
    if not 0 < len(vertices) <= MAX_OBJECT_VERTICES or not 0 < len(faces) <= MAX_OBJECT_TRIANGLES:
        raise ValueError("Empty or excessive evaluated geometry")
    if (not np.isfinite(vertices).all() or np.max(np.abs(vertices)) > 1000
            or faces.min() < 0 or faces.max() >= len(vertices)):
        raise ValueError("Nonfinite, out-of-workspace, or invalid indexed geometry")
    quantized = np.rint(vertices * 1000000).astype(np.int64)
    raw_hash, integer_hash = hashlib.sha256(), hashlib.sha256()
    for start in range(0, len(faces), TRIANGLE_CHUNK):
        indices = faces[start:start + TRIANGLE_CHUNK]
        raw_hash.update(np.ascontiguousarray(vertices[indices], dtype="<f8").tobytes())
        integer_hash.update(np.ascontiguousarray(quantized[indices], dtype="<i8").tobytes())
    return {
        "evaluated_vertex_count": len(vertices),
        "evaluated_triangle_count": len(faces),
        "evaluated_triangle_ordinal_range": [0, len(faces)],
        "full_bounds_corrected_world_metres": [vertices.min(0).tolist(), vertices.max(0).tolist()],
        "full_bounds_quantized_micrometres": [quantized.min(0).tolist(), quantized.max(0).tolist()],
        "ordered_triangle_binary64_sha256": raw_hash.hexdigest(),
        "ordered_triangle_micrometre_sha256": integer_hash.hexdigest(),
    }


def verify_retained_triangles(vertices, faces, crop, expected):
    """Reproduce the pinned pre-quantization crop and collapsed-face omission.

    The complete source fingerprint is independent of this compatibility check.
    No missing full object is excused because its cropped output is empty.
    """
    checked = 0
    for start in range(0, len(faces), TRIANGLE_CHUNK):
        triangles = vertices[faces[start:start + TRIANGLE_CHUNK]]
        if crop is not None:
            lower, upper = crop
            keep = np.all(triangles.max(1) >= lower, axis=1) & np.all(triangles.min(1) <= upper, axis=1)
            triangles = triangles[keep]
        quantized = np.rint(triangles * 1000000).astype(np.int64)
        distinct = (np.any(quantized[:, 0] != quantized[:, 1], axis=1)
                    & np.any(quantized[:, 1] != quantized[:, 2], axis=1)
                    & np.any(quantized[:, 2] != quantized[:, 0], axis=1))
        quantized = quantized[distinct]
        end = checked + len(quantized)
        if end > len(expected) or not np.array_equal(quantized, expected[checked:end]):
            raise ValueError("Full source does not reproduce admitted retained geometry")
        checked = end
    if checked != len(expected):
        raise ValueError("Incomplete admitted retained geometry")
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    if args.output.exists():
        raise ValueError("Inventory output must be a new directory")
    if not 0 < args.source.stat().st_size <= MAX_SOURCE_BYTES or sha(args.source) != SOURCE_SHA:
        raise ValueError("Inventory requires exact bounded Craft09 source")
    for name, expected in HELPER_HASHES.items():
        if sha(TOOLS / name) != expected:
            raise ValueError("Changed source helper: " + name)
    if sha(METADATA) != METADATA_SHA or sha(CONTACT) != CONTACT_SHA or sha(SUPPORT) != SUPPORT_SHA:
        raise ValueError("Changed admitted operating/contact identity")
    metadata = json.loads(METADATA.read_text())
    expected_roster = {row["source_object"]: row for row in metadata["roster"]["included"]}
    if not 0 < len(expected_roster) <= MAX_OBJECTS or len(expected_roster) != len(metadata["roster"]["included"]):
        raise ValueError("Invalid admitted full source roster")
    support = json.loads(SUPPORT.read_text())
    support_objects = {row["source_object"]: {
        "group": support["groups"][row["group"]]["id"],
        "object": index, "triangle_start": row["triangle_start"],
        "triangle_count": row["triangle_count"],
    } for index, row in enumerate(support["objects"])
      if support["groups"][row["group"]]["owner"] == "craft"}
    contact = json.loads(CONTACT.read_text())
    retained = {}
    retained_geometry = {}
    for group in contact["groups"]:
        if group["owner"] == "craft":
            retained_geometry[group["id"]] = np.asarray(group["vertices_micrometres"], dtype=np.int64)[
                np.asarray(group["triangles"], dtype=np.int32)]
            for name in group["source_objects"]:
                if name in retained:
                    raise ValueError("Duplicate admitted contact object")
                retained[name] = group["id"]

    # Lazy Blender imports keep the pure bounds/fingerprint helper testable.
    sys.path.insert(0, str(TOOLS))
    import bpy
    from export_boarding_contact import ancestors, columns, craft_keep, mesh_data, CONVERT
    from wayfarer_operating_blender import OperatingPoseController
    from wayfarer_operating_spec import classify_motion_group

    controller = OperatingPoseController.prepare(args.source).freeze()
    if not controller.frozen or len(bpy.context.scene.objects) > MAX_SCENE_OBJECTS:
        raise ValueError("Unprepared or excessive source scene")
    objects = sorted((obj for obj in bpy.context.scene.objects if craft_keep(obj)), key=lambda obj: obj.name)
    if len(objects) != len(expected_roster) or {obj.name for obj in objects} != set(expected_roster):
        raise ValueError("Complete current source roster differs from admitted render roster")
    rows = []
    total_vertices = total_triangles = 0
    for obj in objects:
        name = checked_name(obj.name)
        parent = checked_name(obj.parent.name) if obj.parent else None
        ancestry = [checked_name(value) for value in ancestors(obj)]
        moving = classify_motion_group(ancestry)
        if len(ancestry) > 32 or len(obj.modifiers) > 32:
            raise ValueError("Object ancestry/modifier capacity")
        expected = expected_roster[name]
        if (obj.type != expected["source_type"] or parent != expected["source_parent"]
                or moving != expected["motion_group"]):
            raise ValueError("Current object classification differs: " + name)
        vertices, faces = mesh_data(obj, "craft")
        summary = summarize_mesh(vertices, faces)
        source_range = support_objects.get(name)
        if (name in retained) != (source_range is not None) or (source_range and source_range["group"] != retained[name]):
            raise ValueError("Admitted support/contact roster disagreement: " + name)
        expected_geometry = np.empty((0, 3, 3), dtype=np.int64)
        if source_range:
            start = source_range["triangle_start"]
            expected_geometry = retained_geometry[source_range["group"]][start:start + source_range["triangle_count"]]
        retained_count = verify_retained_triangles(
            vertices, faces, contact["crop_bounds_metres"]["craft"] if moving is None else None, expected_geometry)
        total_vertices += summary["evaluated_vertex_count"]
        total_triangles += summary["evaluated_triangle_count"]
        if total_vertices > MAX_TOTAL_VERTICES or total_triangles > MAX_TOTAL_TRIANGLES:
            raise ValueError("Full source geometry work capacity")
        modifiers = []
        for modifier in obj.modifiers:
            row = {"name": checked_name(modifier.name), "type": modifier.type,
                   "show_viewport": bool(modifier.show_viewport), "show_render": bool(modifier.show_render)}
            for key in ("thickness", "offset", "width", "segments", "use_even_offset", "use_rim"):
                if hasattr(modifier, key):
                    row[key] = getattr(modifier, key)
            modifiers.append(row)
        construction = {key: obj[key] for key in ("construction", "role", "part", "semantic_role")
                        if key in obj and isinstance(obj[key], (str, int, float, bool))}
        if any(isinstance(value, str) and len(value.encode()) > 4096 for value in construction.values()):
            raise ValueError("Construction context text capacity")
        rows.append({"source_object": name, "source_type": obj.type, "source_parent": parent,
                     "ancestry": ancestry, "motion_group": moving,
                     "source_local_blender_to_corrected_world_columns": columns(CONVERT @ obj.matrix_world),
                     "retained_contact_group": retained.get(name), "retained_original_contact_range": source_range,
                     "retained_quantized_triangles_verified": retained_count,
                     "absent_from_original_contact_crop": name not in retained,
                     "construction_context": construction, "modifier_context": modifiers,
                     **summary})
        del vertices, faces
    if sha(args.source) != SOURCE_SHA:
        raise ValueError("Master changed during read-only inventory")
    document = {
        "schema": "apsis-drift-wayfarer-material-containing-inventory", "version": 1,
        "source_sha256": SOURCE_SHA, "operating_metadata_sha256": METADATA_SHA,
        "original_contact_sha256": CONTACT_SHA, "boarding_support_sha256": SUPPORT_SHA, "source_helper_sha256": HELPER_HASHES,
        "extractor_sha256": sha(Path(__file__)),
        "coordinate_contract": "Blender(x,y,z)->(x,z,-y); frozen corrected source-rest world baked once",
        "fingerprint_contract": "evaluated triangle ordinal order; 3x3 little-endian binary64 or signed int64 micrometre coordinates; SHA256",
        "quantization_metres": 0.000001, "master_unchanged": True,
        "blender_version": bpy.app.version_string,
        "selected_object_count": len(rows), "fixed_object_count": sum(row["motion_group"] is None for row in rows),
        "evaluated_vertex_count": total_vertices, "evaluated_triangle_count": total_triangles,
        "material_permission": False, "actor_permission": False, "objects": rows,
    }
    encoded = (json.dumps(document, indent=2, allow_nan=False) + "\n").encode()
    if len(encoded) > MAX_DOCUMENT_BYTES:
        raise ValueError("Inventory document capacity")
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "inventory.json").write_bytes(encoded)
    print("MATERIAL_INVENTORY", len(rows), total_triangles, "master unchanged; material permission false", flush=True)


if __name__ == "__main__":
    main()
