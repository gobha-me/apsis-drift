#!/usr/bin/env python3
"""Read-only Blender extraction of the admitted station's bounded walking mesh.

blender -b --python tools/export_origin_walk_geometry.py -- \
  --source path/to/station-reference.blend --output assets/native/origin-walk-01
No source file is saved or mutated. No visiting spacecraft is admitted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
import numpy as np

SOURCE_SHA = '6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6'
MODEL_SHA = '79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80'
LOW = np.array([-24.5, -.05, -.875])
HIGH = np.array([.45, 2.1, .875])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    before = hashlib.sha256(args.source.read_bytes()).hexdigest()
    if before != SOURCE_SHA:
        raise ValueError('Unqualified station source identity')
    bpy.ops.wm.open_mainfile(filepath=str(args.source.resolve()))
    bpy.context.scene.frame_set(1)
    bpy.context.view_layer.update()
    deps = bpy.context.evaluated_depsgraph_get()
    excluded = {'KIT04 | full-detail Wayfarer boarding reference',
                'KIT09 | occupied berth fixtures', 'KIT10 | freighter'}
    vertices, indices, sources = [], [], []
    vertex_map = {}
    for obj in sorted(bpy.context.scene.objects, key=lambda item: item.name):
        omit = obj.hide_render or any(
            collection.name.endswith(('guides', 'gauges', 'work')) or
            collection.name in excluded for collection in obj.users_collection)
        omit |= obj.name.startswith(('WF04 REF', 'WF09 REF', 'FRT10 |'))
        omit |= obj.get('service19_group') in ['engine', 'replacement']
        if omit or obj.type not in ('MESH', 'CURVE', 'FONT'):
            continue
        evaluated = obj.evaluated_get(deps)
        mesh = evaluated.to_mesh()
        try:
            mesh.calc_loop_triangles()
            local = np.empty((len(mesh.vertices), 3), dtype=np.float64)
            mesh.vertices.foreach_get('co', local.ravel())
            matrix = np.array(obj.matrix_world)
            world = local @ matrix[:3, :3].T + matrix[:3, 3]
            canonical = world[:, [0, 2, 1]] * [1, 1, -1] + [-.97, 0, .978]
            if not np.isfinite(canonical).all():
                raise ValueError('Non-finite source vertices')
            faces = np.empty((len(mesh.loop_triangles), 3), dtype=np.int32)
            mesh.loop_triangles.foreach_get('vertices', faces.ravel())
            if len(faces) and (faces.min() < 0 or faces.max() >= len(local)):
                raise ValueError('Invalid source triangle buffer')
            triangles = canonical[faces]
            selected = np.all(triangles.max(axis=1) >= LOW, axis=1)
            selected &= np.all(triangles.min(axis=1) <= HIGH, axis=1)
            count = 0
            for face in faces[selected]:
                ids = []
                for vertex in canonical[face]:
                    quantized = tuple(int(round(float(value) * 1_000_000)) for value in vertex)
                    if quantized not in vertex_map:
                        vertex_map[quantized] = len(vertices)
                        vertices.append(quantized)
                    ids.append(vertex_map[quantized])
                if len(set(ids)) == 3:
                    indices.append(ids)
                    count += 1
            if count:
                sources.append({'object': obj.name, 'triangles': count})
        finally:
            evaluated.to_mesh_clear()
    if not vertices or not indices or len(vertices) > 400_000 or len(indices) > 200_000:
        raise ValueError(f'Empty or excessive contact mesh: {len(vertices)} vertices, {len(indices)} triangles')
    document = {'schema_version': 1, 'source_sha256': SOURCE_SHA,
                'model_sha256': MODEL_SHA, 'quantization_metres': .000001,
                'crop_minimum_metres': LOW.tolist(), 'crop_maximum_metres': HIGH.tolist(),
                'vertices_micrometres': vertices, 'triangles': indices}
    encoded = (json.dumps(document, separators=(',', ':')) + '\n').encode()
    if len(encoded) > 16 * 1024 * 1024:
        raise ValueError('Contact document exceeds byte budget')
    if hashlib.sha256(args.source.read_bytes()).hexdigest() != before:
        raise ValueError('Source changed during extraction')
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'geometry.json').write_bytes(encoded)
    metadata = {'schema_version': 1, 'asset_id': 'origin-walk-01',
                'geometry_sha256': hashlib.sha256(encoded).hexdigest(),
                'source_sha256': SOURCE_SHA, 'presentation_model_sha256': MODEL_SHA,
                'source': 'assets/visual/station-reference.blend',
                'source_unchanged': True, 'units': 'metres',
                'coordinate_conversion': 'Blender (x,y,z) to (x-.97,z,-y+.978)',
                'frame': 1, 'vertices': len(vertices), 'triangles': len(indices),
                'maximum_coordinate_rounding_error_metres': .0000005,
                'geometry_reduction': 'Spatial triangle crop only; no simplification',
                'objects': sources, 'paid_provider_calls': 0,
                'license': 'BSD-3-Clause; repository-owned station authoring',
                'limits': ['Bounded hub-to-D1 supported interior contact mesh.',
                           'No ladder, spacecraft cabin, exterior/EVA or AG failure simulation.',
                           'Source selection matches station_export; visiting fixtures excluded.']}
    (args.output / 'provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print('ORIGIN_WALK_EXTRACTED', len(vertices), len(indices), len(encoded), flush=True)


if __name__ == '__main__':
    main()
