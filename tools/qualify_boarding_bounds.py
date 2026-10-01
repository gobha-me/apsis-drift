#!/usr/bin/env python3
"""Conservative source bounds and restored-collision controls. BSD-3-Clause."""
import argparse
import hashlib
from pathlib import Path
import sys

import bpy
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_boarding_contact import SOURCE_HASHES, mesh_data, station_controls, write_json
from qualify_boarding_contact import cache_scene, candidates
from qualify_boarding_intersections import pair_details
from qualify_station_clearance import triangle_box_count, floor_height
from station_clearance_blender import apply_station_clearance
import wayfarer_operating_blender as operating


def row(obj):
    vertices, faces = mesh_data(obj, 'craft')
    return {'obj': {'name': obj.name, 'faces': faces}, 'vertices': vertices, 'tree': None}


def inspect(rows, low, high):
    contacts = []
    for value in rows:
        triangles = value['vertices'][value['obj']['faces']]
        count = triangle_box_count(triangles, np.array(low), np.array(high))
        if count:
            contacts.append({'object': value['obj']['name'], 'triangles': count})
    return contacts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('station', 'craft', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    paths = {'station_reference_sha256': args.station, 'wayfarer_sha256': args.craft}
    for identity, path in paths.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != SOURCE_HASHES[identity]:
            raise ValueError('Bounds source drift')
    bpy.ops.wm.open_mainfile(filepath=str(args.station))
    bpy.context.scene.frame_set(1)
    bpy.context.view_layer.update()
    apply_station_clearance()
    controls = {o.name: f'station_d1_{i:02}' for i, o in enumerate(station_controls(bpy.context.scene))}
    station = candidates(cache_scene('station', controls), {})
    controller = operating.OperatingPoseController.prepare(args.craft).freeze()
    craft = cache_scene('craft')
    controller.apply('roof_transfer', 1.)
    current = candidates(craft, controller.deltas())
    combined = list(station)
    for value in current:
        vertices = value['vertices']
        transformed = np.column_stack((-28.32 + vertices[:, 2], -4.257 + vertices[:, 1], -vertices[:, 0]))
        combined.append(dict(value, vertices=transformed, tree=None))
    # Complete pure vertical union, not merely upper-shaft samples.
    shaft_low, shaft_high = [-23.32, -4.317, -.30], [-22.92, 1.99, .30]
    shaft = inspect(combined, shaft_low, shaft_high)
    standing_low, standing_high = [-23.44, -4.312, -.32], [-22.80, -2.382, .32]
    standing = inspect(combined, standing_low, standing_high)
    inner_low, inner_high = [-.32, -.055, 3.48], [.32, 1.875, 5.52]
    inner_cases = []
    for roof in (0., 1.):
        controller.apply('roof_transfer', roof)
        controller.apply('inner_door', 1., reset=False)
        current = candidates(craft, controller.deltas())
        inner_cases.append({'roof_progress': roof, 'inner_progress': 1., 'contacts': inspect(current, inner_low, inner_high)})
    floor_triangles = np.concatenate([v['vertices'][v['obj']['faces']] for v in current])
    floor_triangles[:, :, 1] += .1
    normals = np.cross(floor_triangles[:, 1] - floor_triangles[:, 0], floor_triangles[:, 2] - floor_triangles[:, 0])
    lengths = np.linalg.norm(normals, axis=1)
    horizontal = (np.abs(normals[:, 1]) >= .99 * lengths) & (lengths > 1e-12)
    floor_triangles = floor_triangles[horizontal]
    supports = []
    for z in np.linspace(3.8, 5.2, 61):
        heights = [floor_height(floor_triangles, dx, float(z) + dz) for dx, dz in ((0, 0), (-.18, 0), (.18, 0), (0, -.18), (0, .18))]
        supports.append({'craft_back_axis_metres': float(z), 'heights_above_foot_metres': heights})
    missing_floor = [float(z) for z in np.linspace(3.60, 3.70, 1001) if floor_height(floor_triangles, 0., float(z)) is None]
    threshold_heights = [{'craft_back_axis_metres': float(z),
                          'height_relative_to_nominal_foot_metres': floor_height(floor_triangles, 0., float(z), -.30, .030)}
                         for z in np.linspace(3.60, 3.70, 101)]
    negative_controls = []
    controller.apply('inner_door', 0.)
    closed_contacts = inspect(candidates(craft, controller.deltas()), inner_low, inner_high)
    negative_controls.append({'name': 'closed_inner_door_blocks_standing_union', 'detected': bool(closed_contacts)})
    original_fraction = operating.INNER_AUTHORED_FRACTION
    try:
        operating.INNER_AUTHORED_FRACTION = .60
        controller.apply('inner_door', 1.)
        a = row(bpy.data.objects['AFT01 | leaf mechanical lower sill -1 0'])
        b = row(bpy.data.objects['AFT01 | pressure frame -1 -4.33'])
        detail = pair_details(a, b)
        negative_controls.append({'name': 'restore_inner_authored_fraction_0.60_hits_frame', 'detected': detail['strict_crossing_pairs'] > 0})
    finally:
        operating.INNER_AUTHORED_FRACTION = original_fraction
    controller.apply('roof_transfer', .20)
    link = bpy.data.objects['AFT01 | hatch hinge link -1 -0.2']
    link.location.x -= .025
    bpy.context.view_layer.update()
    detail = pair_details(row(link), row(bpy.data.objects['AFT01 | pressure ceiling with circular hatch opening']))
    negative_controls.append({'name': 'restore_original_hinge_link_hits_pressure_ceiling', 'detected': detail['strict_crossing_pairs'] > 0})
    link.location.x += .025
    controller.apply('seat_boarding', .575)
    bolt = bpy.data.objects['Swivel mounting bolt']
    original_location = bolt.location.copy()
    bolt.location.x, bolt.location.y = .24, 0.
    bpy.context.view_layer.update()
    detail = pair_details(row(bolt), row(bpy.data.objects['Swivel lock pin']))
    negative_controls.append({'name': 'restore_original_mounting_bolt_hits_withdrawing_pin', 'detected': detail['strict_crossing_pairs'] > 0})
    bolt.location = original_location
    withdrawal = operating.LOCK_WITHDRAWAL_METRES
    try:
        operating.LOCK_WITHDRAWAL_METRES = 0.
        controller.apply('seat_boarding', .80)
        detail = pair_details(row(bpy.data.objects['Swivel lock pin']), row(bpy.data.objects['Lift outer guide']))
        negative_controls.append({'name': 'retain_locked_pin_hits_rotating_lift_guide', 'detected': detail['strict_crossing_pairs'] > 0})
    finally:
        operating.LOCK_WITHDRAWAL_METRES = withdrawal
    for identity, path in paths.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != SOURCE_HASHES[identity]:
            raise ValueError('Master changed in bounds proof')
    result = {'schema_version': 1, 'sources': SOURCE_HASHES, 'source_unchanged': True,
              'shaft': {'dimensions_metres': [.60, .40, 1.95], 'foot_clearance_metres': .040,
                        'foot_interval_metres': [0., -4.357], 'continuous_union_bounds_station_metres': [shaft_low, shaft_high], 'contacts': shaft},
              'standing_transition': {'dimensions_metres': [.64, .64, 1.93], 'foot_clearance_metres': .045,
                                      'bounds_station_metres': [standing_low, standing_high], 'contacts': standing},
              'inner_standing_sweep': {'bounds_craft_metres': [inner_low, inner_high], 'cases': inner_cases, 'floor_support': supports, 'threshold_missing_support_interval_metres': [min(missing_floor), max(missing_floor)] if missing_floor else [],
                                      'threshold_probe_heights_metres': threshold_heights},
              'negative_controls': negative_controls, 'closed_inner_contacts': closed_contacts,
              'limits': ['Continuous reservation means the exact pure axis-aligned box union against source triangles, including contained faces. It does not prove articulated body or limb reach.',
                         'Only roof/ladder deployed and the source-corrected rest cabin are used for full shaft clearance; no AG, EVA, player entry or climb admission is provided.',
                         '61×5floor probes are actual triangle support observations, not continuous terrain proof or additional floor geometry.']}
    result['geometry_pass'] = not shaft and not standing and all(not r['contacts'] for r in inner_cases)
    result['floor_support_complete'] = all(all(h is not None for h in r['heights_above_foot_metres']) for r in supports)
    result['pass'] = not shaft and not standing and all(not r['contacts'] for r in inner_cases) and all(all(h is not None for h in r['heights_above_foot_metres']) for r in supports) and all(r['detected'] for r in negative_controls)
    write_json(args.output / 'bounds-proof.json', result)
    write_json(args.output / 'negative-controls.json', {'negative_controls': negative_controls})
    print('BOARDING_BOUNDS', result['pass'], 'shaft', shaft, 'standing', standing, 'inner', inner_cases, 'controls', negative_controls, flush=True)


if __name__ == '__main__':
    main()
