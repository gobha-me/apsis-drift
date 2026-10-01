#!/usr/bin/env python3
"""Bounded D1 corrected route, service bores and supported-route proof. BSD-3-Clause."""
import argparse
import hashlib
from pathlib import Path
import sys

import bpy
import numpy as np
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_boarding_contact import CONVERT, SOURCE_HASHES, station_controls, write_json
from qualify_boarding_contact import cache_scene, candidates
from qualify_boarding_intersections import pair_details
from station_clearance_blender import apply_station_clearance


def triangle_box_count(triangles, low, high):
    """Exact triangle/AABB separating axes; also catches contained triangles."""
    half = (high - low) * .5
    t = triangles - (high + low) * .5
    possible = np.all(t.max(axis=1) >= -half, axis=1) & np.all(t.min(axis=1) <= half, axis=1)
    t = t[possible]
    if not len(t):
        return 0
    edges = (t[:, 1] - t[:, 0], t[:, 2] - t[:, 1], t[:, 0] - t[:, 2])
    axes = [np.cross(edges[0], edges[1])]
    axes += [np.cross(edge, axis) for edge in edges for axis in np.eye(3)]
    keep = np.ones(len(t), dtype=bool)
    for axis in axes:
        projection = np.einsum('tvi,ti->tv', t, axis)
        radius = np.abs(axis) @ half
        keep &= (projection.min(axis=1) <= radius) & (projection.max(axis=1) >= -radius)
    return int(keep.sum())


def floor_height(triangles, x, z, minimum_height=-.030, maximum_height=.030):
    e1, e2 = triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0]
    den = e1[:, 0] * e2[:, 2] - e1[:, 2] * e2[:, 0]
    ids = np.flatnonzero(np.abs(den) > 1e-12)
    dx, dz = x - triangles[ids, 0, 0], z - triangles[ids, 0, 2]
    u = (dx * e2[ids, 2] - dz * e2[ids, 0]) / den[ids]
    v = (e1[ids, 0] * dz - e1[ids, 2] * dx) / den[ids]
    inside = (u >= -1e-8) & (v >= -1e-8) & (u + v <= 1 + 1e-8)
    ids, u, v = ids[inside], u[inside], v[inside]
    height = triangles[ids, 0, 1] + u * e1[ids, 1] + v * e2[ids, 1]
    good = (height >= minimum_height) & (height <= maximum_height)
    return float(height[good].max()) if good.any() else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('station', 'closure', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    for path, identity in ((args.station, 'station_reference_sha256'), (args.closure, 'station_closure_sha256')):
        if hashlib.sha256(path.read_bytes()).hexdigest() != SOURCE_HASHES[identity]:
            raise ValueError('Station clearance source drift')
    bpy.ops.wm.open_mainfile(filepath=str(args.station))
    bpy.context.scene.frame_set(1)
    bpy.context.view_layer.update()
    controls = {o.name: f'station_d1_{i:02}' for i, o in enumerate(station_controls(bpy.context.scene))}
    original = cache_scene('station', controls)
    original_rows = {r['obj']['name']: r for r in candidates(original, {})}
    apply_station_clearance()
    corrected = cache_scene('station', controls)
    rows = {r['obj']['name']: r for r in candidates(corrected, {})}
    pipe = rows['DK09 | equalization pipe.001']
    # The admitted C++ station domain is a centre corridor; full union below
    # proves the service route doesn't introduce a hidden body obstruction.
    # Walking itself stops before unsupported well, x approximately -22.4167.
    standing_low, standing_high = np.array([-22.7367, .045, -.32]), np.array([-18.48, 1.975, .32])
    body_contacts = []
    for row in rows.values():
        t = row['vertices'][row['obj']['faces']]
        count = triangle_box_count(t, standing_low, standing_high)
        if count:
            body_contacts.append({'object': row['obj']['name'], 'triangles': count})
    floor = np.concatenate([row['vertices'][row['obj']['faces']] for row in rows.values()])
    supports = []
    for x in np.linspace(-22.4167, -18.8, 121):
        probes = [floor_height(floor, float(x) + dx, dz) for dx, dz in ((0, 0), (-.18, 0), (.18, 0), (0, -.18), (0, .18))]
        supports.append({'x_metres': float(x), 'heights_metres': probes})
    negative_controls = []
    for name in ('DK09 | continuous deck with open well.001', 'DK09 | belly shaft mounting flange.001'):
        detail = pair_details(pipe, original_rows[name])
        negative_controls.append({'name': 'restore_unbored_' + name, 'detected': detail['strict_crossing_pairs'] > 0, 'detail': detail})
    for name in ('DK09 | sliding deck hatch 1.001', 'DK09 | capture witness.004'):
        detail = pair_details(original_rows['DK09 | equalization pipe.001'], original_rows[name])
        negative_controls.append({'name': 'restore_original_pipe_' + name, 'detected': detail['strict_crossing_pairs'] > 0, 'detail': detail})
    rests = {controls[o.name]: o.matrix_world.copy() for o in station_controls(bpy.context.scene)}
    bpy.ops.wm.open_mainfile(filepath=str(args.closure))
    moving = {o.name: o for o in station_controls(bpy.context.scene)}
    poses = []
    for p in np.linspace(0, 1, 81):
        frame = 110 + 190 * float(p)
        bpy.context.scene.frame_set(int(frame), subframe=frame - int(frame))
        bpy.context.view_layer.update()
        conversion = Matrix.Translation((-.97, 0, .978)) @ CONVERT
        deltas = {controls[name]: conversion @ obj.matrix_world @ rests[controls[name]].inverted() @ conversion.inverted() for name, obj in moving.items()}
        contacts = []
        for row in candidates(corrected, deltas):
            if row['obj']['name'] == pipe['obj']['name'] or np.any(row['low'] > pipe['high']) or np.any(row['high'] < pipe['low']):
                continue
            detail = pair_details(pipe, row)
            if detail['bvh_triangle_pairs']:
                contacts.append({'object': row['obj']['name'], 'detail': detail})
        poses.append({'progress': float(p), 'frame': frame, 'contacts': contacts})
    unexpected = [dict(pose, contacts=[r for r in pose['contacts'] if r['object'] != 'DK09 | equalization service cabinet.001']) for pose in poses]
    unexpected = [p for p in unexpected if p['contacts']]
    # Exact point/radius proof for the two bore axes and endpoint-preserving
    # correction is in the source component receipt; no fixed pairs are waived.
    result = {'schema_version': 1, 'sources': SOURCE_HASHES, 'source_unchanged': True,
              'pipe_closure_samples': 81, 'poses': poses, 'unexpected_contacts': unexpected,
              'intentional_endpoint_pair': ['DK09 | equalization pipe.001', 'DK09 | equalization service cabinet.001'],
              'endpoint_reason': 'The unchanged first control point (.85m height) terminates inside the authored service cabinet (.84m bottom); this exact service interface persists in the original and corrected route.',
              'standing_sweep_bounds_station_metres': [standing_low.tolist(), standing_high.tolist()],
              'standing_contacts': body_contacts, 'floor_support_samples': supports,
              'negative_controls': negative_controls,
              'limits': ['Pipe vs moving hardware:81 sampled exact triangle-pair tests, not continuous rotation separation.',
                         'Standing corridor is a conservative full AABB union, including contained faces; floor support is121samples×5actual triangle probes over the affected D1 route, not an invisible floor.',
                         'No sealing collar, pressure/load rating, articulated climb, body entry or seat reach is certified.']}
    for path, identity in ((args.station, 'station_reference_sha256'), (args.closure, 'station_closure_sha256')):
        if hashlib.sha256(path.read_bytes()).hexdigest() != SOURCE_HASHES[identity]:
            raise ValueError('Station master changed')
    result['pass'] = not unexpected and not body_contacts and all(all(v is not None for v in row['heights_metres']) for row in supports) and all(row['detected'] for row in negative_controls)
    write_json(args.output / 'station-clearance-qualification.json', result)
    print('STATION_CLEARANCE_QUALIFICATION', result['pass'], 'unexpected', len(unexpected), 'body', len(body_contacts), flush=True)


if __name__ == '__main__':
    main()
