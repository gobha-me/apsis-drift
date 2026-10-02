"""Closed-rest restraint producer checks; BSD-3-Clause. No Blender dependency."""
from collections import Counter
import ctypes
import errno
import hashlib
import json
import lzma
import math
import os
from pathlib import Path
import struct
import sys

SOURCE_SHA256 = '87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677'
MODEL_SHA256 = 'a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c'
PACKAGE_SHA256 = '9a3d2632b3231dfd0a418049a0e80fa6534feaebd3ffa944c2c9635755a5f957'
METADATA_SHA256 = 'db7b0a2de2516adf9925abe74133a6c523bd334062884a1bfc403448c2b1e72e'
NODES = {
    'Shoulder restraint': 'WFRShoulderPort',
    'Shoulder restraint.001': 'WFRShoulderStarboard',
    'Lap restraint': 'WFRLapPort',
    'Lap restraint.001': 'WFRLapStarboard',
    'Anti-submarining strap': 'WFRCrotch',
    'Five-point buckle': 'WFRBuckle',
    'Buckle release': 'WFRRelease',
}
RESIDUAL = 'WFRSeatLiftResidual'
MAX_BYTES = 80 * 1024 * 1024


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False)


def digest(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def verified_member(package, entry):
    """Read pinned bytes once; reject symlink ancestors and changed snapshots."""
    relative = Path(entry['path'])
    require(not relative.is_absolute() and '..' not in relative.parts,
            'Unsafe frozen reference member')
    path = package / relative
    require(not any(p.is_symlink() for p in (path, *path.parents)),
            'Symlink frozen reference member')
    size = entry['bytes']
    require(type(size) is int and 0 < size <= MAX_BYTES and path.is_file() and
            path.stat().st_size == size, 'Frozen reference member size changed')
    with path.open('rb') as stream:
        data = stream.read(size + 1)
    require(len(data) == size and hashlib.sha256(data).hexdigest() == entry['sha256'],
            'Frozen reference member hash/size changed')
    return data


def pinned_json(path, expected_hash, maximum):
    require(path.is_file() and not path.is_symlink() and 0 < path.stat().st_size <= maximum,
            'Frozen JSON dimensions invalid')
    with path.open('rb') as stream:
        data = stream.read(maximum + 1)
    require(len(data) <= maximum and hashlib.sha256(data).hexdigest() == expected_hash,
            'Frozen independent package/metadata byte identity changed')
    return json.loads(data)


def preflight(source, output, package):
    source, package, output = Path(source), Path(package), Path(output)
    require(not os.path.lexists(output), 'Output already exists (including a symlink)')
    require(output.parent.is_dir(), 'Output parent must already exist')
    require(not any(p.is_symlink() for p in (output, *output.parents)),
            'Symlink output paths are unsafe')
    source, output, package = source.resolve(strict=True), output.resolve(strict=False), package.resolve(strict=True)
    require(output.name not in ('', '.', '..') and source.is_file(), 'Unsafe output or source')
    require(source.parent not in output.parents and package not in output.parents,
            'Output must not be inside source or frozen package')
    require(sha(source) == SOURCE_SHA256, 'Wrong selected Craft09 source hash')
    require(sys.platform.startswith('linux') and hasattr(ctypes.CDLL(None), 'renameat2'),
            'Atomic no-replace directory installation is unavailable')
    manifest = pinned_json(package / 'package.json', PACKAGE_SHA256, 2 * 1024 * 1024)
    metadata_entry = next(f for f in manifest['files'] if f['path'] == 'metadata/wayfarer-operating-02.json')
    metadata_bytes = verified_member(package, metadata_entry)
    require(hashlib.sha256(metadata_bytes).hexdigest() == METADATA_SHA256,
            'Frozen independent metadata byte identity changed')
    spec = json.loads(metadata_bytes)
    require(manifest.get('package_id') == 'wayfarer-operating-02' and
            manifest['model']['sha256'] == MODEL_SHA256 and
            manifest['model']['source_sha256'] == SOURCE_SHA256,
            'Wrong frozen operating reference')
    files = {f['path']: f for f in manifest['files']}
    for name in [*manifest['model']['chunks'], *[n for n in files if n.startswith('licenses/')]]:
        verified_member(package, files[name])
    group = next(g for g in spec['groups'] if g['id'] == 'seat_lift')
    members = group['source_objects']
    require(len(members) == 64 and len(set(members)) == 64 and set(NODES) <= set(members),
            'Frozen seat roster must contain exact64 unique members')
    return source, output, package, manifest, spec


def old_model(package, manifest):
    files = {f['path']: f for f in manifest['files']}
    names = manifest['model']['chunks']
    require(type(names) is list and 0 < len(names) <= 64 and all(
        type(files[name]['bytes']) is int and 0 < files[name]['bytes'] <= MAX_BYTES for name in names) and
        sum(files[name]['bytes'] for name in names) <= MAX_BYTES, 'Frozen compressed dimensions invalid')
    chunks = b''.join(verified_member(package, files[name]) for name in manifest['model']['chunks'])
    size = manifest['model']['bytes']
    require(type(size) is int and 0 < size <= MAX_BYTES, 'Frozen decoded dimensions invalid')
    decoder = lzma.LZMADecompressor(memlimit=MAX_BYTES * 4)
    data = decoder.decompress(chunks, max_length=size + 1)
    require(decoder.eof and not decoder.unused_data and len(data) == size and
            hashlib.sha256(data).hexdigest() == MODEL_SHA256,
            'Decoded frozen model hash/size changed or exceeds bounded output')
    return data


def read_glb(data):
    require(28 <= len(data) <= MAX_BYTES, 'GLB byte bounds')
    magic, version, total = struct.unpack_from('<III', data)
    require((magic, version, total) == (0x46546c67, 2, len(data)), 'GLB header')
    count, kind = struct.unpack_from('<II', data, 12)
    require(kind == 0x4e4f534a and 0 < count <= 8 * 1024 * 1024 and count % 4 == 0,
            'GLB JSON bounds')
    require(20 + count + 8 <= len(data), 'GLB JSON buffer boundary')
    doc = json.loads(data[20:20+count])
    binary_count, binary_kind = struct.unpack_from('<II', data, 20+count)
    require(binary_kind == 0x004e4942 and binary_count % 4 == 0 and binary_count == len(data)-28-count,
            'GLB binary boundary')
    require(len(doc['buffers']) == 1 and 'uri' not in doc['buffers'][0], 'Embedded buffer only')
    require(type(doc['buffers'][0].get('byteLength')) is int and
            0 <= doc['buffers'][0]['byteLength'] <= binary_count and
            binary_count - doc['buffers'][0]['byteLength'] <= 3, 'Embedded buffer length')
    return doc, data[28+count:]


def accessor(doc, payload, index):
    require(type(index) is int and 0 <= index < len(doc['accessors']), 'Accessor index')
    a = doc['accessors'][index]
    require('sparse' not in a and not a.get('normalized', False), 'Unsupported accessor encoding')
    formats = {5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
    widths = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
    require(type(a.get('componentType')) is int and a['componentType'] in formats and a.get('type') in widths,
            'Accessor component/type')
    require(type(a['count']) is int and 0 < a['count'] <= 4_000_000, 'Accessor count')
    view_index = a.get('bufferView')
    require(type(view_index) is int and 0 <= view_index < len(doc['bufferViews']), 'Buffer view index')
    v = doc['bufferViews'][view_index]
    require(type(v.get('buffer')) is int and v['buffer'] == 0,
            'Accessor embedded buffer identity')
    base, length = v.get('byteOffset', 0), v.get('byteLength')
    declared = doc['buffers'][0]['byteLength']
    require(type(base) is int and type(length) is int and type(declared) is int and
            0 <= base and 0 < length and 0 < declared <= len(payload) and
            base + length <= declared, 'Buffer view containment')
    token, size = formats[a['componentType']]; width = widths[a['type']]
    offset = a.get('byteOffset', 0); stride = v.get('byteStride', width*size)
    require(type(offset) is int and type(stride) is int and 0 <= offset and
            width*size <= stride <= 252 and stride % size == 0 and offset % size == 0 and
            offset + (a['count']-1)*stride + width*size <= v['byteLength'], 'Accessor boundaries')
    start = base + offset
    require(0 <= start and start + (a['count']-1)*stride + width*size <= len(payload),
            'Accessor binary bounds')
    values = [struct.unpack_from('<'+token*width, payload, start+i*stride)
              for i in range(a['count'])]
    require(all(math.isfinite(x) for v in values for x in v), 'Nonfinite triangle attributes')
    return values


def rotate_key(vertices):
    vertices = tuple(vertices)
    return min(vertices[i:]+vertices[:i] for i in range(3))


def triangles(doc, payload, node_names):
    rows = []
    nodes = doc['nodes']
    selected = [(i, node) for i, node in enumerate(nodes) if node.get('name') in node_names]
    require(len(set(node_names)) == len(node_names) and
            Counter(node.get('name') for _, node in selected) == Counter(node_names),
            'Duplicate or missing selected render node identity')
    scene_index = doc.get('scene', 0)
    require(type(scene_index) is int and 0 <= scene_index < len(doc.get('scenes', [])),
            'Default render scene index')
    roots = doc['scenes'][scene_index].get('nodes', [])
    require(type(roots) is list and len(roots) == len(set(roots)) and all(
        type(i) is int and 0 <= i < len(nodes) for i in roots), 'Scene root indices')
    children = []
    for node in nodes:
        ids = node.get('children', [])
        require(type(ids) is list and all(type(i) is int and 0 <= i < len(nodes) for i in ids),
                'Child node indices')
        children.extend(ids)
    for index, node in selected:
        name = node['name']
        require(index in roots and index not in children, 'Selected node must be an actual unparented scene root')
        require(not node.get('children') and all(k not in node for k in ['matrix', 'translation', 'rotation', 'scale']),
                'Flat identity-rest render node required')
        mesh_index = node.get('mesh')
        require(type(mesh_index) is int and 0 <= mesh_index < len(doc['meshes']), 'Mesh index')
        mesh = doc['meshes'][mesh_index]
        for primitive_index, primitive in enumerate(mesh['primitives']):
            require(type(primitive.get('mode', 4)) is int and primitive.get('mode', 4) == 4 and set(primitive['attributes']) ==
                    {'POSITION', 'NORMAL', 'TEXCOORD_0'}, 'Exact finite triangle attribute roster')
            for attribute_name, width in [('POSITION', 'VEC3'), ('NORMAL', 'VEC3'), ('TEXCOORD_0', 'VEC2')]:
                index = primitive['attributes'][attribute_name]
                require(type(index) is int and 0 <= index < len(doc['accessors']), 'Attribute accessor index')
                require(doc['accessors'][index]['componentType'] == 5126 and
                        doc['accessors'][index]['type'] == width, 'Attribute representation')
            attributes = {k: accessor(doc, payload, a) for k, a in primitive['attributes'].items()}
            count = len(attributes['POSITION'])
            require(all(len(a) == count for a in attributes.values()), 'Attribute lengths')
            indices = accessor(doc, payload, primitive['indices'])
            index_spec = doc['accessors'][primitive['indices']]
            require(index_spec['componentType'] in (5121, 5123, 5125) and
                    index_spec['type'] == 'SCALAR', 'Index representation')
            require(len(indices) % 3 == 0 and all(type(i[0]) is int and 0 <= i[0] < count for i in indices),
                    'Triangle index boundaries')
            material_index = primitive.get('material')
            require(type(material_index) is int and 0 <= material_index < len(doc['materials']), 'Material index')
            material = doc['materials'][material_index]
            for i in range(0, len(indices), 3):
                ids = [indices[i+j][0] for j in range(3)]
                positions = tuple(attributes['POSITION'][j] for j in ids)
                p, q, r = positions
                cross = ((q[1]-p[1])*(r[2]-p[2])-(q[2]-p[2])*(r[1]-p[1]),
                         (q[2]-p[2])*(r[0]-p[0])-(q[0]-p[0])*(r[2]-p[2]),
                         (q[0]-p[0])*(r[1]-p[1])-(q[1]-p[1])*(r[0]-p[0]))
                degenerate = not any(c != 0 for c in cross)
                # The pinned render baseline includes zero-area triangles.
                # Preserve/count them exactly; they grant no contact support.
                semantic = (digest(material), rotate_key(tuple(
                    tuple(attributes[k][j] for k in sorted(attributes)) for j in ids)))
                source_key = (material['name'], rotate_key(positions))
                rows.append({'node': name, 'primitive': primitive_index, 'triangle': i//3,
                             'semantic': semantic, 'source_key': source_key, 'degenerate': degenerate})
    require(set(node_names) == {row['node'] for row in rows}, 'Missing render node')
    return rows


def semantic_receipt(old_rows, new_rows):
    old, new = Counter(r['semantic'] for r in old_rows), Counter(r['semantic'] for r in new_rows)
    require(old == new, 'Closed-rest triangle attributes/materials differ from frozen seat GLB: '
            f'{sum((old-new).values())} missing, {sum((new-old).values())} extra')
    normalized = sorted((canonical(k), count) for k, count in old.items())
    return {'pass': True, 'triangles': len(old_rows), 'triangle_attributes_material_union_sha256': digest(normalized),
            'winding_normals_uvs_materials_exact': True,
            'preserved_degenerate_render_triangles': sum(r['degenerate'] for r in old_rows)}


def install_no_replace(staging, destination):
    # Linux atomic renameat2 guards even a concurrently created empty directory.
    library = ctypes.CDLL(None, use_errno=True)
    call = library.renameat2
    call.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    call.restype = ctypes.c_int
    if call(-100, os.fsencode(staging), -100, os.fsencode(destination), 1) != 0:
        code = ctypes.get_errno()
        if code == errno.EEXIST:
            raise FileExistsError('Concurrent destination exists; refused no-replace install')
        raise OSError(code, os.strerror(code))
