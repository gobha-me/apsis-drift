#!/usr/bin/env python3
"""Build the Apsis Drift cockpit, courier, and Origin Station design masters.

Run with:
  blender --background --python tools/build_visual_designs.py

The script is intentionally deterministic and uses only Blender primitives and
repository-authored geometry.  It writes editable .blend masters, GLB exchange
files, and PNG previews under assets/visual/designs/.
"""

from __future__ import annotations

import math
import os
from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets" / "visual"
OUTPUT.mkdir(parents=True, exist_ok=True)

ACTIVE_COLLECTION: bpy.types.Collection | None = None
ACTIVE_PARENT: bpy.types.Object | None = None


def clear_scene() -> None:
    global ACTIVE_COLLECTION, ACTIVE_PARENT
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        if collection.name != "Collection":
            bpy.data.collections.remove(collection)
    base = bpy.data.collections.get("Collection")
    if base is None:
        base = bpy.data.collections.new("Collection")
        bpy.context.scene.collection.children.link(base)
    base.name = "DESIGN"
    ACTIVE_COLLECTION = base
    ACTIVE_PARENT = None


def collection(name: str) -> bpy.types.Collection:
    global ACTIVE_COLLECTION
    coll = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(coll)
    ACTIVE_COLLECTION = coll
    return coll


def move_to_active(obj: bpy.types.Object) -> bpy.types.Object:
    if ACTIVE_COLLECTION is not None:
        for old in list(obj.users_collection):
            old.objects.unlink(obj)
        ACTIVE_COLLECTION.objects.link(obj)
    if ACTIVE_PARENT is not None:
        obj.parent = ACTIVE_PARENT
    return obj


def empty(name: str, location=(0.0, 0.0, 0.0)) -> bpy.types.Object:
    obj = bpy.data.objects.new(name, None)
    obj.location = location
    move_to_active(obj)
    return obj


def set_parent(parent: bpy.types.Object | None) -> None:
    global ACTIVE_PARENT
    ACTIVE_PARENT = parent


def material(name: str, color, metallic=0.0, roughness=0.45,
             emission=None, emission_strength=0.0, alpha=1.0):
    existing = bpy.data.materials.get(name)
    if existing is not None:
        return existing
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*color, alpha)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if emission is not None:
        bsdf.inputs["Emission Color"].default_value = (*emission, 1.0)
        bsdf.inputs["Emission Strength"].default_value = emission_strength
    if alpha < 1.0:
        bsdf.inputs["Alpha"].default_value = alpha
        mat.surface_render_method = "DITHERED"
    return mat


def add_mat(obj, mat):
    if mat is not None:
        obj.data.materials.append(mat)
    return obj


def bevel(obj, width=0.08, segments=2):
    if width > 0.0:
        mod = obj.modifiers.new("Edge softening", "BEVEL")
        mod.width = width
        mod.segments = segments
    return obj


def cube(name, location, dimensions, mat=None, rotation=(0.0, 0.0, 0.0),
         bevel_width=0.06):
    bpy.ops.mesh.primitive_cube_add(location=location, rotation=rotation)
    obj = move_to_active(bpy.context.object)
    obj.name = name
    obj.dimensions = dimensions
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bevel(obj, bevel_width)
    return add_mat(obj, mat)


def cylinder(name, location, radius, depth, mat=None,
             rotation=(0.0, 0.0, 0.0), vertices=32, bevel_width=0.04):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices, radius=radius, depth=depth,
        location=location, rotation=rotation)
    obj = move_to_active(bpy.context.object)
    obj.name = name
    bevel(obj, bevel_width)
    return add_mat(obj, mat)


def sphere(name, location, scale, mat=None, segments=32, rings=16):
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=segments, ring_count=rings, location=location)
    obj = move_to_active(bpy.context.object)
    obj.name = name
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bpy.ops.object.shade_smooth()
    return add_mat(obj, mat)


def torus(name, location, major_radius, minor_radius, mat=None,
          rotation=(0.0, 0.0, 0.0), major_segments=64, minor_segments=12):
    bpy.ops.mesh.primitive_torus_add(
        major_radius=major_radius, minor_radius=minor_radius,
        major_segments=major_segments, minor_segments=minor_segments,
        location=location, rotation=rotation)
    obj = move_to_active(bpy.context.object)
    obj.name = name
    bpy.ops.object.shade_smooth()
    return add_mat(obj, mat)


def tube(name, start, end, radius, mat=None, vertices=16):
    a, b = Vector(start), Vector(end)
    direction = b - a
    obj = cylinder(name, (a + b) * 0.5, radius, direction.length, mat,
                   vertices=vertices, bevel_width=0.0)
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = Vector((0.0, 0.0, 1.0)).rotation_difference(direction)
    return obj


def cable(name, points, radius, mat=None, bevel_resolution=2):
    curve = bpy.data.curves.new(name + "_curve", type="CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = radius
    curve.bevel_resolution = bevel_resolution
    spline = curve.splines.new("BEZIER")
    spline.bezier_points.add(len(points) - 1)
    for point, value in zip(spline.bezier_points, points):
        point.co = value
        point.handle_left_type = "AUTO"
        point.handle_right_type = "AUTO"
    obj = bpy.data.objects.new(name, curve)
    move_to_active(obj)
    add_mat(obj, mat)
    return obj


def text(name, body, location, size, mat, rotation=(math.pi / 2, 0.0, 0.0),
         align="CENTER"):
    curve = bpy.data.curves.new(name + "_font", type="FONT")
    curve.body = body
    curve.align_x = align
    curve.align_y = "CENTER"
    curve.size = size
    curve.extrude = size * 0.018
    curve.bevel_depth = size * 0.006
    obj = bpy.data.objects.new(name, curve)
    move_to_active(obj)
    obj.location = location
    obj.rotation_euler = rotation
    add_mat(obj, mat)
    return obj


def ring_lights(prefix, center, radius, count, light_radius, mat,
                rotation_offset=0.0, axis="Z"):
    for i in range(count):
        angle = rotation_offset + math.tau * i / count
        if axis == "Z":
            loc = (center[0] + radius * math.cos(angle),
                   center[1] + radius * math.sin(angle), center[2])
        else:
            loc = (center[0] + radius * math.cos(angle), center[1],
                   center[2] + radius * math.sin(angle))
        sphere(f"{prefix}_{i:02d}", loc, (light_radius,) * 3, mat,
               segments=12, rings=6)


def wedge(name, location, dimensions, mat, slope=0.35,
          rotation=(0.0, 0.0, 0.0)):
    x, y, z = (v * 0.5 for v in dimensions)
    verts = [(-x, -y, -z), (x, -y, -z), (x, y, -z), (-x, y, -z),
             (-x * slope, -y, z), (x * slope, -y, z),
             (x, y, z), (-x, y, z)]
    faces = [(0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1),
             (1, 5, 6, 2), (2, 6, 7, 3), (4, 0, 3, 7)]
    mesh = bpy.data.meshes.new(name + "_mesh")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    move_to_active(obj)
    obj.location = location
    obj.rotation_euler = rotation
    bevel(obj, min(dimensions) * 0.08, 2)
    add_mat(obj, mat)
    return obj


def create_palette():
    return {
        "hull": material("Hull ceramic / warm ivory", (0.32, 0.34, 0.34), 0.55, 0.28),
        "dark": material("Charcoal structure", (0.025, 0.034, 0.038), 0.72, 0.24),
        "panel": material("Instrument graphite", (0.055, 0.075, 0.082), 0.35, 0.38),
        "rubber": material("Pressure seal", (0.012, 0.016, 0.018), 0.0, 0.72),
        "copper": material("Thermal copper", (0.38, 0.13, 0.045), 0.78, 0.25),
        "orange": material("Safety orange", (0.62, 0.13, 0.025), 0.25, 0.34),
        "white": material("Exterior ceramic", (0.55, 0.57, 0.56), 0.42, 0.31),
        "glass": material("Canopy glass", (0.02, 0.12, 0.17), 0.15, 0.10,
                          emission=(0.01, 0.05, 0.07), emission_strength=0.35, alpha=0.34),
        "cyan": material("Navigation cyan", (0.01, 0.31, 0.40), 0.1, 0.22,
                         emission=(0.01, 0.75, 1.0), emission_strength=5.0),
        "amber": material("Status amber", (0.52, 0.22, 0.025), 0.1, 0.26,
                          emission=(1.0, 0.28, 0.015), emission_strength=5.0),
        "red": material("Warning red", (0.46, 0.018, 0.012), 0.12, 0.25,
                        emission=(1.0, 0.01, 0.005), emission_strength=4.0),
        "green": material("Ready green", (0.015, 0.26, 0.12), 0.05, 0.3,
                          emission=(0.02, 0.85, 0.26), emission_strength=4.0),
        "solar": material("Solar cell blue", (0.008, 0.035, 0.11), 0.62, 0.18,
                          emission=(0.005, 0.02, 0.06), emission_strength=0.25),
        "radiator": material("Radiator foil", (0.16, 0.18, 0.19), 0.8, 0.24),
        "seat": material("Seat fabric", (0.09, 0.105, 0.10), 0.0, 0.86),
    }


def add_camera(name, location, target, lens=48.0):
    bpy.ops.object.camera_add(location=location)
    cam = bpy.context.object
    cam.name = name
    direction = Vector(target) - cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    cam.data.lens = lens
    cam.data.sensor_width = 36
    bpy.context.scene.camera = cam
    return cam


def add_area(name, location, energy, color, size, target):
    bpy.ops.object.light_add(type="AREA", location=location)
    lamp = bpy.context.object
    lamp.name = name
    lamp.data.energy = energy
    lamp.data.color = color
    lamp.data.shape = "DISK"
    lamp.data.size = size
    lamp.rotation_euler = (Vector(target) - lamp.location).to_track_quat("-Z", "Y").to_euler()
    return lamp


def setup_scene(world_strength=0.014, resolution=(960, 720)):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x, scene.render.resolution_y = resolution
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.film_transparent = False
    scene.render.image_settings.color_depth = "8"
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.render.engine = "BLENDER_EEVEE"
    scene.world.color = (world_strength, world_strength * 1.1, world_strength * 1.3)
    scene.render.resolution_percentage = 100


def metadata(kind, dimensions, notes):
    scene = bpy.context.scene
    scene["apsis_asset_family"] = "Wayfinder industrial design language"
    scene["apsis_design_kind"] = kind
    scene["apsis_units"] = "metres"
    scene["apsis_dimensions"] = dimensions
    scene["apsis_notes"] = notes
    scene["apsis_generator"] = "tools/build_visual_designs.py"
    scene["apsis_generator_version"] = 1


def save_and_export(stem):
    blend_path = OUTPUT / f"{stem}.blend"
    glb_path = OUTPUT / f"{stem}.glb"
    preview_path = OUTPUT / f"{stem}-preview.png"
    bpy.context.scene.render.filepath = str(preview_path)
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path), compress=True)
    bpy.ops.render.render(write_still=True)
    bpy.ops.export_scene.gltf(
        filepath=str(glb_path), export_format="GLB", export_apply=True,
        export_cameras=False, export_lights=False,
        export_extras=True, export_yup=True)


def build_cockpit():
    clear_scene()
    p = create_palette()
    coll = collection("COCKPIT / PILOT CELL")
    root = empty("COCKPIT_ROOT")
    set_parent(root)

    # Pressure shell and structural floor; open front is the exterior viewport.
    cube("Pressure floor", (0, 0.4, 0.05), (7.8, 8.6, 0.22), p["dark"], bevel_width=0.12)
    cube("Raised central deck", (0, 0.8, 0.32), (2.3, 6.4, 0.28), p["panel"], bevel_width=0.10)
    for side in (-1, 1):
        cube(f"Side wall {side:+d}", (side * 3.82, 0.45, 2.1),
             (0.24, 8.4, 4.2), p["hull"], bevel_width=0.09)
        cube(f"Wall crash rail {side:+d}", (side * 3.56, 0.45, 1.26),
             (0.22, 7.8, 0.28), p["orange"], bevel_width=0.07)
        for y in (-2.8, -1.2, 0.4, 2.0, 3.2):
            tube(f"Wall rib {side:+d} {y:+.1f}",
                 (side * 3.55, y, 0.25), (side * 3.55, y, 4.0), 0.075, p["copper"])
    cube("Rear pressure bulkhead", (0, -3.72, 2.1), (7.7, 0.22, 4.2), p["hull"], bevel_width=0.12)
    cylinder("Rear iris hatch", (0, -3.86, 2.0), 1.12, 0.18, p["dark"],
             rotation=(math.pi / 2, 0, 0), vertices=32)
    torus("Rear hatch seal", (0, -3.98, 2.0), 0.84, 0.10, p["orange"],
          rotation=(math.pi / 2, 0, 0))
    for i in range(8):
        a = math.tau * i / 8
        cube(f"Hatch dog {i:02d}", (0.98 * math.cos(a), -4.04, 2.0 + 0.98 * math.sin(a)),
             (0.16, 0.11, 0.28), p["copper"], rotation=(0, -a, 0), bevel_width=0.03)

    # Canopy frame creates a wide central viewport with strong low-resolution landmarks.
    for side in (-1, 1):
        tube(f"Canopy lower longeron {side:+d}", (side * 3.5, 2.0, 1.28),
             (side * 2.45, 4.0, 1.9), 0.14, p["hull"])
        tube(f"Canopy upper longeron {side:+d}", (side * 2.7, 1.9, 4.0),
             (side * 1.7, 4.05, 3.2), 0.13, p["hull"])
        tube(f"Canopy A pillar {side:+d}", (side * 2.43, 4.0, 1.88),
             (side * 1.68, 4.04, 3.2), 0.16, p["orange"])
    tube("Canopy crown", (-1.72, 4.03, 3.2), (1.72, 4.03, 3.2), 0.16, p["hull"])
    tube("Canopy sight divider", (0, 4.02, 1.55), (0, 4.04, 3.25), 0.055, p["dark"])
    wedge("Left canopy pane", (-1.12, 4.09, 2.5), (2.0, 0.035, 1.45), p["glass"], slope=0.70)
    wedge("Right canopy pane", (1.12, 4.09, 2.5), (2.0, 0.035, 1.45), p["glass"], slope=0.70)

    # Pilot acceleration couch and five-point harness.
    cube("Seat pedestal", (0, -0.45, 0.65), (1.36, 1.85, 0.36), p["dark"], bevel_width=0.16)
    cube("Seat pan", (0, -0.18, 0.95), (1.24, 1.05, 0.24), p["seat"],
         rotation=(math.radians(-6), 0, 0), bevel_width=0.14)
    cube("Seat back", (0, -0.82, 1.88), (1.30, 0.28, 1.84), p["seat"],
         rotation=(math.radians(-9), 0, 0), bevel_width=0.17)
    cube("Head restraint", (0, -1.05, 2.92), (0.92, 0.34, 0.62), p["panel"], bevel_width=0.16)
    for side in (-1, 1):
        cube(f"Shoulder wing {side:+d}", (side * 0.72, -0.84, 2.25),
             (0.28, 0.42, 1.0), p["dark"], bevel_width=0.12)
        cable(f"Harness shoulder {side:+d}",
              [(side * 0.36, -1.02, 2.78), (side * 0.28, -0.56, 2.0),
               (side * 0.16, -0.38, 1.32)], 0.045, p["orange"])
        cube(f"Arm rest {side:+d}", (side * 0.86, -0.05, 1.48),
             (0.22, 1.10, 0.16), p["panel"], bevel_width=0.08)

    # Layered consoles, angled inward for the single-seat flight cell.
    for side in (-1, 1):
        x = side * 2.35
        wedge(f"Side console shell {side:+d}", (x, 0.20, 0.95),
              (2.25, 4.45, 1.40), p["panel"], slope=0.80,
              rotation=(0, 0, side * math.radians(-3)))
        for row in range(5):
            y = -1.25 + row * 0.66
            cube(f"Side display {side:+d} {row:02d}",
                 (x - side * 0.16, y, 1.65 + 0.025 * row),
                 (1.22, 0.46, 0.045), p["cyan"] if row in (0, 3) else p["dark"],
                 rotation=(math.radians(4), 0, 0), bevel_width=0.035)
            for col2 in range(4):
                cube(f"Side key {side:+d} {row:02d} {col2:02d}",
                     (x - side * (0.78 - col2 * 0.22), y - 0.31, 1.61),
                     (0.12, 0.12, 0.07), p["amber"] if col2 == row % 4 else p["hull"],
                     bevel_width=0.025)
        # Twin stick: right is flight, left is translation.
        tube(f"Control stick {side:+d}", (side * 0.95, 0.20, 1.55),
             (side * 1.02, 0.34, 2.15), 0.055, p["dark"])
        cube(f"Control grip {side:+d}", (side * 1.03, 0.35, 2.20),
             (0.19, 0.22, 0.34), p["rubber"], bevel_width=0.08)
        sphere(f"Stick status {side:+d}", (side * 1.03, 0.25, 2.34),
               (0.035, 0.035, 0.035), p["green"], segments=10, rings=5)

    # Forward glareshield and three rugged, replaceable display modules.
    wedge("Forward instrument brow", (0, 2.12, 1.84), (5.30, 1.10, 0.62), p["dark"], slope=0.82)
    for index, (x, label) in enumerate(((-1.55, "NAV"), (0, "FLIGHT"), (1.55, "SYSTEM"))):
        cube(f"MFD bezel {label}", (x, 2.40, 2.26), (1.38, 0.16, 1.05), p["panel"],
             rotation=(math.radians(76), 0, 0), bevel_width=0.10)
        cube(f"MFD screen {label}", (x, 2.31, 2.29), (1.12, 0.035, 0.76), p["cyan"],
             rotation=(math.radians(76), 0, 0), bevel_width=0.035)
        text(f"MFD label {label}", label, (x, 2.25, 2.31), 0.14, p["amber"],
             rotation=(math.radians(76), 0, 0))
        for key in range(4):
            cube(f"MFD key {label} {key}", (x - 0.42 + key * 0.28, 2.19, 1.92),
                 (0.13, 0.08, 0.09), p["hull"], bevel_width=0.02)
    # Physical horizon and emergency controls retain meaning if screens fail.
    cylinder("Backup horizon", (0, 2.08, 2.95), 0.30, 0.10, p["dark"],
             rotation=(math.pi / 2, 0, 0), vertices=32)
    cube("Horizon bar", (0, 2.01, 2.95), (0.40, 0.035, 0.055), p["amber"], bevel_width=0.02)
    for x, label in ((-0.48, "ABRT"), (0.48, "DOCK")):
        cylinder(f"Guarded control {label}", (x, 1.94, 3.00), 0.13, 0.12,
                 p["red"] if label == "ABRT" else p["green"],
                 rotation=(math.pi / 2, 0, 0), vertices=20)
        torus(f"Control guard {label}", (x, 1.87, 3.00), 0.19, 0.025, p["copper"],
              rotation=(math.pi / 2, 0, 0), major_segments=24, minor_segments=8)

    # Pedals, overhead power panel, cabin utilities, and visible service routing.
    for side in (-1, 1):
        tube(f"Pedal strut {side:+d}", (side * 0.38, 1.15, 0.40),
             (side * 0.48, 1.78, 0.62), 0.045, p["copper"])
        cube(f"Pedal {side:+d}", (side * 0.49, 1.80, 0.66),
             (0.38, 0.36, 0.10), p["rubber"], rotation=(math.radians(-20), 0, 0), bevel_width=0.05)
    cube("Overhead panel", (0, 1.25, 3.88), (2.65, 2.15, 0.16), p["panel"],
         rotation=(math.radians(5), 0, 0), bevel_width=0.10)
    for row in range(3):
        for col2 in range(7):
            x = -1.03 + col2 * 0.34
            y = 0.65 + row * 0.45
            cylinder(f"Overhead breaker {row:02d} {col2:02d}", (x, y, 3.77),
                     0.055, 0.07, p["amber"] if (row + col2) % 5 == 0 else p["hull"],
                     vertices=12)
    for side in (-1, 1):
        cylinder(f"Oxygen bottle {side:+d}", (side * 3.28, -2.45, 1.08),
                 0.25, 1.12, p["orange"], vertices=24)
        cable(f"Cabin conduit cyan {side:+d}",
              [(side * 3.37, -3.2, 3.1), (side * 3.38, -1.0, 3.2),
               (side * 3.25, 1.6, 2.8)], 0.035, p["cyan"])
        cable(f"Cabin conduit copper {side:+d}",
              [(side * 3.20, -3.2, 3.0), (side * 3.22, -0.8, 3.05),
               (side * 3.06, 1.5, 2.7)], 0.055, p["copper"])
    text("Cockpit maker plate", "APSIS / WAYFINDER-1", (0, -3.56, 3.32),
         0.20, p["amber"], rotation=(math.pi / 2, 0, 0))

    # The preview is a rear cutaway. The complete bulkhead remains present and
    # editable in the master but does not hide the flight cell in the render.
    for obj in bpy.context.scene.objects:
        if (obj.name.startswith("Rear ") or obj.name.startswith("Hatch dog") or
                obj.name == "Cockpit maker plate"):
            obj.hide_render = True

    set_parent(None)
    setup_scene(world_strength=0.006)
    metadata("cockpit", "7.8 m wide x 8.6 m long x 4.2 m high",
             "Single-seat orbital/atmospheric flight cell; viewport and three MFDs align with the terminal cockpit hierarchy.")
    add_camera("Cockpit presentation camera", (0, -6.35, 5.25), (0, 1.05, 1.42), 39)
    add_area("Cockpit key", (0, -2.8, 5.6), 1250, (0.38, 0.65, 1.0), 4.0, (0, 0.3, 1.5))
    add_area("Viewport spill", (0, 4.8, 3.0), 1800, (0.15, 0.55, 1.0), 3.0, (0, 1.2, 1.8))
    add_area("Warm console fill", (-3.0, 0.0, 2.8), 700, (1.0, 0.19, 0.04), 2.0, (0, 0.0, 1.5))
    save_and_export("wayfinder-cockpit")


def build_ship():
    clear_scene()
    p = create_palette()
    collection("WAYFINDER / ORBITAL COURIER")
    root = empty("WAYFINDER_SHIP_ROOT")
    set_parent(root)

    # 28 m reusable courier: lifting body, blunt atmospheric belly, vacuum hardware.
    sphere("Primary pressure hull", (0, 0.2, 1.65), (2.55, 6.4, 1.82), p["white"], 48, 24)
    sphere("Forward flight deck", (0, 5.20, 1.78), (2.08, 2.15, 1.45), p["hull"], 40, 20)
    wedge("Ventral heat shield", (0, 0.55, 0.02), (5.15, 11.7, 0.55), p["dark"], slope=0.75)
    wedge("Canopy visor", (0, 6.18, 2.25), (3.05, 1.18, 1.08), p["glass"], slope=0.62,
          rotation=(math.radians(8), 0, 0))
    tube("Canopy center mullion", (0, 6.78, 1.86), (0, 5.62, 2.80), 0.07, p["orange"])
    for side in (-1, 1):
        tube(f"Canopy edge {side:+d}", (side * 1.48, 6.42, 1.86),
             (side * 1.05, 5.58, 2.85), 0.085, p["dark"])

    # Broad chines/winglets carry atmospheric lift and make the silhouette readable.
    for side in (-1, 1):
        wedge(f"Lifting chine {side:+d}", (side * 3.25, 0.15, 0.82),
              (3.25, 8.7, 0.62), p["hull"], slope=0.18,
              rotation=(0, 0, side * math.radians(-3)))
        wedge(f"Outer wing {side:+d}", (side * 5.05, -0.70, 0.65),
              (2.85, 5.6, 0.34), p["dark"], slope=0.08,
              rotation=(0, 0, side * math.radians(-7)))
        cube(f"Wing edge orange {side:+d}", (side * 5.95, -0.15, 0.72),
             (0.18, 4.35, 0.22), p["orange"],
             rotation=(0, 0, side * math.radians(-9)), bevel_width=0.05)
        # Articulated VTOL / orbital engine nacelle.
        sphere(f"Engine nacelle {side:+d}", (side * 3.1, -4.70, 1.30),
               (1.15, 2.15, 1.06), p["dark"], 32, 16)
        cylinder(f"Main engine bell {side:+d}", (side * 3.1, -6.34, 1.28),
                 0.87, 1.10, p["copper"], rotation=(math.pi / 2, 0, 0), vertices=32)
        cylinder(f"Engine throat {side:+d}", (side * 3.1, -6.91, 1.28),
                 0.48, 0.12, p["cyan"], rotation=(math.pi / 2, 0, 0), vertices=24)
        ring_lights(f"Bell fiducial {side:+d}", (side * 3.1, -6.99, 1.28),
                    0.68, 8, 0.045, p["amber"], axis="Y")
        for pod_y in (3.6, -2.8):
            cube(f"RCS pod {side:+d} {pod_y:+.1f}", (side * 2.66, pod_y, 2.74),
                 (0.55, 0.72, 0.42), p["panel"], bevel_width=0.12)
            for port in range(3):
                cylinder(f"RCS nozzle {side:+d} {pod_y:+.1f} {port}",
                         (side * 2.97, pod_y - 0.22 + port * 0.22, 2.76),
                         0.075, 0.12, p["copper"], rotation=(0, math.pi / 2, 0), vertices=12)

    # Cargo spine, FTL radiator fins, dorsal docking collar, and service detail.
    cube("Dorsal equipment spine", (0, -0.55, 3.33), (1.22, 8.0, 0.66), p["panel"], bevel_width=0.16)
    for y in (-3.15, -1.55, 0.05, 1.65, 3.25):
        cube(f"Spine access bay {y:+.2f}", (0, y, 3.71), (0.94, 1.12, 0.09), p["hull"], bevel_width=0.055)
        for x in (-0.31, 0.31):
            sphere(f"Spine latch {y:+.2f} {x:+.2f}", (x, y, 3.78),
                   (0.045,) * 3, p["amber"], 10, 5)
    cylinder("Dorsal docking tunnel", (0, 1.35, 4.05), 0.82, 0.72, p["hull"], vertices=32)
    torus("Androgynous docking collar", (0, 1.35, 4.43), 0.72, 0.12, p["orange"], major_segments=48)
    ring_lights("Docking alignment light", (0, 1.35, 4.55), 0.57, 12, 0.045, p["green"])
    cylinder("Docking hatch", (0, 1.35, 4.49), 0.53, 0.08, p["dark"], vertices=32)
    for side in (-1, 1):
        wedge(f"FTL heat fin {side:+d}", (side * 1.52, -2.05, 3.56),
              (1.35, 5.1, 0.18), p["radiator"], slope=0.15,
              rotation=(0, math.radians(side * 14), 0))
        for stripe in range(7):
            cube(f"Heat fin stripe {side:+d} {stripe}",
                 (side * (1.08 + stripe * 0.14), -2.05, 3.68),
                 (0.035, 4.35, 0.035), p["copper"], bevel_width=0.01)

    # Landing gear is visibly mechanical and can later become an animation rig.
    for index, (x, y) in enumerate(((0, 4.3), (-3.0, -2.65), (3.0, -2.65))):
        tube(f"Landing strut {index}", (x, y, 0.25), (x * 1.15, y + (0.45 if y > 0 else -0.2), -1.05),
             0.13, p["copper"])
        tube(f"Landing drag brace {index}", (x, y - 0.55, 0.36),
             (x * 1.15, y + (0.45 if y > 0 else -0.2), -0.85), 0.075, p["hull"])
        cube(f"Landing foot {index}", (x * 1.15, y + (0.50 if y > 0 else -0.2), -1.14),
             (0.78, 1.08, 0.16), p["dark"], bevel_width=0.08)
    # Hull panel lines, thermal tiles, rescue markings, antennae.
    for i, y in enumerate((-4.4, -3.1, -1.8, -0.5, 0.8, 2.1, 3.4, 4.7)):
        torus(f"Hull frame band {i:02d}", (0, y, 1.65), 2.38 if abs(y) < 3.5 else 1.85,
              0.035, p["dark"], rotation=(math.pi / 2, 0, 0), major_segments=48, minor_segments=8)
    for side in (-1, 1):
        for i in range(6):
            cube(f"Thermal tile {side:+d} {i:02d}",
                 (side * (0.55 + (i % 2) * 0.62), -2.9 + (i // 2) * 0.82, -0.31),
                 (0.52, 0.68, 0.055), p["panel"], bevel_width=0.025)
        tube(f"Whip antenna {side:+d}", (side * 1.0, -1.8, 3.80),
             (side * 1.22, -1.85, 5.0), 0.018, p["copper"], vertices=8)
    cube("Rescue stripe", (0, 5.15, 3.06), (2.8, 0.42, 0.15), p["orange"], bevel_width=0.05)
    text("Ship dorsal identity", "WAYFINDER 01", (0, 0.0, 4.08), 0.34, p["amber"], rotation=(0, 0, 0))

    set_parent(None)
    setup_scene(world_strength=0.004)
    metadata("ship", "12.2 m wide x 28 m long x 6.0 m landed height",
             "Reusable single-seat courier with atmospheric lifting body, twin orbital engines, dorsal station collar, and explicit three-point landing gear.")
    add_camera("Ship presentation camera", (18.5, -22.0, 13.0), (0, -0.3, 1.35), 52)
    add_area("Ship sun", (-8, -8, 22), 2800, (1.0, 0.72, 0.48), 7.0, (0, 0, 1))
    add_area("Ship rim", (12, 8, 8), 2100, (0.18, 0.45, 1.0), 6.0, (0, 0, 1.5))
    add_area("Ship belly bounce", (0, 0, -8), 900, (0.12, 0.26, 0.40), 5.0, (0, 0, 0))
    save_and_export("wayfinder-courier")


def build_station():
    clear_scene()
    p = create_palette()
    collection("ORIGIN STATION / APSE RING")
    root = empty("ORIGIN_STATION_ROOT")
    set_parent(root)

    # Station scale is 190 m across arrays, 84 m across the inhabited ring.
    torus("Habitat ring pressure hull", (0, 0, 0), 31.0, 3.35, p["hull"], major_segments=96, minor_segments=16)
    torus("Habitat ring dark chine", (0, 0, 0), 31.0, 3.62, p["dark"], major_segments=96, minor_segments=8)
    # Light and hull bands are repeated geometry, readable at distant LODs.
    for i in range(24):
        a = math.tau * i / 24
        loc = (31.0 * math.cos(a), 31.0 * math.sin(a), 0)
        cylinder(f"Ring module {i:02d}", loc, 3.75, 4.35, p["white"], vertices=16,
                 rotation=(0, math.pi / 2, a), bevel_width=0.12)
        sphere(f"Ring navigation lamp {i:02d}",
               (34.55 * math.cos(a), 34.55 * math.sin(a), 0.35),
               (0.22,) * 3, p["cyan"] if i % 2 else p["amber"], 12, 6)
    # Six spokes into a layered zero-g operations hub.
    for i in range(6):
        a = math.tau * i / 6
        inner = (6.0 * math.cos(a), 6.0 * math.sin(a), 0)
        outer = (27.6 * math.cos(a), 27.6 * math.sin(a), 0)
        tube(f"Primary spoke {i:02d}", inner, outer, 0.75, p["hull"], vertices=16)
        tube(f"Spoke utility rail A {i:02d}",
             (inner[0], inner[1], 0.78), (outer[0], outer[1], 0.78), 0.12, p["copper"], vertices=10)
        tube(f"Spoke utility rail B {i:02d}",
             (inner[0], inner[1], -0.78), (outer[0], outer[1], -0.78), 0.12, p["dark"], vertices=10)
    sphere("Operations hub", (0, 0, 0), (7.6, 7.6, 5.25), p["white"], 48, 24)
    torus("Hub equatorial brace", (0, 0, 0), 7.1, 0.42, p["orange"], major_segments=64)
    cylinder("Hub axial core", (0, 0, 0), 3.25, 22.0, p["panel"], vertices=32)
    for z in (-11.4, 11.4):
        sphere(f"Axial observation crown {z:+.1f}", (0, 0, z), (4.3, 4.3, 2.2), p["glass"], 32, 16)
        torus(f"Crown frame {z:+.1f}", (0, 0, z), 4.0, 0.25, p["hull"], major_segments=48)

    # Four docking piers; positive X is the canonical approach corridor.
    for i, a in enumerate((0, math.pi / 2, math.pi, 3 * math.pi / 2)):
        direction = Vector((math.cos(a), math.sin(a), 0))
        start = direction * 34.5
        end = direction * 49.0
        tube(f"Dock pier spine {i:02d}", start, end, 1.05, p["dark"], vertices=20)
        for offset in (-1.6, 1.6):
            tangent = Vector((-math.sin(a), math.cos(a), 0)) * offset
            tube(f"Dock pier truss {i:02d} {offset:+.1f}", start + tangent,
                 end + tangent, 0.20, p["hull"], vertices=10)
            for bay in range(4):
                q = start.lerp(end, (bay + 0.5) / 4) + tangent
                sphere(f"Pier lamp {i:02d} {offset:+.1f} {bay}", q,
                       (0.13,) * 3, p["green"], 10, 5)
        port = direction * 50.0
        cylinder(f"Docking vestibule {i:02d}", port, 3.2, 3.6, p["hull"],
                 rotation=(0, math.pi / 2, a), vertices=32)
        torus(f"Docking collar {i:02d}", direction * 52.0, 1.72, 0.24, p["orange"],
              rotation=(0, math.pi / 2, a), major_segments=48)
        cylinder(f"Docking seal {i:02d}", direction * 52.15, 1.48, 0.18, p["rubber"],
                 rotation=(0, math.pi / 2, a), vertices=32)
        ring_lights(f"Dock approach lights {i:02d}", direction * 52.30, 1.28, 12, 0.11,
                    p["green"] if i == 0 else p["cyan"], rotation_offset=a, axis="Y")

    # Long mast carries tanks, antennae and visually separates industrial zones.
    tube("North mast", (0, 0, 12), (0, 0, 43), 1.05, p["dark"], vertices=20)
    tube("South mast", (0, 0, -12), (0, 0, -38), 1.05, p["dark"], vertices=20)
    for z in (18, 25, 32):
        for i in range(4):
            a = math.tau * i / 4 + (z % 2) * 0.2
            loc = (3.0 * math.cos(a), 3.0 * math.sin(a), z)
            sphere(f"Propellant tank {z} {i}", loc, (1.55, 1.55, 3.0), p["hull"], 24, 12)
            tube(f"Tank feed {z} {i}", loc, (0.8 * math.cos(a), 0.8 * math.sin(a), z - 2.5),
                 0.10, p["copper"], vertices=10)
    for z in (-18, -26, -34):
        torus(f"Mast service ring {z}", (0, 0, z), 3.6, 0.35, p["hull"], major_segments=36)
        ring_lights(f"Mast lamps {z}", (0, 0, z), 3.6, 8, 0.13, p["amber"])

    # Two enormous solar wings, gridded as replaceable cell cassettes.
    for side in (-1, 1):
        tube(f"Solar boom {side:+d}", (side * 7.0, 0, 7.5),
             (side * 76.0, 0, 7.5), 0.48, p["hull"], vertices=16)
        for bay in range(6):
            cx = side * (18.0 + bay * 10.5)
            cube(f"Solar cassette {side:+d} {bay:02d}", (cx, 0, 7.5),
                 (9.4, 0.24, 15.0), p["solar"], bevel_width=0.08)
            cube(f"Solar cassette frame {side:+d} {bay:02d}", (cx, -0.15, 7.5),
                 (9.8, 0.12, 15.4), p["dark"], bevel_width=0.06)
            # Re-overlay cell face after rear frame so it reads in the preview.
            cube(f"Solar face {side:+d} {bay:02d}", (cx, -0.23, 7.5),
                 (9.2, 0.05, 14.8), p["solar"], bevel_width=0.02)
            for row in range(5):
                cube(f"Solar row bus {side:+d} {bay:02d} {row}",
                     (cx, -0.27, 1.7 + row * 2.9), (9.25, 0.025, 0.055), p["copper"], bevel_width=0.0)
            for col2 in range(3):
                cube(f"Solar col bus {side:+d} {bay:02d} {col2}",
                     (cx - 3.05 + col2 * 3.05, -0.27, 7.5), (0.045, 0.025, 14.7), p["hull"], bevel_width=0.0)
        cylinder(f"Solar rotary joint {side:+d}", (side * 11.2, 0, 7.5),
                 1.75, 2.0, p["orange"], rotation=(0, math.pi / 2, 0), vertices=24)

    # Thermal radiator wings at a different angle avoid a generic flat silhouette.
    for side in (-1, 1):
        for level, z in enumerate((-13.0, -22.0)):
            tube(f"Radiator boom {side:+d} {level}", (side * 2.0, 0, z),
                 (side * 24.0, 0, z), 0.32, p["hull"], vertices=12)
            for bay in range(3):
                x = side * (8.0 + bay * 7.0)
                cube(f"Radiator panel {side:+d} {level} {bay}", (x, 0, z),
                     (6.2, 0.18, 6.8), p["radiator"],
                     rotation=(math.radians(side * 18), 0, 0), bevel_width=0.07)
                for fin in range(5):
                    cube(f"Radiator fin {side:+d} {level} {bay} {fin}",
                         (x - side * 2.4 + side * fin * 1.2, -0.18, z),
                         (0.06, 0.06, 6.2), p["copper"],
                         rotation=(math.radians(side * 18), 0, 0), bevel_width=0.0)

    # Communications crown: dishes, range beacons, and a distinct zenith spear.
    for i in range(3):
        a = math.tau * i / 3
        base = Vector((5.0 * math.cos(a), 5.0 * math.sin(a), 39.0))
        bpy.ops.mesh.primitive_cone_add(vertices=32, radius1=2.25, radius2=0.35,
                                       depth=1.05, location=base)
        dish = move_to_active(bpy.context.object)
        dish.name = f"Comms dish {i:02d}"
        dish.rotation_euler = (math.radians(22), 0, a)
        add_mat(dish, p["white"])
        tube(f"Dish feed {i:02d}", base + Vector((0, 0, 0.4)),
             base + Vector((0.7 * math.cos(a), 0.7 * math.sin(a), 2.0)), 0.08, p["copper"], vertices=10)
    tube("Zenith beacon spear", (0, 0, 39), (0, 0, 52), 0.14, p["copper"], vertices=10)
    sphere("Zenith beacon", (0, 0, 52.2), (0.55, 0.55, 0.55), p["cyan"], 16, 8)
    text("Station identity", "ORIGIN / APSE RING", (0, -7.3, 2.0), 1.05, p["amber"],
         rotation=(math.pi / 2, 0, 0))

    set_parent(None)
    setup_scene(world_strength=0.012, resolution=(1100, 760))
    metadata("station", "190 m solar span x 105 m overall height x 104 m docking span",
             "Rotating 84 m habitat ring around a zero-g hub; four standardized docking piers, non-rotating utility mast, solar wings, radiators, tanks, and comms crown.")
    add_camera("Station presentation camera", (128, -155, 102), (0, 0, 3), 56)
    add_area("Station sun", (-75, -60, 120), 180000, (1.0, 0.72, 0.46), 24.0, (0, 0, 0))
    add_area("Station blue rim", (100, 70, 20), 130000, (0.12, 0.42, 1.0), 26.0, (0, 0, 2))
    add_area("Station underside", (-20, 10, -80), 60000, (0.10, 0.25, 0.40), 20.0, (0, 0, -5))
    save_and_export("origin-apse-station")


def main():
    only = os.environ.get("APSIS_DESIGN_ONLY", "").strip().lower()
    if only in ("", "cockpit"):
        build_cockpit()
    if only in ("", "ship", "courier"):
        build_ship()
    if only in ("", "station"):
        build_station()
    print(f"Apsis Drift visual designs written to {OUTPUT}")


if __name__ == "__main__":
    main()
