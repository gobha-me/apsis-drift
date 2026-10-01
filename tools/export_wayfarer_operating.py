#!/usr/bin/env python3
"""Separate source-bound operating Wayfarer producer. BSD-3-Clause.

Run with Blender, --source exact-craft09.blend --output-dir NEW_DIRECTORY.
The frozen source, old flight export and every authoring master stay untouched.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import struct
import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import wayfarer_gltf_checks
from wayfarer_operating_glb_audit import audit
# Preserve the original qualified exporter file byte-for-byte. Its dependency
# gets the uniquely named admitted local helper without a public module rename.
sys.modules["check_hopper_gltf"] = wayfarer_gltf_checks
import wayfarer_flight_export_base as base
from wayfarer_operating_blender import CONVERT, OperatingPoseController, ancestry, columns
from wayfarer_operating_spec import (BASE_EXPORTER_SHA256, CHANNEL_IDS,
                                    CONTACT_ROLES, MOTION_RIGS, POSE_IDS,
                                    RUNTIME_NODES, SOURCE_SHA256,
                                    classify_motion_group)

MODEL = "wayfarer-operating-02.glb"
MANIFEST = "wayfarer-operating-02.json"


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def exclusion(obj):
    if obj.type not in ("MESH", "CURVE", "FONT"):
        return "non-render object"
    if obj.hide_render or obj.hide_get() or obj.hide_viewport:
        return "authored hidden object"
    if obj.name.startswith(("FIT |", "REVIEW |", "SENSOR |")):
        return "fit, review or sensor fixture"
    if obj.name.startswith("WF03 |") and any(
            part in obj.name for part in (" UI ", "flight attitude", "flight vector")):
        return "baked flight UI"
    if obj.name in ["WF02 | " + role + " display" for role in ("NAV", "FLIGHT", "SYSTEMS")]:
        return "baked instrument surface"
    return None


def finite_matrix(matrix):
    value = np.asarray(matrix, dtype=np.float64)
    if not np.isfinite(value).all() or value.shape != (4, 4):
        raise ValueError("Nonfinite or malformed operating transform")
    if np.max(np.abs(value[3] - [0, 0, 0, 1])) > 1e-6:
        raise ValueError("Operating transform is not affine")
    if abs(np.linalg.det(value[:3, :3]) - 1) > 1e-5:
        raise ValueError("Operating transform is not rigid")


def read_glb(path):
    with path.open("rb") as stream:
        magic, version, total = struct.unpack("<III", stream.read(12))
        count, kind = struct.unpack("<II", stream.read(8))
        if magic != 0x46546C67 or version != 2 or total != path.stat().st_size or kind != 0x4E4F534A or count > 8 * 1024 * 1024:
            raise ValueError("Invalid produced GLB header")
        return json.loads(stream.read(count))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    source, output = args.source.resolve(), args.output_dir.resolve()
    if sha(TOOLS / "wayfarer_flight_export_base.py") != BASE_EXPORTER_SHA256:
        raise ValueError("Qualified base exporter changed")
    if sha(source) != SOURCE_SHA256:
        raise ValueError("Selected craft09 source changed")
    names = [MODEL, MANIFEST, "export-checks.json"]
    if any((output / name).exists() for name in names):
        raise ValueError("Refuse replacing an existing operating delivery")
    output.mkdir(parents=True, exist_ok=True)
    source_hash = sha(source)
    controller = OperatingPoseController.prepare(source)
    scene = bpy.context.scene
    originals = [obj for obj in scene.objects if base.keep(obj)]
    if len(originals) != 1746 or len({obj.name for obj in originals}) != len(originals):
        raise ValueError("Qualified source object roster changed")
    if any(exclusion(obj) is not None for obj in originals):
        raise ValueError("Visibility policy disagrees with qualified exporter")
    excluded = [{"source_object": obj.name, "reason": exclusion(obj)}
                for obj in sorted(scene.objects, key=lambda obj: obj.name)
                if not base.keep(obj)]
    if any(record["reason"] is None for record in excluded):
        raise ValueError("Source exclusion lacks an explicit reason")
    # Preserve the qualified independent gear curve, then freeze the whole film.
    gear = [obj for obj in originals if obj.get("hopper_craft_gear01") and obj.get("gear_role") in ("moving", "door")]
    gear_rest = {obj: obj.matrix_world.copy() for obj in gear}
    gear_motion = {obj: [] for obj in gear}
    for frame in range(60, 101, 2):
        scene.frame_set(frame)
        bpy.context.view_layer.update()
        for obj in gear:
            delta = obj.matrix_world @ gear_rest[obj].inverted()
            gear_motion[obj].append(CONVERT @ delta @ CONVERT.inverted())
    controller.freeze()
    eye = CONVERT @ bpy.data.objects["FIT | eye midpoint"].matrix_world.translation
    if (eye - Vector((0, 1.365, -2.49))).length > 1e-5:
        raise ValueError("Qualified runtime seat calibration changed")
    object_rest = {obj: obj.matrix_world.copy() for obj in originals}
    membership = {obj: classify_motion_group(ancestry(obj)) for obj in originals}
    if set(group for group in membership.values() if group) != set(RUNTIME_NODES):
        raise ValueError("Missing operating rig geometry")
    rear_rest = bpy.data.objects["AFT01 | rear ramp main hinge"].matrix_world.copy()
    signatures, gear_group, gear_preview = {}, {}, {}
    for obj, poses in gear_motion.items():
        signature = tuple(round(v, 5) for matrix in poses for row in matrix for v in row)
        if signature not in signatures:
            name = "HopperGear%02d" % len(signatures)
            signatures[signature] = name
            gear_preview[name] = [columns(matrix) for matrix in poses]
        gear_group[obj] = signatures[signature]
    groups = []
    for source_rig, group in MOTION_RIGS.items():
        rig = controller.rigs[group]
        members = [obj.name for obj in originals if membership[obj] == group]
        groups.append({"id": group, "runtime_node": RUNTIME_NODES[group],
                       "source_rig": source_rig,
                       "source_parent": rig.parent.name if rig.parent else None,
                       "source_rest_transform": columns(CONVERT @ rig.matrix_world @ CONVERT.inverted()),
                       "runtime_rest_transform": columns(Matrix.Identity(4)),
                       "source_objects": members, "contact_role": CONTACT_ROLES[group]})
    proof_errors, channels, poses = [], [], {}

    def qualify_pose(label):
        deltas = controller.deltas()
        for group, matrix in deltas.items():
            finite_matrix(matrix)
        error = 0.
        for obj in originals:
            group = membership[obj]
            if group:
                # Compare the actual source world matrix, not just a marker.
                delta = CONVERT.inverted() @ deltas[group] @ CONVERT
                predicted = delta @ object_rest[obj]
            else:
                predicted = object_rest[obj]
            discrepancy = float(np.max(np.abs(np.asarray(predicted) - np.asarray(obj.matrix_world))))
            error = max(error, discrepancy)
            if discrepancy > 5e-6:
                raise ValueError("Source group motion mismatch: " + obj.name)
        if bpy.data.objects["AFT01 | rear ramp main hinge"].matrix_world != rear_rest:
            raise ValueError("Operating transfer changed the rear ramp")
        proof_errors.append({"pose": label, "maximum_source_matrix_error": error})
        return {group: columns(matrix) for group, matrix in deltas.items()}

    for channel in CHANNEL_IDS:
        samples = []
        for index in range(21):
            progress = index / 20
            controller.apply(channel, progress)
            samples.append({"progress": progress,
                            "transforms": qualify_pose(channel + ":" + str(index))})
        channels.append({"id": channel, "samples": samples})
    controller.reset()
    poses["rest"] = qualify_pose("rest")
    for name, channel, progress in (("roof_open", "roof_transfer", .4),
                                    ("transfer_deployed", "roof_transfer", 1.),
                                    ("inner_open", "inner_door", 1.),
                                    ("seat_boarding", "seat_boarding", 1.)):
        controller.apply(channel, progress)
        poses[name] = qualify_pose(name)
    boarding_eye = CONVERT @ bpy.data.objects["FIT | eye midpoint"].matrix_world.translation
    controller.apply("roof_transfer", 1.)
    controller.apply("inner_door", 1., reset=False)
    poses["transfer_cabin"] = qualify_pose("transfer_cabin")
    if set(poses) != set(POSE_IDS):
        raise ValueError("Operating pose identity mismatch")
    controller.reset()
    screens = []
    for page, role in enumerate(("NAV", "FLIGHT", "SYSTEMS")):
        rig = bpy.data.objects["WF02 | " + role + " display assembly"]
        matrix = CONVERT @ rig.matrix_world @ Matrix.Translation((0, 0, .039))
        screens.append({"page": page, "role": role, "transform": columns(matrix),
                        "size": [.48, .275] if role == "FLIGHT" else [.335, .25]})
    # Qualified export preparation is retained exactly in geometry/material policy.
    for obj in originals:
        if obj.type == "CURVE":
            obj.data.resolution_u = min(obj.data.resolution_u, 4)
            obj.data.bevel_resolution = min(obj.data.bevel_resolution, 1)
        for modifier in obj.modifiers:
            if modifier.type == "BEVEL":
                modifier.segments = min(modifier.segments, 2)
            if modifier.type == "SUBSURF":
                modifier.levels = min(modifier.levels, 1)
                modifier.render_levels = modifier.levels
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    batches_by_name, reductions, roster = defaultdict(list), [], []
    source_triangles = 0
    collection = bpy.data.collections.new("Wayfarer operating derivative")
    scene.collection.children.link(collection)
    for index, obj in enumerate(originals):
        evaluated = obj.evaluated_get(deps)
        mesh = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=True, depsgraph=deps)
        if not mesh.polygons:
            raise ValueError("Included source object has empty geometry: " + obj.name)
        uv_values = np.zeros(len(mesh.loops) * 2, dtype=np.float32)
        if mesh.uv_layers.active:
            mesh.uv_layers.active.data.foreach_get("uv", uv_values)
        for layer in list(mesh.uv_layers):
            mesh.uv_layers.remove(layer)
        mesh.uv_layers.new(name="UVMap").data.foreach_set("uv", uv_values)
        for material in mesh.materials:
            if material and material.use_nodes:
                for node in material.node_tree.nodes:
                    if node.type in ("NORMAL_MAP", "UVMAP"):
                        node.uv_map = "UVMap"
        mesh.transform(obj.matrix_world)
        mesh.calc_loop_triangles()
        before = len(mesh.loop_triangles)
        source_triangles += before
        copy = bpy.data.objects.new("Runtime " + obj.name, mesh)
        collection.objects.link(copy)
        generated = obj.get("asset_generated_surface", any(
            material and material.name.startswith("Material_0") for material in mesh.materials))
        if before > 5000 and generated:
            cap = 55000 if obj.name.startswith("ENGINE04 ") else (35000 if "nose skin" in obj.name else 85000)
            target = min(cap, max(3000, int(before * .24)))
            modifier = copy.modifiers.new("Derived mesh reduction", "DECIMATE")
            modifier.ratio = target / before
            bpy.context.view_layer.objects.active = copy
            bpy.ops.object.modifier_apply(modifier=modifier.name)
            reductions.append({"source": obj.name, "before_triangles": before, "target_triangles": target})
        group = membership[obj]
        batch = RUNTIME_NODES[group] if group else gear_group.get(obj, "HopperStructure")
        if "fitted glass" in obj.name:
            batch = "HopperGlass"
        elif obj.name.startswith("ENGINE04 "):
            batch = "HopperEnginePort" if obj.get("engine04_side") == -1 else "HopperEngineStarboard"
        batches_by_name[batch].append(copy)
        roster.append({"source_object": obj.name, "source_type": obj.type,
                       "source_parent": obj.parent.name if obj.parent else None,
                       "runtime_node": batch, "motion_group": group})
        if index % 200 == 0:
            print("OPERATING_EXPORT_PREP", index, len(originals), flush=True)
    batches = []
    bpy.ops.object.select_all(action="DESELECT")
    reverse = {name: group for group, name in RUNTIME_NODES.items()}
    for name, objects in batches_by_name.items():
        for obj in objects:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = objects[0]
        bpy.ops.object.join()
        batch = objects[0]
        batch.name = name
        if name in reverse:
            group = reverse[name]
            batch["operating_group"] = group
            batch["source_rig"] = groups[list(RUNTIME_NODES).index(group)]["source_rig"]
            batch["source_sha256"] = SOURCE_SHA256
        batches.append(batch)
        batch.select_set(False)
    texture_reductions = []
    for image in bpy.data.images:
        limit = int(image.get("runtime_max_size", 2048))
        if limit not in (1024, 2048, 4096, 8192):
            raise ValueError("Unsupported authored runtime texture limit")
        if image.type == "IMAGE" and max(image.size) > limit:
            old = list(image.size)
            factor = limit / max(old)
            image.scale(round(old[0] * factor), round(old[1] * factor))
            texture_reductions.append({"image": image.name, "source_size": old,
                                       "runtime_size": list(image.size)})
    total, vertices, bounds, geometry_hashes = 0, 0, [], {}
    for obj in batches:
        obj.data.calc_loop_triangles()
        total += len(obj.data.loop_triangles)
        vertices += len(obj.data.vertices)
        coords = np.empty(len(obj.data.vertices) * 3, dtype=np.float32)
        obj.data.vertices.foreach_get("co", coords)
        indices = np.empty(len(obj.data.loops), dtype=np.int32)
        obj.data.loops.foreach_get("vertex_index", indices)
        if not len(coords) or not np.isfinite(coords).all() or not len(indices) or indices.min() < 0 or indices.max() >= len(coords) // 3:
            raise ValueError("Invalid operating mesh buffers")
        if np.max(np.abs(np.asarray(obj.matrix_world) - np.eye(4))) > 1e-7:
            raise ValueError("Operating mesh is not a flat identity world batch")
        geometry_hashes[obj.name] = hashlib.sha256(coords.tobytes() + indices.tobytes()).hexdigest()
        bounds.append((coords.reshape(-1, 3).min(0), coords.reshape(-1, 3).max(0)))
        obj.select_set(True)
    if total >= 1500000:
        raise ValueError("Operating derivative exceeds qualified triangle budget")
    model = output / MODEL
    bpy.ops.export_scene.gltf(filepath=str(model), export_format="GLB", use_selection=True,
                             export_apply=True, export_animations=False,
                             export_cameras=False, export_lights=False,
                             export_yup=True, export_extras=True)
    gltf = read_glb(model)
    node_names = [node.get("name") for node in gltf["nodes"]]
    binding_receipts = []
    for group in groups:
        name = group["runtime_node"]
        matches = [(index, node) for index, node in enumerate(gltf["nodes"]) if node.get("name") == name]
        if len(matches) != 1:
            raise ValueError("Produced GLB has missing/ambiguous operating group")
        index, node = matches[0]
        extras = node.get("extras", {})
        if extras != {"operating_group": group["id"], "source_rig": group["source_rig"], "source_sha256": SOURCE_SHA256}:
            raise ValueError("Produced GLB operating identity extras changed")
        if "mesh" not in node or node.get("children") or any(index in other.get("children", []) for other in gltf["nodes"]):
            raise ValueError("Produced operating group is not a flat scene-root mesh")
        if len([item for item in node_names if item.replace(".", "_") == name]) != 1:
            raise ValueError("Godot name sanitization produces an ambiguous binding")
        binding_receipts.append({"group": group["id"], "source_rig": group["source_rig"],
                                 "glb_node_index": index, "glb_node_name": name,
                                 "runtime_node_name": name, "mesh_index": node["mesh"],
                                 "geometry_sha256": geometry_hashes[name]})
    texture_check = wayfarer_gltf_checks.check(model)
    binary_check = audit(model)
    exporter = {"path": "tools/export_wayfarer_operating.py", "sha256": sha(__file__),
                "base_sha256": BASE_EXPORTER_SHA256,
                "classification_sha256": sha(TOOLS / "wayfarer_operating_spec.py"),
                "pose_helper_sha256": sha(TOOLS / "wayfarer_operating_blender.py"),
                "validation_helper_sha256": sha(TOOLS / "wayfarer_gltf_checks.py"),
                "binary_audit_sha256": sha(TOOLS / "wayfarer_operating_glb_audit.py")}
    manifest = {
        "schema_version": 1, "id": "wayfarer-operating-02", "units": "metres",
        "axes": "Godot +Y up, -Z forward",
        "source": {"path": "assets/visual/hopper-craft-09.blend", "sha256": SOURCE_SHA256},
        "exporter": exporter, "model": {"file": MODEL, "sha256": sha(model)},
        "groups": groups, "channels": channels, "poses": poses,
        "derivative_corrections": controller.derivative_corrections,
        "anchors": {"pilot_eye": list(eye), "boarding_eye": list(boarding_eye),
                    "roof_collar": [0., 2.907, 5.20]},
        "roster": {"included": roster, "excluded": excluded},
        "gear_preview": gear_preview, "gear_preview_samples": 21,
        "screens": screens, "seat_adjustment_metres": {"up": .18, "forward": .08},
        "licenses": ["LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"],
        "limits": ["Source-bound mechanism inspection derivative; no actual pilot boarding or seat action.",
                   "All moving nodes contain world-baked rest geometry; complete ancestor-composed deltas apply once.",
                   "Rear ramp and gear stay in sealed/stowed source rest during transfer channels.",
                   "The entry arm preserves source local rest; the existing swivel lock has an explicit derivative withdrawal sequence.",
                   "Internal link/bolt fit corrections and shorter inner stroke are recorded; source master geometry is never rewritten.",
                   "The corrected-flight-to-authored-seat trajectory requires separate occupied/contact qualification.",
                   "Gear preview retains the qualified asset curve but has no gameplay authority."]}
    (output / MANIFEST).write_text(json.dumps(manifest, indent=2, allow_nan=False) + "\n")
    if sha(source) != source_hash:
        raise ValueError("Source master changed during derivative production")
    report = {
        "schema_version": 1, "pass": True, "source_sha256": source_hash,
        "model_sha256": sha(model), "manifest_sha256": sha(output / MANIFEST),
        "exporter": exporter, "source_unchanged": True,
        "source_objects": len(originals), "excluded_objects": len(excluded),
        "source_evaluated_triangles": source_triangles,
        "runtime_triangles": total, "vertices": vertices, "mesh_batches": len(batches),
        "operating_motion_groups": len(groups), "gear_motion_groups": len(signatures),
        "source_pose_proof": proof_errors, "glb_bindings": binding_receipts,
        "derivative_corrections": controller.derivative_corrections,
        "derivative_fit_measurements": controller.fit_measurements,
        "mesh_buffer_sha256": geometry_hashes, "texture_bindings": texture_check,
        "glb_buffer_audit": binary_check,
        "texture_reductions": texture_reductions, "reductions": reductions,
        "bounds_blender_metres": [np.min([item[0] for item in bounds], 0).tolist(),
                                   np.max([item[1] for item in bounds], 0).tolist()],
        "file_bytes": model.stat().st_size,
        "limits": "Producer/source transforms, geometry buffers, UVs and GLB identity only; contact sweeps, actual Godot import, displayed review and package admission remain separate."}
    (output / "export-checks.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print("WAYFARER_OPERATING_EXPORTED", total, "triangles", len(batches), "batches", model.stat().st_size, "bytes", flush=True)


if __name__ == "__main__":
    main()
