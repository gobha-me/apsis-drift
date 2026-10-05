#!/usr/bin/env python3
"""Arithmetic/codec controls; no Blender import, source master, or membership query."""

import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/boarding'))
spec = importlib.util.spec_from_file_location('material_source_completion', ROOT / 'tools/boarding/material_source_completion.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class CompletionControls(unittest.TestCase):
    def geometry(self):
        return (np.array([[0., 0., 0.], [1.0000004, 0., 0.], [0., 1., 0.], [0., 0., 1.]], dtype=np.float64),
                np.array([[0, 2, 1], [0, 1, 3], [1, 2, 3], [2, 0, 3]], dtype=np.int32))

    def test_no_blender_import(self):
        self.assertNotIn('bpy', sys.modules)
        self.assertLessEqual(c.MAX_BINARY_BYTES + c.MAX_METADATA_BYTES, 32 * 1024 * 1024)

    def test_binary_packet_ordinals_and_independent_fingerprints(self):
        vertices, faces = self.geometry()
        stream = io.BytesIO()
        w = c.PayloadWriter(stream)
        result = w.mesh(vertices, faces)
        vb = b''.join(struct.pack('<ddd', *v) for v in vertices)
        qv = [tuple(round(float(x) * 1000000) for x in v) for v in vertices]
        qb = b''.join(struct.pack('<qqq', *v) for v in qv)
        fb = b''.join(struct.pack('<iii', *f) for f in faces)
        self.assertEqual(stream.getvalue(), vb + qb + fb)
        self.assertEqual(result['vertices_binary64']['offset'], 0)
        self.assertEqual(result['vertices_quantized_micrometres']['offset'], len(vb))
        self.assertEqual(result['triangle_indices']['offset'], len(vb) + len(qb))
        raw_triangles = b''.join(struct.pack('<ddd', *vertices[i]) for f in faces for i in f)
        q_triangles = b''.join(struct.pack('<qqq', *qv[i]) for f in faces for i in f)
        self.assertEqual(result['ordered_triangle_binary64_sha256'], hashlib.sha256(raw_triangles).hexdigest())
        self.assertEqual(result['ordered_triangle_micrometre_sha256'], hashlib.sha256(q_triangles).hexdigest())
        self.assertEqual(w.bytes, len(vb + qb + fb))
        self.assertEqual((w.vertices, w.triangles, w.records), (4, 4, 1))
        # Fingerprints bind ordinal order independently of the same containing bounds.
        changed = c.inventory.summarize_mesh(vertices, faces[::-1])
        self.assertEqual(changed['full_bounds_corrected_world_metres'], result['full_bounds_corrected_world_metres'])
        with self.assertRaises(ValueError):
            c.verify_summary(changed, result)

    def test_invalid_geometry_writes_nothing(self):
        vertices, faces = self.geometry()
        nan = vertices.copy()
        nan[0, 1] = float('nan')
        inf = vertices.copy()
        inf[1, 0] = float('inf')
        outside = vertices.copy()
        outside[1, 0] = 1001
        bad_indices = faces.copy()
        bad_indices[0, 0] = len(vertices)
        negative = faces.copy()
        negative[0, 1] = -1
        controls = [(vertices[:, :2], faces), (vertices, faces[:, :2]),
                    (vertices[:0], faces), (vertices, faces[:0]), (nan, faces), (inf, faces),
                    (outside, faces), (vertices, bad_indices), (vertices, negative),
                    (vertices.astype(np.float32), faces), (vertices, faces.astype(np.float64))]
        for v, f in controls:
            with self.subTest(shape=(v.shape, f.shape)):
                stream = io.BytesIO()
                writer = c.PayloadWriter(stream)
                with self.assertRaises(ValueError):
                    writer.mesh(v, f)
                self.assertEqual(stream.getvalue(), b'')
                self.assertEqual((writer.bytes, writer.records), (0, 0))

    def test_lowered_storage_and_work_boundaries(self):
        vertices, faces = self.geometry()
        exact = len(vertices) * 48 + len(faces) * 12
        for limit, succeeds in [(exact, True), (exact - 1, False), (0, False)]:
            stream = io.BytesIO()
            with patch.object(c, 'MAX_BINARY_BYTES', limit):
                if succeeds:
                    c.PayloadWriter(stream).mesh(vertices, faces)
                    self.assertEqual(len(stream.getvalue()), exact)
                else:
                    with self.assertRaises(ValueError):
                        c.PayloadWriter(stream).mesh(vertices, faces)
                    self.assertEqual(stream.getvalue(), b'')
        for key, value in [('MAX_RECORDS', 0), ('MAX_CAPTURE_VERTICES', 3),
                           ('MAX_CAPTURE_TRIANGLES', 3), ('MAX_NUMERIC_SCRATCH_BYTES', 0)]:
            stream = io.BytesIO()
            with patch.object(c, key, value), self.assertRaises(ValueError):
                c.PayloadWriter(stream).mesh(vertices, faces)
            self.assertEqual(stream.getvalue(), b'')
        with self.assertRaises(ValueError):
            c.PayloadWriter(io.BytesIO()).array(vertices, '<f4')

    def test_scratch_refusal_precedes_fingerprint_arithmetic(self):
        vertices, faces = self.geometry()
        stream = io.BytesIO()
        with patch.object(c, 'MAX_NUMERIC_SCRATCH_BYTES', 0), patch.object(c.inventory, 'summarize_mesh') as arithmetic:
            with self.assertRaises(ValueError):
                c.PayloadWriter(stream).mesh(vertices, faces)
            arithmetic.assert_not_called()
        self.assertEqual(stream.getvalue(), b'')

    def test_short_write_cannot_publish_a_complete_record(self):
        class ShortWrite:
            def write(self, block):
                return len(block) - 1
        vertices, faces = self.geometry()
        writer = c.PayloadWriter(ShortWrite())
        with self.assertRaises(ValueError):
            writer.mesh(vertices, faces)
        self.assertEqual((writer.records, writer.vertices, writer.triangles), (0, 0, 0))

    def test_aggregate_capacity_remains_a_real_bound(self):
        vertices, faces = self.geometry()
        stream = io.BytesIO()
        writer = c.PayloadWriter(stream)
        with patch.object(c, 'MAX_CAPTURE_VERTICES', 4), patch.object(c, 'MAX_CAPTURE_TRIANGLES', 4):
            writer.mesh(vertices, faces)
            first = stream.getvalue()
            with self.assertRaises(ValueError):
                writer.mesh(vertices, faces)
        self.assertEqual(stream.getvalue(), first)
        self.assertEqual(writer.records, 1)

    def test_pinned_json_identity_and_byte_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'arithmetic.json'
            raw = b'{"x":1}\n'
            path.write_bytes(raw)
            pin = hashlib.sha256(raw).hexdigest()
            self.assertEqual(c.checked_json(path, len(raw), pin, len(raw)), ({'x': 1}, pin))
            for kwargs in [(len(raw)-1, pin, None), (len(raw), '0'*64, None), (len(raw), pin, len(raw)+1)]:
                with self.assertRaises(ValueError):
                    c.checked_json(path, *kwargs)

    def test_inventory_roster_and_permission_bindings(self):
        document = {'schema': 'apsis-drift-wayfarer-material-containing-inventory', 'version': 1,
                    'source_sha256': c.inventory.SOURCE_SHA,
                    'operating_metadata_sha256': c.inventory.METADATA_SHA,
                    'original_contact_sha256': c.inventory.CONTACT_SHA,
                    'boarding_support_sha256': c.inventory.SUPPORT_SHA,
                    'source_helper_sha256': c.inventory.HELPER_HASHES,
                    'master_unchanged': True, 'material_permission': False,
                    'selected_object_count': 2, 'fixed_object_count': 1,
                    'objects': [{'source_object': 'arithmetic fixed', 'motion_group': None},
                                {'source_object': 'arithmetic moving', 'motion_group': 'test'}]}
        self.assertEqual(len(c.validate_inventory(document)), 2)
        for key, value in [('schema', 'wrong'), ('source_sha256', '0'*64), ('material_permission', True),
                           ('master_unchanged', False), ('selected_object_count', 3), ('fixed_object_count', 2)]:
            changed = copy.deepcopy(document)
            changed[key] = value
            with self.assertRaises(ValueError):
                c.validate_inventory(changed)
        changed = copy.deepcopy(document)
        changed['objects'][1]['source_object'] = changed['objects'][0]['source_object']
        with self.assertRaises(ValueError):
            c.validate_inventory(changed)


if __name__ == '__main__':
    unittest.main()
