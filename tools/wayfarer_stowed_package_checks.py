"""Pure source-specific stowed render/contact validation; BSD-3-Clause.

The low-level fixture API validates explicit small profiles. Production first
authenticates independently selected decoded bytes; no receipt path, owner,
Blender, geometry producer, numerical proof or filesystem callback is followed.
"""
import hashlib
import json
import math
import struct

import stowed_asset_identity as identity
import wayfarer_corrected_rest_geometry as geometry

FRAME = identity.FRAME
PAYLOAD_PINS = identity.PAYLOAD_PINS
MAX_FILE = 32 * 1024 * 1024
MAX_TOTAL = 128 * 1024 * 1024
MAX_OUTPUT = 2 * 1024 * 1024
MAX_FACES = 20000
MAX_VERTICES = 20000
ROW_KEYS = {'node', 'node_face', 'source_object', 'source_face',
            'actual_primitive', 'actual_primitive_triangle'}
PROFILE_KEYS = {'source_sha256', 'coordinate_namespace', 'groups', 'objects',
                'retained_objects', 'replacement_objects', 'baseline_objects', 'frame'}
IDENTITY_KEYS = {'vertices', 'faces', 'node', 'introduced_connector',
                 'position_sha256', 'indices_sha256', 'uv_sha256', 'normal_sha256',
                 'material_names_sha256', 'emitted_sha256'}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def closed_json(raw, maximum=MAX_FILE):
    require(type(raw) is bytes and 0 < len(raw) <= maximum, 'Bounded immutable JSON bytes')
    depth = 0; quoted = False; escaped = False; token_start = 0
    for index, byte in enumerate(raw):
        if quoted:
            require(index - token_start <= 131072, 'JSON string token bound')
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True; token_start = index
        elif byte in (123, 91):
            depth += 1; require(depth <= 32, 'JSON depth before decoding')
        elif byte in (125, 93):
            depth -= 1; require(depth >= 0, 'JSON nesting')
    require(depth == 0 and not quoted, 'Complete JSON lexical closure')
    def pairs(values):
        result = {}
        for key, value in values:
            require(key not in result, 'Duplicate JSON key')
            result[key] = value
        return result
    def nonfinite(value):
        raise ValueError('Nonfinite JSON literal: ' + value)
    try:
        result = json.loads(raw, object_pairs_hook=pairs, parse_constant=nonfinite)
        pending = [result]
        while pending:
            value = pending.pop()
            if type(value) is dict:
                pending.extend(value.values())
            elif type(value) is list:
                pending.extend(value)
            elif type(value) is float:
                require(math.isfinite(value), 'Nonfinite JSON scalar')
        return result
    except (UnicodeError, json.JSONDecodeError, RecursionError, OverflowError) as error:
        raise ValueError('Malformed bounded JSON') from error


def packed(rows, width):
    require(type(rows) is list and 0 < len(rows) <= MAX_VERTICES * 3, 'Bounded scalar rows')
    output = bytearray()
    for row in rows:
        require(type(row) in (list, tuple) and len(row) == width, 'Scalar row dimension')
        require(all(type(x) in (int, float) and math.isfinite(x) and abs(x) <= 20 for x in row),
                'Finite true bounded scalar')
        raw = struct.pack('<' + 'f' * width, *row)
        restored = struct.unpack('<' + 'f' * width, raw)
        require(all(struct.pack('<d', float(a)) == struct.pack('<d', b)
                    for a, b in zip(row, restored)), 'Exact source binary32 including signed zero')
        output.extend(raw)
    return bytes(output)


def indices(rows, nv):
    require(type(rows) is list and 0 < len(rows) <= MAX_FACES, 'Bounded ordered source faces')
    require(all(type(t) is list and len(t) == 3 and
                all(type(i) is int and 0 <= i < nv for i in t) for t in rows),
            'True source index boundaries')
    return b''.join(struct.pack('<III', *row) for row in rows)


def describe_object(source, node, emitted_faces):
    """Fixture enrollment helper; production expectations remain external literals."""
    require(type(source) is dict, 'Source object')
    nv = len(source['vertices']); nt = len(source['triangles'])
    require(0 < nv <= MAX_VERTICES and 0 < nt <= MAX_FACES, 'Source mesh dimensions')
    point_raw = packed(source['vertices'], 3)
    face_raw = indices(source['triangles'], nv)
    uv = source['triangle_uv_gltf']
    require(type(uv) is list and len(uv) == nt and
            all(type(t) is list and len(t) == 3 for t in uv), 'Full source UV dimensions')
    uv_raw = packed([p for t in uv for p in t], 2)
    materials = source['triangle_materials']
    require(type(materials) is list and len(materials) == nt and
            all(type(n) is str and 0 < len(n) <= 256 for n in materials), 'Full material names')
    require(type(source['introduced_connector']) is bool, 'True connector tag')
    normals = source.get('corner_normals_blender')
    require(source['introduced_connector'] or normals is not None,
            'Original source has complete recorded loop normals')
    normal_sha = None
    if normals is not None:
        require(type(normals) is list and 0 < len(normals) <= 3 * nt, 'Recorded normal row dimensions')
        normal_sha = digest(packed(normals, 3))
    require(len(emitted_faces) == nt, 'Complete ordered emitted faces')
    # Cyclic rotation is joint across all corner attributes; winding stays fixed.
    emitted = [[row['material'], geometry.corner_bit_key(row)[1]] for row in emitted_faces]
    encoded = [[m, [[(k, v.hex()) for k, v in corner] for corner in corners]]
               for m, corners in emitted]
    return {'vertices': nv, 'faces': nt, 'node': node,
            'introduced_connector': source['introduced_connector'],
            'position_sha256': digest(point_raw), 'indices_sha256': digest(face_raw),
            'uv_sha256': digest(uv_raw), 'normal_sha256': normal_sha,
            'material_names_sha256': digest(canonical(materials)),
            'emitted_sha256': digest(canonical(encoded))}


def emitted_objects(model_raw, attribution_raw, expected_objects, expected_groups=None):
    require(type(model_raw) is bytes and 28 <= len(model_raw) <= MAX_FILE, 'Bounded immutable GLB')
    require(type(expected_objects) is int and 0 < expected_objects <= 128, 'Source object count')
    require(len(model_raw) >= 20, 'GLB JSON header')
    length = struct.unpack_from('<I', model_raw, 12)[0]
    require(0 < length <= 8 * 1024 * 1024 and 20 + length <= len(model_raw), 'GLB JSON reservation')
    closed_json(model_raw[20:20 + length], 8 * 1024 * 1024)
    decoded = geometry.decode_glb(model_raw)
    doc = decoded['document']
    require(type(doc.get('nodes')) is list and 0 < len(doc['nodes']) <= 128 and
            type(doc.get('meshes')) is list and len(doc['meshes']) == len(doc['nodes']),
            'Exact flat node/mesh dimensions')
    require(not any(doc.get(key) for key in ('animations', 'skins', 'cameras')),
            'Static geometry only')
    names = [node.get('name') for node in doc['nodes']]
    require(all(type(n) is str and 0 < len(n) <= 256 for n in names) and
            len(set(names)) == len(names), 'Unique named nodes')
    require({node.get('mesh') for node in doc['nodes']} == set(range(len(names))),
            'Unique complete mesh ownership')
    for node in doc['nodes']:
        require(not any(key in node for key in ('children', 'matrix', 'translation', 'rotation',
                                               'scale', 'skin', 'weights')), 'Flat static identity node')
    scene = doc.get('scene', 0)
    require(type(scene) is int and type(doc.get('scenes')) is list and
            len(doc['scenes']) == 1 and scene == 0 and
            type(doc['scenes'][0].get('nodes')) is list and
            all(type(i) is int for i in doc['scenes'][0]['nodes']) and
            sorted(doc['scenes'][0]['nodes']) == list(range(len(names))), 'Exact full identity-root scene')
    attribute_coverage = {}
    for mesh in doc['meshes']:
        require(not any(key in mesh for key in ('weights', 'extensions')), 'Static mesh declaration')
        for primitive in mesh['primitives']:
            require(not any(key in primitive for key in ('targets', 'extensions')),
                    'No hidden primitive mutation')
            for accessor_index in list(primitive['attributes'].values()) + [primitive['indices']]:
                require(type(accessor_index) is int and 0 <= accessor_index < len(doc['accessors']),
                        'True accessor index')
                count = doc['accessors'][accessor_index]['count']
                require(type(count) is int and 0 < count <= MAX_FACES * 3, 'Accessor allocation bound')
            iv = geometry.original.accessor(doc, decoded['payload'], primitive['indices'])
            used = {row[0] for row in iv}
            require(all(type(i) is int for i in used), 'True referenced accessor rows')
            for accessor_index in primitive['attributes'].values():
                attribute_coverage.setdefault(accessor_index, set()).update(used)
    # A shared accessor may serve several material primitives. Its complete
    # union must be referenced; hidden extra POSITION/NORMAL/UV rows refuse.
    for accessor_index, used in attribute_coverage.items():
        require(used == set(range(doc['accessors'][accessor_index]['count'])),
                'Complete referenced attribute rows')
    nodes = {name: geometry.node_faces(decoded, name) for name in names}
    require(sum(len(values) for values in nodes.values()) <= MAX_FACES, 'Complete render face bound')
    attr = closed_json(attribution_raw)
    require(type(attr) is dict and {'model_sha256', 'faces'} <= set(attr) and
            attr['model_sha256'] == digest(model_raw), 'Attribution model identity')
    require(all(key in ('model_sha256', 'faces') or key.startswith('all69_objects_all')
                for key in attr), 'Known attribution fields')
    faces = attr['faces']
    require(type(faces) is list and 0 < len(faces) <= MAX_FACES, 'Attribution face dimension')
    result = {}; used = set()
    for row in faces:
        require(type(row) is dict and set(row) == ROW_KEYS, 'Exact attribution row')
        require(type(row['node']) is str and row['node'] in nodes and
                type(row['source_object']) is str and 0 < len(row['source_object']) <= 256,
                'Attributed source/node names')
        require(all(type(row[key]) is int and 0 <= row[key] < MAX_FACES for key in
                    ('node_face', 'source_face', 'actual_primitive', 'actual_primitive_triangle')),
                'True bounded attribution indices')
        key = (row['node'], row['node_face'])
        require(key not in used and row['node_face'] < len(nodes[row['node']]), 'Unique emitted occurrence')
        actual = nodes[row['node']][row['node_face']]
        require(actual['primitive'] == row['actual_primitive'] and
                actual['primitive_triangle'] == row['actual_primitive_triangle'], 'Exact primitive attribution')
        rows = result.setdefault(row['source_object'], {})
        require(row['source_face'] not in rows, 'Unique source face')
        rows[row['source_face']] = (row, actual); used.add(key)
    require(used == {(n, i) for n, values in nodes.items() for i in range(len(values))},
            'Complete source/emitted occurrence bijection')
    require(len(result) == expected_objects, 'Complete source object roster')
    groups = {}
    for name, rows in result.items():
        require(set(rows) == set(range(len(rows))), 'Complete ordered source faces: ' + name)
        require(len({row['node'] for row, _ in rows.values()}) == 1, 'Single source node: ' + name)
        node = next(iter(rows.values()))[0]['node']; groups.setdefault(node, set()).add(name)
    if expected_groups is not None:
        require(groups == {g['node']: set(g['source_objects']) for g in expected_groups},
                'Exact render group membership')
    return result


def validate_geometry(model_raw, contact_raw, attribution_raw, baseline_model_raw,
                      baseline_attribution_raw, *, profile):
    """Pure low-level fixture API; an explicit profile is not production admission."""
    try:
        return _validate_geometry(model_raw, contact_raw, attribution_raw,
                                  baseline_model_raw, baseline_attribution_raw, profile)
    except (KeyError, TypeError, IndexError, struct.error, OverflowError) as error:
        raise ValueError('Malformed stowed buffers/profile') from error


def _validate_geometry(model_raw, contact_raw, attribution_raw, baseline_model_raw,
                       baseline_attribution_raw, profile):
    raws = (model_raw, contact_raw, attribution_raw, baseline_model_raw, baseline_attribution_raw)
    require(all(type(raw) is bytes and 0 < len(raw) <= MAX_FILE for raw in raws) and
            sum(map(len, raws)) <= MAX_TOTAL, 'Bounded immutable geometry inputs')
    require(type(profile) is dict and set(profile) == PROFILE_KEYS, 'Closed fixture profile')
    require(canonical(profile['frame']) == canonical(FRAME), 'Exact once-applied REST frame profile')
    groups = profile['groups']; expected = profile['objects']
    require(type(groups) is list and 0 < len(groups) <= 128 and
            type(expected) is dict and 0 < len(expected) <= 128, 'Finite profile roster')
    members = []
    for group in groups:
        require(type(group) is dict and set(group) == {'node', 'source_objects'} and
                type(group['node']) is str and type(group['source_objects']) is list and
                0 < len(group['source_objects']) <= 128, 'Closed group profile')
        members.extend(group['source_objects'])
    require(len({g['node'] for g in groups}) == len(groups) and len(set(members)) == len(members)
            and set(members) == set(expected), 'Complete unique profile ownership')
    for key in ('retained_objects', 'replacement_objects'):
        names = profile[key]
        require(type(names) is list and 0 < len(names) <= len(expected) and
                len(set(names)) == len(names) and set(names) <= set(expected), 'Selected profile roster')
    contact = closed_json(contact_raw)
    require(type(contact) is dict and set(contact) == {'schema', 'model_sha256', 'source_sha256',
            'coordinate_namespace', 'objects', 'actual_emitted_buffer_bits_verified',
            'runtime_actor_attachment_motion_admitted'}, 'Closed contact document')
    require(contact['schema'] == 'apsis.restraint-stowed-prototype-contact/1' and
            contact['model_sha256'] == digest(model_raw) and
            contact['source_sha256'] == profile['source_sha256'] and
            contact['coordinate_namespace'] == profile['coordinate_namespace'] and
            contact['actual_emitted_buffer_bits_verified'] is True and
            contact['runtime_actor_attachment_motion_admitted'] is False, 'Contact identity/frame/scope')
    require(type(contact['objects']) is dict and set(contact['objects']) == set(expected),
            'Complete contact object roster')
    current = emitted_objects(model_raw, attribution_raw, len(expected), groups)
    baseline = emitted_objects(baseline_model_raw, baseline_attribution_raw, profile['baseline_objects'])
    object_rows = []; retained_faces = 0
    for name in members:
        source = contact['objects'][name]; selected = expected[name]
        require(type(selected) is dict and set(selected) == IDENTITY_KEYS, 'Closed object identity')
        actual_rows = current[name]
        ordered = [actual_rows[i][1] for i in range(len(actual_rows))]
        observed = describe_object(source, selected['node'], ordered)
        require(canonical(observed) == canonical(selected), 'Exact source/normal/emitted profile: ' + name)
        require(all(row['node'] == selected['node'] for row, _ in actual_rows.values()),
                'Exact selected source node')
        for face, (_, row) in actual_rows.items():
            points = [source['vertices'][i] for i in source['triangles'][face]]
            uv = source['triangle_uv_gltf'][face]
            expected_points = [packed([p], 3) for p in points]
            expected_uv = [packed([p], 2) for p in uv]
            corners = row['corners']
            require(any(expected_points[shift:] + expected_points[:shift] ==
                        [packed([c['POSITION']], 3) for c in corners] and
                        expected_uv[shift:] + expected_uv[:shift] ==
                        [packed([c['TEXCOORD_0']], 2) for c in corners] for shift in range(3)),
                    'Exact source POSITION/UV occurrence: ' + name + ':' + str(face))
            require(row['material']['name'] == source['triangle_materials'][face],
                    'Exact source material ownership')
        if name in profile['retained_objects']:
            require(name in baseline and set(baseline[name]) == set(actual_rows),
                    'Complete retained baseline source faces: ' + name)
            for face in actual_rows:
                old = baseline[name][face][1]; new = actual_rows[face][1]
                require(canonical(old['material']) == canonical(new['material']) and
                        geometry.corner_bit_key(old) == geometry.corner_bit_key(new),
                        'Retained full corner/material bits: ' + name + ':' + str(face))
            retained_faces += len(actual_rows)
        object_rows.append({'source_object': name, 'node': selected['node'],
                            'vertices': observed['vertices'], 'faces': observed['faces'],
                            'emitted_sha256': observed['emitted_sha256']})
    frame = canonical(FRAME)
    original = {row['source_object']: row for row in identity.ORIGINAL_RANGES}
    runtime_objects = []
    for name in profile['replacement_objects']:
        source = contact['objects'][name]
        runtime_objects.append({'source_object': name, 'render_node': expected[name]['node'],
            'introduced_connector': source['introduced_connector'],
            'original_catalog_range': original.get(name),
            'vertices': source['vertices'], 'triangles': source['triangles'],
            'source_faces': list(range(len(source['triangles'])))})
    runtime = canonical({'schema': 'apsis.wayfarer-stowed-runtime-contact/1',
        'model_sha256': digest(model_raw), 'source_sha256': profile['source_sha256'],
        'frame_sha256': digest(frame), 'objects': runtime_objects,
        'retained_originals_and_halo_added_again': False, 'runtime_actor_admitted': False})
    require(len(runtime) + len(frame) <= MAX_OUTPUT, 'Compact contact/frame output cap')
    preservation = {'schema': 'apsis.wayfarer-stowed-render-preservation/1',
        'source_sha256': profile['source_sha256'], 'model_sha256': digest(model_raw),
        'contact_sha256': digest(contact_raw), 'attribution_sha256': digest(attribution_raw),
        'groups': groups, 'objects': object_rows, 'complete_source_faces': sum(r['faces'] for r in object_rows),
        'retained_objects': len(profile['retained_objects']), 'retained_faces': retained_faces,
        'retained_complete_corner_material_bits_match': True,
        'changed_normal_profile_is_versioned_not_unchanged': True,
        'geometry_or_numerical_replays': 0, 'runtime_actor_admitted': False}
    return {'preservation': preservation, 'runtime_contact': runtime, 'frame': frame}


def validate_payloads(payloads):
    """Production selection: independently authenticate every named byte role first."""
    require(type(payloads) is dict and bool(identity.PAYLOAD_PINS) and
            set(payloads) == set(identity.PAYLOAD_PINS), 'Exact selected production payload roster')
    total = 0
    for name, pin in identity.PAYLOAD_PINS.items():
        raw = payloads[name]
        require(type(raw) is bytes and 0 < len(raw) <= MAX_FILE and
                len(raw) == pin['bytes'] and digest(raw) == pin['sha256'], 'Independent payload identity: ' + name)
        total += len(raw)
    require(total <= MAX_TOTAL, 'Production payload aggregate bound')
    qualification = closed_json(payloads['qualification.json'], 65536)
    require(canonical(qualification) == canonical(identity.EVIDENCE), 'Exact portable qualification')
    require(qualification['schema'] == 'apsis.wayfarer-stowed-static-qualification/1' and
            type(qualification['candidate']) is int and qualification['candidate'] == 19 and
            qualification['source_sha256'] == identity.SOURCE_SHA256 and
            qualification['model_sha256'] == digest(payloads['model.glb']) and
            qualification['contact_sha256'] == digest(payloads['contact.json']) and
            qualification['attribution_sha256'] == digest(payloads['face-attribution.json']),
            'Current independently qualified payload binding')
    for row in qualification['correspondence']['objects']:
        source = identity.PROFILE['objects'][row['source_object']]
        fields = {'point_bits_sha256': 'position_sha256',
                  'ordered_indices_sha256': 'indices_sha256',
                  'UV_bits_sha256': 'uv_sha256', 'flat_normal_bits_sha256': 'normal_sha256',
                  'complete_vertices': 'vertices', 'complete_ordered_faces': 'faces',
                  'current_emitted_node': 'node'}
        require(all(canonical(row[k]) == canonical(source[v]) for k, v in fields.items()),
                'Accepted seven-part source/phase binding')
    for row in qualification['correspondence']['evaluated_normal_observation']['shoulders']:
        require(row['after_normal_f4_pin']['sha256'] ==
                identity.PROFILE['objects'][row['name']]['normal_sha256'],
                'Exact registered AFTER normal profile')
    result = validate_geometry(payloads['model.glb'], payloads['contact.json'],
        payloads['face-attribution.json'], payloads['baseline/model.glb'],
        payloads['baseline/face-attribution.json'], profile=identity.PROFILE)
    result['preservation']['qualification'] = qualification
    return result
