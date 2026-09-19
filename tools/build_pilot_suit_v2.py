#!/usr/bin/env python3
"""Original BSD-3-Clause female pressure-suit mesh study, iteration two.

blender -b --factory-startup --python tools/build_pilot_suit_v2.py -- --size 1100
No downloaded assets. Outputs are isolated in build-godot/pilot-suit-v2.
This is an authored geometry/material checkpoint, not cinematic completion.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
import numpy as np
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
import build_hero_assets as art
import build_flight_cell
import build_seated_pilots as export_helpers
import flight_cell

OUT = ROOT/"build-godot"/"pilot-suit-v2"
PARENT = None
TEXTURES = []


def own(obj):
    obj.parent = PARENT
    return obj


def box(*args, **kwargs):
    return own(art.box(*args, **kwargs))


def wire(*args, **kwargs):
    return own(art.wire(*args, **kwargs))


def cylinder(*args, **kwargs):
    return own(art.cyl(*args, **kwargs))


def ring(*args, **kwargs):
    return own(art.ring(*args, **kwargs))


def mesh(name, vertices, faces, material, uv=None):
    if not vertices or any(not math.isfinite(float(x)) for v in vertices for x in v):
        raise ValueError("Invalid authored mesh: "+name)
    obj = own(art.obj(name, art.mesh(name, vertices, faces), mat=material))
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    layer = obj.data.uv_layers.new(name="Physical fabric coordinates")
    for polygon in obj.data.polygons:
        for index in polygon.loop_indices:
            vertex = obj.data.loops[index].vertex_index
            point = vertices[vertex]
            layer.data[index].uv = uv[vertex] if uv else (point[0]/.08, point[2]/.08)
    return obj


def image(name, pixels, data=False):
    height, width = pixels.shape[:2]
    result = bpy.data.images.new(name, width=width, height=height, alpha=True)
    if data:
        result.colorspace_settings.name = "Non-Color"
    result.pixels.foreach_set(np.asarray(pixels, dtype=np.float32).ravel())
    result.filepath_raw = str(OUT/(name+".png"))
    result.file_format = "PNG"
    result.save()
    result.pack()
    TEXTURES.append(Path(result.filepath_raw).name)
    return result


def materials():
    art.materials()
    n = 512
    yy, xx = np.mgrid[:n, :n]/n
    # An original deterministic woven textile tile. Warp/weft cross at alternate
    # heights; sparse heavier ripstop threads and slow dye variation are separate.
    warp = .5+.5*np.cos(xx*math.tau*64)
    weft = .5+.5*np.cos(yy*math.tau*64)
    over = ((np.floor(xx*64)+np.floor(yy*64)) % 2)*2-1
    height = (warp+weft)*.35+over*(warp-weft)*.2
    ripstop = np.exp(-((np.sin(xx*math.pi*8))/.14)**2)+np.exp(-((np.sin(yy*math.pi*8))/.14)**2)
    dye = .5*np.sin(xx*math.tau*3+np.cos(yy*math.tau*2))+.25*np.cos((xx+yy)*math.tau*7)
    noise = np.random.default_rng(26702).normal(0, .017, (n, n))
    height += ripstop*.15
    pixels = np.ones((n, n, 4))
    cloth_color = np.array((.245, .300, .320))
    pixels[:, :, :3] = cloth_color[None, None, :]*(.88+.11*height+.06*dye+noise)[:, :, None]
    color = image("original-slate-weave-color", pixels)
    normal = np.ones((n, n, 4))
    dx = (np.roll(height, -1, axis=1)-np.roll(height, 1, axis=1))*.27
    dy = (np.roll(height, -1, axis=0)-np.roll(height, 1, axis=0))*.27
    denom = np.sqrt(dx*dx+dy*dy+1)
    normal[:, :, 0] = -.5*dx/denom+.5
    normal[:, :, 1] = -.5*dy/denom+.5
    normal[:, :, 2] = .5/denom+.5
    normal_image = image("original-slate-weave-normal", normal, True)
    rough = np.ones((n, n, 4))
    rough[:, :, :3] = np.clip(.77+.07*dye+.06*height, .60, .94)[:, :, None]
    rough_image = image("original-slate-weave-roughness", rough, True)
    palette = {
        "cloth": ((.24, .30, .32), 0, .80),
        "stretch": ((.025, .034, .040), 0, .79),
        "binding": ((.075, .090, .096), 0, .83),
        "zipper": ((.29, .26, .20), .02, .80),
        "webbing": ((.060, .062, .056), 0, .90),
        "rubber": ((.014, .019, .021), 0, .62),
        "composite": ((.055, .073, .085), .18, .38),
        "metal": ((.19, .22, .23), .78, .32),
        "amber": ((.48, .24, .065), .08, .63),
        "skin": ((.46, .27, .19), 0, .63),
        "lip": ((.25, .092, .080), 0, .57),
        "brow": ((.040, .024, .017), 0, .82),
        "eye": ((.72, .69, .60), 0, .25),
        "iris": ((.10, .13, .095), 0, .28),
        "pupil": ((.003, .005, .006), 0, .18),
    }
    for name, (rgb, metal, roughness) in palette.items():
        material = bpy.data.materials.new("Suit v2 / "+name)
        material.diffuse_color = (*rgb, 1)
        material.use_nodes = True
        shader = material.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Base Color"].default_value = (*rgb, 1)
        shader.inputs["Metallic"].default_value = metal
        shader.inputs["Roughness"].default_value = roughness
        if name in ("cloth", "stretch", "binding", "zipper", "webbing"):
            node = material.node_tree.nodes.new("ShaderNodeTexImage")
            node.image = normal_image
            bump = material.node_tree.nodes.new("ShaderNodeNormalMap")
            bump.inputs["Strength"].default_value = .6 if name == "cloth" else .3
            material.node_tree.links.new(node.outputs["Color"], bump.inputs["Color"])
            material.node_tree.links.new(bump.outputs["Normal"], shader.inputs["Normal"])
        if name == "cloth":
            for texture, socket in ((color, "Base Color"), (rough_image, "Roughness")):
                node = material.node_tree.nodes.new("ShaderNodeTexImage")
                node.image = texture
                material.node_tree.links.new(node.outputs["Color"], shader.inputs[socket])
        art.MAT[name] = material
    visor = bpy.data.materials.new("Suit v2 / optical visor")
    visor.diffuse_color = (.58, .76, .81, .12)
    visor.use_nodes = True
    shader = visor.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (.58, .76, .81, 1)
    shader.inputs["Roughness"].default_value = .105
    shader.inputs["Metallic"].default_value = .12
    shader.inputs["Alpha"].default_value = .12
    visor.surface_render_method = "DITHERED"
    art.MAT["visor"] = visor


def interpolate(values, t):
    p = t*(len(values)-1)
    k = min(len(values)-2, int(p))
    f = p-k
    return values[k]*(1-f)+values[k+1]*f


def smooth_path(a, b, c, t):
    """Two cubic segments share a tangent at a fleshed joint, not a ball/socket."""
    a, b, c = Vector(a), Vector(b), Vector(c)
    tangent = (c-a).normalized()*min((a-b).length, (b-c).length)*.28
    if t <= .5:
        u = t*2
        p0, p1, p2, p3 = a, a+(b-a)*.38, b-tangent, b
    else:
        u = (t-.5)*2
        p0, p1, p2, p3 = b, b+tangent, c+(b-c)*.30, c
    return p0*(1-u)**3+p1*(3*u*(1-u)**2)+p2*(3*u*u*(1-u))+p3*u**3


def limb(name, a, b, c, widths, depths, leg=False):
    sides, steps = 64, 88
    vertices, uv, frames = [], [], []
    compression = ((Vector(a)-Vector(b)).normalized()+(Vector(c)-Vector(b)).normalized()).normalized()
    uvec = None
    previous = None
    distance = 0
    for row in range(steps+1):
        t = row/steps
        centre = smooth_path(a, b, c, t)
        tangent = (smooth_path(a, b, c, min(1, t+.0001))-smooth_path(a, b, c, max(0, t-.0001))).normalized()
        if uvec is None:
            uvec = tangent.cross(Vector((0, 1, 0))).normalized()
        else:
            uvec = (uvec-tangent*uvec.dot(tangent)).normalized()
        vvec = tangent.cross(uvec).normalized()
        if previous is not None:
            distance += (centre-previous).length
        previous = centre
        rx, ry = interpolate(widths, t), interpolate(depths, t)
        frames.append((centre, uvec.copy(), vvec.copy(), rx, ry))
        for i in range(sides+1):
            theta = math.tau*i/sides
            radial = uvec*math.cos(theta)+vvec*math.sin(theta)
            inner = max(0, radial.dot(compression))**2
            folds = .0055*math.exp(-((t-.49)/.15)**2)*inner*math.sin(92*t+2*math.sin(theta*2))
            # Broad diagonal cloth pull and softer gathered cuff; never uniform
            # ring corrugations around the whole limb.
            folds += .0025*math.sin(21*t+theta*3)*math.sin(math.pi*t)**2
            folds += .002*math.exp(-((t-.94)/.06)**2)*math.sin(theta*5+69*t)
            vertex = centre+uvec*(rx*math.cos(theta))+vvec*(ry*math.sin(theta))+radial*folds
            vertices.append(vertex)
            uv.append((i/sides*math.tau*(rx+ry)*.5/.08, distance/.08))
    faces = [tuple(range(sides, -1, -1))]
    for row in range(steps):
        for i in range(sides):
            a0 = row*(sides+1)+i
            faces.append((a0, a0+1, a0+sides+2, a0+sides+1))
    faces.append(tuple(range(steps*(sides+1), (steps+1)*(sides+1))))
    result = mesh(name, vertices, faces, "cloth", uv)
    # Follow the same garment's actual surface, not an arbitrary line in space.
    for theta in ((.1, math.pi-.1) if leg else (.35, math.pi+.35)):
        points = [p+u*(rx*math.cos(theta))+v*(ry*math.sin(theta))
                  for p, u, v, rx, ry in frames[3:-3]]
        wire(name+" sewn panel seam", points, .0014, "binding")
    return result


def torso():
    sections = [(.432, -.16, .139, .085), (.46, -.17, .164, .115),
                (.52, -.18, .171, .125), (.59, -.205, .155, .113),
                (.66, -.225, .129, .099), (.73, -.234, .125, .101),
                (.80, -.24, .144, .111), (.87, -.247, .158, .114),
                (.94, -.257, .167, .113), (1.035, -.263, .179, .099),
                (1.077, -.275, .151, .076), (1.119, -.29, .067, .058)]
    vertices, uv = [], []
    sides, steps = 96, 100
    for j in range(steps+1):
        t = j/steps
        z, cy, rx, ry = (interpolate([s[k] for s in sections], t) for k in range(4))
        for i in range(sides+1):
            theta = math.tau*i/sides
            x = rx*math.cos(theta)
            front = max(0, math.sin(theta))
            # Continuous, modest natural chest shaping beneath a working suit;
            # broad tailorable volumes rather than detached armour cups.
            bust = .022*math.exp(-((abs(x)-.072)/.058)**2-((z-.913)/.070)**2)*front**3
            waist_fold = .0045*math.exp(-((z-.65)/.065)**2)*math.sin(z*155+theta*2)*front
            hip_fold = .003*math.exp(-((z-.56)/.07)**2)*math.cos(theta*4+z*63)
            y = cy+ry*math.sin(theta)+bust+waist_fold+hip_fold
            vertices.append((x, y, z))
            uv.append((theta*(rx+ry)*.5/.08, z/.08))
    faces = [tuple(range(sides, -1, -1))]
    for row in range(steps):
        for i in range(sides):
            a = row*(sides+1)+i
            faces.append((a, a+1, a+sides+2, a+sides+1))
    faces.append(tuple(range(steps*(sides+1), (steps+1)*(sides+1))))
    mesh("Female tailored pressure garment torso and pelvis", vertices, faces, "cloth", uv)
    return sections


def front_y(sections, z, x=0):
    for a, b in zip(sections, sections[1:]):
        if a[0] <= z <= b[0]:
            f = (z-a[0])/(b[0]-a[0])
            cy, rx, ry = (a[k]*(1-f)+b[k]*f for k in (1, 2, 3))
            v = cy+ry*math.sqrt(max(0, 1-(x/rx)**2))
            v += .022*math.exp(-((abs(x)-.072)/.058)**2-((z-.913)/.070)**2)
            return v
    raise ValueError("Torso trim outside garment")


def ribbon(name, points, width, mat):
    verts = []
    points = [Vector(p) for p in points]
    for i, p in enumerate(points):
        tangent = (points[min(i+1, len(points)-1)]-points[max(0, i-1)]).normalized()
        side = Vector((tangent.z, 0, -tangent.x)).normalized()*width*.5
        verts.extend((p-side, p+side))
    faces = [(i*2+1, i*2, i*2+2, i*2+3) for i in range(len(points)-1)]
    obj = mesh(name, verts, faces, mat)
    solid = obj.modifiers.new("Manufactured webbing thickness", "SOLIDIFY")
    solid.thickness = .002
    return obj


def trim(sections):
    for x, width, name, mat in ((.034, .033, "Offset pressure zipper placket", "zipper"),
                                 (-.112, .009, "Left shaped torso seam", "binding"),
                                 (.114, .009, "Right shaped torso seam", "binding")):
        points = [(x, front_y(sections, z, x)+.003, z) for z in np.linspace(.60, 1.025, 55)]
        ribbon(name, points, width, mat)
    for z in np.linspace(.61, 1.014, 81):
        y = front_y(sections, float(z), .034)+.006
        box("Pressure zip paired teeth", (.034, y, z), (.008, .003, .002), "metal", .0005)
    box("Pressure zip guarded pull", (.036, front_y(sections, .994, .034)+.010, .994), (.013, .006, .026), "metal", .002)
    # Small coherent side service hardware. Actual plumbing/pressure certification
    # remains out of scope; placement is a visible manufactured interface.
    y = front_y(sections, .656, .092)+.012
    box("Recessed right waist service manifold", (.105, y, .652), (.107, .026, .065), "composite", .011)
    for x in (.079, .127):
        cylinder("Service connector locking recess", (x, y+.017, .652), .020, .008, "rubber", axis=(0, 1, 0), n=40)
        ring("Service connector threaded lock", (x, y+.023, .652), .015, .0022, "metal", axis=(0, 1, 0), n=40)
        cylinder("Service connector dust plug", (x, y+.024, .652), .011, .003, "binding", axis=(0, 1, 0), n=32)
        box("Service plug key slot", (x, y+.026, .652), (.010, .002, .002), "metal", .0005)
    for side in (-1, 1):
        # Webbing is surface fitted continuously, with the same seat anchors.
        xs = [(1.022, side*.127), (.975, side*.121), (.92, side*.112),
              (.84, side*.092), (.76, side*.076), (.68, side*.062), (.589, side*.037)]
        points = [(side*.18, -.54, 1.16), (side*.145, -.32, 1.077)]
        points += [(x, front_y(sections, z, x)+.010, z) for z, x in xs]
        ribbon("Continuous four-point shoulder restraint", points, .030, "webbing")
        ribbon("Lap restraint to seat hardpoint", [(side*.25, -.24, .515),
                (side*.16, -.058, .553), (side*.037, -.044, .581)], .035, "webbing")
        z, x = .92, side*.112
        y = front_y(sections, z, x)+.017
        box("Harness ladder adjuster", (x, y, z), (.041, .009, .028), "metal", .003)
        box("Harness adjuster slot", (x, y+.005, z), (.025, .003, .013), "webbing", .001)
    box("Four-point compact release buckle", (0, -.031, .580), (.074, .025, .054), "metal", .009)
    cylinder("Recessed harness release", (0, -.016, .58), .018, .006, "amber", axis=(0, 1, 0))


def glove(side, elbow, grip):
    g = Vector(grip)
    # Soft tailored palm / back with four unequal fingers; no metallic knuckles.
    palm = own(art.sphere("Pressure glove palm", g+Vector((0, -.019, .004)), (.043, .039, .029), "rubber"))
    for i, length in enumerate((.049, .059, .056, .043)):
        x = g.x+(i-1.5)*.017
        z = g.z+.017-(.003 if i in (0, 3) else 0)
        pts = [(x, g.y-.031, z), (x, g.y+length*.40, z+.013),
               (x, g.y+length*.73, z-.003), (x, g.y+length*.43, z-.024)]
        wire("Differentiated gloved finger", pts, .0084, "rubber")
        wire("Finger dorsal stitched gusset", [(p[0], p[1], p[2]+.006) for p in pts[:3]], .0010, "binding")
    wire("Glove anatomically opposed thumb", [g+Vector((side*.038, -.026, -.010)),
         g+Vector((side*.050, .005, .010)), g+Vector((side*.026, .028, .024))], .012, "rubber")
    wrist = g+(Vector(elbow)-g).normalized()*.062
    cylinder("Flexible glove pressure cuff", wrist, .033, .035, "rubber", axis=Vector(grip)-Vector(elbow))
    ring("Glove cuff locking band", wrist, .034, .003, "metal", axis=Vector(grip)-Vector(elbow), n=40)


def boots(pose):
    for side, ankle in zip((-1, 1), pose["ankles"]):
        x, y, _ = ankle
        # The original fit/pedal contact remains exact, but human-like asymmetry
        # distinguishes medial arch and outer toe in the authored shoe last.
        verts = []
        sections = [(-.056, .047, .310), (-.022, .054, .345), (.030, .058, .322),
                    (.079, .060, .285), (.124, .060, .251), (.174, .049, .216),
                    (.188, .034, .195)]
        for local_y, w, top in sections:
            for xx, z in [(-w, .181), (-w, top-.024), (-w*.64, top),
                          (w*.70, top), (w, top-.024), (w, .181)]:
                verts.append((x+xx+side*.006*(local_y+.056), y+local_y, z))
        faces = [tuple(range(5, -1, -1))]
        for row in range(len(sections)-1):
            for i in range(6):
                a, b = row*6+i, row*6+(i+1) % 6
                faces.append((a, b, b+6, a+6))
        faces.append(tuple(range((len(sections)-1)*6, len(sections)*6)))
        boot = mesh("Fitted flexible flight boot", verts, faces, "rubber")
        bevel = boot.modifiers.new("Rounded leather/rubber last", "BEVEL")
        bevel.width, bevel.segments = .010, 4
        boot.modifiers.new("Last weighted normals", "WEIGHTED_NORMAL")
        box("Boot compliant traction sole", (x, y+.065, .173), (.128, .248, .027), "rubber", .005)
        for j in range(6):
            box("Boot sole functional tread", (x, y-.028+j*.037, .160), (.117, .012, .003), "binding", .001)
        for z in (.28, .315):
            box("Boot instep retention webbing", (x, y-.014, z), (.112, .023, .012), "webbing", .004)


FACE_SECTIONS = [(-.111, .023, -.261, .017), (-.099, .043, -.261, .037),
    (-.080, .058, -.262, .055), (-.053, .065, -.261, .069),
    (-.022, .067, -.261, .078), (.010, .064, -.263, .074),
    (.039, .063, -.263, .075), (.067, .061, -.267, .071),
    (.091, .049, -.274, .060), (.109, .027, -.278, .035)]


def face_displacement(x, dz):
    return (.010*math.exp(-((abs(x)-.037)/.017)**2-((dz+.029)/.025)**2)
            -.010*math.exp(-((abs(x)-.030)/.019)**2-((dz-.002)/.010)**2)
            +.005*math.exp(-((abs(x)-.028)/.025)**2-((dz-.020)/.009)**2)
            +.010*math.exp(-(x/.011)**2-((dz+.001)/.037)**2)
            +.017*math.exp(-(x/.013)**2-((dz+.030)/.013)**2)
            +.004*math.exp(-(x/.023)**2-((dz+.061)/.005)**2)
            +.005*math.exp(-(x/.023)**2-((dz+.069)/.005)**2)
            +.006*math.exp(-(x/.026)**2-((dz+.090)/.013)**2))


def face_front(x, dz):
    for a, b in zip(FACE_SECTIONS, FACE_SECTIONS[1:]):
        if a[0] <= dz <= b[0]:
            f = (dz-a[0])/(b[0]-a[0])
            rx, cy, ry = (a[k]*(1-f)+b[k]*f for k in (1, 2, 3))
            front = math.sqrt(max(0, 1-(x/rx)**2))
            return cy+ry*front+front**4*face_displacement(x, dz)
    raise ValueError("Facial detail outside head surface")


def face():
    """An original continuous facial surface: anatomical landmarks, no sphere head.

    The result is an explicit work-in-progress sculpt, not photoreal scan quality.
    Front is +Y; nominal eyes are centered around the authoritative design eye.
    """
    eye = Vector(flight_cell.SPEC["pilot_eye_blender"])
    eye_z = eye.z
    sections = FACE_SECTIONS
    steps, sides = 128, 128
    verts, uv = [], []
    for j in range(steps+1):
        dz, rx, cy, ry = (interpolate([p[k] for p in sections], j/steps) for k in range(4))
        for i in range(sides+1):
            angle = math.tau*i/sides
            x = rx*math.cos(angle)
            f = max(0, math.sin(angle))
            # Broad cheeks, sockets and brow planes; local bridge, nose tip,
            # philtrum, upper/lower lips and chin are one continuous mesh.
            y = cy+ry*math.sin(angle)
            y += f**4*face_displacement(x, dz)
            verts.append((x, y, eye_z+dz))
            uv.append((i/sides, j/steps))
    faces = []
    for row in range(steps):
        for i in range(sides):
            a = row*(sides+1)+i
            faces.append((a, a+1, a+sides+2, a+sides+1))
    head = mesh("Original female facial sculpt", verts, faces, "skin", uv)
    for side in (-1, 1):
        x = side*.030
        # Shallow almond eye surfaces bounded by separate continuous eyelids.
        points = []
        for i in range(33):
            t = math.tau*i/32
            px, pz = x+.0127*math.cos(t), .0042*math.sin(t)
            points.append((px, face_front(px, pz)+.0008, eye_z+pz))
        ey = face_front(x, 0)+.0016
        eyeverts = [(x, ey, eye_z), *points]
        mesh("Visible almond eye", eyeverts, [(0, i+2, i+1) for i in range(32)], "eye")
        iris = own(art.sphere("Natural iris", (x, ey+.0005, eye_z), (.0041, .0007, .0041), "iris"))
        own(art.sphere("Pupil", (x, ey+.0012, eye_z), (.0020, .0003, .0020), "pupil"))
        wire("Upper and lower eyelid rim", points, .0012, "skin")
        brow = [(x+v, face_front(x+v, .020)+.0008, eye_z+.020+(.003*(1-(v/.017)**2))) for v in np.linspace(-.017, .017, 15)]
        wire("Subtle shaped eyebrow", brow, .0016, "brow")
        own(art.sphere("Nostril recess", (side*.007, face_front(side*.007, -.038)+.0005, eye_z-.038), (.0027, .0008, .0014), "brow"))
    mouth = [(x, face_front(float(x), -.065)+.0007, eye_z-.065+.0015*math.cos(x/.022*math.pi))
             for x in np.linspace(-.022, .022, 29)]
    wire("Resting closed lip seam", mouth, .0011, "lip")
    return head


def helmet():
    centre = Vector((0, -.292, 1.306))
    # Trimmed shell only: no opaque full sphere left behind the visor.
    verts, uv = [], []
    rows, cols = 44, 96
    for j in range(rows+1):
        lat = -.82+j/rows*2.27
        for i in range(cols+1):
            az = .88+i/cols*(math.tau-1.76)
            verts.append((.109*math.sin(az)*math.cos(lat),
                          centre.y+.132*math.cos(az)*math.cos(lat),
                          centre.z+.146*math.sin(lat)))
            uv.append((i/cols, j/rows))
    faces = []
    for j in range(rows):
        for i in range(cols):
            a = j*(cols+1)+i
            faces.append((a+cols+1, a+cols+2, a+1, a))
    shell = mesh("Low-profile engineered helmet shell", verts, faces, "composite", uv)
    solid = shell.modifiers.new("Helmet shell laminate thickness", "SOLIDIFY")
    solid.thickness = .005
    capverts = []
    for j in range(17):
        lat = .87+j/16*.70
        for i in range(49):
            az = -.94+i/48*1.88
            capverts.append((.113*math.sin(az)*math.cos(lat),
                centre.y+.142*math.cos(az)*math.cos(lat), centre.z+.150*math.sin(lat)))
    capfaces = []
    for j in range(16):
        for i in range(48):
            a = j*49+i
            capfaces.append((a+49, a+50, a+1, a))
    mesh("Closed helmet forehead crown", capverts, capfaces, "composite")
    # Optical panel, original side frame and positive lower seal.
    visorverts = []
    for j in range(33):
        lat = -.69+j/32*1.56
        for i in range(49):
            az = -.94+i/48*1.88
            visorverts.append((.113*math.sin(az)*math.cos(lat),
                               centre.y+.142*math.cos(az)*math.cos(lat),
                               centre.z+.150*math.sin(lat)))
    faces = []
    for j in range(32):
        for i in range(48):
            a = j*49+i
            faces.append((a+49, a+50, a+1, a))
    mesh("Pilot optical visor", visorverts, faces, "visor")
    for j in (0, 32):
        wire("Visor upper/lower structural seal", visorverts[j*49:(j+1)*49], .006, "rubber")
        wire("Visor hard rim", [(x, y+.002, z) for x, y, z in visorverts[j*49:(j+1)*49]], .0025, "metal")
    for i in (0, 48):
        wire("Visor cheek perimeter seal", [visorverts[j*49+i] for j in range(33)], .006, "rubber")
    ring("Helmet positive pressure lock", (0, -.282, 1.173), .076, .010, "composite", n=64)
    for side in (-1, 1):
        box("Compact cheek air passage housing", (side*.078, -.230, 1.191), (.035, .052, .027), "composite", .007)
        cylinder("Visible locking latch", (side*.098, -.232, 1.211), .011, .007, "metal", axis=(1, 0, 0))
        box("Flush occipital communications module", (side*.081, -.365, 1.321), (.026, .039, .068), "composite", .006)
        for z in (1.301, 1.311, 1.321):
            box("Helmet communications cooling slit", (side*.096, -.365, z), (.003, .027, .002), "rubber", .0005)
    wire("Helmet crown assembly seam", [(-.042, -.390, 1.387), (-.040, -.338, 1.444),
         (-.039, -.277, 1.448), (-.036, -.230, 1.427)], .0014, "binding")


def build():
    global PARENT
    art.assembly("SUIT V2 | female tailored study")
    body, head = art.obj("PilotBody", None), art.obj("PilotHead", None)
    PARENT = body
    sections = torso()
    pose = flight_cell.fit(1.78)
    joints = []
    for i, side in enumerate((-1, 1)):
        shoulder = (side*.145, -.260, 1.057)
        grip = flight_cell.SPEC["grips"][i]
        wrist = Vector(grip)+Vector((side*.008, -.040, .033))
        elbow = flight_cell.joint(shoulder, wrist, .279, .280, (side*.55, -.1, -.9))
        limb("Continuous fitted sleeve", shoulder, elbow, wrist,
             [.062, .063, .060, .051, .046, .043, .049, .044, .033],
             [.067, .065, .058, .049, .044, .043, .047, .040, .029])
        glove(side, elbow, grip)
        hip = (side*.101, -.159, .515)
        ankle = pose["ankles"][i]
        knee = flight_cell.joint(hip, ankle, .422, .424, (0, 0, 1))
        limb("Continuous tailored seated trouser", hip, knee, ankle,
             [.091, .094, .090, .080, .071, .065, .063, .054, .044],
             [.095, .092, .086, .076, .071, .065, .061, .049, .039], True)
        joints.append({"side": side, "shoulder": list(shoulder), "elbow": list(elbow),
                       "grip": list(grip), "hip": list(hip), "knee": list(knee), "ankle": list(ankle)})
    boots(pose)
    join_garment(body)
    trim(sections)
    cylinder("Garment neck pressure bellows", (0, -.282, 1.141), .066, .047, "stretch")
    for z in (1.124, 1.139, 1.153):
        ring("Neck seal flexible fold", (0, -.282, z), .069, .003, "rubber", n=48)
    PARENT = head
    face()
    helmet()
    PARENT = None
    return body, head, pose, joints


def join_garment(body):
    """Close/union the tailored shell; remove exposed open sleeve/hip boundaries.

    This is garment topology only, not a collider. Reproject physical-scale UVs
    after voxel union so original weave remains present in the exported mesh.
    """
    cloth = [obj for obj in body.children_recursive if obj.type == "MESH"
             and obj.name.startswith(("Female tailored", "Continuous fitted", "Continuous tailored"))]
    bpy.ops.object.select_all(action="DESELECT")
    for obj in cloth:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = cloth[0]
    bpy.ops.object.join()
    garment = bpy.context.object
    garment.name = "Continuous female tailored garment shell"
    remesh = garment.modifiers.new("Join garment armholes and seat tailoring", "REMESH")
    remesh.mode = "VOXEL"
    remesh.voxel_size = .0025
    remesh.use_smooth_shade = True
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    smooth = garment.modifiers.new("Cloth surface relaxation", "SMOOTH")
    smooth.factor = .4
    smooth.iterations = 3
    bpy.ops.object.modifier_apply(modifier=smooth.name)
    layer = garment.data.uv_layers.new(name="Physical weave UV")
    for polygon in garment.data.polygons:
        normal = polygon.normal
        drop = max(range(3), key=lambda k: abs(normal[k]))
        axes = [k for k in range(3) if k != drop]
        for index in polygon.loop_indices:
            p = garment.data.vertices[garment.data.loops[index].vertex_index].co
            layer.data[index].uv = (p[axes[0]]/.08, p[axes[1]]/.08)


def studio(pose):
    build_flight_cell.seat(art, pose)
    for obj in bpy.context.scene.objects:
        if obj.parent is None and obj.name.startswith("SeatMoving harness"):
            obj.hide_render = True
    art.assembly("REVIEW ONLY | environment")
    art.box("Studio floor", (0, 0, -.04), (100, 100, .06), "panel", 0)
    for side, grip, ankle in zip((-1, 1), flight_cell.SPEC["grips"], pose["ankles"]):
        art.box("Control grip reference", grip, (.047, .070, .085), "black", .009)
        art.box("Pedal reference", (ankle[0], ankle[1]+.065, .135), (.14, .25, .035), "steel", .003)
        art.box("Pedal support reference", (ankle[0], ankle[1]+.065, .076), (.09, .23, .09), "black", .006)
    art.area("Broad neutral key", (2, 3, 3.2), (0, 0, .85), 420, 2.4, (1, .93, .84))
    art.area("Garment edge separation", (-2, -1.5, 2.7), (0, 0, .8), 300, 2, (.70, .83, 1))
    art.area("Facial fill", (-1.5, 2, 1.9), (0, -.22, 1.25), 95, 1.4, (1, .87, .76))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--size", type=int, default=1100)
    parser.add_argument("--samples", type=int, default=32)
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    if not 512 <= args.size <= 2400 or not 8 <= args.samples <= 128:
        raise ValueError("Bounded size/sample count required")
    flight_cell.checks()
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.filepaths.save_version = 0
    art.CACHE.clear()
    art.MAT.clear()
    materials()
    body, head, pose, joints = build()
    export_helpers.OUT = OUT
    exported = export_helpers.export("female-v2", body, head)
    studio(pose)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = args.samples
    scene.cycles.use_denoising = True
    scene.render.resolution_x = scene.render.resolution_y = args.size
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.use_stamp = False
    scene.world = bpy.data.worlds.new("Neutral studio")
    scene.world.use_nodes = True
    scene.world.node_tree.nodes["Background"].inputs[0].default_value = (.12, .15, .18, 1)
    scene.world.node_tree.nodes["Background"].inputs[1].default_value = .30
    renders = []
    for name, loc, target, lens in [
            ("seated", (2.5, 3.8, 2.0), (0, .08, .83), 75),
            ("side", (3.5, .4, 1.5), (0, .05, .86), 70),
            ("portrait", (1.1, 2, 1.65), (0, -.23, 1.23), 90),
            ("eye-down", pose["eye"], (0, .4, .36), 24)]:
        camera = art.camera("Review "+name, loc, target, lens)
        for obj in [head, *head.children_recursive]:
            obj.hide_render = name == "eye-down"
        path = OUT/("female-v2-"+name+".png")
        scene.render.filepath = str(path)
        bpy.ops.render.render(write_still=True)
        export_helpers.scrub_png(path)
        renders.append({"file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    for obj in [head, *head.children_recursive]:
        obj.hide_render = False
    scene.camera = bpy.data.objects["Review seated"]
    master = OUT/"pilot-female-v2.blend"
    bpy.ops.wm.save_as_mainfile(filepath=str(master), compress=True)
    manifest = {"schema_version": 1, "status": "Original female anatomy/garment/material mesh study, not cinematic final",
        "license": "BSD-3-Clause (repository LICENSE.md)", "external_assets": [],
        "source": "tools/build_pilot_suit_v2.py", "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "dependencies_sha256": {"tools/"+p: hashlib.sha256((ROOT/"tools"/p).read_bytes()).hexdigest()
            for p in ("build_hero_assets.py", "build_flight_cell.py", "build_seated_pilots.py", "flight_cell.py")},
        "layout_sha256": hashlib.sha256(flight_cell.SPEC_PATH.read_bytes()).hexdigest(),
        "visual_reference": {"file": "pilot-direction-concept-v1.png", "kind": "Human-approved AI-generated visual direction; not mesh or texture input",
             "sha256": "c47da0591c6054b1c8d3b0f3dc76362fd4359f923ca84328de7778f76642f7ae"},
        "coordinates": "Cabin-local metres, Blender +Y forward/+Z up, GLB +Y up/-Z forward. Mount at cabin identity.",
        "groups": ["PilotBody", "PilotHead"], "design_eye_blender": list(pose["eye"]), "joints": joints,
        "asset": exported, "textures": [{"file": p, "sha256": hashlib.sha256((OUT/p).read_bytes()).hexdigest()} for p in TEXTURES],
        "renders": renders, "master": {"file": master.name, "sha256": hashlib.sha256(master.read_bytes()).hexdigest()},
        "limits": ["Static mesh, no animation rig or LODs", "Original procedural face is a sculpt study, not scan-quality portrait",
                   "No collision or comprehensive ergonomic certification", "Studio seat context, actual Godot appearance requires review",
                   "Textures are original deterministic code-authored weave, not downloaded or AI-generated bitmap art"]}
    (OUT/"provenance.json").write_text(json.dumps(manifest, indent=2)+"\n")
    print("PILOT V2 COMPLETE "+json.dumps(exported), flush=True)


if __name__ == "__main__":
    main()
