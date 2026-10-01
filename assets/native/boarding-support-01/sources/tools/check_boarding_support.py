#!/usr/bin/env python3
"""Check a boarding sidecar against admitted contact arrays without Blender."""
import argparse
import hashlib
import json
import math
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def length(a):
    return math.sqrt(sum(v*v for v in a))


def check(support, contact):
    assert support['schema_version'] == 1
    groups = {g['id']: g for g in contact['groups']}
    assert support['sources'] == contact['sources']
    assert support['coordinate_contracts'] == contact['coordinate_contracts']
    counts = {identity: 0 for identity in groups}
    names = {identity: [] for identity in groups}
    patches, inverted = [], 0
    for record in support['objects']:
        identity = record['group']
        group = groups[identity]
        start, count = record['triangle_start'], record['triangle_count']
        assert isinstance(start, int) and isinstance(count, int) and count >= 0
        assert start == counts[identity] and start + count <= len(group['triangles'])
        counts[identity] += count
        names[identity].append(record['source_object'])
        assert record['default_collision'] == 'obstacle'
        assert record['owner'] == group['owner'] and record['motion_group'] == group['motion_group']
        assert count == record['crop_retained_triangles']-record['quantized_degenerate_triangles_removed']
        matrix = record['source_corrected_world_rows']
        assert len(matrix) == 4 and all(len(row) == 4 and all(math.isfinite(v) for v in row) for row in matrix)
        for patch in record['candidate_patches']:
            assert record['semantic_label'] in ('rung', 'handhold', 'cushion', 'pedal')
            ids = []
            for offset, size in patch['triangle_ranges']:
                assert isinstance(offset, int) and isinstance(size, int) and size > 0
                assert start <= offset and offset+size <= start+count
                ids.extend(range(offset, offset+size))
            assert ids == sorted(set(ids)) and len(ids) == patch['triangle_count'] > 0
            assert patch['representative_triangle'] in ids
            total_area = 0
            points = []
            for index in ids:
                verts = [[v*contact['quantization_metres'] for v in group['vertices_micrometres'][i]] for i in group['triangles'][index]]
                area2 = length(cross(sub(verts[1], verts[0]), sub(verts[2], verts[0])))
                assert area2 > 1e-12
                total_area += area2/2
                points.extend(verts)
                if index == patch['representative_triangle']:
                    bary = patch['representative_barycentric']
                    assert len(bary) == 3 and all(0 <= v <= 1 for v in bary) and abs(sum(bary)-1) < 1e-12
                    point = [sum(bary[i]*verts[i][axis] for i in range(3)) for axis in range(3)]
                    assert length(sub(point, patch['representative_contact_point_rest_m'])) < 1e-10
            assert abs(total_area-patch['surface_area_m2']) < 1e-10
            bounds = [[min(p[a] for p in points) for a in range(3)], [max(p[a] for p in points) for a in range(3)]]
            assert max(abs(x-y) for a,b in zip(bounds,patch['bounds_rest_m']) for x,y in zip(a,b)) < 1e-10
            if 'contact_side_direction_rest' in patch:
                assert abs(length(patch['contact_side_direction_rest'])-1) < 1e-9
            if record['semantic_label'] in ('rung', 'handhold'):
                predicate = patch['required_point_predicate']
                assert predicate['type'] == 'rest_axis_interval'
                assert abs(length(predicate['unit_axis_rest'])-1) < 1e-9
                assert predicate['end_margin_m'] == .03
                lower, upper = predicate['inclusive_interval_m']
                assert math.isfinite(lower) and math.isfinite(upper) and lower < upper
            inverted += patch.get('opposite_winding_triangles', 0)
            patches.append(patch['id'])
    assert len(set(patches)) == len(patches)
    for identity, group in groups.items():
        assert counts[identity] == len(group['triangles'])
        assert names[identity] == group['source_objects']
    assert len(patches) == support['checks']['candidate_patches']
    return {'groups': len(groups), 'objects': len(support['objects']), 'triangles_covered_once': sum(counts.values()),
            'patches': len(patches), 'candidate_triangles_with_opposite_source_winding': inverted,
            'checks': 'Gap-free per-object range coverage, exact source roster, finite transforms, referenced patch triangles, surface areas/bounds/barycentric points and end-margin predicates'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-root', type=Path, required=True)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    provenance = json.loads((args.package/'provenance.json').read_text())
    for path, digest in provenance['engine_bindings_sha256'].items():
        assert sha(args.engine_root/path) == digest, path
    for path, digest in provenance['artifacts_sha256'].items():
        assert sha(args.package/path) == digest, path
    contact_path = args.engine_root/'assets/native/wayfarer-operating-02/metadata/contact.json'
    support = json.loads((args.package/'support.json').read_text())
    assert sha(contact_path) == support['contact_sha256']
    contact = json.loads(contact_path.read_text())
    result = check(support, contact)
    result.update({'support_sha256': sha(args.package/'support.json'), 'contact_sha256': sha(contact_path), 'checker_sha256': sha(Path(__file__))})
    if args.report:
        args.report.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
