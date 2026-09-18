#!/usr/bin/env python3
"""Validate the delivered GLB buffers, floating point geometry and LOD budgets."""
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VISUAL = ROOT / 'assets' / 'visual'
COMPONENTS = {5120: ('b',1), 5121: ('B',1), 5122: ('h',2),
              5123: ('H',2), 5125: ('I',4), 5126: ('f',4)}
WIDTH = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}


def check(path):
    data = path.read_bytes()
    magic, version, size = struct.unpack_from('<4sII', data)
    assert magic == b'glTF' and version == 2 and size == len(data), path
    cursor = 12
    chunks = {}
    while cursor < len(data):
        length, kind = struct.unpack_from('<I4s', data, cursor)
        assert length % 4 == 0 and cursor+8+length <= len(data), path
        chunks[kind] = data[cursor+8:cursor+8+length]
        cursor += 8+length
    assert cursor == len(data) and b'JSON' in chunks and b'BIN\0' in chunks, path
    doc = json.loads(chunks[b'JSON']); binary = chunks[b'BIN\0']
    assert len(doc['buffers']) == 1 and doc['buffers'][0]['byteLength'] <= len(binary)
    for view in doc['bufferViews']:
        assert view.get('buffer',0) == 0
        assert 0 <= view.get('byteOffset',0) <= len(binary)
        assert view.get('byteOffset',0)+view['byteLength'] <= len(binary)
    values = []
    for a in doc['accessors']:
        assert 'sparse' not in a
        view = doc['bufferViews'][a['bufferView']]
        fmt, size = COMPONENTS[a['componentType']]
        width = WIDTH[a['type']]; stride = view.get('byteStride',size*width)
        offset = a.get('byteOffset',0)
        assert stride >= size*width and offset >= 0
        assert offset + max(0,a['count']-1)*stride + size*width <= view['byteLength']
        base = view.get('byteOffset',0)+offset
        decoded = [struct.unpack_from('<'+fmt*width,binary,base+i*stride) for i in range(a['count'])]
        if fmt == 'f':
            assert all(math.isfinite(x) for row in decoded for x in row), path
        values.append(decoded)
    mesh_counts = []
    for m in doc['meshes']:
        triangles = 0
        for p in m['primitives']:
            assert p.get('mode',4) == 4
            positions = values[p['attributes']['POSITION']]
            indices = values[p['indices']]
            assert len(indices) % 3 == 0
            assert all(0 <= row[0] < len(positions) for row in indices), path
            triangles += len(indices)//3
        mesh_counts.append(triangles)
    triangles = 0
    for node in doc['nodes']:
        for field in ('matrix','translation','rotation','scale'):
            assert all(math.isfinite(x) for x in node.get(field,[])), path
        if 'mesh' in node:
            assert 0 <= node['mesh'] < len(mesh_counts)
            triangles += mesh_counts[node['mesh']]
    assert triangles > 0
    return triangles


def main():
    for asset in ('cockpit','ship','station'):
        metrics = json.loads((VISUAL/f'hero-{asset}-lods.json').read_text())
        counts = []
        for level in ('hero','near','mid','far'):
            p = VISUAL/f'hero-{asset}-{level}.glb'
            count = check(p)
            recorded = metrics[level]
            assert count == recorded['triangles'], (p,count,recorded)
            assert p.stat().st_size == recorded['bytes']
            if recorded['budget'] is not None:assert count <= recorded['budget'],p
            counts.append(count)
        assert counts == sorted(counts,reverse=True), counts
        print(f'{asset}: hero / near / mid / far = {counts}; buffers and finite geometry OK')


if __name__ == '__main__':
    main()
