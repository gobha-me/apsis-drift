#!/usr/bin/env python3
"""Produced GLB binary bounds/nonfinite regression controls. BSD-3-Clause."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from wayfarer_operating_glb_audit import audit


def fixture():
    payload = struct.pack('<9f3H', 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 2)
    document = {'asset': {'version': '2.0'}, 'buffers': [{'byteLength': len(payload)}],
                'bufferViews': [{'buffer': 0, 'byteOffset': 0, 'byteLength': 36},
                                {'buffer': 0, 'byteOffset': 36, 'byteLength': 6}],
                'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 3, 'type': 'VEC3'},
                              {'bufferView': 1, 'componentType': 5123, 'count': 3, 'type': 'SCALAR'}],
                'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1}]}]}
    return document, payload


def encode(document, payload):
    text = json.dumps(document, allow_nan=False).encode()
    text += b' ' * (-len(text) % 4)
    payload += b'\0' * (-len(payload) % 4)
    chunks = struct.pack('<II', len(text), 0x4E4F534A) + text
    chunks += struct.pack('<II', len(payload), 0x004E4942) + payload
    return struct.pack('<III', 0x46546C67, 2, 12 + len(chunks)) + chunks


class BinaryAuditTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / 'fixture.glb'

    def tearDown(self):
        self.directory.cleanup()

    def check(self, document, payload):
        self.path.write_bytes(encode(document, payload))
        return audit(self.path)

    def test_actual_valid_triangle(self):
        result = self.check(*fixture())
        self.assertEqual(result['triangles'], 1)
        self.assertTrue(result['all_triangle_indices_in_bounds'])

    def test_nonfinite_position_is_rejected(self):
        for value in (float('nan'), float('inf'), -float('inf')):
            with self.subTest(value=value):
                document, payload = fixture()
                payload = struct.pack('<f', value) + payload[4:]
                with self.assertRaisesRegex(ValueError, 'nonfinite'):
                    self.check(document, payload)

    def test_real_index_boundary(self):
        document, payload = fixture()
        for value in (3, 65535):
            with self.subTest(index=value):
                changed = payload[:-2] + struct.pack('<H', value)
                with self.assertRaisesRegex(ValueError, 'vertex boundary'):
                    self.check(document, changed)

    def test_dimensions_and_malformed_tables(self):
        mutations = [
            lambda d: d['buffers'][0].update(uri='external.bin'),
            lambda d: d['buffers'][0].update(byteLength=45),
            lambda d: d['bufferViews'][0].update(byteOffset=8),
            lambda d: d['bufferViews'][0].update(buffer=False),
            lambda d: d['bufferViews'][0].update(byteStride=8),
            lambda d: d['accessors'][0].update(count=0),
            lambda d: d['accessors'][0].update(count=4),
            lambda d: d['accessors'][0].update(count=True),
            lambda d: d['accessors'][0].update(byteOffset=2),
            lambda d: d['accessors'][0].update(sparse={}),
            lambda d: d['accessors'][0].update(type='VEC9'),
            lambda d: d['meshes'][0]['primitives'][0].update(indices=2),
            lambda d: d['meshes'][0]['primitives'][0].update(mode=1),
            lambda d: d['meshes'][0].update(primitives=[]),
            lambda d: d['bufferViews'].__setitem__(0, None),
            lambda d: d['accessors'].__setitem__(0, None),
            lambda d: d['meshes'].__setitem__(0, None),
        ]
        original, payload = fixture()
        for index, mutation in enumerate(mutations):
            with self.subTest(case=index):
                document = copy.deepcopy(original)
                mutation(document)
                with self.assertRaises(ValueError):
                    self.check(document, payload)

    def test_truncation_and_declared_length(self):
        encoded = encode(*fixture())
        for value in (encoded[:-1], encoded[:20], encoded + b'\0\0\0\0'):
            with self.subTest(length=len(value)):
                self.path.write_bytes(value)
                with self.assertRaises(ValueError):
                    audit(self.path)


if __name__ == '__main__':
    unittest.main()
