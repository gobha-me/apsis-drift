"""Independent fixed corrected-closed evidence checks; BSD-3-Clause.

This module never admits source collision, actor support, a route or runtime art.
Portable low-level checks accept small independent test fixtures; the production
entry point additionally requires the frozen source/package and complete roster.
"""
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct

import wayfarer_restraint_export_checks as legacy

SOURCE_SHA256 = legacy.SOURCE_SHA256
PACKAGE_SHA256 = legacy.PACKAGE_SHA256
METADATA_SHA256 = legacy.METADATA_SHA256
MAX_BYTES = legacy.MAX_BYTES
MAX_JSON_BYTES = 24 * 1024 * 1024
NAMESPACE = ('fixed diagnostic craft canonical metres (+Y up, -Z forward), '
             'world-baked flat identity nodes; not an existing C++ rest-to-pose replacement')
FIXED_NODES = {
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
RESIDUAL_NODE = 'WFCClosedSeatResidual'
CHANGED = frozenset(('Shoulder restraint', 'Shoulder restraint.001',
                     'Lap restraint', 'Lap restraint.001', 'Seat service manifold'))
CONNECTORS = frozenset(('Corrected restraint shoulder connector port',
                       'Corrected restraint shoulder connector starboard'))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False)


def digest(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def closed_json(data, maximum=MAX_JSON_BYTES):
    require(type(data) is bytes and 0 < len(data) <= maximum, 'JSON byte bounds')
    def pairs(rows):
        result = {}
        for key, value in rows:
            require(key not in result, 'Duplicate JSON key')
            result[key] = value
        return result
    def nonfinite(_):
        raise ValueError('Nonfinite JSON literal')
    try:
        result = json.loads(data, object_pairs_hook=pairs, parse_constant=nonfinite)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ValueError('Malformed bounded JSON') from error
    pending = [(result, 0)]
    while pending:
        value, depth = pending.pop()
        require(depth <= 32, 'JSON nesting bound')
        if isinstance(value, dict):
            pending.extend((v, depth + 1) for v in value.values())
        elif isinstance(value, list):
            pending.extend((v, depth + 1) for v in value)
        elif isinstance(value, float):
            require(math.isfinite(value), 'Nonfinite JSON value')
    return result


def read_member(directory, relative, expected=None, maximum=MAX_BYTES):
    directory = Path(directory)
    relative = Path(relative)
    require(not relative.is_absolute() and bool(relative.parts) and
            '..' not in relative.parts and '.' not in relative.parts,
            'Unsafe package member path')
    path = directory / relative
    require(not any(p.is_symlink() for p in (path, *path.parents)),
            'Symlink package member or ancestor')
    require(path.is_file() and 0 < path.stat().st_size <= maximum,
            'Missing or oversized package member')
    with path.open('rb') as stream:
        data = stream.read(maximum + 1)
    require(0 < len(data) <= maximum, 'Package read bounds')
    if expected is not None:
        require(type(expected.get('bytes')) is int and len(data) == expected['bytes'] and
                hashlib.sha256(data).hexdigest() == expected.get('sha256'),
                'Package member byte identity changed')
    return data


def cyclic(values):
    require(len(values) == 3, 'Exactly three triangle corners required')
    values = tuple(values)
    return min(values[i:] + values[:i] for i in range(3))


def corner_bits(corner):
    return tuple((key, struct.pack('<' + 'f' * len(value), *value).hex())
                 for key, value in sorted(corner.items()))


def semantic_key(face):
    return (face['material_sha256'], cyclic([corner_bits(c) for c in face['corners']]))


def semantic_digest(faces):
    counts = Counter(semantic_key(face) for face in faces)
    return digest(sorted((canonical(key), count) for key, count in counts.items()))


def decode_glb(data, node_names, exact_nodes=False):
    """Strict old parser plus independently decoded complete corner records."""
    try:
        doc, payload = legacy.read_glb(data)
        json_length = struct.unpack_from('<I', data, 12)[0]
        doc = closed_json(data[20:20 + json_length], 8 * 1024 * 1024)
        require(type(node_names) in (list, tuple) and node_names and
                len(node_names) == len(set(node_names)) and
                all(type(n) is str and n for n in node_names), 'Render node roster')
        # Reuse the already-reviewed parser/bounds/flat-root gate unchanged.
        legacy.triangles(doc, payload, node_names)
        if exact_nodes:
            require(len(doc['nodes']) == len(node_names) and
                    len(doc['meshes']) == len(node_names) and not doc.get('animations') and
                    not doc.get('skins') and not doc.get('cameras') and
                    {n['mesh'] for n in doc['nodes']} == set(range(len(doc['meshes']))),
                    'Exact static flat node inventory')
        rows = {}
        cache = {}
        def values(index):
            if index not in cache:
                cache[index] = legacy.accessor(doc, payload, index)
            return cache[index]
        for node in doc['nodes']:
            name = node.get('name')
            if name not in node_names:
                continue
            faces = []
            for pi, primitive in enumerate(doc['meshes'][node['mesh']]['primitives']):
                require(not primitive.get('targets'), 'Static source has no morph targets')
                attrs = {k: values(v) for k, v in primitive['attributes'].items()}
                indices = [v[0] for v in values(primitive['indices'])]
                material = doc['materials'][primitive['material']]
                require(type(material.get('name')) is str and material['name'], 'Named source material')
                for start in range(0, len(indices), 3):
                    ids = indices[start:start + 3]
                    corners = [{k: list(v[index]) for k, v in attrs.items()} for index in ids]
                    require(all(abs(x) <= 16 for c in corners for x in c['POSITION']),
                            'Fixed seat coordinate workspace')
                    faces.append({'primitive': pi, 'primitive_triangle': start // 3,
                                  'material': material, 'material_sha256': digest(material),
                                  'corners': corners})
            rows[name] = faces
        return doc, rows
    except (KeyError, IndexError, TypeError, struct.error, OverflowError,
            UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ValueError('Malformed corrected-rest GLB') from error


def validate_geometry(geometry):
    require(type(geometry) is dict and 0 < len(geometry) <= 66, 'Source geometry object bound')
    total = 0
    for name, item in geometry.items():
        require(type(name) is str and name and type(item) is dict, 'Source object identity')
        vertices, faces = item['vertices'], item['triangles']
        require(type(vertices) is list and 3 <= len(vertices) <= 100_000 and
                type(faces) is list and 0 < len(faces) <= 50_000, 'Source geometry dimensions')
        require(all(type(p) is list and len(p) == 3 and all(
            type(x) in (int, float) and math.isfinite(x) and abs(x) <= 16 and
            struct.unpack('<f', struct.pack('<f', x))[0] == x for x in p)
                    for p in vertices), 'Exact finite binary32 source coordinates')
        require(all(type(t) is list and len(t) == 3 and all(
            type(i) is int and 0 <= i < len(vertices) for i in t) for t in faces),
                    'Source face index boundaries')
        materials, uvs = item['triangle_materials'], item['triangle_uv_gltf']
        require(type(materials) is list and len(materials) == len(faces) and all(
            type(m) is str and m for m in materials), 'Source per-face material dimensions')
        require(type(uvs) is list and len(uvs) == len(faces) and all(
            type(t) is list and len(t) == 3 and all(type(p) is list and len(p) == 2 and
                all(type(x) in (int, float) and math.isfinite(x) for x in p) for p in t)
                    for t in uvs), 'Source per-corner UV dimensions')
        require(type(item['introduced_connector']) is bool, 'Connector source category')
        require({i for t in faces for i in t} == set(range(len(vertices))),
                'Unused source vertex cannot acquire invisible contact geometry')
        total += len(faces)
    require(total <= 50_000, 'Complete source face bound')


def validate_attribution(report, rows, expected_groups):
    """Reconstruct ownership from actual face corners, never receipt pass flags."""
    geometry = report['prepared_source_geometry']
    validate_geometry(geometry)
    require(report['groups'] == expected_groups, 'Exact source/group attribution roster')
    roster = [n for g in expected_groups for n in g['source_objects']]
    require(len(roster) == len(set(roster)) and set(roster) == set(geometry),
            'Complete unique source membership')
    require(set(rows) == {g['node'] for g in expected_groups}, 'Complete render group inventory')
    seen = set()
    by_object = defaultdict(list)
    nodes = []
    for group in expected_groups:
        expected = defaultdict(list)
        for name in group['source_objects']:
            g = geometry[name]
            for index, face in enumerate(g['triangles']):
                key = (g['triangle_materials'][index], cyclic([tuple(g['vertices'][v]) for v in face]))
                expected[key].append((name, index))
        matches = []
        for actual in rows[group['node']]:
            positions = [c['POSITION'] for c in actual['corners']]
            key = (actual['material']['name'], cyclic([tuple(p) for p in positions]))
            choices = expected.get(key, [])
            # Equal geometric faces retain distinct, stable source occurrences.
            # UV corners refine that occurrence; no unique physical identity is
            # invented for coincident caps or preserved render degenerates.
            selected = None
            for occurrence, (candidate, face_index) in enumerate(choices):
                source = geometry[candidate]
                source_positions = [source['vertices'][v] for v in source['triangles'][face_index]]
                for shift in range(3):
                    source_uv = source['triangle_uv_gltf'][face_index]
                    if (positions == source_positions[shift:] + source_positions[:shift] and
                        [struct.pack('<ff', *c['TEXCOORD_0']) for c in actual['corners']] ==
                        [struct.pack('<ff', *uv) for uv in source_uv[shift:] + source_uv[:shift]]):
                        selected = occurrence
                        break
                if selected is not None:
                    break
            require(selected is not None, 'Missing/changed source occurrence or corner UV')
            name, index = choices.pop(selected)
            require((name, index) not in seen, 'Duplicate emitted source face')
            seen.add((name, index))
            source = geometry[name]
            by_object[name].append(actual)
            matches.append({'primitive': actual['primitive'], 'primitive_triangle': actual['primitive_triangle'],
                            'source_object': name, 'prepared_source_face': index,
                            'prepared_source_vertex_ids': source['triangles'][index],
                            'material_name': actual['material']['name'],
                            'material_sha256': actual['material_sha256'], 'emitted_positions': positions,
                            'emitted_corner_attributes': actual['corners']})
        nodes.append({'node': group['node'], 'source_objects': group['source_objects'], 'faces': matches})
    require(seen == {(n, i) for n, g in geometry.items() for i in range(len(g['triangles']))},
            'Incomplete source face attribution')
    return dict(by_object), nodes


def compare_baselines(published_faces, original_rest_faces, original_posed_objects,
                      corrected_objects, changed=CHANGED):
    require(Counter(semantic_key(f) for f in published_faces) ==
            Counter(semantic_key(f) for f in original_rest_faces),
            'Published rest differs from original rest attribute union')
    require(set(original_posed_objects) <= set(corrected_objects), 'Missing original object in corrected pose')
    untouched = []
    for name, old_faces in original_posed_objects.items():
        new_faces = corrected_objects[name]
        if name not in changed:
            require(Counter(semantic_key(f) for f in old_faces) ==
                    Counter(semantic_key(f) for f in new_faces),
                    'Unrelated same-tuple geometry/topology/corner/material change: ' + name)
            untouched.append(name)
        require({f['material_sha256'] for f in old_faces} ==
                {f['material_sha256'] for f in new_faces}, 'Original material descriptor set changed: ' + name)
    return {'published_rest_triangles': len(published_faces),
            'untouched_objects': sorted(untouched),
            'untouched_triangles': sum(len(original_posed_objects[n]) for n in untouched)}

FROZEN_GEOMETRY_DIGESTS = {
    'original_rest': '0f66d277ddc8c978347e3f4359b89b4854586c00d95dc8f80831d81d072ffa23',
    'original_posed': 'dba2155a29f8682d76aa69a91f61e04297fe4e08743e1c7b347660f0c0e4f336',
    'corrected_closed': '1f0fd326019aa2b35762dad3251ac4d8999e2730114122a0a244ac1f333ce391',
}
FROZEN_CORNER_DIGESTS = {
    'original_rest': '798c34454109934bd458771eb55662a97911c0ead2aa833998bc40999f044218',
    'original_posed': 'dfa0b1ddf9e372dfd295f3510cf6ff2d9b5283c9bccf069c6abe8153b4ea8bfa',
    'corrected_closed': '7ecd6f6f0ffde95d60687e5bd9bdb1b3794b0d7a87fe20ac1c65962936bfed6b',
}
LICENSES = {
    'licenses/HOPPER_GENERATED_CONCEPTS.md': (1783, 'e0b30edddcc4e847687c3510fe29b83ecdaabaccc777b89164304ee69b0c5879'),
    'licenses/HOPPER_MESHY_TRIAL.md': (3084, '0b4d01c948f92f2d13b85c6f8f19c8dc77696285c8b764e5a3fc506446178dad'),
    'licenses/LICENSE.md': (1521, '984659cb7e96b257c5190f461f9ef2313971f28dc84be4495d926bd1196b3a6d'),
    'licenses/MESHY_QUALIFICATION_OUTPUT.md': (1762, '46456e7d2ee3ba826b67cbd4c4d9611755ca1d35a1776afa4257a927fa014225'),
    'licenses/STATION_KIT_01.md': (11622, 'db66c2c3dca629867586d156c3956a45de85e6a45e657df951fe4d2bd9c354b7'),
}


def geometry_digest(geometry):
    keys = ('vertices', 'triangles', 'triangle_materials',
            'triangle_uv_gltf', 'introduced_connector')
    return digest({name: {k: item[k] for k in keys} for name, item in geometry.items()})


def corner_digest(objects):
    normalized = {}
    for name, faces in objects.items():
        counts = Counter(cyclic([corner_bits(c) for c in face['corners']]) for face in faces)
        normalized[name] = sorted((canonical(k), n) for k, n in counts.items())
    return digest(normalized)


def expected_groups(members, mode):
    require(type(members) is list and len(members) == 64 and len(set(members)) == 64,
            'Frozen original64 unique source members')
    if mode != 'corrected_closed':
        return [{'node': 'BaselineSeat%02d' % i, 'source_objects': [name]}
                for i, name in enumerate(members)]
    require(set(FIXED_NODES.values()) - CONNECTORS <= set(members), 'Missing fixed original member')
    groups = [{'node': node, 'source_objects': [name]} for node, name in FIXED_NODES.items()]
    groups.append({'node': RESIDUAL_NODE, 'source_objects': [n for n in members
                   if n not in FIXED_NODES.values()]})
    require(len(groups) == 11 and len(groups[-1]['source_objects']) == 56,
            'Exact eleven-group residual roster')
    return groups


def validate_snapshot(report, model_data, groups, mode):
    require(type(report) is dict and report.get('mode') == mode and
            mode in FROZEN_GEOMETRY_DIGESTS, 'Snapshot mode identity')
    require(report.get('source_sha256') == SOURCE_SHA256 and
            report.get('namespace') == NAMESPACE and
            type(report.get('operating_tuple')) is list and
            all(type(q) is int for q in report['operating_tuple']) and
            report['operating_tuple'] == ([0, 0, 0, 0] if mode == 'original_rest' else [1, 1, 1, 0]),
            'Source, coordinate namespace or fixed operating tuple changed')
    require(report.get('not_admitted') is True and report.get('source_file_unchanged') is True and
            report.get('all_1746_original_signatures_restored') is True,
            'Source restoration or diagnostic-only scope missing')
    model = report['model']
    require(type(model.get('bytes')) is int and len(model_data) == model['bytes'] and
            hashlib.sha256(model_data).hexdigest() == model.get('sha256'), 'Snapshot model byte identity')
    _, rows = decode_glb(model_data, [g['node'] for g in groups], True)
    objects, attribution = validate_attribution(report, rows, groups)
    require(geometry_digest(report['prepared_source_geometry']) == FROZEN_GEOMETRY_DIGESTS[mode],
            'Frozen preregistered source geometry/UV/material/category changed')
    require(corner_digest(objects) == FROZEN_CORNER_DIGESTS[mode],
            'Frozen emitted position/normal/UV corner bits changed')
    return objects, attribution


def validate_contact(contact, report, objects):
    require(contact.get('model_sha256') == report['model']['sha256'] and
            contact.get('source_sha256') == SOURCE_SHA256 and
            contact.get('coordinate_namespace') == NAMESPACE and
            type(contact.get('fixed_operating_tuple')) is list and
            all(type(q) is int for q in contact['fixed_operating_tuple']) and
            contact['fixed_operating_tuple'] == [1, 1, 1, 0] and
            contact.get('actor_load_motion_or_runtime_admission') is False,
            'Contact snapshot identity or authority changed')
    entries = contact['objects']
    require(type(entries) is list and len(entries) == len(objects) and
            len({o['source_object'] for o in entries}) == len(entries) and
            {o['source_object'] for o in entries} == set(objects), 'Complete unique contact source inventory')
    lookup = {name: group['node'] for group in report['groups'] for name in group['source_objects']}
    for item in entries:
        name = item['source_object']
        source = report['prepared_source_geometry'][name]
        validate_geometry({name: {'vertices': item['vertices_metres'],
                                  'triangles': item['triangles'],
                                  'triangle_materials': item['triangle_materials'],
                                  'triangle_uv_gltf': source['triangle_uv_gltf'],
                                  'introduced_connector': item['introduced_connector']}})
        require(item['node'] == lookup[name] and item['vertices_metres'] == source['vertices'] and
                item['triangles'] == source['triangles'] and
                item['triangle_materials'] == source['triangle_materials'] and
                item['introduced_connector'] is source['introduced_connector'],
                'Contact must retain the actual attributed emitted source faces')
    return {o['source_object']: {'vertices': o['vertices_metres'], 'triangles': o['triangles']}
            for o in entries}


def validate_files(directory, manifest, required_paths):
    require(type(manifest) is dict and type(manifest.get('files')) is list and
            0 < len(manifest['files']) <= 32, 'Package file inventory dimensions')
    files = manifest['files']
    require(all(type(f) is dict and set(f) == {'path', 'bytes', 'sha256'} and
                type(f['path']) is str and type(f['sha256']) is str and len(f['sha256']) == 64
                for f in files), 'Closed package file identity schema')
    paths = [f['path'] for f in files]
    require(len(paths) == len(set(paths)) and set(paths) == set(required_paths),
            'Exact complete package payload/license inventory')
    output = {f['path']: read_member(directory, f['path'], f) for f in files}
    actual = {str(p.relative_to(directory)) for p in Path(directory).rglob('*') if p.is_file() or p.is_symlink()}
    require(actual == set(paths) | {'package.json'}, 'Unlisted output payload or symlink')
    return output


LIMITS = [
    'Fixed diagnostic corrected-closed q0 only; no runtime admission or C++ pose mapping.',
    'No release/opening curve, occupied body, seat load, reach or boarding route.',
    'No weld/strength/latch/pressure qualification or service functionality.',
    'Mutual ribbon material proof and finite static attachment only; no whole-source solid clearance.',
    'Pressure shells remain source surfaces enclosing cabin air, never filled material.',
]
PAYLOADS = ('model.glb', 'producer.json', 'contact.json', 'face-attribution.json',
            'verification.json', 'static-composite.json', 'provenance.json',
            'finite-volume-proof.json', 'finite-attachment-proof.json',
            'evidence/original-rest.glb', 'evidence/original-rest.json',
            'evidence/original-posed.glb', 'evidence/original-posed.json')


def equal_document(actual, expected, message):
    # Canonical JSON comparison distinguishes booleans from numbers as well as
    # array order and field sets; numeric float32 bit equality is checked earlier.
    require(canonical(actual) == canonical(expected), message)


def old_reference(package):
    manifest = closed_json(read_member(package, 'package.json',
                           {'bytes': (Path(package) / 'package.json').stat().st_size,
                            'sha256': PACKAGE_SHA256}, 2 * 1024 * 1024))
    require(manifest.get('package_id') == 'wayfarer-operating-02' and
            manifest['model']['sha256'] == legacy.MODEL_SHA256 and
            manifest['model']['source_sha256'] == SOURCE_SHA256, 'Frozen operating package identity')
    entry = next(f for f in manifest['files'] if f['path'] == 'metadata/wayfarer-operating-02.json')
    metadata = read_member(package, entry['path'], entry)
    require(hashlib.sha256(metadata).hexdigest() == METADATA_SHA256, 'Frozen source metadata identity')
    spec = closed_json(metadata)
    members = [r['source_object'] for r in spec['roster']['included'] if r['motion_group'] == 'seat_lift']
    group = next(g for g in spec['groups'] if g['id'] == 'seat_lift')
    require(members == group['source_objects'], 'Frozen complete source order')
    expected_groups(members, 'original_rest')
    _, rows = decode_glb(legacy.old_model(Path(package), manifest), ['WFOpSeatLift'])
    return members, rows['WFOpSeatLift']


def validate_provenance(provenance, reports, payload, manifest, project):
    require(type(provenance) is dict and set(provenance) == {
        'schema', 'id', 'source', 'old_package', 'producer', 'helpers', 'baselines',
        'registered_corrections', 'source_restoration', 'licenses', 'namespace', 'limits'},
        'Closed provenance schema')
    require(provenance['schema'] == 'apsis.corrected-closed-provenance/1' and
            provenance['id'] == 'wayfarer-corrected-closed-01' and
            provenance['namespace'] == NAMESPACE, 'Provenance identity')
    equal_document(provenance['limits'], LIMITS, 'Diagnostic scope limits changed')
    equal_document(provenance['source'],
                   {'file': 'assets/visual/hopper-craft-09.blend', 'sha256': SOURCE_SHA256},
                   'Source identity changed')
    equal_document(provenance['old_package'], {'id': 'wayfarer-operating-02',
        'package_sha256': PACKAGE_SHA256, 'model_sha256': legacy.MODEL_SHA256,
        'metadata_sha256': METADATA_SHA256}, 'Original published reference identity changed')
    producer = provenance['producer']
    require(type(producer) is dict and set(producer) == {'file', 'sha256', 'blender_version'} and
            producer['file'] == 'tools/export_wayfarer_corrected_rest.py' and
            type(producer['blender_version']) is str and producer['blender_version'] == '5.2.2' and
            set(reports) == {'original_rest', 'original_posed', 'corrected_closed'} and
            all(report.get('blender_version') == producer['blender_version']
                for report in reports.values()),
            'Qualified Blender version must agree in provenance and all three snapshots')
    producer_bytes = read_member(project, producer['file'])
    require(hashlib.sha256(producer_bytes).hexdigest() == producer['sha256'],
            'Producer implementation identity changed')
    helpers = provenance['helpers']
    required = {'wayfarer_corrected_rest_geometry.py', 'wayfarer_corrected_rest_proof.py',
                'wayfarer_corrected_rest_checks.py', 'wayfarer_restraint_export_checks.py',
                'wayfarer_operating_blender.py', 'wayfarer_operating_spec.py',
                'wayfarer_flight_export_base.py', 'wayfarer_operating_glb_audit.py',
                'wayfarer_gltf_checks.py'}
    require(type(helpers) is dict and set(helpers) == required | {'wayfarer_corrected_rest_package.py'},
            'Complete closed helper dependency roster')
    for name, expected in helpers.items():
        require(hashlib.sha256(read_member(project, 'tools/' + name)).hexdigest() == expected,
                'Dependency implementation identity changed: ' + name)
    entries = {f['path']: f for f in manifest['files']}
    baseline = {}
    for mode, stem, operating_tuple in (('original_rest', 'original-rest', [0, 0, 0, 0]),
                                        ('original_posed', 'original-posed', [1, 1, 1, 0])):
        baseline[mode] = {key: {'file': path, 'bytes': entries[path]['bytes'],
                               'sha256': entries[path]['sha256']}
                          for key, path in (('model', 'evidence/' + stem + '.glb'),
                                            ('metadata', 'evidence/' + stem + '.json'))}
        baseline[mode]['tuple'] = operating_tuple
    equal_document(provenance['baselines'], baseline, 'Baseline snapshot byte identity changed')
    corrected = reports['corrected_closed']
    equal_document(provenance['registered_corrections'],
                   {'ribbons': corrected['declared_edits'], 'manifold': corrected['manifold'],
                    'connectors': corrected['connector_records']}, 'Registered correction records changed')
    restoration = provenance['source_restoration']
    equal_document(restoration, {
        'snapshots': {mode: report['restoration'] for mode, report in reports.items()},
        'source_file_sha256_before': SOURCE_SHA256, 'source_file_sha256_after': SOURCE_SHA256,
        'source_master_saved': False}, 'Source restoration provenance changed')
    for report in reports.values():
        record = report['restoration']
        require(type(record) is dict and set(record) == {'objects', 'signature_sha256', 'signature_fields'} and
                type(record['objects']) is int and record['objects'] == 1746 and
                type(record['signature_sha256']) is str and len(record['signature_sha256']) == 64 and
                type(record['signature_fields']) is list and 0 < len(record['signature_fields']) <= 64 and
                all(type(s) is str and 0 < len(s) <= 256 for s in record['signature_fields']),
                'Bounded restoration signature inventory')
        signatures = report['original_signatures_before_and_restored']
        require(type(signatures) is dict and len(signatures) == 1746 and
                all(type(name) is str and name and type(sha) is str and len(sha) == 64 and
                    all(c in '0123456789abcdef' for c in sha)
                    for name, sha in signatures.items()) and
                digest(signatures) == record['signature_sha256'],
                'Actual restored source signature aggregate changed')
    equal_document(provenance['licenses'],
                   {name: {'bytes': size, 'sha256': sha}
                    for name, (size, sha) in LICENSES.items()}, 'Inherited license provenance changed')


def validate_derivative(output_dir, old_package=None):
    """Read-only validation of complete private staging; never grants runtime authority.

    The old published package is read independently. Source restoration records
    are exporter evidence, not a claim that this reader reopened a Blender master.
    """
    try:
        return _validate_derivative(Path(output_dir), old_package)
    except (KeyError, TypeError, IndexError, StopIteration, OverflowError, OSError,
            struct.error, RecursionError) as error:
        raise ValueError('Malformed or missing corrected-closed evidence') from error


def _validate_derivative(directory, old_package):
    import wayfarer_corrected_rest_proof as proof
    project = Path(__file__).resolve().parents[1]
    old_package = Path(old_package) if old_package is not None else project / 'assets/native/wayfarer-operating-02'
    manifest = closed_json(read_member(directory, 'package.json', maximum=2 * 1024 * 1024))
    require(type(manifest) is dict and set(manifest) ==
            {'schema', 'id', 'source_sha256', 'model', 'files', 'limits'} and
            manifest['schema'] == 'apsis.wayfarer-corrected-closed-assets/1' and
            manifest['id'] == 'wayfarer-corrected-closed-01' and
            manifest['source_sha256'] == SOURCE_SHA256, 'Closed package source identity')
    equal_document(manifest['limits'], LIMITS, 'Package diagnostic scope changed')
    payload = validate_files(directory, manifest, (*PAYLOADS, *LICENSES))
    documents = {name: closed_json(data, 64 * 1024 * 1024 if name == 'face-attribution.json'
                                   else MAX_JSON_BYTES)
                 for name, data in payload.items() if name.endswith('.json')}
    for name, (size, sha) in LICENSES.items():
        require(len(payload[name]) == size and hashlib.sha256(payload[name]).hexdigest() == sha,
                'Inherited license bytes changed: ' + name)
    entries = {f['path']: f for f in manifest['files']}
    model_entry = entries['model.glb']
    equal_document(manifest['model'], {'file': 'model.glb', 'bytes': model_entry['bytes'],
                                     'sha256': model_entry['sha256']}, 'Model package identity changed')
    members, published = old_reference(old_package)
    reports = {'original_rest': documents['evidence/original-rest.json'],
               'original_posed': documents['evidence/original-posed.json'],
               'corrected_closed': documents['producer.json']}
    snapshots = {}
    attribution = None
    for mode, report in reports.items():
        file = {'original_rest': 'evidence/original-rest.glb',
                'original_posed': 'evidence/original-posed.glb',
                'corrected_closed': 'model.glb'}[mode]
        require(report['model']['file'] == file, 'Snapshot package model path')
        snapshots[mode], derived = validate_snapshot(report, payload[file], expected_groups(members, mode), mode)
        if mode == 'corrected_closed':
            attribution = derived
    baseline = compare_baselines(published, [f for faces in snapshots['original_rest'].values() for f in faces],
                                 snapshots['original_posed'], snapshots['corrected_closed'])
    require(baseline['published_rest_triangles'] == 15916 and len(baseline['untouched_objects']) == 59 and
            baseline['untouched_triangles'] == 15696, 'Frozen full preservation counts')
    report = reports['corrected_closed']
    model_sha = model_entry['sha256']
    equal_document(documents['face-attribution.json'], {
        'schema': 'apsis.corrected-closed-emitted-face-attribution/1', 'model_sha256': model_sha,
        'coordinate_namespace': NAMESPACE, 'nodes': attribution}, 'Actual complete face attribution changed')
    contact = documents['contact.json']
    require(set(contact) == {'schema', 'model_sha256', 'source_sha256', 'coordinate_namespace',
        'fixed_operating_tuple', 'quantization', 'objects', 'actor_load_motion_or_runtime_admission'} and
        contact['schema'] == 'apsis.corrected-closed-exact-source-contact/1' and
        contact['quantization'] == 'none; exact binary32 GLB coordinates represented as JSON binary64 numbers',
        'Closed exact contact schema')
    validate_contact(contact, report, snapshots['corrected_closed'])
    fresh = proof.prove_static_composite(contact)
    for name, key in (('finite-volume-proof.json', 'volume'),
                      ('finite-attachment-proof.json', 'attachments'), ('static-composite.json', 'semantics')):
        equal_document(documents[name], fresh[key], 'Stored finite proof differs from actual emitted geometry')
    verification = documents['verification.json']
    expected = {'schema': 'apsis.corrected-closed-emitted-verification/1', 'model_sha256': model_sha,
        'published_rest_union_corner_bits_equal': True, 'published_rest_faces': 15916,
        'original_same_pose_tuple': [1, 1, 1, 0],
        'preserved_objects': [{'source_object': name, 'triangles': len(snapshots['original_posed'][name]),
                               'corner_bits_identical': True} for name in members if name not in CHANGED],
        'all_original_material_descriptor_objects': members, 'exact_source_uv_faces': 15940,
        'complete_attributed_faces': 15940, 'source_vertices_bound_to_actual_emitted_faces': True,
        'runtime_admission': False, 'limits': LIMITS}
    require(set(verification) == set(expected) | {'glb_audit'}, 'Closed verification schema')
    equal_document({k: verification[k] for k in expected}, expected, 'Stored preservation evidence changed')
    from wayfarer_operating_glb_audit import audit
    equal_document(verification['glb_audit'], audit(directory / 'model.glb'),
                   'Stored GLB audit differs from actual output buffers')
    validate_provenance(documents['provenance.json'], reports, payload, manifest, project)
    return {'schema': 'apsis.corrected-closed-independent-validation/1', 'pass': True,
            'package_sha256': hashlib.sha256(read_member(directory, 'package.json')).hexdigest(),
            'model_sha256': model_sha, 'source_sha256': SOURCE_SHA256,
            'objects': 66, 'groups': 11, 'attributed_faces': 15940,
            'rest_faces': 15916, 'preserved_objects': 59, 'preserved_faces': 15696,
            'fresh_finite_volume_and_attachment_proofs': True,
            'runtime_actor_route_or_motion_admission': False}


# Packaging callers historically use this spelling; both are the same closed gate.
validate_package = validate_derivative


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output_dir', type=Path)
    parser.add_argument('--old-package', type=Path)
    args = parser.parse_args()
    print(json.dumps(validate_derivative(args.output_dir, args.old_package), indent=2, sort_keys=True))
