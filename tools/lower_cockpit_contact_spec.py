"""Bounded closed lower-contact admission; source matrices remain provenance only."""
import json
import math

import prepare_native_assets as base
from lower_cockpit_contact_identity import HALO_HEADER, IDENTITIES, POLICY

MAX_RUNTIME = 2 * 1024 * 1024
MAX_SOURCE = 6 * 1024 * 1024
MAX_CONTACT = 32 * 1024 * 1024
MAX_TOTAL = 8 * 1024 * 1024
MAX_DEPTH = 16
OBJECT_KEYS = ('owner', 'source_object', 'source_evaluated_triangles', 'crop_retained_triangles',
               'bounds_corrected_rest_m', 'source_corrected_world_rows', 'motion_group',
               'admitted_range', 'halo_triangle_start', 'halo_triangle_count',
               'evaluated_source_triangle_indices', 'halo_bounds_rest_m')


def decode(raw, maximum):
    base.require(type(raw) is bytes and 0 < len(raw) <= maximum, 'document byte bound')
    depth, quoted, escaped = 0, False, False
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
        elif byte in (91, 123):
            depth += 1
            base.require(depth <= MAX_DEPTH, 'JSON depth bound')
        elif byte in (93, 125):
            depth -= 1
            base.require(depth >= 0, 'unbalanced JSON')
    def number(text):
        value = float(text)
        base.require(math.isfinite(value), 'nonfinite JSON number')
        return value
    def invalid(text):
        raise ValueError('nonfinite JSON constant: ' + text)
    return json.loads(raw.decode('utf-8'), object_pairs_hook=base.pairs_unique,
                      parse_float=number, parse_constant=invalid)


def encode(value):
    return (json.dumps(value, indent=2, allow_nan=False) + '\n').encode('utf-8')


def integer(value, low, high):
    base.require(type(value) is int and low <= value <= high, 'invalid integer/index')


def array(value, count):
    base.require(type(value) is list and len(value) == count, 'array dimension/count')


def exact(value, selected):
    # Python equality alone would admit bool as integer, or floats as integer indices.
    base.require(type(value) is type(selected), 'selected value type changed')
    if type(selected) is dict:
        base.keys(value, selected)
        for key in selected:
            exact(value[key], selected[key])
    elif type(selected) is list:
        array(value, len(selected))
        for item, expected in zip(value, selected):
            exact(item, expected)
    else:
        base.require(value == selected, 'independently selected value changed')


def vector(value):
    array(value, 3)
    for x in value:
        base.require(type(x) in (int, float) and math.isfinite(x) and abs(x) <= 100,
                     'nonfinite/unbounded vector')


def bounds(value):
    array(value, 2)
    for v in value:
        vector(v)
    base.require(all(a <= b for a, b in zip(*value)), 'inverted bounds')


def affine(value):
    array(value, 4)
    for row in value:
        array(row, 4)
        for x in row:
            base.require(type(x) in (int, float) and math.isfinite(x) and abs(x) <= 100,
                         'nonfinite/unbounded source transform')
    base.require(value[3] == [0, 0, 0, 1], 'nonaffine source provenance')
    # Legitimate source scales must pass; this matrix is never applied to baked vertices.
    a, b, c = [row[:3] for row in value[:3]]
    determinant = sum(x * y for x, y in zip(a, cross(b, c)))
    base.require(1e-8 < determinant < 100, 'collapsed/reflected source transform')


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def validate_policy(value):
    exact(value, POLICY)
    return value


def validate_halo(value, contact, support):
    base.keys(value, (*HALO_HEADER, 'vertices_micrometres', 'triangles', 'objects'))
    for key, selected in HALO_HEADER.items():
        exact(value[key], selected)
    base.require(contact['sources'] == value['sources'] and
                 contact['coordinate_contracts']['craft'] == value['coordinate_contract'] and
                 contact['quantization_metres'] == value['quantization_metres'], 'original source/frame mismatch')
    base.require(support['identities']['contact_sha256'] == IDENTITIES['contact_sha256'],
                 'original attribution identity mismatch')
    vertices, triangles, objects = (value[k] for k in ('vertices_micrometres', 'triangles', 'objects'))
    array(vertices, 4598)
    array(triangles, 8100)
    array(objects, 75)
    for vertex in vertices:
        array(vertex, 3)
        for x in vertex:
            integer(x, -128_000_000, 128_000_000)
    # Bounds are integer micrometres, not retained whole-triangle geometry extrema.
    lower = POLICY['coverage']['lower_static_bounds_micrometres']
    old = POLICY['coverage']['original_static_bounds_micrometres']
    triangle_bounds, used = [], set()
    for triangle in triangles:
        array(triangle, 3)
        for index in triangle:
            integer(index, 0, len(vertices)-1)
        used.update(triangle)
        a, b, c = [vertices[index] for index in triangle]
        base.require(any(cross([b[i]-a[i] for i in range(3)], [c[i]-a[i] for i in range(3)])),
                     'degenerate quantized triangle')
        lo = [min(v[i] for v in (a,b,c)) for i in range(3)]
        hi = [max(v[i] for v in (a,b,c)) for i in range(3)]
        # One micrometre is extraction/quantization validation only.
        # It never expands the policy's exact reservation coverage boxes.
        base.require(all(hi[i] >= lower[0][i]-1 and lo[i] <= lower[1][i]+1 for i in range(3)),
                     'halo triangle outside selected source query')
        base.require(not all(hi[i] >= old[0][i] and lo[i] <= old[1][i] for i in range(3)),
                     'halo duplicates original crop geometry')
        triangle_bounds.append((lo, hi))
    base.require(len(used) == len(vertices), 'unreferenced halo vertex')
    groups = support['groups']
    original = {obj['source_object']: obj for obj in support['objects']
                if groups[obj['group']]['id'] == 'craft_fixed'}
    original_names = next(g['source_objects'] for g in contact['groups'] if g['id'] == 'craft_fixed')
    cursor, names = 0, set()
    for obj in objects:
        optional = ('evaluated_source_triangle_indices_in_region',) if 'evaluated_source_triangle_indices_in_region' in obj else ()
        base.keys(obj, (*OBJECT_KEYS, *optional))
        base.require(obj['owner'] == 'craft' and obj['motion_group'] is None, 'unknown/moving halo owner')
        name = obj['source_object']
        base.require(type(name) is str and 0 < len(name) <= 256 and name not in names, 'unknown/duplicate source name')
        names.add(name)
        integer(obj['source_evaluated_triangles'], 1, 2_000_000)
        integer(obj['crop_retained_triangles'], 0, obj['source_evaluated_triangles'])
        integer(obj['halo_triangle_start'], 0, len(triangles)-1)
        integer(obj['halo_triangle_count'], 1, len(triangles)-cursor)
        base.require(obj['halo_triangle_start'] == cursor, 'range gap/overlap')
        source_ids = obj['evaluated_source_triangle_indices']
        array(source_ids, obj['halo_triangle_count'])
        prior = -1
        for index in source_ids:
            integer(index, 0, obj['source_evaluated_triangles']-1)
            base.require(index > prior, 'source face order/duplicate')
            prior = index
        base.require(obj['crop_retained_triangles']+len(source_ids) <= obj['source_evaluated_triangles'],
                     'source triangle partition overflow')
        admitted = obj['admitted_range']
        if admitted is None:
            base.require(obj['crop_retained_triangles'] == 0 and name not in original,
                         'omitted object has original membership')
        else:
            base.keys(admitted, ('group', 'triangle_start', 'triangle_count', 'exact_coordinate_match'))
            base.require(admitted['group'] == 'craft_fixed' and admitted['exact_coordinate_match'] is True,
                         'wrong original namespace/owner')
            integer(admitted['triangle_start'], 0, 349374)
            integer(admitted['triangle_count'], 1, 349375-admitted['triangle_start'])
            source = original.get(name)
            base.require(source is not None and name in original_names and
                         source['default_collision'] == 'obstacle' and
                         admitted['triangle_start'] == source['triangle_start'] and
                         admitted['triangle_count'] == source['triangle_count'] == obj['crop_retained_triangles'],
                         'old source object/range binding mismatch')
        affine(obj['source_corrected_world_rows'])
        bounds(obj['bounds_corrected_rest_m'])
        bounds(obj['halo_bounds_rest_m'])
        end = cursor + len(source_ids)
        selected = triangle_bounds[cursor:end]
        actual = [[min(t[0][i] for t in selected)*1e-6 for i in range(3)],
                  [max(t[1][i] for t in selected)*1e-6 for i in range(3)]]
        for side in range(2):
            base.require(all(abs(actual[side][i]-obj['halo_bounds_rest_m'][side][i]) <= 1.000001e-6
                             for i in range(3)), 'object quantized bounds mismatch')
        base.require(all(obj['bounds_corrected_rest_m'][0][i]-1e-6 <= actual[0][i] and
                         actual[1][i] <= obj['bounds_corrected_rest_m'][1][i]+1e-6 for i in range(3)),
                     'geometry outside source object bounds')
        if optional:
            ranges = obj[optional[0]]
            base.require(type(ranges) is list and 0 < len(ranges) <= obj['source_evaluated_triangles'],
                         'region range count')
            previous = 0
            for pair in ranges:
                array(pair, 2)
                integer(pair[0], previous, obj['source_evaluated_triangles']-1)
                integer(pair[1], 1, obj['source_evaluated_triangles']-pair[0])
                previous = pair[0]+pair[1]
            base.require(all(any(start <= index < start+count for start,count in ranges) for index in source_ids),
                         'halo face outside recorded region ranges')
        cursor = end
    base.require(cursor == len(triangles), 'incomplete object partition')
    return value
