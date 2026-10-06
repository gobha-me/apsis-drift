#!/usr/bin/env python3
"""Pure extraction controls; no Blender scene or source master is loaded."""

import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

import numpy as np

SOURCE = Path(__file__).resolve().parents[2] / "tools/boarding/material_inventory.py"
SPEC = importlib.util.spec_from_file_location("material_inventory", SOURCE)
inventory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory)


class FullSourceBounds(unittest.TestCase):
    def setUp(self):
        self.vertices = np.array([[0., 0., 0.], [1., 0., 0.], [0., 1., 0.], [7., -2., 4.]])
        self.faces = np.array([[0, 1, 2]], dtype=np.int32)

    def test_invalid_arrays_before_geometry(self):
        for vertices, faces in [
            (self.vertices.reshape(-1), self.faces),
            (self.vertices, self.faces.reshape(-1)),
            (self.vertices.astype(np.float32), self.faces),
            (self.vertices, self.faces.astype(np.float64)),
            (np.empty((0, 3)), self.faces),
            (self.vertices, np.empty((0, 3), dtype=np.int32)),
            (self.vertices, np.array([[0, 1, -1]], dtype=np.int32)),
            (self.vertices, np.array([[0, 1, 4]], dtype=np.int32)),
            (self.vertices, np.array([[0, 1, 2**63]], dtype=np.uint64)),
        ]:
            with self.subTest(vertices=vertices.shape, faces=faces.shape):
                with self.assertRaises(ValueError):
                    inventory.summarize_mesh(vertices, faces)

    def test_nonfinite_and_workspace(self):
        for scalar in [float("nan"), float("inf"), -float("inf"), np.nextafter(1000., float("inf"))]:
            vertices = self.vertices.copy()
            vertices[0, 0] = scalar
            with self.assertRaises(ValueError):
                inventory.summarize_mesh(vertices, self.faces)
        vertices = self.vertices.copy()
        vertices[0, 0] = 1000.
        self.assertEqual(inventory.summarize_mesh(vertices, self.faces)["evaluated_vertex_count"], 4)

    def test_full_uncropped_containing_bounds(self):
        result = inventory.summarize_mesh(self.vertices, self.faces)
        # A cropped face or used-vertex summary misses the fourth vertex.
        self.assertEqual(result["full_bounds_corrected_world_metres"], [[0., -2., 0.], [7., 1., 4.]])
        self.assertEqual(result["full_bounds_quantized_micrometres"], [[0, -2000000, 0], [7000000, 1000000, 4000000]])
        self.assertEqual(result["evaluated_triangle_ordinal_range"], [0, 1])

    def test_independent_ordered_triangle_encoding(self):
        faces = np.array([[2, 0, 1], [1, 2, 0]], dtype=np.int32)
        result = inventory.summarize_mesh(self.vertices, faces)
        raw, integer = bytearray(), bytearray()
        for face in [[2, 0, 1], [1, 2, 0]]:
            for index in face:
                for value in self.vertices[index]:
                    raw.extend(struct.pack("<d", float(value)))
                    integer.extend(struct.pack("<q", round(float(value) * 1000000)))
        self.assertEqual(result["ordered_triangle_binary64_sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(result["ordered_triangle_micrometre_sha256"], hashlib.sha256(integer).hexdigest())
        reversed_result = inventory.summarize_mesh(self.vertices, faces[::-1])
        self.assertNotEqual(result["ordered_triangle_binary64_sha256"], reversed_result["ordered_triangle_binary64_sha256"])

    def test_quantization_is_separate_from_raw_geometry(self):
        vertices = np.array([[.5e-6, -1.5e-6, 2.5e-6], [3.5e-6, -4.5e-6, 5.5e-6], [6.5e-6, -7.5e-6, 8.5e-6]])
        result = inventory.summarize_mesh(vertices, self.faces)
        expected = [[round(float(v) * 1000000) for v in row] for row in vertices]
        for axis in range(3):
            self.assertEqual(result["full_bounds_quantized_micrometres"][0][axis], min(row[axis] for row in expected))
            self.assertEqual(result["full_bounds_quantized_micrometres"][1][axis], max(row[axis] for row in expected))
        self.assertEqual(result["full_bounds_corrected_world_metres"][0], vertices.min(0).tolist())

    def test_exact_and_one_less_vertex_triangle_capacity(self):
        with patch.object(inventory, "MAX_OBJECT_VERTICES", 4), patch.object(inventory, "MAX_OBJECT_TRIANGLES", 1):
            inventory.summarize_mesh(self.vertices, self.faces)
        with patch.object(inventory, "MAX_OBJECT_VERTICES", 3):
            with self.assertRaises(ValueError):
                inventory.summarize_mesh(self.vertices, self.faces)
        with patch.object(inventory, "MAX_OBJECT_TRIANGLES", 0):
            with self.assertRaises(ValueError):
                inventory.summarize_mesh(self.vertices, self.faces)

    def test_name_byte_capacity(self):
        self.assertEqual(inventory.checked_name("é" * 256), "é" * 256)
        for value in ["", None, "é" * 257]:
            with self.assertRaises(ValueError):
                inventory.checked_name(value)

    def test_absent_crop_still_has_full_geometry(self):
        vertices = self.vertices + 100.
        expected = np.empty((0, 3, 3), dtype=np.int64)
        self.assertEqual(inventory.verify_retained_triangles(vertices, self.faces, [[-1]*3, [1]*3], expected), 0)
        self.assertEqual(inventory.summarize_mesh(vertices, self.faces)["evaluated_triangle_count"], 1)

    def test_retained_order_and_geometry_substitution_refuse(self):
        expected = np.array([[[0, 0, 0], [1000000, 0, 0], [0, 1000000, 0]]], dtype=np.int64)
        self.assertEqual(inventory.verify_retained_triangles(self.vertices, self.faces, None, expected), 1)
        for changed in [expected[:, ::-1], expected + 1, np.concatenate([expected, expected])]:
            with self.assertRaises(ValueError):
                inventory.verify_retained_triangles(self.vertices, self.faces, None, changed)

    def test_prequantization_crop_and_collapsed_face_policy(self):
        vertices = np.array([[1.0000001, 0., 0.], [1.0000002, .1, 0.], [1.0000003, 0., .1]])
        # These round onto x=1, but the pinned RAW crop must still exclude them.
        empty = np.empty((0, 3, 3), dtype=np.int64)
        self.assertEqual(inventory.verify_retained_triangles(vertices, self.faces, [[-1]*3, [1]*3], empty), 0)
        vertices = np.array([[0., 0., 0.], [.1e-6, 0., 0.], [0., 1., 0.]])
        self.assertEqual(inventory.verify_retained_triangles(vertices, self.faces, None, empty), 0)


if __name__ == "__main__":
    unittest.main()
