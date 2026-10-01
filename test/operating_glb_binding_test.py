#!/usr/bin/env python3
"""Binary-valid graph/source admission controls; no synthetic asset license."""
import copy
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from validate_operating_asset_glb import validate_craft_model, validate_station_model
from operating_glb_fixture import (craft_fixture, craft_groups, decode, encode,
                                   station_corrections, station_fixture)


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'fixture.glb'
        self.spec = {'groups': craft_groups()}
        self.closure = {'corrections': station_corrections()}

    def tearDown(self):
        self.temp.cleanup()

    def craft(self, value):
        self.path.write_bytes(value)
        return validate_craft_model(self.path, self.spec)

    def station(self, value):
        self.path.write_bytes(value)
        return validate_station_model(self.path, self.closure)

    def test_real_valid_buffers_and_source_bindings(self):
        result = self.craft(craft_fixture())
        self.assertEqual(result['binary_audit']['triangles'], 14)
        self.assertEqual(len(result['motion_bindings']), 13)
        self.assertEqual(result['motion_bindings'][0]['glb_node_index'], 1)
        result = self.station(station_fixture())
        self.assertEqual(result['binary_audit']['meshes'], 3)
        self.assertEqual(len(result['replacement_bindings']), 3)

    def test_binary_invalidity_before_graph_admission(self):
        raw = craft_fixture()
        document, payload = decode(raw)
        for invalid in (raw[:-1], struct.pack('<4sII', b'glTF', 2, 24) + b'test fixture',
                        encode(document, struct.pack('<f', float('nan')) + payload[4:]),
                        encode(document, payload[:40] + struct.pack('<H', 3) + payload[42:])):
            with self.subTest(size=len(invalid)):
                with self.assertRaises(ValueError):
                    self.craft(invalid)

    def test_flat_graph_indices_and_runtime_names(self):
        original, payload = decode(craft_fixture())
        mutations = [
            lambda d: d['nodes'][0].update(children=[1]),
            lambda d: d['scenes'][0]['nodes'].pop(),
            lambda d: d['scenes'][0]['nodes'].__setitem__(0, 1),
            lambda d: d['scenes'][0]['nodes'].__setitem__(0, True),
            lambda d: d['nodes'][1].update(mesh=99),
            lambda d: d['nodes'][1].update(mesh=False),
            lambda d: d['nodes'][1].update(mesh=0),
            lambda d: d['nodes'][0].update(name='WFOpRoofPort'),
            lambda d: d['nodes'][1].update(name='WFOpUnlisted'),
            lambda d: d.update(asset=[]),
            lambda d: d.update(animations=[{}]),
            lambda d: d['nodes'][1].update(skin=0),
            lambda d: d['nodes'][1].update(camera=0),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(case=index):
                document = copy.deepcopy(original)
                mutate(document)
                with self.assertRaises(ValueError):
                    self.craft(encode(document, payload))
        document = copy.deepcopy(original)
        document['nodes'][0]['name'] = 'duplicate.1'
        document['nodes'][1]['name'] = 'duplicate_1'
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            self.craft(encode(document, payload))

    def test_missing_or_changed_source_extras(self):
        original, payload = decode(craft_fixture())
        mutations = [lambda d: d['nodes'][1].pop('extras'),
                     lambda d: d['nodes'][1]['extras'].update(source_sha256='0' * 64),
                     lambda d: d['nodes'][1]['extras'].update(source_rig='wrong'),
                     lambda d: d['nodes'][1]['extras'].update(operating_group='seat_lock'),
                     lambda d: d['nodes'][1]['extras'].update(extra=True),
                     lambda d: d['nodes'][0].update(extras={'operating_group': 'unknown'})]
        for index, mutate in enumerate(mutations):
            with self.subTest(case=index):
                document = copy.deepcopy(original)
                mutate(document)
                with self.assertRaises(ValueError):
                    self.craft(encode(document, payload))

    def test_nonidentity_mixed_and_nonfinite_node_rest(self):
        original, payload = decode(craft_fixture())
        for field, value in [('translation', [0, 0, .01]), ('translation', [0, 0]),
                             ('translation', [True, 0, 0]), ('translation', [10 ** 1000, 0, 0]),
                             ('rotation', [0, 0, 1, 0]), ('scale', [1, 1, 2]),
                             ('matrix', [1] * 16)]:
            with self.subTest(field=field, value=str(value)[:40]):
                document = copy.deepcopy(original)
                document['nodes'][1][field] = value
                with self.assertRaises(ValueError):
                    self.craft(encode(document, payload))
        document = copy.deepcopy(original)
        document['nodes'][1].update(translation=[0, 0, 0], matrix=[1, 0, 0, 0] * 4)
        with self.assertRaisesRegex(ValueError, 'Mixed'):
            self.craft(encode(document, payload))
        for number in (float('nan'), float('inf'), -float('inf')):
            document = copy.deepcopy(original)
            document['nodes'][1]['translation'] = [number, 0, 0]
            with self.assertRaises(ValueError):
                self.craft(encode(document, payload, allow_nan=True))

    def test_three_station_replacements_and_exact_sources(self):
        original, payload = decode(station_fixture())
        for mutation in (lambda d: d['nodes'][0].update(name='unknown'),
                         lambda d: d['nodes'][1].update(name=d['nodes'][0]['name']),
                         lambda d: d['nodes'][0].update(translation=[0, 1, 0]),
                         lambda d: d['nodes'][0].update(extras={'source_object': 'wrong'})):
            document = copy.deepcopy(original)
            mutation(document)
            with self.assertRaises(ValueError):
                self.station(encode(document, payload))
        self.closure['corrections'][0]['source_object'] = 'wrong'
        with self.assertRaisesRegex(ValueError, 'source binding'):
            self.station(station_fixture())


if __name__ == '__main__':
    unittest.main()
