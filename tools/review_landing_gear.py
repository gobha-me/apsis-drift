#!/usr/bin/env python3
"""Reversible Blender-only landing gear proposal; never changes flight assets.

blender -b --factory-startup -t 8 --python tools/review_landing_gear.py -- --width 1920 --samples 32
"""
import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_hero_assets as art

SOURCE = ROOT / "assets/visual/hero-ship.blend"
OUTPUT = ROOT / "build-godot/gear-study"
PADS = (("FORE", 0.0, 6.7, -.28), ("PORT", -3.5, -2.7, -.08),
        ("STARBOARD", 3.5, -2.7, -.08))
CONTACT_Z = -1.328
STUDY_REVISION = 4


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def scrub_png_metadata(path):
    """Remove Blender's source-path text without decoding/changing pixels."""
    raw = path.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Expected generated PNG")
    chunks = [raw[:8]]
    offset = 8
    while offset < len(raw):
        if offset + 12 > len(raw):
            raise ValueError("Truncated PNG chunk")
        size = struct.unpack_from(">I", raw, offset)[0]
        end = offset + size + 12
        if end > len(raw):
            raise ValueError("Invalid PNG chunk extent")
        kind = raw[offset + 4:offset + 8]
        if kind not in (b"tEXt", b"zTXt", b"iTXt", b"eXIf"):
            chunks.append(raw[offset:end])
        offset = end
    path.write_bytes(b"".join(chunks))


def finalize_images():
    path = OUTPUT / "provenance.json"
    manifest = json.loads(path.read_text())
    for entry in manifest["poses"]:
        image_path = OUTPUT / entry["image"]
        if image_path.parent != OUTPUT or image_path.suffix != ".png":
            raise ValueError("Unexpected study image path")
        scrub_png_metadata(image_path)
        entry["png_sha256"] = digest(image_path)
        entry["png_metadata"] = "Text/EXIF removed; pixel and color chunks preserved verbatim"
        sidecar = image_path.with_suffix(".json")
        metadata = json.loads(sidecar.read_text())
        metadata.update(entry)
        sidecar.write_text(json.dumps(metadata, indent=2) + "\n")
    path.write_text(json.dumps(manifest, indent=2) + "\n")


def pin(name, point, radius=.075, width=.31, axis=(1, 0, 0)):
    direction = Vector(axis).normalized()
    art.cyl(name, point, radius, width, "steel", direction, 32)
    for side in (-1, 1):
        p = Vector(point) + direction * (side * (width / 2 + .008))
        art.cyl(name + " captive cap", p, radius * .78, .018, "orange", direction, 6)


def mechanism(name, x, pad_y, pivot_z, pose):
    art.assembly("STUDY | " + name + " articulated gear", 0)
    pivot = Vector((x, pad_y - .72, pivot_z))
    extended = Vector((x, pad_y, CONTACT_Z + .20))
    length = (extended - pivot).length
    if pose == "stowed":
        end = pivot + Vector((0, math.sqrt(length * length - .015**2), -.015))
    elif pose == "compressed":
        end = extended + Vector((0, 0, .30))
    else:
        end = extended
    axis = (end - pivot).normalized()
    # A rigid swinging leg with telescopic shock absorption; no scaling away
    # its length to squeeze it into the bay. The shoe counter-rotates to level.
    body_end = pivot + axis * .64
    art.beam(name + " oleo housing", pivot, body_end, .108, "paint", 32)
    art.beam(name + " polished piston", pivot + axis * .53, end, .064, "steel", 32)
    for distance in (.10, .56, .64):
        art.ring(name + " machined gland", pivot + axis * distance, .11, .018,
                 "steel", axis, 40)
    art.ring(name + " piston wiper", body_end + axis * .015, .074, .012,
             "rubber", axis, 32)
    pin(name + " trunnion", pivot, .115, .48)
    pin(name + " shoe pivot", end, .075, .48)
    for side in (-1, 1):
        art.box(name + " hull clevis", (x + side * .19, pivot.y, pivot_z + .05),
                (.095, .29, .32), "steel", .025)
        art.box(name + " shoe clevis", (x + side * .19, end.y, end.z - .035),
                (.095, .20, .22), "paint", .018)
    shoe_bottom = end.z - .20
    art.box(name + " replaceable landing shoe", (x, end.y, shoe_bottom + .094),
            (.75, 1.10, .14), "paint", .035)
    for row in range(7):
        art.box(name + " sole contact rib", (x, end.y - .45 + row * .15, shoe_bottom + .012),
                (.70, .08, .024), "rubber", .008)
    for side in (-1, 1):
        art.beam(name + " shoe load rail", (x + side * .25, end.y - .45, shoe_bottom + .155),
                 (x + side * .25, end.y + .45, shoe_bottom + .155), .028, "steel", 16)
        for yy in (-.39, .39):
            art.cyl(name + " sole bolt", (x + side * .28, end.y + yy, shoe_bottom + .147),
                    .033, .018, "steel", n=6)
    # Two equal pinned drag links fold sideways into the bay, not upward into
    # the wing. Pin axes are perpendicular to their actual articulation plane.
    brace_lateral = .22
    brace_root = pivot + Vector((brace_lateral, .33, .05))
    collar_offset = .15 if name == "FORE" else .25
    collar = end - axis * collar_offset
    # Include the spherical bearing and the outermost gland torus, not just
    # collar centre clearance. These sampled poses still do not prove a sweep.
    bearing_gland_clearance = math.hypot(brace_lateral - .11, (collar - pivot).length - .64) - (.072 + .018)
    if bearing_gland_clearance < .020:
        raise ValueError("Piston bearing enters gland envelope or 20mm review margin")
    brace_end = collar + Vector((brace_lateral, 0, 0))
    root_from_pivot = brace_root - pivot
    root_axis_distance = (root_from_pivot - axis * root_from_pivot.dot(axis)).length
    root_housing_clearance = root_axis_distance - (.108 + .072)
    if root_housing_clearance < .020:
        raise ValueError("Drag root bearing enters housing envelope or 20mm review margin")
    art.ring(name + " drag link piston collar", collar, .070, .016, "steel", axis, 32)
    art.beam(name + " drag link piston lug", collar, brace_end, .036, "steel", 20)
    delta = brace_end - brace_root
    half = delta.length / 2
    link_length = .56
    if half >= link_length:
        raise ValueError("Drag link assembly cannot reach requested pose")
    knee = (brace_root + brace_end) / 2 - Vector((1, 0, 0)) * math.sqrt(link_length**2 - half**2)
    link_axis = delta.cross(Vector((1, 0, 0))).normalized()
    for a, b in ((brace_root, knee), (knee, brace_end)):
        art.beam(name + " folding drag link", a, b, .037, "steel", 16)
    for p in (brace_root, knee, brace_end):
        pin(name + " drag link pin", p, .049, .15, link_axis)
    for p in (brace_root, brace_end):
        art.sphere(name + " drag link spherical bearing", p, (.072, .072, .072), "steel")
        art.ring(name + " drag link bearing race", p, .070, .012, "black", link_axis, 32)
    # Actuator endpoints remain attached to hull and leg; it is not propulsion.
    anchor = pivot + Vector((-.24, 1.55, .045))
    driven_axis = pivot + axis * .42
    driven = driven_axis + Vector((-.24, 0, 0))
    actuator_axis = (driven - anchor).normalized()
    actuator_length = (driven - anchor).length
    if not 1.05 <= actuator_length <= 1.45:
        raise ValueError("Deployment actuator outside proposed stroke range")
    art.beam(name + " deployment actuator body", anchor, anchor + actuator_axis * .80, .046, "black", 20)
    art.beam(name + " deployment actuator rod", anchor + actuator_axis * .70, driven, .025, "steel", 20)
    art.beam(name + " actuator eye mounting lug", driven_axis, driven, .035, "steel", 20)
    pin(name + " actuator root", anchor, .047, .12)
    pin(name + " actuator eye", driven, .047, .12)
    # Rear cassettes attach to wing underside; nose cassette sits below belly.
    roof = .24 if name != "FORE" else -.11
    for name_suffix, root in (("drag link hull bracket", brace_root), ("actuator hull bracket", anchor)):
        art.box(name + " " + name_suffix, (root.x, root.y, (roof + root.z) / 2),
                (.12, .14, max(.06, roof - root.z + .04)), "steel", .012)
    rear, front = pad_y - .82, pad_y + 1.22
    for side in (-1, 1):
        art.box(name + " load spreader", (x + side * .40, (rear + front) / 2, roof),
                (.11, front - rear, .10), "steel", .016)
        art.box(name + " mounting bearer", (x + side * .19, pivot.y, (roof + pivot_z) / 2),
                (.12, .35, max(.06, roof - pivot_z)), "paint", .016)
        art.box(name + " bay cheek", (x + side * .475, (rear + front) / 2, (roof - .52) / 2),
                (.06, front - rear, roof + .52), "paint", .015)
        hinge = Vector((x + side * .475, (rear + front) / 2, -.53))
        # Doors open outwards; closed halves meet at the centreline.
        angle = 0 if pose == "stowed" else side * math.radians(-166)
        offset = Vector((-side * .2375, 0, 0))
        offset.rotate(Matrix.Rotation(angle, 3, "Y"))
        art.box(name + " hinged bay door", hinge + offset, (.475, front - rear - .06, .035),
                "ivory", .012, rot=(0, angle, 0))
        art.beam(name + " door hinge", hinge + Vector((0, -.84, 0)),
                 hinge + Vector((0, .84, 0)), .024, "steel", 16)
        for yy in (rear + .12, front - .12):
            art.cyl(name + " bearer captive bolt", (x + side * .40, yy, roof + .058),
                    .032, .018, "steel", n=6)
        for yy in (rear + .13, (rear + front) / 2, front - .13):
            art.cyl(name + " cheek captive bolt", (x + side * .514, yy, roof - .09),
                    .024, .016, "steel", (side, 0, 0), 6)
        art.box(name + " service panel", (x + side * .514, (rear + front) / 2, roof - .24),
                (.012, 1.33, .23), "panel", .008)
        art.box(name + " gear index stripe", (x + side * .525, front - .29, roof - .24),
                (.012, .08, .22), "orange", .003)
    art.box(name + " bay end bulkhead front", (x, front, (roof - .52) / 2),
            (.97, .06, roof + .52), "paint", .018)
    # The trunnion casing needs an actual opening in the rear bulkhead, not
    # hidden intersections in a solid rectangular wall. Side posts carry load.
    opening_half_width = .17
    side_width = (.97 - 2 * opening_half_width) / 2
    for side in (-1, 1):
        art.box(name + " bay end bulkhead rear post", (x + side * (opening_half_width + side_width / 2), rear, (roof - .52) / 2),
                (side_width, .06, roof + .52), "paint", .014)
    for lower, upper in ((-.52, pivot_z - .16), (pivot_z + .16, roof)):
        if upper > lower:
            art.box(name + " bay end bulkhead rear bridge", (x, rear, (lower + upper) / 2),
                    (.34, .06, upper - lower), "paint", .008)
    art.box(name + " bay roof", (x, (rear + front) / 2, roof + .025),
            (.97, front - rear, .05), "black", .012)
    return {"name": name, "pose": pose, "pad_center_blender_metres": [x, end.y, shoe_bottom],
            "pad_size_metres": [.75, 1.10], "pivot_blender_metres": list(pivot),
            "leg_length_metres": (end - pivot).length,
            "deployment_actuator_housing_metres": .80,
            "deployment_actuator_endpoint_distance_metres": actuator_length,
            "drag_links_metres": [link_length, link_length],
            "drag_collar_distance_from_shoe_pivot_metres": collar_offset,
            "bearing_to_gland_clearance_metres": bearing_gland_clearance,
            "root_bearing_to_housing_clearance_metres": root_housing_clearance,
            "rear_bulkhead_trunnion_clearance_metres": [.34, .32],
            "drag_endpoint_joints": "spherical bearing at hull and piston collar; pinned folding knee",
            "bay_y_limits_metres": [rear, front], "door_bottom_stowed_metres": -.5475,
            "compression_vertical_metres": .30 if pose == "compressed" else 0}


def setup(pose, width, samples):
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    for collection in bpy.data.collections:
        if collection.name.startswith(("40 |", "ARCHIVE |", "STAGE |")):
            collection.hide_render = True
    art.CACHE.clear()
    art.materials()
    receipts = [mechanism(*pad, pose) for pad in PADS]
    bpy.context.view_layer.update()
    # Bounding boxes are a conservative packaging diagnostic, NOT a mesh
    # collision/swept-volume certificate. Preserve and report any failures.
    moving_terms = ("oleo housing", "polished piston", "machined gland", "piston wiper",
                    "shoe", "sole", "folding drag link", "drag link pin", "drag link piston",
                    "drag link spherical", "drag link bearing", "deployment actuator", "actuator eye")
    for receipt in receipts:
        moving = [o for o in bpy.data.objects if o.type == "MESH"
                  and o.name.startswith(receipt["name"] + " ")
                  and any(term in o.name for term in moving_terms)]
        points = [o.matrix_world @ Vector(corner) for o in moving for corner in o.bound_box]
        lower = [min(p[i] for p in points) for i in range(3)]
        upper = [max(p[i] for p in points) for i in range(3)]
        receipt["moving_mesh_aabb_metres"] = {"min": lower, "max": upper}
        if pose == "stowed":
            x = receipt["pad_center_blender_metres"][0]
            rear, front = receipt["bay_y_limits_metres"]
            roof = -.11 if receipt["name"] == "FORE" else .24
            receipt["aabb_inside_proposed_bay"] = (
                lower[0] >= x - .445 and upper[0] <= x + .445
                and lower[1] >= rear + .03 and upper[1] <= front - .03
                and lower[2] >= -.5125 and upper[2] <= roof)
    art.assembly("STUDY | isolated review lighting", 9)
    art.area("Gear warm key", (6, 8, -7), (0, 1, -.2), 2100, 8, (1, .88, .72))
    art.area("Gear cool rim", (-9, -8, -2), (0, -2, 0), 2600, 7, (.48, .68, 1))
    art.area("Top softbox", (2, 9, 13), (0, 0, 0), 5000, 11, (.82, .89, 1))
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.resolution_x = width
    scene.render.resolution_y = width * 9 // 16
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.use_stamp = False
    scene.render.use_stamp_filename = False
    scene.world.node_tree.nodes["Background"].inputs["Color"].default_value = (.018, .024, .04, 1)
    scene.world.node_tree.nodes["Background"].inputs["Strength"].default_value = .30
    scene["gear_study"] = "Separate articulation proposal; no flight integration or collision certification"
    return receipts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--samples", type=int, default=32)
    parser.add_argument("--finalize-only", action="store_true")
    parser.add_argument("--checkpoint", action="store_true", help="Only deployed ship and deployed/stowed mechanism cutaways")
    parser.add_argument("--poses", nargs="+", choices=("deployed", "stowed", "compressed"),
                        default=["deployed", "stowed", "compressed"])
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    if args.finalize_only:
        finalize_images()
        return
    if args.checkpoint:
        args.poses = ["deployed", "stowed"]
    if not 640 <= args.width <= 3840 or not 1 <= args.samples <= 256:
        raise ValueError("Invalid review dimensions/samples")
    # Lock to reviewed immutable descriptor positions, without editing C++.
    descriptor = (ROOT / "src/craft_frame.cpp").read_text()
    for expected in ("{0, -1328, -6700}", "{-3500, -1328, 2700}", "{3500, -1328, 2700}"):
        if expected not in descriptor:
            raise ValueError("Craft support recipe changed; review study coordinates")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    before = digest(SOURCE)
    manifest = {"schema_version": 1, "study_revision": STUDY_REVISION,
                "script_sha256": digest(Path(__file__)),
                "source_kind": "code-authored", "license": "BSD-3-Clause",
                "attribution": "Apsis Drift contributors", "source": "assets/visual/hero-ship.blend",
                "source_sha256": before, "script": "tools/review_landing_gear.py",
                "blender_version": bpy.app.version_string, "units": "metres",
                "body_from_blender": "(x,y,z) -> (x,z,-y)", "craft_frame": {"id": 1, "version": 1},
                "render": {"engine": "Cycles CPU", "samples": args.samples, "width": args.width},
                "limitations": ["No flight/gear state integration or animation timeline",
                  "Proposed enlarged cassettes, not canonical hull changes",
                  "No swept-volume collision, strength, buckling or actuator-force certification",
                  "All three 300mm compressions are illustrative poses, not a suspension solver",
                  "No added propulsion; original lift-engine art retained unchanged"], "poses": []}
    for pose in args.poses:
        receipt = setup(pose, args.width, args.samples)
        views = [("ship", (16, 23, -10), (0, 0, .2), 51),
                 ("mechanism", (6.8, .4, -3.3), (3.5, -2.7, -.40), 59),
                 ("cutaway", (6.8, .4, -3.3), (3.5, -2.7, -.40), 59)]
        if pose == "compressed":
            views = views[1:]
        if args.checkpoint:
            views = [view for view in views if view[0] == "cutaway" or (pose == "deployed" and view[0] == "ship")]
        for view, camera, target, lens in views:
            removed = []
            if view == "cutaway":
                for obj in bpy.data.objects:
                    if obj.name.startswith("STARBOARD") and any(part in obj.name for part in
                            ("bay cheek", "bay end bulkhead", "bay roof", "hinged bay door", "door hinge", "service panel", "gear index stripe", "cheek captive bolt")):
                        obj.hide_render = True
                        removed.append(obj.name)
            art.camera("STUDY camera " + view, camera, target, lens)
            stem = "gear-" + pose + "-" + view
            bpy.context.scene.render.filepath = str(OUTPUT / (stem + ".png"))
            bpy.ops.render.render(write_still=True)
            scrub_png_metadata(OUTPUT / (stem + ".png"))
            metadata = {"pose": pose, "view": view, "camera": camera, "target": target,
                        "cutaway_hidden_objects": removed,
                        "png_sha256": digest(OUTPUT / (stem + ".png")),
                        "png_metadata": "Text/EXIF removed; pixel and color chunks preserved verbatim",
                        "gear": receipt, "image": stem + ".png"}
            (OUTPUT / (stem + ".json")).write_text(json.dumps({**manifest, **metadata}, indent=2) + "\n")
            manifest["poses"].append(metadata)
            for object_name in removed:
                bpy.data.objects[object_name].hide_render = False
        bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT / ("gear-" + pose + ".blend")), compress=True)
    if digest(SOURCE) != before:
        raise RuntimeError("Canonical source unexpectedly changed")
    manifest["canonical_source_unchanged"] = True
    (OUTPUT / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    finalize_images()
    print("GEAR STUDY COMPLETE", OUTPUT)


if __name__ == "__main__":
    main()
