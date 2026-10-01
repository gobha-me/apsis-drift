#!/usr/bin/env python3
"""Source-bound contact and D1 closure extraction; masters are read-only.

Run Blender --background --python this_file -- --station station-reference.blend
--craft hopper-craft-09.blend --closure station-kit-09.blend
--runtime-proof report.json --output build-native/boarding-contact-01.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import bpy
import numpy as np
from mathutils import Matrix
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
from wayfarer_operating_spec import MOTION_RIGS, classify_motion_group
from wayfarer_operating_blender import OperatingPoseController
from station_clearance_blender import apply_station_clearance, COORDINATE_CONTRACT as CORRECTION_CONTRACT

SOURCE_HASHES = {
    'station_reference_sha256': '6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6',
    'wayfarer_sha256': '87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677',
    'station_closure_sha256': '6a12e1e6846be4de6c89ce0c65b154567a8a07818d37bf574de2a319109319b4',
}
MODEL_SHA = '79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80'
CONVERT = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))
CROPS = {'station': [[-24.8, -1.8, -1.65], [-18.4, 3.5, 1.65]],
         'craft': [[-1.05, -.20, -3.5], [1.05, 2.97, 6.35]]}
CONTRACTS = {'station': 'Blender(x,y,z)->(x-.97,z,-y+.978)',
             'craft': 'Blender(x,y,z)->(x,z,-y); source-rest world baked'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def columns(matrix):
    data = [[float(v) for v in matrix.col[i][:3]] for i in range(4)]
    if not np.isfinite(data).all():
        raise ValueError('Nonfinite source transform')
    return data


def ancestors(obj):
    result = []
    while obj:
        result.append(obj.name)
        obj = obj.parent
    return result


def station_keep(obj):
    excluded = {'KIT04 | full-detail Wayfarer boarding reference',
                'KIT09 | occupied berth fixtures', 'KIT10 | freighter'}
    return obj.type in ('MESH', 'CURVE', 'FONT') and not (
        obj.hide_render or any(c.name.endswith(('guides', 'gauges', 'work')) or
        c.name in excluded for c in obj.users_collection) or
        obj.name.startswith(('WF04 REF', 'WF09 REF', 'FRT10 |')) or
        obj.get('service19_group') in ['engine', 'replacement'])


def craft_keep(obj):
    if obj.type not in ('MESH', 'CURVE', 'FONT') or obj.hide_render or obj.hide_get() or obj.hide_viewport:
        return False
    if obj.name.startswith(('FIT |', 'REVIEW |', 'SENSOR |')):
        return False
    if obj.name.startswith('WF03 |') and (' UI ' in obj.name or 'flight attitude' in obj.name or 'flight vector' in obj.name):
        return False
    return obj.name not in ['WF02 | ' + role + ' display' for role in ('NAV', 'FLIGHT', 'SYSTEMS')]


def station_controls(scene):
    selected = [o for o in scene.objects if o.get('dock_id') == 'D1' and (
        o.get('dock_part') in ('deck hatch', 'hatch gasket', 'capture carriage', 'safety gate') or
        'dock_leaf_index' in o or o.get('role') == 'dock lock bolt')]
    selected.sort(key=lambda o: o.name)
    if len(selected) != 17:
        raise ValueError('Expected seventeen D1 controls')
    names = [o.name.replace('.', '_') for o in selected]
    if len(set(names)) != 17:
        raise ValueError('Sanitized D1 name collision')
    return selected


def properties(obj):
    return {key: obj[key] for key in ('dock_id', 'dock_part', 'dock_leaf_index', 'role', 'hatch_side') if key in obj}


def mesh_data(obj, owner):
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    mesh = evaluated.to_mesh()
    try:
        mesh.calc_loop_triangles()
        vertices = np.empty((len(mesh.vertices), 3), dtype=np.float64)
        mesh.vertices.foreach_get('co', vertices.ravel())
        matrix = np.array(obj.matrix_world)
        vertices = vertices @ matrix[:3, :3].T + matrix[:3, 3]
        vertices = vertices[:, [0, 2, 1]] * [1, 1, -1]
        if owner == 'station': vertices += [-.97, 0, .978]
        faces = np.empty((len(mesh.loop_triangles), 3), dtype=np.int32)
        mesh.loop_triangles.foreach_get('vertices', faces.ravel())
        if not np.isfinite(vertices).all() or (len(faces) and (faces.min() < 0 or faces.max() >= len(vertices))):
            raise ValueError('Invalid evaluated source mesh')
        return vertices, faces
    finally:
        evaluated.to_mesh_clear()


def write_json(path, data, compact=False):
    encoded = (json.dumps(data, separators=(',', ':') if compact else None,
                          indent=None if compact else 2, allow_nan=False) + '\n').encode()
    if len(encoded) > 64 * 1024 * 1024:
        raise ValueError('Contact package file byte budget exceeded')
    path.write_bytes(encoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('station', 'craft', 'closure', 'runtime-proof', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    paths = {'station_reference_sha256': args.station, 'wayfarer_sha256': args.craft,
             'station_closure_sha256': args.closure}
    for key, path in paths.items():
        if sha(path) != SOURCE_HASHES[key]: raise ValueError('Stale ' + key)
    proof = json.loads(args.runtime_proof.read_text())
    if proof['count'] != 17 or proof['problems'] or proof['glb_sha256'] != MODEL_SHA or not proof['glb_unchanged']:
        raise ValueError('Unqualified runtime station proof')
    runtime = {row['source_name']: row for row in proof['records']}
    if len(runtime) != 17: raise ValueError('Duplicate runtime proof source')
    args.output.mkdir(parents=True, exist_ok=True)
    component_proof = json.loads((args.output / 'station-clearance-checks.json').read_text())
    component_path = args.output / 'station-d1-clearance-01.glb'
    if sha(component_path) != component_proof['model_sha256'] or not component_proof['source_unchanged'] or not all(component_proof['checks'].values()):
        raise ValueError('Unqualified station replacement component')
    groups = {}; control_bindings = []; controls = {}; station_rest = {}
    for owner, path, frame in [('station', args.station, 1), ('craft', args.craft, 100)]:
        if owner == 'craft':
            # Render and contact share the same frozen, source-bound correction
            # helper. Never reproduce only a subset of its corrected rest pose.
            controller = OperatingPoseController.prepare(path).freeze()
            scene = bpy.context.scene
            motion = MOTION_RIGS
        else:
            bpy.ops.wm.open_mainfile(filepath=str(path)); scene = bpy.context.scene
            scene.frame_set(frame); bpy.context.view_layer.update()
            corrected = apply_station_clearance()
            expected = {row['id']: row for row in component_proof['corrections']}
            for row, *_ in corrected:
                produced = dict(expected[row['id']]); produced['model_sha256'] = ''
                if row != produced:
                    raise ValueError('Render/contact station correction disagreement')
            for index, obj in enumerate(station_controls(scene)):
                identifier = f'station_d1_{index:02}'
                controls[obj.name] = identifier
                native = runtime[obj.name]
                if native['name'] != obj.name.replace('.', '_') or native['parent'] != obj.parent.name.replace('.', '_'):
                    raise ValueError('Runtime parent/name drift')
                extras = properties(obj)
                if any(native['metadata'].get('extras', {}).get(key) != value for key, value in extras.items()):
                    raise ValueError('Runtime extras drift')
                rest = columns(CONVERT @ obj.matrix_local @ CONVERT.inverted())
                if np.max(np.abs(np.array(rest) - native['transform_columns'])) > .00001:
                    raise ValueError('Runtime rest transform drift')
                station_rest[obj.name] = obj.matrix_local.copy()
                control_bindings.append({'id': identifier, 'source_object': obj.name,
                    'runtime_node': native['name'], 'runtime_parent': native['parent'],
                    'expected_extras': extras, 'rest_transform': rest, 'knots': []})
            motion = controls
        selected = sorted((obj for obj in scene.objects if (station_keep(obj) if owner == 'station' else craft_keep(obj))), key=lambda o: o.name)
        crop_lo, crop_hi = np.array(CROPS[owner])
        for obj in selected:
            ancestry = ancestors(obj)
            moving = (classify_motion_group(ancestry) if owner == 'craft' else
                      next((motion[name] for name in ancestry if name in motion), None))
            vertices, faces = mesh_data(obj, owner)
            if moving is None:
                triangles = vertices[faces]
                keep = np.all(triangles.max(axis=1) >= crop_lo, axis=1) & np.all(triangles.min(axis=1) <= crop_hi, axis=1)
                faces = faces[keep]
            if not len(faces): continue
            identifier = f'{owner}_{moving}' if moving else owner + '_fixed'
            group = groups.setdefault(identifier, {'id': identifier, 'owner': owner,
                'motion_group': moving, 'source_objects': [], 'vertices_micrometres': [], 'triangles': [], '_map': {}})
            group['source_objects'].append(obj.name)
            for face in faces:
                ids = []
                for vertex in vertices[face]:
                    key = tuple(int(round(float(value) * 1000000)) for value in vertex)
                    if key not in group['_map']:
                        group['_map'][key] = len(group['vertices_micrometres']); group['vertices_micrometres'].append(key)
                    ids.append(group['_map'][key])
                if len(set(ids)) == 3: group['triangles'].append(ids)
        print('CONTACT_SOURCE', owner, 'groups', len(groups), flush=True)
    for group in groups.values():
        del group['_map']
        print('CONTACT_GROUP', group['id'], len(group['vertices_micrometres']),len(group['triangles']),flush=True)
        if len(group['vertices_micrometres']) > 2000000 or len(group['triangles']) > 4000000:
            raise ValueError('Contact group buffer budget')
    if set(g['motion_group'] for g in groups.values() if g['owner'] == 'craft' and g['motion_group']) != set(MOTION_RIGS.values()):
        raise ValueError('Missing craft contact group')
    # Resolve all channels from the independent authored closure source.
    bpy.ops.wm.open_mainfile(filepath=str(args.closure)); scene = bpy.context.scene
    scene.frame_set(110); bpy.context.view_layer.update()
    source_controls = {o.name: o for o in station_controls(scene)}
    if set(source_controls) != set(controls): raise ValueError('Closure source control roster drift')
    for binding in control_bindings:
        obj = source_controls[binding['source_object']]
        scene.frame_set(110); bpy.context.view_layer.update()
        if np.max(np.abs(np.array(obj.matrix_local) - np.array(station_rest[obj.name]))) > .00001:
            raise ValueError('Independent closure open pose drift')
        frames = {110., 300.}
        if not obj.animation_data or not obj.animation_data.action:
            raise ValueError('Missing authored closure channel')
        for layer in obj.animation_data.action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    for curve in bag.fcurves:
                        for key in curve.keyframe_points:
                            frame = float(key.co.x)
                            if 110 <= frame <= 300:
                                if key.interpolation != 'LINEAR': raise ValueError('Unsupported closure interpolation')
                                frames.add(frame)
        for frame in sorted(frames):
            scene.frame_set(int(frame), subframe=frame-int(frame)); bpy.context.view_layer.update()
            binding['knots'].append({'progress': (frame-110)/190,
                                    'transform': columns(CONVERT @ obj.matrix_local @ CONVERT.inverted())})
    contact = {'schema_version': 1, 'sources': SOURCE_HASHES, 'coordinate_contracts': CONTRACTS,
        'quantization_metres': .000001, 'crop_bounds_metres': CROPS,
        'groups': sorted(groups.values(), key=lambda g: g['id'])}
    station = {'schema_version': 1, 'source_sha256': SOURCE_HASHES['station_reference_sha256'],
        'closure_source_sha256': SOURCE_HASHES['station_closure_sha256'], 'model_sha256': MODEL_SHA,
        'coordinate_contract': 'Godot metre node-local axes; Blender(x,y,z)->(x,z,-y)',
        'timeline_frames': [110, 300], 'attached_closed_progress': 160/190, 'bindings': control_bindings,
        'correction_coordinate_contract': CORRECTION_CONTRACT, 'corrections': component_proof['corrections']}
    for key,path in paths.items():
        if sha(path) != SOURCE_HASHES[key]: raise ValueError('Master changed during extraction')
    write_json(args.output/'contact.json', contact, True)
    write_json(args.output/'station-closure.json', station)
    print('BOARDING_CONTACT_EXTRACTED', len(groups), sum(len(g['triangles']) for g in groups.values()),flush=True)


if __name__ == '__main__': main()
