"""Source-specific corrected-closed seat buffers and ownership; BSD-3-Clause.

No Blender dependency, scene mutation, motion or gameplay admission.
"""
from collections import Counter, defaultdict
import hashlib
import json
import math
import struct

import wayfarer_restraint_export_checks as original

SOURCE_SHA256 = original.SOURCE_SHA256
PACKAGE_ID = 'wayfarer-corrected-closed-01'
NAMESPACE = ('fixed diagnostic craft canonical metres (+Y up, -Z forward), '
             'world-baked flat identity nodes; '
             'not an existing C++ rest-to-pose replacement')
PARTS = {
    'WFCClosedShoulderPort': 'Shoulder restraint',
    'WFCClosedShoulderStarboard': 'Shoulder restraint.001',
    'WFCClosedLapPort': 'Lap restraint',
    'WFCClosedLapStarboard': 'Lap restraint.001',
    'WFCClosedCrotch': 'Anti-submarining strap',
    'WFCClosedBuckle': 'Five-point buckle',
    'WFCClosedBuckleRelease': 'Buckle release',
    'WFCClosedManifold': 'Seat service manifold',
    'WFCClosedConnectorPort': 'Corrected restraint shoulder connector port',
    'WFCClosedConnectorStarboard': 'Corrected restraint shoulder connector starboard',
}
RESIDUAL = 'WFCClosedSeatResidual'
CHANGED = frozenset(('Shoulder restraint', 'Shoulder restraint.001',
                     'Lap restraint', 'Lap restraint.001', 'Seat service manifold'))
CONNECTORS = frozenset(('Corrected restraint shoulder connector port',
                        'Corrected restraint shoulder connector starboard'))
LIMITS = ['Fixed diagnostic corrected-closed q0 only; no runtime admission or C++ pose mapping.',
          'No release/opening curve, occupied body, seat load, reach or boarding route.',
          'No weld/strength/latch/pressure qualification or service functionality.',
          'Mutual ribbon material proof and finite static attachment only; no whole-source solid clearance.',
          'Pressure shells remain source surfaces enclosing cabin air, never filled material.']


def require(value, message):
    if not value:
        raise ValueError(message)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def cyclic(values):
    values = tuple(values)
    require(len(values) == 3, 'Triangle requires three corners')
    return min(values[i:] + values[:i] for i in range(3))


def decode_glb(raw):
    """Return document/payload after the unchanged strict embedded GLB boundary."""
    try:
        document, payload = original.read_glb(raw)
        require(type(document) is dict and type(document.get('nodes')) is list and
                1 <= len(document['nodes']) <= 128, 'GLB node dimensions')
        return {'document': document, 'payload': payload}
    except (KeyError, TypeError, IndexError, struct.error, UnicodeError, RecursionError) as error:
        raise ValueError('Malformed GLB: ' + str(error)) from error


def node_faces(decoded, name):
    """Strict flat node -> per-primitive triangle/corner records, finite float32."""
    doc, payload = decoded['document'], decoded['payload']
    # Reuse the original parser's scene, transform, attribute/type/index checks.
    original.triangles(doc, payload, [name])
    matches = [(i, n) for i, n in enumerate(doc['nodes']) if n.get('name') == name]
    require(len(matches) == 1, 'Missing or duplicate render node: ' + name)
    node = matches[0][1]
    result = []
    for pi, primitive in enumerate(doc['meshes'][node['mesh']]['primitives']):
        attrs = {k: original.accessor(doc, payload, v) for k, v in primitive['attributes'].items()}
        indices = [x[0] for x in original.accessor(doc, payload, primitive['indices'])]
        material = doc['materials'][primitive['material']]
        material_sha = digest(canonical(material).encode())
        for index in range(0, len(indices), 3):
            ids = indices[index:index+3]
            result.append({'primitive': pi, 'primitive_triangle': index//3,
                           'material': material, 'material_sha256': material_sha,
                           'corners': [{k: v[i] for k, v in attrs.items()} for i in ids]})
    require(0 < len(result) <= 20000, 'Seat node triangle budget')
    return result


def geometry_key(row):
    return row['material']['name'], cyclic(tuple(c['POSITION']) for c in row['corners'])


def corner_bit_key(row):
    """Keep signed zero and every emitted float32 bit in all corner attributes."""
    return row['material_sha256'], cyclic(tuple(
        (k, struct.pack('<'+'f'*len(v), *v)) for k, v in sorted(c.items()))
        for c in row['corners'])


def source_key(source, face):
    return source['triangle_materials'][face], cyclic(
        tuple(source['vertices'][i]) for i in source['triangles'][face])


def validate_source_geometry(sources, expected_names):
    require(type(sources) is dict and set(sources) == set(expected_names), 'Complete source geometry roster')
    total = 0
    for name, g in sources.items():
        require(type(name) is str and type(g) is dict, 'Source geometry entry')
        vertices, faces = g.get('vertices'), g.get('triangles')
        require(type(vertices) is list and 1 <= len(vertices) <= 20000 and
                type(faces) is list and 1 <= len(faces) <= 20000, 'Source mesh dimensions')
        require(all(type(p) in (list, tuple) and len(p) == 3 and all(
            type(x) in (int, float) and math.isfinite(x) and abs(x) <= 20 for x in p) for p in vertices),
            'Nonfinite or out-of-domain source points')
        require(all(type(t) in (list, tuple) and len(t) == 3 and all(
            type(i) is int and 0 <= i < len(vertices) for i in t) for t in faces), 'Source indices')
        require(type(g.get('triangle_materials')) is list and len(g['triangle_materials']) == len(faces) and
                all(type(n) is str and 0 < len(n) <= 256 for n in g['triangle_materials']), 'Source material attribution')
        require(type(g.get('triangle_uv_gltf')) is list and len(g['triangle_uv_gltf']) == len(faces) and
                all(len(uv) == 3 and all(len(p) == 2 and all(type(x) in (int, float) and
                    math.isfinite(x) for x in p) for p in uv) for uv in g['triangle_uv_gltf']), 'Source UV attribution')
        require(type(g.get('introduced_connector')) is bool and g['introduced_connector'] == (name in CONNECTORS),
                'Source connector category')
        total += len(faces)
    require(total == 15940, 'Exact corrected seat triangle budget')


def verify_emitted(producer, corrected_raw, original_posed, original_rest, old_rest):
    """Return contact/attribution/verification from actual buffers; no pass-flag trust."""
    groups, source = producer['groups'], producer['prepared_source_geometry']
    names = [n for g in groups for n in g['source_objects']]
    expected_nodes = set(PARTS) | {RESIDUAL}
    require(len(groups) == 11 and {g['node'] for g in groups} == expected_nodes and
            len(names) == 66 and len(set(names)) == 66, 'Exact eleven-node/66 ownership')
    for node, name in PARTS.items():
        require(next(g['source_objects'] for g in groups if g['node'] == node) == [name], 'Component source binding')
    require(len(next(g['source_objects'] for g in groups if g['node'] == RESIDUAL)) == 56, 'Exact56 residual')
    validate_source_geometry(source, names)
    decoded = decode_glb(corrected_raw); doc = decoded['document']
    require(not doc.get('animations') and len(doc['nodes']) == 11 and len(doc['meshes']) == 11,
            'Static flat eleven-mesh derivative')
    original_rows = original_posed['rows_by_object']
    require(len(original_rows) == 64 and set(original_rows) == set(names)-CONNECTORS, 'Original same-pose64 roster')
    rest_equal = Counter(corner_bit_key(t) for t in original_rest) == Counter(corner_bit_key(t) for t in old_rest)
    require(rest_equal and len(old_rest) == 15916, 'Published original-rest emitted corner bits differ')
    seen, emitted = set(), defaultdict(list)
    attribution, contact = [], []
    uv_count = 0
    for group in groups:
        candidates = defaultdict(list)
        for n in group['source_objects']:
            for i in range(len(source[n]['triangles'])):
                candidates[source_key(source[n], i)].append((n, i))
        matches = []
        for row in node_faces(decoded, group['node']):
            choices = candidates.get(geometry_key(row), [])
            selected = None
            actual_positions = [tuple(c['POSITION']) for c in row['corners']]
            actual_uv_bits = [struct.pack('<ff', *c['TEXCOORD_0']) for c in row['corners']]
            for occurrence, (owner, index) in enumerate(choices):
                g = source[owner]
                points = [tuple(g['vertices'][i]) for i in g['triangles'][index]]
                for shift in range(3):
                    uv = g['triangle_uv_gltf'][index]
                    if (actual_positions == points[shift:]+points[:shift] and
                        actual_uv_bits == [struct.pack('<ff', *p) for p in uv[shift:]+uv[:shift]]):
                        selected = occurrence
                        break
                if selected is not None:
                    break
            require(selected is not None, 'Missing exact emitted source face attribution')
            n, face = choices.pop(selected)
            require((n, face) not in seen, 'Duplicated emitted source face')
            seen.add((n, face)); emitted[n].append(row)
            positions = [tuple(c['POSITION']) for c in row['corners']]
            g = source[n]; src = [tuple(g['vertices'][i]) for i in g['triangles'][face]]
            shifts = [i for i in range(3) if positions == src[i:]+src[:i]]
            require(shifts, 'Source face winding differs')
            shift = shifts[0]; uv = g['triangle_uv_gltf'][face]
            expected_uv = uv[shift:]+uv[:shift]
            actual_uv = [c['TEXCOORD_0'] for c in row['corners']]
            require([struct.pack('<ff', *p) for p in actual_uv] ==
                    [struct.pack('<ff', *p) for p in expected_uv], 'Emitted source UV bits differ')
            uv_count += 1
            matches.append({'primitive': row['primitive'], 'primitive_triangle': row['primitive_triangle'],
                            'source_object': n, 'prepared_source_face': face,
                            'prepared_source_vertex_ids': g['triangles'][face],
                            'material_name': row['material']['name'], 'material_sha256': row['material_sha256'],
                            'emitted_positions': [list(p) for p in positions],
                            'emitted_corner_attributes': [{k: list(v) for k, v in c.items()} for c in row['corners']]})
        attribution.append({'node': group['node'], 'source_objects': group['source_objects'], 'faces': matches})
    require(seen == {(n, i) for n, g in source.items() for i in range(len(g['triangles']))},
            'Missing source render face')
    for n, g in source.items():
        require({i for t in g['triangles'] for i in t} == set(range(len(g['vertices']))), 'Unverified unused source vertex')
        contact.append({'source_object': n, 'node': next(q['node'] for q in groups if n in q['source_objects']),
                        'vertices_metres': g['vertices'], 'triangles': g['triangles'],
                        'triangle_materials': g['triangle_materials'], 'introduced_connector': n in CONNECTORS})
    preserved, materials = [], []
    for n, old in original_rows.items():
        new = emitted[n]
        require({r['material_sha256'] for r in old} == {r['material_sha256'] for r in new},
                'Original source material descriptors differ: ' + n)
        materials.append(n)
        if n not in CHANGED:
            require(Counter(corner_bit_key(t) for t in old) == Counter(corner_bit_key(t) for t in new),
                    'Untouched same-pose corner bits differ: ' + n)
            preserved.append({'source_object': n, 'triangles': len(old), 'corner_bits_identical': True})
    require(len(preserved) == 59 and sum(p['triangles'] for p in preserved) == 15696, 'Exact59 preserved membership')
    model_sha = digest(corrected_raw)
    contact_doc = {'schema': 'apsis.corrected-closed-exact-source-contact/1', 'model_sha256': model_sha,
                   'source_sha256': SOURCE_SHA256, 'coordinate_namespace': NAMESPACE,
                   'fixed_operating_tuple': [1,1,1,0],
                   'quantization': 'none; exact binary32 GLB coordinates represented as JSON binary64 numbers',
                   'objects': contact, 'actor_load_motion_or_runtime_admission': False}
    face_doc = {'schema': 'apsis.corrected-closed-emitted-face-attribution/1', 'model_sha256': model_sha,
                'coordinate_namespace': NAMESPACE, 'nodes': attribution}
    verification = {'schema': 'apsis.corrected-closed-emitted-verification/1', 'model_sha256': model_sha,
                    'published_rest_union_corner_bits_equal': True, 'published_rest_faces': 15916,
                    'original_same_pose_tuple': [1,1,1,0], 'preserved_objects': preserved,
                    'all_original_material_descriptor_objects': materials, 'exact_source_uv_faces': uv_count,
                    'complete_attributed_faces': len(seen), 'source_vertices_bound_to_actual_emitted_faces': True,
                    'runtime_admission': False, 'limits': LIMITS}
    return contact_doc, face_doc, verification
