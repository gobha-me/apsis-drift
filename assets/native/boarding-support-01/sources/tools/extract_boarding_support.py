#!/usr/bin/env python3
"""Recover exact operating-02 triangle ownership and bounded contact candidates.

Run in Blender. Source masters and the admitted package are read-only. The
output does not grant collision exceptions or certify an actor's reach.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def ranges(values):
    result = []
    for value in sorted(set(int(v) for v in values)):
        if result and result[-1][0] + result[-1][1] == value:
            result[-1][1] += 1
        else:
            result.append([value, 1])
    return result


def label(name):
    if any(s in name for s in ('ladder rung', 'docking throat rung')):
        return 'rung'
    if name.startswith('AFT01 | grab handle '):
        return 'handhold'
    if name in ('Replaceable pan cushion', 'Lumbar pad', 'Upper back pad', 'Headrest pad', 'Elbow pad', 'Elbow pad.001'):
        return 'cushion'
    if 'contoured palm grip' in name:
        return 'control_grip'
    if name.startswith(('Rudder / brake pedal', 'Pedal grip rib')):
        return 'pedal'
    if any(s in name.lower() for s in ('stringer', 'rail', 'shell', 'bearing', 'hinge', 'bolt', 'lock', 'guide', 'pivot', 'stanchion')):
        return 'forbidden_structure_or_mechanism'
    return 'unclassified_obstacle'


def candidates(record, obj, vertices, faces, triangle_ids):
    """Only nominate actual surface triangles; never invent support rectangles."""
    kind = record['semantic_label']
    if kind not in ('rung', 'handhold', 'cushion', 'control_grip', 'pedal'):
        return []
    triangles = vertices[faces]
    cross = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    length = np.linalg.norm(cross, axis=1)
    normals = cross / np.maximum(length[:, None], 1e-20)
    centers = triangles.mean(axis=1)
    lo, hi = vertices.min(axis=0), vertices.max(axis=0)
    name = obj.name
    tree = BVHTree.FromPolygons(vertices.tolist(), faces.tolist(), all_triangles=True)
    result = []

    def make(role, mask, direction=None, predicate=None):
        valid = mask & (length > 1e-12)
        # Test first visible surface from the contact side independently of the
        # source winding. Some historical cushion meshes have inward winding.
        if direction is not None:
            direction = np.asarray(direction, dtype=float)
            direction /= np.linalg.norm(direction)
            depth = float(np.linalg.norm(hi - lo) + .1)
            for i in np.flatnonzero(valid):
                hit = tree.ray_cast(Vector(centers[i] + direction * depth), Vector(-direction), depth + .01)
                if hit[0] is None or np.linalg.norm(np.asarray(hit[0]) - centers[i]) > .00002:
                    valid[i] = False
        ids = np.flatnonzero(valid)
        if not len(ids):
            return
        weights = length[ids]
        center = np.average(centers[ids], axis=0, weights=weights)
        # Use a point on a real triangle, not the average suspended over a curve.
        nearest = ids[np.argmin(np.linalg.norm(centers[ids] - center, axis=1))]
        patch = {'id': record['group'] + '/' + name + '/' + role,
                 'role': role, 'body_contact': {'boot_support': 'boot sole', 'hand_grasp': 'hand/fingers',
                     'seat_pan': 'pelvis/thigh', 'back_rest': 'back', 'head_rest': 'helmet/head',
                     'elbow_rest': 'elbow/forearm', 'control_palm': 'hand', 'pedal_sole': 'boot sole'}[role],
                 'triangle_ranges': ranges(triangle_ids[ids]), 'triangle_count': len(ids),
                 'surface_area_m2': float(weights.sum() / 2),
                 'representative_triangle': int(triangle_ids[nearest]),
                 'representative_barycentric': [1/3, 1/3, 1/3],
                 'representative_contact_point_rest_m': centers[nearest].tolist(),
                 'bounds_rest_m': [triangles[ids].min(axis=(0, 1)).tolist(), triangles[ids].max(axis=(0, 1)).tolist()],
                 'status': 'geometry candidate only; engine must constrain contact body, side, point, pose and penetration'}
        if direction is not None:
            patch['contact_side_direction_rest'] = direction.tolist()
            patch['opposite_winding_triangles'] = int(np.count_nonzero(normals[ids] @ direction < 0))
        if predicate:
            patch['required_point_predicate'] = predicate
        result.append(patch)

    if kind in ('rung', 'handhold'):
        # Infer the measured long axis of this straight rod, not an assumed
        # world axis. This remains valid for the stowed swivel ladder.
        center = vertices.mean(axis=0)
        _, _, axes = np.linalg.svd(vertices - center, full_matrices=False)
        axis = axes[0]
        if axis[np.argmax(np.abs(axis))] < 0:
            axis = -axis
        along = (vertices - center) @ axis
        interval = [float(along.min() + .03), float(along.max() - .03)]
        assert interval[1] > interval[0], name
        predicate = {'type': 'rest_axis_interval', 'origin_rest_m': center.tolist(), 'unit_axis_rest': axis.tolist(),
                     'inclusive_interval_m': interval, 'end_margin_m': .03,
                     'rule': 'Inverse-transform the candidate contact point to group rest, then dot(point-origin,axis) must lie in interval. Triangle membership alone is insufficient.'}
        side = np.abs(normals @ axis) < .25
        make('hand_grasp', side, predicate=predicate)
        if kind == 'rung':
            make('boot_support', side & (np.abs(normals[:, 1]) >= .65), [0, 1, 0], predicate)
    else:
        # Source local axes follow the actual object's corrected rest matrix.
        # Local +Y is the cushion face toward the pilot; local +Z is seat/pedal up.
        source_axis = np.array([0., 1., 0.]) if name in ('Lumbar pad', 'Upper back pad', 'Headrest pad') else np.array([0., 0., 1.])
        direction = np.array(obj.matrix_world)[:3, :3] @ source_axis
        direction = direction[[0, 2, 1]] * [1, 1, -1]
        direction /= np.linalg.norm(direction)
        if kind == 'control_grip':
            # Named palm grip itself only. Buttons, bellows, stem, shell and
            # armrest stay obstacles. No authored palm-contact patch exists.
            record['candidate_omission'] = 'Named grip located; no certified palm surface. Bounds are not contact permission.'
            return []
        role = ('pedal_sole' if kind == 'pedal' else 'seat_pan' if name == 'Replaceable pan cushion'
                else 'head_rest' if name == 'Headrest pad' else 'elbow_rest' if name.startswith('Elbow pad') else 'back_rest')
        make(role, np.abs(normals @ direction) >= .65, direction)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('engine-root', 'craft', 'station', 'motion-fixtures', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    if args.output.exists():
        raise ValueError('Use a fresh output directory')
    engine = args.engine_root.resolve()
    package = engine / 'assets/native/wayfarer-operating-02'
    prior = json.loads((args.motion_fixtures / 'provenance.json').read_text())
    bindings = prior['engine_bindings_sha256']
    for path, digest in bindings.items():
        assert sha(engine / path) == digest, path
    manifest = json.loads((package / 'package.json').read_text())
    for row in manifest['files']:
        assert sha(package / row['path']) == row['sha256'], row['path']
    contact = json.loads((package / 'metadata/contact.json').read_text())
    assert sha(args.craft) == contact['sources']['wayfarer_sha256']
    assert sha(args.station) == contact['sources']['station_reference_sha256']
    sys.path.insert(0, str(engine / 'tools'))
    import export_boarding_contact as source
    from wayfarer_operating_blender import OperatingPoseController
    from wayfarer_operating_spec import classify_motion_group
    from station_clearance_blender import apply_station_clearance

    expected = {g['id']: g for g in contact['groups']}
    built = {}
    records = []
    for owner, path in [('station', args.station), ('craft', args.craft)]:
        if owner == 'craft':
            controller = OperatingPoseController.prepare(path).freeze()
            corrections = controller.derivative_corrections
        else:
            bpy.ops.wm.open_mainfile(filepath=str(path))
            bpy.context.scene.frame_set(1)
            bpy.context.view_layer.update()
            apply_station_clearance()
            controls = {o.name: f'station_d1_{i:02}' for i, o in enumerate(source.station_controls(bpy.context.scene))}
        selected = sorted((o for o in bpy.context.scene.objects if (source.station_keep(o) if owner == 'station' else source.craft_keep(o))), key=lambda o: o.name)
        crop_lo, crop_hi = np.array(source.CROPS[owner])
        for obj in selected:
            ancestry = source.ancestors(obj)
            moving = (classify_motion_group(ancestry) if owner == 'craft' else next((controls[n] for n in ancestry if n in controls), None))
            vertices, faces = source.mesh_data(obj, owner)
            total_source_faces = len(faces)
            if moving is None:
                triangles = vertices[faces]
                keep = np.all(triangles.max(axis=1) >= crop_lo, axis=1) & np.all(triangles.min(axis=1) <= crop_hi, axis=1)
                faces = faces[keep]
            if not len(faces):
                continue
            identity = f'{owner}_{moving}' if moving else owner + '_fixed'
            group = built.setdefault(identity, {'vertices': [], 'triangles': [], 'map': {}, 'objects': []})
            start = len(group['triangles'])
            kept_faces = []
            for face in faces:
                ids = []
                for vertex in vertices[face]:
                    key = tuple(int(round(float(value) * 1000000)) for value in vertex)
                    if key not in group['map']:
                        group['map'][key] = len(group['vertices'])
                        group['vertices'].append(key)
                    ids.append(group['map'][key])
                if len(set(ids)) == 3:
                    group['triangles'].append(ids)
                    kept_faces.append(face)
            end = len(group['triangles'])
            assert expected[identity]['triangles'][start:end] == group['triangles'][start:end], (identity, obj.name, 'triangles')
            group['objects'].append(obj.name)
            if kept_faces:
                points = vertices[np.array(kept_faces)].reshape(-1, 3)
                bounds = [points.min(axis=0).tolist(), points.max(axis=0).tolist()]
            else:
                bounds = None
            row = {'source_object': obj.name, 'group': identity, 'owner': owner, 'motion_group': moving,
                   'triangle_start': start, 'triangle_count': end-start,
                   'semantic_label': label(obj.name), 'default_collision': 'obstacle',
                   'source_evaluated_triangles': total_source_faces,
                   'crop_retained_triangles': len(faces), 'quantized_degenerate_triangles_removed': len(faces)-(end-start),
                   'bounds_rest_m': bounds,
                   'source_corrected_world_rows': [list(v) for v in obj.matrix_world],
                   'candidate_patches': []}
            if kept_faces and row['semantic_label'] in ('rung', 'handhold', 'cushion', 'control_grip', 'pedal'):
                # Patches derive from the exact admitted quantized geometry.
                qv = np.array(group['vertices'], dtype=float) * 1e-6
                qf = np.array(group['triangles'][start:end], dtype=np.int32)
                used, inverse = np.unique(qf, return_inverse=True)
                row['candidate_patches'] = candidates(row, obj, qv[used], inverse.reshape(-1, 3), np.arange(start, end))
            records.append(row)
        print('SUPPORT_EXTRACTED', owner, 'objects', len(records), flush=True)
    assert set(built) == set(expected)
    for identity, group in built.items():
        exp = expected[identity]
        assert np.array_equal(group['vertices'], exp['vertices_micrometres']), (identity, 'vertices')
        assert group['triangles'] == exp['triangles'], (identity, 'triangles')
        assert group['objects'] == exp['source_objects'], (identity, 'objects')
    manifest_corrections = json.loads((package / 'metadata/wayfarer-operating-02.json').read_text())['derivative_corrections']
    assert corrections == manifest_corrections
    for path, digest in bindings.items():
        assert sha(engine / path) == digest, path
    assert sha(args.craft) == contact['sources']['wayfarer_sha256']
    assert sha(args.station) == contact['sources']['station_reference_sha256']
    report = {'schema_version': 1, 'id': 'wayfarer-boarding-support-01',
        'status': 'Exact source ownership; contact patches are bounded proposals, not accepted actor contacts',
        'contact_sha256': sha(package / 'metadata/contact.json'),
        'models': {key: manifest[key] for key in ('model', 'station_model')},
        'sources': contact['sources'], 'coordinate_contracts': contact['coordinate_contracts'],
        'quantization_metres': contact['quantization_metres'],
        'triangle_numbering': 'Zero-based within contact.json group.triangles; all [start,count] ranges are half-open',
        'source_rest': {'craft': 'Source frame100, OperatingPoseController.prepare().freeze(); seat +0.08m source Y and +0.18m source Z; declared derivative corrections baked once.',
                        'station': 'Reference frame1 with the three admitted D1 clearance corrections applied once.'},
        'motion_contract': 'Coordinates and triangle IDs are group-rest baked. Apply the existing canonical group motion delta once; do not reapply source local transforms or corrections.',
        'contact_policy': 'All triangles remain body obstacles. A candidate can permit only the named body part to touch its named triangles from the contact side when the pose/point predicate also passes. No volume penetration, whole-object/group exemption, reach claim or cushion compression is granted.',
        'patch_probe': 'Candidate faces come from admitted micrometre-quantized triangles. Face direction tolerance abs(dot)>=0.65; first-hit visibility from contact side within20 micrometres excludes hidden opposite faces. Straight rod grip excludes cap normals abs(dot(axis))>=0.25 and requires a30mm end margin at the contact point.',
        'checks': {'groups': len(expected), 'objects': len(records),
                   'triangles': sum(len(g['triangles']) for g in expected.values()),
                   'vertices': sum(len(g['vertices_micrometres']) for g in expected.values()),
                   'exact_all_vertices_triangles_and_object_order': True,
                   'candidate_patches': sum(len(r['candidate_patches']) for r in records)},
        'missing': ['No certified actor reach/posture/collision envelope or climb animation.',
                    'Control palm grips located by name and measured bounds; no precise palm face patch authored.',
                    'No named pedal faces survived in the admitted contact roster; no cockpit boot support is proposed.',
                    'Harness/buckle/webbing contact, compression and garment clearance unqualified; remain obstacles.',
                    'Visibility probes are within each source object, not clearance from neighboring objects. Existing harnesses, rails and shells retain full collision checks.',
                    'Static source crop can truncate surfaces; roster covers admitted triangles only.',
                    'No station upper-deck handhold or continuous complete boarding route invented.'],
        'objects': records}
    args.output.mkdir(parents=True)
    write(args.output / 'support.json', report)
    shutil.copytree(package / 'licenses', args.output / 'licenses')
    write(args.output / 'provenance.json', {'schema_version': 1,
        'engine_bindings_sha256': bindings, 'package_sha256': sha(package / 'package.json'),
        'extractor_sha256': sha(Path(__file__)),
        'artifacts_sha256': {str(p.relative_to(args.output)): sha(p) for p in sorted(args.output.rglob('*')) if p.is_file()},
        'scope': 'Source-bound triangle metadata only. Inherited five-document operating-02 license roster; no paid generation or model edits.'})
    print('BOARDING_SUPPORT_READY', report['checks'], flush=True)


if __name__ == '__main__':
    main()
