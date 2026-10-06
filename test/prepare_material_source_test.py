#!/usr/bin/env python3
"""Pure preparation controls; no source/master loading or material assessment."""
import hashlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "prepare_material_source", ROOT / "tools/boarding/prepare_material_source.py")
PREP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREP)


class PreparationTests(unittest.TestCase):
    def test_original_and_runtime_group_namespaces(self):
        self.assertEqual(PREP.original_group_index('craft_fixed'), 0)
        self.assertEqual(PREP.original_group_index('craft_roof_port'), 7)
        self.assertEqual(PREP.GROUPS.index('roof_port'), 0)
        self.assertEqual(PREP.original_group_index('craft_seat_lift'), 11)
        self.assertEqual(PREP.GROUPS.index('seat_lift'), 10)
        self.assertEqual(PREP.original_group_index('craft_seat_swivel'), 13)
        self.assertEqual(PREP.GROUPS.index('seat_swivel'), 9)
        self.assertEqual(PREP.original_group_index(None), -1)
        with self.assertRaises(ValueError):
            PREP.original_group_index('invented_group')

    def test_array_codec_and_malformed_packets(self):
        data = struct.pack('<3d', 1., 2., 3.)
        descriptor = dict(offset=0, bytes=24, rows=1, columns=3, dtype='<f8',
                          sha256=hashlib.sha256(data).hexdigest())
        self.assertEqual(list(PREP.array(data, descriptor, '<f8')), [(1., 2., 3.)])
        for update in ({'offset': -1}, {'bytes': 25}, {'rows': 2}, {'columns': 4},
                       {'dtype': '<i8'}, {'sha256': '0' * 64}):
            with self.subTest(update=update), self.assertRaises(ValueError):
                list(PREP.array(data, descriptor | update, '<f8'))

    def test_nonfinite_workspace_and_dimensions(self):
        for value in (float('nan'), float('inf'), 1001.):
            with self.subTest(value=value), self.assertRaises(ValueError):
                PREP.scalar(value)
        with self.assertRaises(ValueError):
            PREP.bounds([[1., 0., 0.], [0., 1., 1.]])
        with self.assertRaises(ValueError):
            PREP.transform([[0., 0., 0.]] * 3)

    def test_short_writes_and_output_limit(self):
        class Short(io.StringIO):
            def write(self, value):
                return len(value) - 1
        with self.assertRaises(ValueError):
            PREP.Emitter(Short()).write('packet')
        emitter = PREP.Emitter(io.StringIO())
        emitter.bytes = PREP.MAX_EMISSION
        with self.assertRaises(ValueError):
            emitter.write('x')

    def test_input_pin_before_decode(self):
        data = struct.pack('<3d', 1., 2., 3.)
        pin = hashlib.sha256(data).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'input'
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                PREP.pinned(path, 23, pin)
            with self.assertRaises(ValueError):
                PREP.pinned(path, 24, '0' * 64)
            self.assertEqual(PREP.pinned(path, 24, pin), data)

    def test_unknown_constructor(self):
        with self.assertRaises(ValueError):
            PREP.validate_constructor({'source_kind': 'invented',
                                       'certified_material_membership': False})


if __name__ == '__main__':
    unittest.main()
