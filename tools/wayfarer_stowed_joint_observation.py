# SPDX-License-Identifier: BSD-3-Clause
"""Observe only the ten declared static attachment boundaries; no permission."""
import hashlib
import itertools
import json
import math
import struct

MAX_CAPTURE = 64 * 1024 * 1024
MAX_BRIDGE = 65536
MAX_REGISTRATION = 65536
MAX_OUTPUT = 1024 * 1024
MAX_ENTRIES = 16_000_000
MAX_OBSERVATION_OPERATIONS = 64_000_000
NAMESPACE = 'actual direct REST canonical metres, flat world-baked prototype, no runtime mapping/admission'
IDS = ('shoulder_port', 'shoulder_starboard', 'lap_port', 'lap_starboard', 'crotch')
ROLES = ('capture',) + IDS
OWNER_PROFILE = {'Anti-submarining strap': ['source_0160', 12, 20], 'Contoured seat-pan shell': ['source_0953', 378, 752], 'Harness shoulder anchor': ['source_1196', 56, 108], 'Harness shoulder anchor.001': ['source_1197', 56, 108], 'Lap restraint': ['source_1200', 12, 20], 'Lap restraint.001': ['source_1201', 12, 20], 'Shoulder restraint': ['source_1396', 20, 36], 'Shoulder restraint.001': ['source_1397', 20, 36]}
ATTACHMENTS = [{'bridge_id': 'shoulder_port', 'cap_face_ids': [0, 1], 'cap_triangles': [[2, 3, 0], [1, 2, 0]], 'end': 'root', 'id': 'shoulder_port:root', 'kind': 'copied_whole', 'source_boundary_vertex_ids': [1, 11, 10, 0], 'source_face_ids': [18, 19], 'source_id': 'source_1396'}, {'bridge_id': 'shoulder_port', 'cap_face_ids': [2, 3, 4, 5], 'cap_triangles': [[16, 15, 14], [17, 16, 14], [12, 17, 14], [13, 12, 14]], 'end': 'fixed', 'id': 'shoulder_port:fixed', 'kind': 'partial_anchor', 'source_boundary_vertex_ids': None, 'source_face_ids': [104, 105], 'source_id': 'source_1196'}, {'bridge_id': 'shoulder_starboard', 'cap_face_ids': [0, 1], 'cap_triangles': [[2, 3, 0], [1, 2, 0]], 'end': 'root', 'id': 'shoulder_starboard:root', 'kind': 'copied_whole', 'source_boundary_vertex_ids': [1, 11, 10, 0], 'source_face_ids': [18, 19], 'source_id': 'source_1397'}, {'bridge_id': 'shoulder_starboard', 'cap_face_ids': [2, 3, 4, 5], 'cap_triangles': [[16, 15, 14], [17, 16, 14], [12, 17, 14], [13, 12, 14]], 'end': 'fixed', 'id': 'shoulder_starboard:fixed', 'kind': 'partial_anchor', 'source_boundary_vertex_ids': None, 'source_face_ids': [104, 105], 'source_id': 'source_1197'}, {'bridge_id': 'lap_port', 'cap_face_ids': [0, 1], 'cap_triangles': [[2, 3, 0], [1, 2, 0]], 'end': 'root', 'id': 'lap_port:root', 'kind': 'copied_whole', 'source_boundary_vertex_ids': [1, 7, 6, 0], 'source_face_ids': [10, 11], 'source_id': 'source_1200'}, {'bridge_id': 'lap_port', 'cap_face_ids': [2, 3], 'cap_triangles': [[139, 138, 137], [136, 139, 137]], 'end': 'fixed', 'id': 'lap_port:fixed', 'kind': 'copied_whole', 'source_boundary_vertex_ids': [61, 76, 77, 62], 'source_face_ids': [86, 87], 'source_id': 'source_0953'}, {'bridge_id': 'lap_starboard', 'cap_face_ids': [0, 1], 'cap_triangles': [[2, 3, 0], [1, 2, 0]], 'end': 'root', 'id': 'lap_starboard:root', 'kind': 'copied_whole', 'source_boundary_vertex_ids': [1, 7, 6, 0], 'source_face_ids': [10, 11], 'source_id': 'source_1201'}, {'bridge_id': 'lap_starboard', 'cap_face_ids': [2, 3], 'cap_triangles': [[139, 138, 137], [136, 139, 137]], 'end': 'fixed', 'id': 'lap_starboard:fixed', 'kind': 'copied_whole', 'source_boundary_vertex_ids': [72, 87, 88, 73], 'source_face_ids': [108, 109], 'source_id': 'source_0953'}, {'bridge_id': 'crotch', 'cap_face_ids': [0, 1], 'cap_triangles': [[2, 3, 0], [1, 2, 0]], 'end': 'root', 'id': 'crotch:root', 'kind': 'copied_whole', 'source_boundary_vertex_ids': [1, 7, 6, 0], 'source_face_ids': [10, 11], 'source_id': 'source_0160'}, {'bridge_id': 'crotch', 'cap_face_ids': [2, 3, 4, 5], 'cap_triangles': [[215, 218, 217], [216, 215, 217], [214, 219, 218], [215, 214, 218]], 'end': 'fixed', 'id': 'crotch:fixed', 'kind': 'copied_whole', 'source_boundary_vertex_ids': [68, 67, 66, 81, 82, 83], 'source_face_ids': [96, 97, 98, 99], 'source_id': 'source_0953'}]
BRIDGE_PROFILE = {
    'shoulder_port': (18, 32), 'shoulder_starboard': (18, 32),
    'lap_port': (140, 276), 'lap_starboard': (140, 276), 'crotch': (220, 436),
}


def pin(raw):
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


class ObservationRefusal(ValueError):
    def __init__(self, reason, context):
        super().__init__(reason)
        self.observation_context = dict(context)


class Work:
    def __init__(self, limit):
        if type(limit) is not int or not 0 < limit <= MAX_OBSERVATION_OPERATIONS:
            raise ObservationRefusal('Observation work limit', {'coverage_complete': False})
        self.limit = limit
        self.context = {'operations': 0, 'stage': 'registration', 'caps_completed': 0,
                        'source_face_visits': 0, 'bridge_face_visits': 0,
                        'coverage_complete': False, 'physical_permission': False}

    def require(self, condition, reason):
        if not condition:
            raise ObservationRefusal(reason, self.context)

    def charge(self, amount=1):
        self.require(self.context['operations'] + amount <= self.limit, 'Observation work exhausted')
        self.context['operations'] += amount


def closed(value, keys, w):
    w.require(type(value) is dict and set(value) == set(keys), 'Closed metadata schema')


def integer(value, lo, hi, w):
    w.require(type(value) is int and lo <= value <= hi, 'True integer count/index')


def sha_string(value, w):
    w.require(type(value) is str and len(value) == 64 and all(c in '0123456789abcdef' for c in value), 'SHA256 metadata')


def decode(raw, maximum, w, entry_limit=MAX_ENTRIES):
    # The reviewed capture-reader decoder's byte/depth/duplicate/type guards.
    w.require(type(raw) is bytes and 0 < len(raw) <= maximum, 'Bounded immutable JSON')
    w.charge(len(raw))  # lexical byte visits, independent of numerical allowances
    depth = 0
    quoted = escaped = primitive = False
    tokens = 0
    for byte in raw:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
            primitive = False
            tokens += 1
            w.require(tokens <= entry_limit, 'JSON lexical entry ceiling before allocation')
        elif byte in (91, 123):
            primitive = False
            tokens += 1
            w.require(tokens <= entry_limit, 'JSON lexical entry ceiling before allocation')
            depth += 1
            w.require(depth <= 32, 'JSON depth')
        elif byte in (93, 125):
            primitive = False
            depth -= 1
            w.require(depth >= 0, 'JSON closure')
        elif byte in (9, 10, 13, 32, 44, 58):
            primitive = False
        elif not primitive:
            primitive = True
            tokens += 1
            w.require(tokens <= entry_limit, 'JSON lexical entry ceiling before allocation')
    w.require(depth == 0 and not quoted, 'JSON closure')
    def pairs(rows):
        result = {}
        for key, value in rows:
            w.require(key not in result, 'Duplicate JSON key')
            result[key] = value
        return result
    def bad(_):
        raise ObservationRefusal('Nonfinite JSON constant', w.context)
    try:
        value = json.loads(raw, object_pairs_hook=pairs, parse_constant=bad)
    except (UnicodeError, json.JSONDecodeError) as error:
        raise ObservationRefusal('JSON decoding', w.context) from error
    todo, count = [value], 0
    while todo:
        item = todo.pop()
        count += 1
        w.charge()
        w.require(count <= entry_limit, 'JSON entry ceiling')
        if type(item) is dict:
            todo.extend(item.values())
        elif type(item) is list:
            todo.extend(item)
        elif type(item) is float:
            w.require(math.isfinite(item), 'Nonfinite JSON scalar')
    return value


def mesh(value, nv, nf, w):
    vv, ff = value['vertices'], value.get('triangles', value.get('faces'))
    w.require(type(vv) is list and len(vv) == nv and type(ff) is list and len(ff) == nf, 'Exact complete mesh dimensions')
    for p in vv:
        w.charge()
        w.require(type(p) is list and len(p) == 3, 'Raw point dimensions')
        for x in p:
            w.require(type(x) is float and math.isfinite(x) and abs(x) <= 128, 'Finite raw coordinate')
            w.require(struct.pack('<d', x) == struct.pack('<d', struct.unpack('<f', struct.pack('<f', x))[0]), 'Exact binary32 including signed zero')
    for face in ff:
        w.charge()
        w.require(type(face) is list and len(face) == 3, 'Face index dimensions')
        for i in face:
            integer(i, 0, nv - 1, w)
    # Unrelated retained degenerate faces are neither removed nor validated.
    return vv, ff


def point_bits(p):
    return struct.pack('<fff', *p).hex()


def payload_pins(vv, ff, w=None):
    if w is not None:
        w.charge(len(vv) + len(ff))
    return (hashlib.sha256(b''.join(struct.pack('<fff', *p) for p in vv)).hexdigest(),
            hashlib.sha256(b''.join(struct.pack('<iii', *f) for f in ff)).hexdigest())


def perimeter(faces, selected, expected_edges, w):
    # Same finite index-cycle method as the reviewed fixed-anchor observer.
    uses = {}
    for fid in selected:
        face = faces[fid]
        w.require(len(set(face)) == 3, 'Selected indexed simplex')
        for i in range(3):
            a, b = face[i], face[(i + 1) % 3]
            uses.setdefault(tuple(sorted((a, b))), []).append((fid, a, b))
            w.charge()
    boundary, interior = [], []
    for edge, rows in sorted(uses.items()):
        if len(rows) == 1:
            boundary.append({'vertex_ids': list(edge), 'owner_face': rows[0][0],
                             'directed_vertex_ids': list(rows[0][1:])})
        else:
            w.require(len(rows) == 2 and rows[0][1:] == rows[1][1:][::-1], 'Opposed internal diagonal')
            interior.append({'vertex_ids': list(edge), 'face_ids': [r[0] for r in rows]})
    w.require(len(boundary) == expected_edges, 'Exact union boundary count')
    links = {}
    for row in boundary:
        a, b = row['vertex_ids']
        links.setdefault(a, []).append(b)
        links.setdefault(b, []).append(a)
    w.require(len(links) == expected_edges and all(len(v) == 2 for v in links.values()), 'Perimeter cycle degree')
    seen, todo = set(), [min(links)]
    while todo:
        i = todo.pop()
        if i not in seen:
            seen.add(i)
            todo.extend(links[i])
    w.require(seen == set(links), 'Connected perimeter cycle')
    return boundary, interior


def incidence(faces, edge, kind, w):
    full, endpoints = [], []
    for fid, face in enumerate(faces):
        w.charge()
        w.context[kind + '_face_visits'] += 1
        shared = sorted(set(face) & set(edge))
        if len(shared) == 2:
            # All three indexed pairs of a triangle are actual edges.
            full.append({'face': fid, 'indices': face[:]})
        elif shared:
            endpoints.append({'face': fid, 'indices': face[:], 'vertex_ids': shared})
    return {'edge_faces': full, 'endpoint_only_faces': endpoints}


def endpoint_incidence(faces, vid, kind, w):
    rows = []
    for fid, face in enumerate(faces):
        w.charge()
        w.context[kind + '_face_visits'] += 1
        if vid in face:
            rows.append({'face': fid, 'indices': face[:]})
    return rows


def rectangle(vv, boundary, w):
    ids = sorted({i for r in boundary for i in r['vertex_ids']})
    pp = [vv[i] for i in ids]
    values = [sorted({p[k] for p in pp}) for k in range(3)]
    w.require(sorted(map(len, values)) == [1, 2, 2], 'Supported exact indexed owner rectangle')
    w.require(len({tuple(p) for p in pp}) == 4 and all(tuple(p) in {tuple(q) for q in pp} for p in itertools.product(*values)), 'Complete owner rectangle corners')
    for row in boundary:
        a, b = [vv[i] for i in row['vertex_ids']]
        w.require(sum(a[k] != b[k] for k in range(3)) == 1, 'Exact axis-aligned owner edge')
    return values


def contains_axis_edge(a, b, p, w):
    varying = [k for k in range(3) if a[k] != b[k]]
    w.require(len(varying) == 1, 'Exact noncollapsed axis edge')
    k = varying[0]
    return all(p[j] == a[j] for j in range(3) if j != k) and min(a[k], b[k]) <= p[k] <= max(a[k], b[k])


def observe_attachments(registration_bytes, independently_expected_registration_sha256, buffers,
                        *, operation_limit=MAX_OBSERVATION_OPERATIONS):
    w = Work(operation_limit)
    sha_string(independently_expected_registration_sha256, w)
    w.require(type(registration_bytes) is bytes and 0 < len(registration_bytes) <= MAX_REGISTRATION, 'Registration bytes before hashing')
    w.require(pin(registration_bytes)['sha256'] == independently_expected_registration_sha256, 'Independent registration SHA')
    r = decode(registration_bytes, MAX_REGISTRATION, w, 8192)
    closed(r, ('schema', 'namespace', 'model_sha256', 'source_sha256', 'buffers', 'owners', 'bridges', 'attachments'), w)
    w.require(r['schema'] == 'apsis.all-attachment-perimeter-intake/1' and r['namespace'] == NAMESPACE, 'Purpose and REST namespace')
    for k in ('model_sha256', 'source_sha256'):
        sha_string(r[k], w)
    closed(r['buffers'], ROLES, w)
    w.require(type(buffers) is dict and set(buffers) == set(ROLES), 'Exact six immutable roles')
    for name in ROLES:
        row = r['buffers'][name]
        closed(row, ('bytes', 'sha256'), w)
        integer(row['bytes'], 1, MAX_CAPTURE if name == 'capture' else MAX_BRIDGE, w)
        sha_string(row['sha256'], w)
        w.require(type(buffers[name]) is bytes and len(buffers[name]) == row['bytes'], 'Role bytes before hashing ' + name)
        w.require(pin(buffers[name]) == row, 'Approved whole role ' + name)
    w.require(type(r['owners']) is list and len(r['owners']) == 8 and type(r['bridges']) is list and len(r['bridges']) == 5 and type(r['attachments']) is list and len(r['attachments']) == 10, 'Closed observation rosters')
    owners = {}
    for row in r['owners']:
        closed(row, ('source_id', 'source_object', 'vertices', 'faces', 'source_signature', 'float32_bits_sha256', 'int32_bits_sha256'), w)
        name = row['source_object']
        w.require(type(name) is str and name in OWNER_PROFILE and name not in owners, 'Unique fixed owner')
        w.require([row['source_id'], row['vertices'], row['faces']] == OWNER_PROFILE[name], 'Fixed owner occurrence/count')
        integer(row['vertices'], 1, 566, w); integer(row['faces'], 1, 1100, w)
        for k in ('source_signature', 'float32_bits_sha256', 'int32_bits_sha256'):
            sha_string(row[k], w)
        owners[name] = row
    w.require(set(owners) == set(OWNER_PROFILE), 'Complete fixed owner names')
    bridges = {}
    for index, row in enumerate(r['bridges']):
        closed(row, ('id', 'source_object', 'vertices', 'faces', 'float32_bits_sha256', 'int32_bits_sha256'), w)
        w.require(row['id'] == IDS[index] and row['source_object'] == 'Stowed restraint static connector ' + IDS[index], 'Fixed ordered bridge occurrence')
        w.require((row['vertices'], row['faces']) == BRIDGE_PROFILE[row['id']], 'Fixed bridge dimensions')
        integer(row['vertices'], 1, 220, w); integer(row['faces'], 1, 436, w)
        for k in ('float32_bits_sha256', 'int32_bits_sha256'):
            sha_string(row[k], w)
        bridges[row['id']] = row
    for expected, actual in zip(ATTACHMENTS, r['attachments']):
        closed(actual, tuple(expected) + ('finite_cap_evidence_sha256',), w)
        w.require(canonical({k: actual[k] for k in expected}) == canonical(expected), 'Exact selected descriptor/index metadata')
        sha_string(actual['finite_cap_evidence_sha256'], w)
    w.context['stage'] = 'capture_decode'
    capture = decode(buffers['capture'], MAX_CAPTURE, w)
    w.require(type(capture) is dict and capture['namespace'] == NAMESPACE and capture['rest_shape_once'] is True and capture['operating_tuple'] == [0, 0, 0, 0] and all(type(i) is int for i in capture['operating_tuple']), 'Captured original REST frame')
    w.require(capture['source_sha256'] == r['source_sha256'] and capture['model_output']['model']['sha256'] == r['model_sha256'], 'Captured source/model identity')
    geometry = capture['prepared_source_geometry']
    w.require(type(geometry) is dict and len(geometry) == 69 and sum(type(g) is dict and g.get('introduced_connector') is False for g in geometry.values()) == 64, 'Complete prepared and original capture roster')
    before, restored = capture['all1746_covered_signatures_before'], capture['all1746_covered_signatures_restored']
    w.require(type(before) is dict and type(restored) is dict and len(before) == len(restored) == 1746, 'Original signature roster')
    source_meshes, bridge_meshes = {}, {}
    for name, row in owners.items():
        w.require(name in geometry and geometry[name]['introduced_connector'] is False and before[name] == restored[name] == row['source_signature'], 'Original owner and source signature')
        vv, ff = mesh(geometry[name], row['vertices'], row['faces'], w)
        w.require(payload_pins(vv, ff, w) == (row['float32_bits_sha256'], row['int32_bits_sha256']), 'Original owner complete raw payload')
        source_meshes[row['source_id']] = (vv, ff)
    for identity, row in bridges.items():
        g = decode(buffers[identity], MAX_BRIDGE, w)
        closed(g, ('vertices', 'faces'), w)
        vv, ff = mesh(g, row['vertices'], row['faces'], w)
        w.require(payload_pins(vv, ff, w) == (row['float32_bits_sha256'], row['int32_bits_sha256']), 'Original bridge complete raw payload')
        w.require(row['source_object'] in geometry and geometry[row['source_object']]['introduced_connector'] is True, 'Capture bridge occurrence')
        cv, cf = mesh(geometry[row['source_object']], row['vertices'], row['faces'], w)
        w.require(payload_pins(cv, cf, w) == payload_pins(vv, ff, w), 'Captured and original bridge raw correspondence')
        bridge_meshes[identity] = (vv, ff)
    records = []
    for descriptor in r['attachments']:
        w.context['stage'] = descriptor['id']
        bv, bf = bridge_meshes[descriptor['bridge_id']]
        sv, sf = source_meshes[descriptor['source_id']]
        caps, selected = descriptor['cap_face_ids'], descriptor['source_face_ids']
        for i in caps: integer(i, 0, len(bf) - 1, w)
        for i in selected: integer(i, 0, len(sf) - 1, w)
        w.require([bf[i] for i in caps] == descriptor['cap_triangles'], 'Actual cap ordered triangles/diagonals')
        count = 6 if descriptor['kind'] == 'partial_anchor' or descriptor['id'] == 'crotch:fixed' else 4
        cb, ci = perimeter(bf, caps, count, w)
        sb, si = perimeter(sf, selected, 4 if descriptor['kind'] == 'partial_anchor' else count, w)
        mapping, values = {}, None
        if descriptor['kind'] == 'copied_whole':
            reference = descriptor['source_boundary_vertex_ids']
            w.require(set(reference) == {i for row in sb for i in row['vertex_ids']}, 'Registered original source boundary')
            for bid in sorted({i for row in cb for i in row['vertex_ids']}):
                matched = [sid for sid in reference if point_bits(bv[bid]) == point_bits(sv[sid])]
                w.require(len(matched) == 1, 'Unique registered source-index raw point mapping')
                mapping[bid] = matched[0]
            w.require(len(set(mapping.values())) == count, 'Injective registered cap boundary mapping')
            for cap, source in zip(caps, selected):
                original = sf[source]
                mapped = [mapping[i] for i in reversed(bf[cap])]
                w.require(any(mapped == original[k:] + original[:k] for k in range(3)), 'Copied original cap opposition/actual diagonal')
        else:
            values = rectangle(sv, sb, w)
            for bid in sorted({i for row in cb for i in row['vertex_ids']}):
                w.require(all(min(values[k]) <= bv[bid][k] <= max(values[k]) for k in range(3)), 'Partial cap point in finite original rectangle')
        edges = []
        for row in cb:
            edge = row['vertex_ids']; pp = [bv[i] for i in edge]
            w.require(point_bits(pp[0]) != point_bits(pp[1]) and pp[0] != pp[1], 'Noncollapsed selected cap edge')
            matches = []
            if mapping:
                mapped = sorted(mapping[i] for i in edge)
                matches = [x for x in sb if x['vertex_ids'] == mapped]
            else:
                matches = [x for x in sb if all(contains_axis_edge(*[sv[i] for i in x['vertex_ids']], p, w) for p in pp)]
            w.require(len(matches) <= 1, 'Unique finite indexed owner edge')
            owner_rows = []
            for match in matches:
                inc = incidence(sf, match['vertex_ids'], 'source', w)
                for e in inc['endpoint_only_faces']:
                    e['endpoint_in_finite_cap_interval'] = [contains_axis_edge(*pp, sv[i], w) if not mapping else i in [mapping[j] for j in edge] for i in e['vertex_ids']]
                owner_rows.append({'source_edge': match['vertex_ids'], 'owner_face': match['owner_face'], 'incidence': inc})
            edges.append({'cap_edge': edge, 'directed_cap_edge': row['directed_vertex_ids'], 'cap_owner_face': row['owner_face'], 'raw_endpoint_bits': [point_bits(p) for p in pp], 'bridge_incidence': incidence(bf, edge, 'bridge', w), 'source_owner_matches': owner_rows, 'relation': 'FINITE_INDEXED_OWNER_EDGE' if matches else 'NOT_ON_INDEXED_OWNER_PERIMETER'})
        corners = []
        for bid in sorted({i for row in cb for i in row['vertex_ids']}):
            sid = mapping.get(bid)
            endpoints = []
            if sid is not None:
                endpoints = [{'source_vertex': sid, 'raw_point_bits': point_bits(sv[sid]), 'source_incident_faces': endpoint_incidence(sf, sid, 'source', w)}]
            else:
                for source_vertex in sorted({i for row in sb for i in row['vertex_ids']}):
                    if bv[bid] == sv[source_vertex]:
                        endpoints.append({'source_vertex': source_vertex, 'raw_point_bits': point_bits(sv[source_vertex]), 'source_incident_faces': endpoint_incidence(sf, source_vertex, 'source', w)})
            corners.append({'bridge_vertex': bid, 'raw_point_bits': point_bits(bv[bid]), 'bridge_incident_faces': endpoint_incidence(bf, bid, 'bridge', w), 'original_indexed_corner_matches': endpoints, 'source_corner_binding_kind': 'REGISTERED_RAW_INDEXED' if sid is not None else 'EXACT_RECTANGLE_POINT_RELATION_ONLY', 'finite_owner_edge_point_contacts': [x['vertex_ids'] for x in sb if contains_axis_edge(*[sv[i] for i in x['vertex_ids']], bv[bid], w)] if not mapping else [x['vertex_ids'] for x in sb if sid in x['vertex_ids']], 'corner_contact_is_not_edge_permission': True})
        for source_edge in sb:
            source_edge['raw_endpoint_bits'] = [point_bits(sv[i]) for i in source_edge['vertex_ids']]
            source_edge['incidence'] = incidence(sf, source_edge['vertex_ids'], 'source', w)
        source_corners = [{'source_vertex': i, 'raw_point_bits': point_bits(sv[i]), 'source_incident_faces': endpoint_incidence(sf, i, 'source', w)} for i in sorted({v for row in sb for v in row['vertex_ids']})]
        records.append({'descriptor': descriptor, 'source_boundary': sb, 'source_corners': source_corners, 'source_internal_diagonals': si, 'cap_boundary': cb, 'cap_internal_diagonals': ci, 'copied_boundary_vertex_map': [[k, mapping[k]] for k in sorted(mapping)], 'edges': edges, 'corners': corners})
        w.context['caps_completed'] += 1
    result = {'schema': 'apsis.all-attachment-perimeter-observation/1', 'status': 'COMPLETE_DECLARED_ATTACHMENT_PERIMETER_OBSERVATION', 'registration_sha256': independently_expected_registration_sha256, 'input_pins': r['buffers'], 'namespace': NAMESPACE, 'records': records,
              'source_index_rosters': [{**row, 'indices': source_meshes[row['source_id']][1]} for row in r['owners']],
              'bridge_index_rosters': [{**row, 'indices': bridge_meshes[row['id']][1]} for row in r['bridges']],
              'counts': {'owners': 8, 'bridges': 5, 'caps': 10, 'cap_triangles': 26, 'boundary_edge_occurrences': 46, 'source_vertices': 566, 'source_faces': 1100, 'bridge_vertices': 536, 'bridge_faces': 1052}, 'coverage_complete': True,
              'observation_work': {**w.context, 'coverage_complete': True}, 'operation_limit': operation_limit,
              'physical_permission': False, 'source_material_runtime_authority': False,
              'factory_import_seed_classification_numeric_calls': 0}
    raw = canonical(result)
    w.require(len(raw) <= MAX_OUTPUT, 'Complete observation byte ceiling')
    w.context['coverage_complete'] = True
    return raw
