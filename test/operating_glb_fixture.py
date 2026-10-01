"""Tiny real buffer/graph controls; synthetic bytes grant no admission/license."""
import json
import struct

from wayfarer_operating_spec import MOTION_RIGS, RUNTIME_NODES, SOURCE_SHA256
from validate_operating_asset_glb import STATION_SOURCE_OBJECTS


def encode(document, payload, *, allow_nan=False):
    raw = json.dumps(document, allow_nan=allow_nan).encode()
    raw += b' ' * (-len(raw) % 4)
    payload += b'\0' * (-len(payload) % 4)
    chunks = struct.pack('<II', len(raw), 0x4E4F534A) + raw
    chunks += struct.pack('<II', len(payload), 0x004E4942) + payload
    return struct.pack('<III', 0x46546C67, 2, 12 + len(chunks)) + chunks


def decode(value):
    count = struct.unpack_from('<I', value, 12)[0]
    document = json.loads(value[20:20 + count])
    offset = 20 + count
    length = struct.unpack_from('<I', value, offset)[0]
    return document, value[offset + 8:offset + 8 + length]


def triangle_document(nodes):
    payload = struct.pack('<9f3H', 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 2)
    document = {'asset': {'version': '2.0'}, 'scene': 0,
        'scenes': [{'nodes': list(range(len(nodes)))}], 'nodes': nodes,
        'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1}]} for _ in nodes],
        'buffers': [{'byteLength': len(payload)}],
        'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': 36},
                        {'buffer': 0, 'byteOffset': 36, 'byteLength': 6}],
        'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 3, 'type': 'VEC3'},
                      {'bufferView': 1, 'componentType': 5123, 'count': 3, 'type': 'SCALAR'}]}
    return document, payload


def craft_groups():
    rigs = {group: rig for rig, group in MOTION_RIGS.items()}
    return [{'id': group, 'runtime_node': node, 'source_rig': rigs[group]}
            for group, node in RUNTIME_NODES.items()]


def craft_fixture():
    nodes = [{'name': 'HopperStructure', 'mesh': 0}]
    for group in craft_groups():
        nodes.append({'name': group['runtime_node'], 'mesh': len(nodes),
                      'extras': {'operating_group': group['id'], 'source_rig': group['source_rig'],
                                 'source_sha256': SOURCE_SHA256}})
    return encode(*triangle_document(nodes))


def station_corrections():
    return [{'source_object': source, 'replacement_node': node}
            for node, source in STATION_SOURCE_OBJECTS.items()]


def station_fixture():
    nodes = [{'name': correction['replacement_node'], 'mesh': index}
             for index, correction in enumerate(station_corrections())]
    return encode(*triangle_document(nodes))
