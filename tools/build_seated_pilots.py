#!/usr/bin/env python3
"""Original BSD-3-Clause procedural seated pilot studies; not production characters.

Run: blender -b --factory-startup --python tools/build_seated_pilots.py
Outputs ONLY to ignored build-godot/pilot-study. No downloaded art, textures,
human scans, trained assets, or external character libraries are used.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_hero_assets as art
import build_flight_cell
import flight_cell

OUT = ROOT / "build-godot" / "pilot-study"
PARENT = None


def parent_new(fn, *args, **kwargs):
    result = fn(*args, **kwargs)
    result.parent = PARENT
    return result


def box(*args, **kwargs):
    return parent_new(art.box, *args, **kwargs)


def sphere(*args, **kwargs):
    return parent_new(art.sphere, *args, **kwargs)


def beam(*args, **kwargs):
    return parent_new(art.beam, *args, **kwargs)


def wire(*args, **kwargs):
    return parent_new(art.wire, *args, **kwargs)


def band(name, a, b, width, material="harness"):
    """Flat webbing, not a round hose. Face lies along the chest's X/Z plane."""
    a, b = Vector(a), Vector(b)
    side = Vector((b.z-a.z, 0, a.x-b.x)).normalized() * width * .5
    verts = [a-side, a+side, b+side, b-side]
    result = parent_new(art.obj, name, art.mesh(name, verts, [(3, 2, 1, 0)]), mat=material)
    modifier = result.modifiers.new("Woven strap thickness", "SOLIDIFY")
    modifier.thickness = .004
    return result


def loft(name, sections, material="cloth", sides=32, wrinkle=0):
    """Closed elliptical suit tailoring sections in XY, smoothly joined in Z."""
    verts = []
    for row, (z, x, y, rx, ry) in enumerate(sections):
        for i in range(sides):
            angle = i*math.tau/sides
            irregular = 1 + wrinkle*math.sin(angle*5+row*.73)*math.sin(math.pi*row/(len(sections)-1))
            verts.append((x+rx*math.cos(angle)*irregular, y+ry*math.sin(angle)*irregular, z))
    faces = [tuple(range(sides-1, -1, -1))]
    for row in range(len(sections)-1):
        for i in range(sides):
            a = row*sides+i
            b = row*sides+(i+1) % sides
            faces.append((a, b, b+sides, a+sides))
    faces.append(tuple(range((len(sections)-1)*sides, len(sections)*sides)))
    result = parent_new(art.obj, name, art.mesh(name, verts, faces), mat=material)
    for face in result.data.polygons:
        face.use_smooth = len(face.vertices) == 4
    return result


def limb(name, a, b, radius, material="cloth", flatten=.90):
    """Authored articulated garment with taper, sewn longitudinal seam and cuffs."""
    a, b = Vector(a), Vector(b)
    axis = b-a
    length = axis.length
    q = Vector((0, 0, 1)).rotation_difference(axis.normalized())
    # Deliberate local fold structure, not random noisy displacement or capsules.
    profile = [(0, .78), (.025, .92), (.055, .98), (.075, .87), (.10, 1.0),
               (.13, .91), (.17, .98), (.24, .97), (.34, .96), (.48, .93),
               (.62, .90), (.72, .86), (.77, .91), (.80, .79), (.83, .88),
               (.86, .76), (.90, .86), (.94, .76), (.965, .82), (1, .76)]
    obj = loft(name, [(t*length, 0, 0, radius*r, radius*r*flatten)
                      for t, r in profile], material, wrinkle=.045)
    obj.location = a
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = q
    for sign in (-1, 1):
        seam = [a + q @ Vector((sign*radius*r*.82, radius*r*flatten*.58, t*length))
                for t, r in profile]
        wire(name+" stitched seam", seam, .0016, "seam")
    for t in (.105, .865, .94):
        parent_new(art.cyl, name+" flexible sewn cuff", a+axis*t,
                   radius*(.97 if t < .2 else .80), .009, "seam", axis=axis)
    return obj


def make_materials():
    art.materials()  # Seat context uses the established palette.
    palette = {
        "cloth": ((.21, .26, .27), 0, .89),
        "reinforced": ((.065, .087, .10), .08, .76),
        "seam": ((.105, .135, .14), 0, .92),
        "shell": ((.63, .69, .66), .25, .31),
        "gasket": ((.013, .023, .027), 0, .62),
        "visor": ((.016, .062, .080), .65, .19),
        "harness": ((.26, .19, .092), 0, .83),
        "hardware": ((.36, .42, .44), .85, .26),
        "mark": ((.81, .37, .10), .1, .52),
        "sole": ((.025, .034, .033), 0, .91),
    }
    for name, (color, metal, rough) in palette.items():
        mat = bpy.data.materials.new("Pilot / "+name)
        mat.use_nodes = True
        node = mat.node_tree.nodes.get("Principled BSDF")
        node.inputs["Base Color"].default_value = (*color, 1)
        node.inputs["Metallic"].default_value = metal
        node.inputs["Roughness"].default_value = rough
        art.MAT[name] = mat


def visor_patch(center):
    """Curved opaque optical visor; eye is behind it, shell opening has no glass stack."""
    verts = []
    rows, cols = 12, 32
    for j in range(rows+1):
        latitude = -.40 + j/rows*.91
        for i in range(cols+1):
            angle = -.99 + i/cols*1.98
            verts.append((center.x+.142*math.sin(angle)*math.cos(latitude),
                          center.y+.159*math.cos(angle)*math.cos(latitude),
                          center.z+.179*math.sin(latitude)))
    faces = []
    for j in range(rows):
        for i in range(cols):
            a = j*(cols+1)+i
            faces.append((a+cols+1, a+cols+2, a+1, a))
    obj = parent_new(art.obj, "PilotHead_optical_visor", art.mesh("Optical visor", verts, faces), mat="visor")
    for face in obj.data.polygons:
        face.use_smooth = True
    for j in (0, rows):
        wire("PilotHead_visor compressed perimeter seal", verts[j*(cols+1):(j+1)*(cols+1)], .006, "gasket")
    for i in (0, cols):
        wire("PilotHead_visor side seal", [verts[j*(cols+1)+i] for j in range(rows+1)], .006, "gasket")


def glove(side, elbow, grip):
    grip = Vector(grip)
    wrist = grip + (Vector(elbow)-grip).normalized()*.052
    limb("Pressure glove wrist", wrist, grip, .038, "reinforced")
    sphere("Glove articulated palm", grip+Vector((0, -.017, 0)), (.046, .041, .036), "reinforced")
    # Four closed fingers curl down and around the existing vertical grip.
    for i in range(4):
        x = grip.x+(i-1.5)*.020
        z = grip.z+.019
        pts = [(x, grip.y-.035, z), (x, grip.y+.021, z+.020),
               (x, grip.y+.047, z-.006), (x, grip.y+.030, z-.030)]
        for j in range(3):
            beam("Glove finger articulated phalanx", pts[j], pts[j+1], .0105, "reinforced", 12)
        sphere("Glove knuckle shell", pts[1], (.010, .012, .010), "shell")
    thumb = [grip+Vector((side*.043, -.025, -.013)),
             grip+Vector((side*.052, .015, .005)), grip+Vector((side*.023, .035, .025))]
    for a, b in zip(thumb, thumb[1:]):
        beam("Glove opposed thumb", a, b, .014, "reinforced", 12)


def boot_upper(foot):
    """Continuous flat-bottom shoe last, with a tapered toe instead of stacked boxes."""
    sections = [(-.120, .049, .285), (-.090, .061, .320), (-.030, .064, .298),
                (.035, .065, .277), (.083, .064, .251), (.115, .051, .226),
                (.123, .034, .211)]
    vertices = []
    for y, width, top in sections:
        for x, z in [(-width, .188), (-width, top-.023), (-width*.74, top),
                     (width*.74, top), (width, top-.023), (width, .188)]:
            vertices.append((foot.x+x, foot.y+y, z))
    faces = [tuple(range(5, -1, -1))]
    for row in range(len(sections)-1):
        for i in range(6):
            a, b = row*6+i, row*6+(i+1) % 6
            faces.append((a, b, b+6, a+6))
    faces.append(tuple(range((len(sections)-1)*6, len(sections)*6)))
    result = parent_new(art.obj, "Boot shaped continuous upper", art.mesh("Boot tailored last", vertices, faces), mat="reinforced")
    modifier = result.modifiers.new("Rounded flexible boot edges", "BEVEL")
    modifier.width, modifier.segments = .009, 3
    result.modifiers.new("Boot weighted corner normals", "WEIGHTED_NORMAL")
    wire("Boot reinforced toe stitching", [(foot.x-.058, foot.y+.067, .245),
          (foot.x-.039, foot.y+.067, .260), (foot.x+.039, foot.y+.067, .260),
          (foot.x+.058, foot.y+.067, .245)], .0018, "seam")


def pilot(kind):
    global PARENT
    pose = flight_cell.fit(1.78)
    art.assembly("PILOT STUDY | "+kind)
    body = art.obj("PilotBody", None)
    head = art.obj("PilotHead", None)
    body["study_status"] = "Original static suited fit study, not production rig"
    PARENT = body
    # Equal-stature two adult proportion studies. Same suit family, no scale fudge.
    feminine = kind == "female"
    hip_rx = .155 if feminine else .148
    shoulder_rx = .183 if feminine else .197
    loft("Pressure suit pelvis", [(.432, 0, -.16, .121, .085),
         (.46, 0, -.16, hip_rx, .108), (.53, 0, -.16, hip_rx, .119),
         (.59, 0, -.19, .130, .10), (.63, 0, -.20, .126, .10)])
    loft("Tailored pressure suit torso", [(.56, 0, -.19, .126, .10),
         (.64, 0, -.195, .130 if feminine else .144, .106),
         (.665, 0, -.20, .120 if feminine else .136, .097),
         (.69, 0, -.204, .133 if feminine else .149, .108),
         (.78, 0, -.215, .145 if feminine else .163, .108),
         (.90, 0, -.22, .169 if feminine else .180, .116),
         (1.01, 0, -.22, shoulder_rx, .104),
         (1.064, 0, -.25, shoulder_rx*.86, .091),
         (1.10, 0, -.285, .089, .071)])
    for x in (-.095, .095):
        wire("Torso curved tailoring seam", [(x*.9, -.096, .62), (x, -.105, .78),
              (x*1.35, -.112, .98), (x*.7, -.14, 1.079)], .0022, "seam")
    box("Chest communications module", (0, -.082, .887), (.105, .026, .090), "reinforced", .012)
    box("Chest protected display", (0, -.066, .900), (.068, .009, .022), "visor", .003)
    for x in (-.033, 0, .033):
        parent_new(art.cyl, "Chest recessed button", (x, -.063, .865), .006, .008, "hardware", axis=(0, 1, 0), n=12)
    # Sealed collar is body-owned; helmet shell and its locking ring are head-owned.
    parent_new(art.cyl, "Suit neck pressure bellows", (0, -.285, 1.133), .073, .076, "gasket")
    for z in (1.105, 1.12, 1.137):
        parent_new(art.ring, "Collar sealed bellows rib", (0, -.285, z), .075, .004, "reinforced")
    parent_new(art.ring, "Shoulder collar locking seat", (0, -.285, 1.153), .087, .007, "hardware")
    for i, side in enumerate((-1, 1)):
        shoulder, elbow = pose["shoulders"][i], pose["elbows"][i]
        grip, hip = flight_cell.SPEC["grips"][i], pose["hips"][i]
        knee, ankle = pose["knees"][i], pose["ankles"][i]
        sphere("Shoulder flexible pressure joint", shoulder, (.077, .077, .075), "cloth")
        limb("Tailored upper sleeve", shoulder, elbow, .065)
        sphere("Elbow flexible joint", elbow, (.052, .055, .055), "cloth")
        limb("Tailored forearm sleeve", elbow, grip, .055)
        glove(side, elbow, grip)
        limb("Seated suit thigh", hip, knee, .087)
        sphere("Knee articulated soft joint", knee, (.074, .072, .067), "cloth")
        sphere("Knee protective sewn overlay", Vector(knee)+Vector((0, 0, .050)), (.060, .067, .012), "reinforced")
        limb("Shaped pressure suit shin", knee, ankle, .070)
        # Sole fixed to the shared pedal datum. A small 2.5 mm tread compression
        # matches the authored reference; these are not contact-physics shapes.
        foot = Vector(ankle)+Vector((0, .065, -.005))
        boot_upper(foot)
        box("Boot continuous traction sole", (foot.x, foot.y, .175), (.135, .250, .030), "sole", .007)
        for j in range(6):
            box("Boot sole transverse tread", (foot.x, foot.y-.101+j*.040, .1615), (.126, .010, .004), "sole", .001)
        for z in (.235, .268):
            box("Boot closure retention strap", (foot.x, foot.y-.051, z), (.132, .026, .014), "gasket", .004)
        box("Suit shoulder service patch", (side*.185, -.134, 1.022), (.043, .010, .033), "mark", .004)
        # Webbing originates on the same seat-back hardpoint as the authored couch.
        points = [(side*.18, -.54, 1.16), (side*.14, -.245, 1.098),
                  (side*.125, -.103, 1.005), (side*.103, -.091, .84),
                  (side*.065, -.043, .576)]
        for a, b in zip(points, points[1:]):
            band("Four-point restraint shoulder webbing", a, b, .036)
        band("Four-point restraint lap webbing", (side*.252, -.24, .52),
             (side*.048, -.027, .558), .040)
        box("Harness shoulder adjuster", (side*.106, -.079, .862), (.046, .011, .031), "hardware", .004)
        for z in (.65, .70, .75):
            box("Harness stitch bar", (side*.08, -.070, z), (.024, .004, .002), "seam", .0005)
    box("Harness central rotary latch", (0, -.015, .563), (.089, .030, .070), "hardware", .010)
    parent_new(art.cyl, "Harness latch guarded release", (0, .004, .563), .022, .011, "mark", axis=(0, 1, 0))
    PARENT = head
    # Keep gameplay eye exact, place it toward the helmet's front rather than at
    # the skull centre. This also keeps the sealed neck behind first-person sight.
    center = Vector(pose["eye"])+Vector((0, -.100, .020))
    sphere("PilotHead_pressure helmet shell", center, (.138, .148, .175), "shell")
    visor_patch(center)
    parent_new(art.ring, "PilotHead_lower pressure lock", (0, -.285, 1.166), .085, .012, "shell")
    for side in (-1, 1):
        parent_new(art.cyl, "PilotHead_visor hinge bearing", (side*.136, center.y+.017, center.z+.015),
                   .034, .016, "hardware", axis=(1, 0, 0), n=32)
        parent_new(art.cyl, "PilotHead_ear communications cap", (side*.144, center.y-.041, center.z+.003),
                   .032, .013, "reinforced", axis=(1, 0, 0), n=32)
        for z in (-.015, 0, .015):
            box("PilotHead_ear cap grille", (side*.152, center.y-.041, center.z+z), (.004, .031, .004), "gasket", .001)
        wire("PilotHead_crown parting seam", [(side*.055, center.y-.127, center.z+.059),
              (side*.055, center.y-.083, center.z+.145),
              (side*.055, center.y+.011, center.z+.160)], .0025, "seam")
    # Eye anchor exported as metadata, not a second gameplay camera.
    anchor = parent_new(art.obj, "PilotHead_design_eye", None, pose["eye"])
    anchor["reference_only"] = True
    PARENT = None
    return body, head, pose


def scrub_png(path):
    raw = path.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Expected PNG")
    result, offset = raw[:8], 8
    while offset < len(raw):
        count = struct.unpack_from(">I", raw, offset)[0]
        end = offset+count+12
        if end > len(raw):
            raise ValueError("Truncated PNG")
        if raw[offset+4:offset+8] not in (b"tEXt", b"zTXt", b"iTXt", b"eXIf"):
            result += raw[offset:end]
        offset = end
    path.write_bytes(result)


def export(kind, body, head):
    # Preserve the editable disconnected master; export evaluated meshes joined
    # by material within each visibility group. Small seams are not draw calls.
    graph = bpy.context.evaluated_depsgraph_get()
    temporary = []
    for group in (body, head):
        batches = {}
        originals = list(group.children_recursive)
        for obj in originals:
            if obj.type not in ("MESH", "CURVE"):
                continue
            evaluated = obj.evaluated_get(graph)
            mesh = bpy.data.meshes.new_from_object(evaluated, depsgraph=graph)
            material = obj.material_slots[0].material
            mesh.materials.clear()
            mesh.materials.append(material)
            copy = bpy.data.objects.new("Export component", mesh)
            art.CURRENT.objects.link(copy)
            copy.matrix_world = obj.matrix_world.copy()
            batches.setdefault(material.name, []).append(copy)
        for index, parts in enumerate(batches.values()):
            bpy.ops.object.select_all(action="DESELECT")
            for obj in parts:
                obj.select_set(True)
            bpy.context.view_layer.objects.active = parts[0]
            if len(parts) > 1:
                bpy.ops.object.join()
            merged = bpy.context.object
            merged.name = group.name+"_surface_"+str(index)
            merged.parent = group
            temporary.append(merged)
    bpy.ops.object.select_all(action="DESELECT")
    objects = [body, head, *temporary]
    for obj in objects:
        obj.select_set(True)
    path = OUT / ("pilot-"+kind+".glb")
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLB", use_selection=True,
                             export_apply=True, export_yup=True, export_cameras=False, export_lights=False)
    for obj in temporary:
        mesh = obj.data
        bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.meshes.remove(mesh)
    raw = path.read_bytes()
    length = struct.unpack_from("<I", raw, 12)[0]
    doc = json.loads(raw[20:20+length])
    triangles = sum(doc["accessors"][p["indices"]]["count"]//3
                    for node in doc["nodes"] if "mesh" in node
                    for p in doc["meshes"][node["mesh"]]["primitives"])
    return {"file": path.name, "bytes": len(raw), "triangles": triangles,
            "material_surfaces": len(doc["meshes"]),
            "sha256": hashlib.sha256(raw).hexdigest(), "nodes": len(doc["nodes"])}


def stage(pose):
    # Established seat and simplified control/pedal context are review-only,
    # excluded from the character GLBs. Do not claim this cutaway is full cabin.
    build_flight_cell.seat(art, pose)
    for obj in list(bpy.context.scene.objects):
        if "harness" in obj.name.lower() or "buckle" in obj.name.lower():
            if obj.parent is None:
                obj.hide_render = True
    art.assembly("REVIEW ONLY | grip and pedal context")
    for i, side in enumerate((-1, 1)):
        g = flight_cell.SPEC["grips"][i]
        art.box("Review grip", g, (.047, .070, .085), "black", .010)
        art.box("Review armrest", (side*.35, .020, .660), (.105, .42, .055), "panel", .010)
        a = pose["ankles"][i]
        art.box("Review pedal", (a[0], a[1]+.065, .135), (.140, .250, .035), "steel", .003, rot=(-.15, 0, 0))
        for j in range(5):
            art.box("Review pedal tread", (a[0], a[1]-.015+j*.04, .160), (.12, .008, .005), "black", .001)
    art.assembly("REVIEW ONLY | lighting and backdrop")
    art.box("Review floor", (0, 0, -.041), (200, 200, .060), "panel", 0)
    art.area("Broad warm key", (2, 3, 4), (0, 0, .8), 400, 3, (1, .91, .80))
    art.area("Cool shoulder rim", (-2, -2, 3), (0, 0, .9), 550, 2, (.52, .73, 1))
    art.area("Soft front fill", (-2, 2, 1.4), (0, 0, .8), 110, 2, (.8, .9, 1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--samples", type=int, default=32)
    parser.add_argument("--size", type=int, default=1200)
    parser.add_argument("--kind", choices=("male", "female", "both"), default="both")
    parser.add_argument("--no-render", action="store_true")
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    if not 8 <= args.samples <= 256 or not 512 <= args.size <= 4096:
        raise ValueError("Bounded sample count/size required")
    flight_cell.checks()
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = {"schema_version": 1, "status": "Original static adult suited pilot presentation / fit studies",
                "license": "BSD-3-Clause (repository LICENSE.md)", "external_assets": [],
                "source": "tools/build_seated_pilots.py", "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "layout_sha256": hashlib.sha256(flight_cell.SPEC_PATH.read_bytes()).hexdigest(),
                "units": "metres", "source_axes": "Blender +Y forward +Z up; GLB converts to +Y up -Z forward",
                "origin": "Cabin datum. Parent GLB at identity under existing cabin, not ship root.",
                "groups": {"PilotBody": "Body, gloves, boots, collar, harness", "PilotHead": "Helmet, visor, head locking ring, reference eye"},
                "fit": "Both adults nominal 1.78 m synthetic design fixture; same anchors, modest torso proportion differences. Not anthropometric coverage.",
                "limitations": ["Static articulated meshes, not skinned or animated", "No facial mesh beneath opaque visor", "No production LODs yet", "No collision or clearance certification", "Hands use authored grip anchors; no animated reach", "Review context is seat/grips/pedals, not full cockpit"],
                "assets": []}
    manifest["source_dependencies_sha256"] = {
        "tools/"+name: hashlib.sha256((ROOT/"tools"/name).read_bytes()).hexdigest()
        for name in ("build_hero_assets.py", "build_flight_cell.py", "flight_cell.py")}
    kinds = ("male", "female") if args.kind == "both" else (args.kind,)
    for kind in kinds:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        art.CACHE.clear()
        art.MAT.clear()
        make_materials()
        body, head, pose = pilot(kind)
        entry = export(kind, body, head)
        entry["eye_blender"] = list(pose["eye"])
        entry["fit_cushion_gap_metres"] = .002
        entry["renders"] = []
        stage(pose)
        scene = bpy.context.scene
        scene.render.engine = "CYCLES"
        scene.cycles.samples = args.samples
        scene.cycles.use_denoising = True
        scene.render.resolution_x = args.size
        scene.render.resolution_y = args.size
        scene.render.resolution_percentage = 100
        scene.render.image_settings.file_format = "PNG"
        scene.render.use_stamp = False
        scene.world = bpy.data.worlds.new("Review soft studio")
        scene.world.use_nodes = True
        scene.world.node_tree.nodes["Background"].inputs[0].default_value = (.11, .14, .19, 1)
        scene.world.node_tree.nodes["Background"].inputs[1].default_value = .25
        views = [("front", (2.5, 3.6, 2.1), (0, .13, .80), 70),
                 ("side", (3.8, .15, 1.45), (0, .12, .79), 70),
                 ("eye-down", pose["eye"], (0, .40, .33), 22)]
        for name, loc, target, lens in views:
            art.camera("Review "+name, loc, target, lens)
            head.hide_render = name == "eye-down"
            for obj in head.children_recursive:
                obj.hide_render = name == "eye-down"
            path = OUT / ("pilot-"+kind+"-"+name+".png")
            scene.render.filepath = str(path)
            if not args.no_render:
                bpy.ops.render.render(write_still=True)
                scrub_png(path)
                entry["renders"].append({"file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        head.hide_render = False
        for obj in head.children_recursive:
            obj.hide_render = False
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT / ("pilot-"+kind+".blend")), compress=True)
        entry["blend_sha256"] = hashlib.sha256((OUT / ("pilot-"+kind+".blend")).read_bytes()).hexdigest()
        manifest["assets"].append(entry)
        # Variant-specific attribution survives a subsequent --kind invocation.
        (OUT / ("pilot-"+kind+"-provenance.json")).write_text(
            json.dumps({**manifest, "assets": [entry]}, indent=2)+"\n")
        (OUT / "provenance.json").write_text(json.dumps(manifest, indent=2)+"\n")
    print("PILOT STUDY OUTPUT: "+str(OUT), flush=True)


if __name__ == "__main__":
    main()
