#!/usr/bin/env python3
"""Bounded D1 service-route derivative; never save an authoring master. BSD-3-Clause."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

import bpy
import numpy as np
from mathutils import Matrix

SOURCE_SHA256 = '6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6'
MODEL_NAME = 'station-d1-clearance-01.glb'
COORDINATE_CONTRACT = 'Correction records: original Blender node-local metres; replacement GLB: identity Godot node-local metres (x,z,-y)'
CONVERT = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))
PIPE_POINTS = [[1.340000033378601, 1.7799999713897705, .8500000238418579],
               [1.340000033378601, 2.6500000953674316, .3499999940395355],
               [.50, 3.23, .35], [.50, 3.23, -.55], [.85, 2.65, -.55],
               [.85, 2.65, -.91], [.6800000071525574, 2.6500000953674316, -.9100000262260437]]
PARTS = (
    ('pipe_route', 'pipe_route', 'DK09 | equalization pipe.001', 'D1_CLEARANCE_PIPE', []),
    ('deck_service_bore', 'service_bore', 'DK09 | continuous deck with open well.001', 'D1_CLEARANCE_DECK', [-.014, 0.]),
    ('flange_service_bore', 'service_bore', 'DK09 | belly shaft mounting flange.001', 'D1_CLEARANCE_FLANGE', [-.30, -.14]),
)


def columns(matrix):
    result = [[float(v) for v in matrix.col[i][:3]] for i in range(4)]
    if not np.isfinite(result).all():
        raise ValueError('Nonfinite station replacement rest')
    return result


def evaluated_mesh(obj):
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    return bpy.data.meshes.new_from_object(evaluated)


def geometry_record(mesh):
    mesh.calc_loop_triangles()
    vertices = np.empty((len(mesh.vertices), 3), dtype=np.float64)
    mesh.vertices.foreach_get('co', vertices.ravel())
    faces = np.empty((len(mesh.loop_triangles), 3), dtype=np.int32)
    mesh.loop_triangles.foreach_get('vertices', faces.ravel())
    if not np.isfinite(vertices).all() or not len(vertices) or not len(faces):
        raise ValueError('Empty/nonfinite replacement geometry')
    if faces.min() < 0 or faces.max() >= len(vertices) or len(vertices) > 100000 or len(faces) > 200000:
        raise ValueError('Replacement geometry buffer bounds')
    edges = Counter()
    for face in faces:
        for a, b in zip(face, np.roll(face, -1)):
            edges[tuple(sorted((int(a), int(b))))] += 1
    # This digest is source geometry provenance, not Godot import buffers. UV and
    # normal splits are intentionally outside this contact topology contract.
    canonical = {'vertices_micrometres': np.rint(vertices[:, [0, 2, 1]] * [1, 1, -1] * 1e6).astype(np.int64).tolist(),
                 'triangles': faces.tolist()}
    digest = hashlib.sha256(json.dumps(canonical, separators=(',', ':'), allow_nan=False).encode()).hexdigest()
    triangles = vertices[faces]
    volume = abs(float(np.einsum('ti,ti->t', triangles[:, 0], np.cross(triangles[:, 1], triangles[:, 2])).sum()) / 6.)
    return {'sha256': digest, 'vertices': len(vertices), 'triangles': len(faces),
            'boundary_edges': sum(n == 1 for n in edges.values()),
            'nonmanifold_edges': sum(n > 2 for n in edges.values()),
            'volume_cubic_metres': volume}


def apply_station_clearance():
    """Mutate only three D1 objects in the current in-memory source scene."""
    if bpy.context.scene.get('d1_clearance_applied'):
        raise ValueError('Station clearance already applied')
    records = []
    for identity, kind, name, replacement, interval in PARTS:
        obj = bpy.data.objects.get(name)
        if obj is None or obj.get('dock_id') != 'D1' or obj.parent.name != 'KIT09 | dock D1':
            raise ValueError('Station replacement source/parent/extras drift')
        original_mesh = evaluated_mesh(obj)
        before = geometry_record(original_mesh)
        if len(original_mesh.materials) != 1:
            raise ValueError('Expected one authored station surface material')
        old_points = []
        if kind == 'pipe_route':
            if obj.type != 'CURVE' or len(obj.data.splines) != 1 or obj.data.splines[0].type != 'POLY':
                raise ValueError('Expected original four-point POLY pipe')
            old_points = [list(p.co[:3]) for p in obj.data.splines[0].points]
            expected = [PIPE_POINTS[0], PIPE_POINTS[1], [.6800000071525574, 2.6500000953674316, -.20000000298023224], PIPE_POINTS[-1]]
            if old_points != expected or abs(obj.data.bevel_depth - .018) > 1e-8:
                raise ValueError('Original pipe points/radius drift')
            obj.data = obj.data.copy()  # The D2 authoring instance shares data.
            obj.data.splines.clear()
            spline = obj.data.splines.new('POLY')
            spline.points.add(len(PIPE_POINTS) - 1)
            for point, xyz in zip(spline.points, PIPE_POINTS):
                point.co = (*xyz, 1.)
        else:
            # Cut a real capped 40mm service bore in the exact original node-local
            # coordinates. Keep every surrounding deck/flange face and material.
            bpy.ops.mesh.primitive_cylinder_add(vertices=128, radius=.020, depth=1., location=(0, 0, 0))
            cutter = bpy.context.object
            cutter.matrix_world = obj.matrix_world @ Matrix.Translation((.50, 3.23, sum(interval) / 2.))
            modifier = obj.modifiers.new('D1 explicit 40mm service bore', 'BOOLEAN')
            modifier.operation = 'DIFFERENCE'
            modifier.solver = 'EXACT'
            modifier.object = cutter
            bpy.context.view_layer.update()
            baked = evaluated_mesh(obj)
            obj.modifiers.remove(modifier)
            obj.data = baked
            bpy.data.objects.remove(cutter, do_unlink=True)
        bpy.context.view_layer.update()
        new_mesh = evaluated_mesh(obj)
        after = geometry_record(new_mesh)
        if after['nonmanifold_edges'] or (kind == 'service_bore' and after['boundary_edges']):
            raise ValueError('Nonmanifold or uncapped station bore')
        if kind == 'pipe_route' and after['boundary_edges'] != before['boundary_edges']:
            raise ValueError('Pipe endpoint topology changed')
        if before['sha256'] == after['sha256']:
            raise ValueError('Station correction did not change geometry')
        if kind == 'service_bore':
            # Exact solver must remove only the specified regular128-sided
            # cylinder volume. This checks for an omitted bore, stray cap or
            # unintended surrounding loss as well as the manifold edge check.
            expected_volume = 128 * .020 ** 2 * np.sin(2 * np.pi / 128) * .5 * (interval[1] - interval[0])
            actual_volume = before['volume_cubic_metres'] - after['volume_cubic_metres']
            if abs(actual_volume - expected_volume) > 1e-7:
                raise ValueError('Station bore removed unexpected surrounding volume')
            before['bore_expected_removal_cubic_metres'] = float(expected_volume)
            after['bore_actual_removal_cubic_metres'] = actual_volume
        extras = {key: obj[key] for key in ('dock_id', 'dock_part', 'role', 'hatch_side') if key in obj}
        record = {'id': identity, 'filename': MODEL_NAME, 'model_sha256': '',
                  'source_object': name, 'runtime_node': name.replace('.', '_'),
                  'runtime_parent': obj.parent.name.replace('.', '_'), 'expected_extras': extras,
                  'rest_transform': columns(CONVERT @ obj.matrix_local @ CONVERT.inverted()),
                  'replacement_node': replacement, 'original_geometry_sha256': before['sha256'],
                  'replacement_geometry_sha256': after['sha256'], 'surface_count': 1, 'kind': kind,
                  'original_points_metres': old_points,
                  'replacement_points_metres': PIPE_POINTS if kind == 'pipe_route' else [],
                  'tube_radius_metres': .018 if kind == 'pipe_route' else 0.,
                  'bore_centre_metres': [.50, 3.23, 0.] if interval else [0., 0., 0.],
                  'bore_radius_metres': .020 if interval else 0.,
                  'bore_depth_interval_metres': interval}
        records.append((record, obj, new_mesh, before, after))
        bpy.data.meshes.remove(original_mesh)
    bpy.context.scene['d1_clearance_applied'] = True
    return records


def export_component(records, output):
    output.mkdir(parents=True, exist_ok=True)
    model = output / MODEL_NAME
    bpy.ops.object.select_all(action='DESELECT')
    exports = []
    for record, source, mesh, before, after in records:
        obj = bpy.data.objects.new(record['replacement_node'], mesh)
        bpy.context.scene.collection.objects.link(obj)
        obj.matrix_world = Matrix.Identity(4)
        obj.select_set(True)
        exports.append(obj)
    bpy.context.view_layer.objects.active = exports[0]
    bpy.ops.export_scene.gltf(filepath=str(model), export_format='GLB', use_selection=True,
                              export_yup=True, export_animations=False, export_extras=False,
                              export_apply=False, export_texcoords=True, export_normals=True,
                              export_materials='EXPORT')
    with model.open('rb') as stream:
        magic, version, total = struct.unpack('<III', stream.read(12))
        size, kind = struct.unpack('<II', stream.read(8))
        if magic != 0x46546c67 or version != 2 or total != model.stat().st_size or kind != 0x4e4f534a:
            raise ValueError('Malformed station replacement GLB')
        document = json.loads(stream.read(size))
    if len(document['nodes']) != 3 or len(document['meshes']) != 3:
        raise ValueError('Expected three replacement nodes/meshes')
    expected = {row[0]['replacement_node'] for row in records}
    if {node.get('name') for node in document['nodes']} != expected:
        raise ValueError('Station replacement node roster drift')
    for node in document['nodes']:
        if 'children' in node or node.get('translation', [0, 0, 0]) != [0, 0, 0] or node.get('rotation', [0, 0, 0, 1]) != [0, 0, 0, 1] or node.get('scale', [1, 1, 1]) != [1, 1, 1] or 'matrix' in node:
            raise ValueError('Nonidentity station replacement node')
        if len(document['meshes'][node['mesh']]['primitives']) != 1:
            raise ValueError('Replacement surface/material count drift')
    digest = hashlib.sha256(model.read_bytes()).hexdigest()
    for record, *_ in records:
        record['model_sha256'] = digest
    return digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--station', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    if hashlib.sha256(args.station.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError('Station source hash drift')
    bpy.ops.wm.open_mainfile(filepath=str(args.station))
    bpy.context.scene.frame_set(1)
    bpy.context.view_layer.update()
    records = apply_station_clearance()
    digest = export_component(records, args.output)
    if hashlib.sha256(args.station.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError('Station master changed')
    result = {'schema_version': 1, 'source_sha256': SOURCE_SHA256, 'model_sha256': digest,
              'coordinate_contract': COORDINATE_CONTRACT, 'corrections': [r[0] for r in records],
              'geometry_digest_contract': 'SHA256 compact JSON {vertices_micrometres,triangles}; evaluated source node-local axes (x,z,-y), ties-to-even micron rounding; provenance only, not imported render buffers',
              'topology': [{'id': r[0]['id'], 'before': r[3], 'after': r[4]} for r in records],
              'source_unchanged': True, 'checks': {'finite_buffers': True, 'bore_manifold_capped': True, 'identity_three_mesh_glb': True},
              'limits': ['Pipe endpoints retain authored open tube boundary topology. No pressure, seal, load rating or service-flow certification is claimed.',
                         'Service bores have 2mm nominal radial clearance around the18mm tube; no sealing collar is modeled.',
                         'Motion, fixed-neighbor, body-route and floor-support qualification is separate.']}
    (args.output / 'station-clearance-checks.json').write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
    print('STATION_CLEARANCE_COMPONENT', digest, flush=True)


if __name__ == '__main__':
    main()
