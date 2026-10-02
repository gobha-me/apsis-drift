#!/usr/bin/env python3
"""Portable restraint-export parser/integrity refusal tests; BSD-3-Clause.

Fixtures are independently built here. No Blender, selected authoring master,
private path or production-model decompression is required.
"""
import copy
import ctypes
import hashlib
import json
import lzma
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import wayfarer_restraint_export_checks as checks


# Two distinct wound triangles and one deliberately collinear render triangle.
POINTS = (((0, 0, 0), (1, 0, 0), (0, 1, 0)),
          ((0, 0, 1), (2, 0, 1), (0, 2, 1)),
          ((2, 0, 0), (3, 0, 0), (4, 0, 0)))
ATOMIC_AVAILABLE = sys.platform.startswith('linux') and hasattr(ctypes.CDLL(None), 'renameat2')


def hash_bytes(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, sort_keys=True, allow_nan=False) + '\n').encode()


def glb_fixture(groups=(('FrozenSeat', (0, 1, 2)),), winding=(0, 1, 2)):
    """Build real independent accessors, embedded views and root mesh nodes."""
    doc = {'asset': {'version': '2.0'}, 'buffers': [], 'bufferViews': [],
           'accessors': [], 'meshes': [], 'nodes': [], 'scene': 0,
           'scenes': [{'nodes': list(range(len(groups)))}],
           'materials': [{'name': 'Fixture fabric', 'doubleSided': True,
                          'pbrMetallicRoughness': {'baseColorFactor': [.2, .3, .4, 1],
                                                 'metallicFactor': 0, 'roughnessFactor': .8}}]}
    binary = bytearray()

    def add_accessor(values, token, component, kind):
        binary.extend(b'\0' * (-len(binary) % 4))
        offset = len(binary)
        for value in values:
            binary.extend(struct.pack('<' + token * len(value), *value))
        view = len(doc['bufferViews'])
        doc['bufferViews'].append({'buffer': 0, 'byteOffset': offset,
                                   'byteLength': len(binary) - offset})
        index = len(doc['accessors'])
        doc['accessors'].append({'bufferView': view, 'componentType': component,
                                 'count': len(values), 'type': kind})
        return index

    for name, chosen in groups:
        positions = [point for triangle in chosen for point in POINTS[triangle]]
        normals = [(0, 0, 1)] * len(positions)
        uvs = [(0, 0), (1, 0), (0, 1)] * len(chosen)
        indices = [(base + vertex,) for base in range(0, len(positions), 3)
                   for vertex in winding]
        attrs = {'POSITION': add_accessor(positions, 'f', 5126, 'VEC3'),
                 'NORMAL': add_accessor(normals, 'f', 5126, 'VEC3'),
                 'TEXCOORD_0': add_accessor(uvs, 'f', 5126, 'VEC2')}
        primitive = {'attributes': attrs,
                     'indices': add_accessor(indices, 'H', 5123, 'SCALAR'), 'material': 0}
        mesh = len(doc['meshes'])
        doc['meshes'].append({'primitives': [primitive]})
        doc['nodes'].append({'name': name, 'mesh': mesh})
    doc['buffers'] = [{'byteLength': len(binary)}]
    return doc, bytes(binary)


def encode_glb(doc, payload, allow_nonfinite=False):
    text = (json.dumps(doc, allow_nan=True).encode() if allow_nonfinite else json_bytes(doc))
    text += b' ' * (-len(text) % 4)
    binary = payload + b'\0' * (-len(payload) % 4)
    chunks = struct.pack('<II', len(text), 0x4e4f534a) + text
    chunks += struct.pack('<II', len(binary), 0x004e4942) + binary
    return struct.pack('<III', 0x46546c67, 2, 12 + len(chunks)) + chunks


def parse_rows(doc, payload, names=('FrozenSeat',)):
    parsed, binary = checks.read_glb(encode_glb(doc, payload))
    return checks.triangles(parsed, binary, list(names))


class TriangleIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.document, self.payload = glb_fixture()
        self.original = parse_rows(self.document, self.payload)

    def test_independent_split_union_retains_attributes_and_degenerate(self):
        doc, payload = glb_fixture((('Strap', (1,)), ('Residual', (2, 0))))
        rows = parse_rows(doc, payload, ('Strap', 'Residual'))
        result = checks.semantic_receipt(self.original, rows)
        self.assertEqual(result['triangles'], 3)
        self.assertEqual(result['preserved_degenerate_render_triangles'], 1)
        self.assertEqual([r['degenerate'] for r in rows], [False, True, False])
        self.assertTrue(result['winding_normals_uvs_materials_exact'])

    def test_cyclic_vertex_order_preserves_union_but_reversal_refuses(self):
        for winding in ((1, 2, 0), (2, 0, 1)):
            with self.subTest(winding=winding):
                rows = parse_rows(*glb_fixture(winding=winding))
                self.assertTrue(checks.semantic_receipt(self.original, rows)['pass'])
        rows = parse_rows(*glb_fixture(winding=(0, 2, 1)))
        with self.assertRaises(ValueError):
            checks.semantic_receipt(self.original, rows)

    def test_normals_uvs_positions_and_full_material_change_refuse(self):
        for attribute in ('NORMAL', 'TEXCOORD_0', 'POSITION'):
            with self.subTest(attribute=attribute):
                doc = copy.deepcopy(self.document)
                payload = bytearray(self.payload)
                index = doc['meshes'][0]['primitives'][0]['attributes'][attribute]
                view = doc['bufferViews'][doc['accessors'][index]['bufferView']]
                struct.pack_into('<f', payload, view['byteOffset'], .125)
                with self.assertRaises(ValueError):
                    checks.semantic_receipt(self.original, parse_rows(doc, bytes(payload)))
        for mutate in (lambda m: m.update(name='Different fabric'),
                       lambda m: m.update(doubleSided=False),
                       lambda m: m['pbrMetallicRoughness'].update(roughnessFactor=.5)):
            doc = copy.deepcopy(self.document)
            mutate(doc['materials'][0])
            with self.assertRaises(ValueError):
                checks.semantic_receipt(self.original, parse_rows(doc, self.payload))

    def test_degenerate_removal_duplication_and_attribute_change_refuse(self):
        for chosen in ((0, 1), (0, 1, 2, 2)):
            with self.subTest(triangles=chosen):
                rows = parse_rows(*glb_fixture((('FrozenSeat', chosen),)))
                with self.assertRaises(ValueError):
                    checks.semantic_receipt(self.original, rows)
        doc, payload = glb_fixture()
        changed = bytearray(payload)
        view = doc['bufferViews'][doc['accessors'][2]['bufferView']]
        struct.pack_into('<f', changed, view['byteOffset'] + 6 * 8, .25)
        with self.assertRaises(ValueError):
            checks.semantic_receipt(self.original, parse_rows(doc, bytes(changed)))

    def test_duplicate_missing_nested_and_non_root_selected_nodes_refuse(self):
        mutations = [lambda d: d['nodes'].append(copy.deepcopy(d['nodes'][0])),
                     lambda d: d['nodes'][0].update(name='Unselected'),
                     lambda d: d['scenes'][0].update(nodes=[]),
                     lambda d: d['nodes'].append({'name': 'Parent', 'children': [0]}),
                     lambda d: d['nodes'][0].update(children=[1]),
                     lambda d: d['scenes'][0].update(nodes=[0, 0]),
                     lambda d: d['scenes'][0].update(nodes=[True]),
                     lambda d: d.update(scene=True)]
        for case, mutate in enumerate(mutations):
            with self.subTest(case=case):
                doc = copy.deepcopy(self.document)
                # Give the child-bearing selected node a valid actual child index.
                if case == 4:
                    doc['nodes'].append({'name': 'Child'})
                mutate(doc)
                with self.assertRaises(ValueError):
                    parse_rows(doc, self.payload)
        with self.assertRaises(ValueError):
            parse_rows(self.document, self.payload, ('FrozenSeat', 'FrozenSeat'))

    def test_nonidentity_selected_rest_and_bad_mesh_material_indices_refuse(self):
        for field, value in (('translation', [0, 0, 0]), ('rotation', [0, 0, 0, 1]),
                             ('scale', [1, 1, 1]), ('matrix', [1] * 16),
                             ('mesh', True), ('mesh', -1), ('mesh', 9)):
            with self.subTest(field=field, value=value):
                doc = copy.deepcopy(self.document)
                doc['nodes'][0][field] = value
                with self.assertRaises(ValueError):
                    parse_rows(doc, self.payload)
        for value in (True, -1, 9, .0):
            doc = copy.deepcopy(self.document)
            doc['meshes'][0]['primitives'][0]['material'] = value
            with self.assertRaises(ValueError):
                parse_rows(doc, self.payload)

    def test_index_bounds_count_and_attribute_representation_refuse(self):
        for value in (9, 65535):
            doc = copy.deepcopy(self.document)
            payload = bytearray(self.payload)
            view = doc['bufferViews'][3]
            struct.pack_into('<H', payload, view['byteOffset'], value)
            with self.assertRaises(ValueError):
                parse_rows(doc, bytes(payload))
        for mutate in (lambda d: d['accessors'][3].update(count=8),
                       lambda d: d['accessors'][1].update(count=8),
                       lambda d: d['accessors'][2].update(type='VEC3'),
                       lambda d: d['meshes'][0]['primitives'][0].update(mode=1),
                       lambda d: d['meshes'][0]['primitives'][0]['attributes'].update(COLOR_0=0),
                       lambda d: d['meshes'][0]['primitives'][0]['attributes'].update(POSITION=True)):
            doc = copy.deepcopy(self.document)
            mutate(doc)
            with self.assertRaises(ValueError):
                parse_rows(doc, self.payload)


class BinaryBoundaryTests(unittest.TestCase):
    def test_valid_interleaved_and_nonzero_accessor_offset_preserve_union(self):
        original, payload = glb_fixture()
        baseline = parse_rows(original, payload)
        positions_length = original['bufferViews'][0]['byteLength']
        interleaved = b''.join(struct.pack('<4f', *point, 123)
                               for triangle in POINTS for point in triangle)
        doc = copy.deepcopy(original)
        extra = len(interleaved) - positions_length
        doc['bufferViews'][0].update(byteLength=len(interleaved), byteStride=16)
        for view in doc['bufferViews'][1:]:
            view['byteOffset'] += extra
        changed = interleaved + payload[positions_length:]
        doc['buffers'][0]['byteLength'] = len(changed)
        self.assertTrue(checks.semantic_receipt(baseline, parse_rows(doc, changed))['pass'])
        # An accessor-local prefix is valid when its containing view owns it.
        doc = copy.deepcopy(original)
        doc['bufferViews'][0]['byteLength'] += 4
        doc['accessors'][0]['byteOffset'] = 4
        for view in doc['bufferViews'][1:]:
            view['byteOffset'] += 4
        changed = struct.pack('<f', 123) + payload
        doc['buffers'][0]['byteLength'] = len(changed)
        self.assertTrue(checks.semantic_receipt(baseline, parse_rows(doc, changed))['pass'])

    def test_real_glb_round_trip_and_truncated_chunks(self):
        doc, payload = glb_fixture()
        raw = encode_glb(doc, payload)
        parsed, binary = checks.read_glb(raw)
        self.assertEqual(parsed, doc)
        self.assertEqual(binary[:len(payload)], payload)
        for cut in (0, 12, 19, len(raw) - len(payload) - 5, len(raw) - 1):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                checks.read_glb(raw[:cut])
        for offset, value in ((4, 1), (8, len(raw) + 4), (12, 0),
                              (12, 8 * 1024 * 1024 + 4), (16, 0x004e4942)):
            altered = bytearray(raw)
            struct.pack_into('<I', altered, offset, value)
            with self.assertRaises(ValueError):
                checks.read_glb(bytes(altered))
        # Preserve the header total while claiming a too-long binary chunk.
        text_length = struct.unpack_from('<I', raw, 12)[0]
        altered = bytearray(raw)
        struct.pack_into('<I', altered, 20 + text_length, len(payload) + 4)
        with self.assertRaises(ValueError):
            checks.read_glb(bytes(altered))

    def test_unaligned_binary_chunk_refuses_even_with_consistent_header(self):
        doc, payload = glb_fixture()
        raw = bytearray(encode_glb(doc, payload))
        raw.pop()  # Remove padding only; declared payload still fits.
        text_length = struct.unpack_from('<I', raw, 12)[0]
        struct.pack_into('<I', raw, 8, len(raw))
        struct.pack_into('<I', raw, 20 + text_length, len(raw) - 28 - text_length)
        with self.assertRaisesRegex(ValueError, 'binary boundary'):
            checks.read_glb(bytes(raw))

    def test_embedded_buffer_dimensions_types_and_uri_refuse(self):
        doc, payload = glb_fixture()
        padded_length = (len(payload) + 3) // 4 * 4
        for value in (-1, True, padded_length + 1, len(payload) - 4):
            changed = copy.deepcopy(doc)
            changed['buffers'][0]['byteLength'] = value
            with self.assertRaises(ValueError):
                checks.read_glb(encode_glb(changed, payload))
        doc['buffers'][0]['uri'] = 'external.bin'
        with self.assertRaises(ValueError):
            checks.read_glb(encode_glb(doc, payload))

    def test_empty_embedded_buffer_never_admits_negative_declared_length(self):
        doc = {'asset': {'version': '2.0'}, 'buffers': [{'byteLength': -1}]}
        with self.assertRaises(ValueError):
            checks.read_glb(encode_glb(doc, b''))

    def test_numeric_enums_and_declared_accessor_length_require_integer_types(self):
        original, payload = glb_fixture()
        for mutate in (lambda d: d['accessors'][0].update(componentType=5126.0),
                       lambda d: d['accessors'][3].update(componentType=5123.0),
                       lambda d: d['meshes'][0]['primitives'][0].update(mode=4.0)):
            doc = copy.deepcopy(original)
            mutate(doc)
            with self.assertRaises(ValueError):
                parse_rows(doc, payload)
        for value in (True, float(len(payload))):
            doc = copy.deepcopy(original)
            doc['buffers'][0]['byteLength'] = value
            with self.assertRaises(ValueError):
                checks.accessor(doc, payload, 0)

    def test_accessor_counts_types_strides_offsets_and_view_bounds_refuse(self):
        original, payload = glb_fixture()
        cases = [lambda d: d['accessors'][0].update(count=0),
                 lambda d: d['accessors'][0].update(count=True),
                 lambda d: d['accessors'][0].update(count=4_000_001),
                 lambda d: d['accessors'][0].update(componentType=5123),
                 lambda d: d['accessors'][0].update(type='MAT3'),
                 lambda d: d['accessors'][0].update(bufferView=-1),
                 lambda d: d['accessors'][0].update(bufferView=True),
                 lambda d: d['accessors'][0].update(byteOffset=-4),
                 lambda d: d['accessors'][0].update(byteOffset=2),
                 lambda d: d['accessors'][0].update(byteOffset=4),
                 lambda d: d['accessors'][0].update(normalized=True),
                 lambda d: d['accessors'][0].update(sparse={}),
                 lambda d: d['bufferViews'][0].update(buffer=True),
                 lambda d: d['bufferViews'][0].update(byteOffset=-4),
                 lambda d: d['bufferViews'][0].update(byteOffset=True),
                 lambda d: d['bufferViews'][0].update(byteLength=0),
                 lambda d: d['bufferViews'][0].update(byteLength=len(payload) + 1),
                 lambda d: d['bufferViews'][0].update(byteStride=8),
                 lambda d: d['bufferViews'][0].update(byteStride=13),
                 lambda d: d['bufferViews'][0].update(byteStride=256),
                 lambda d: d['bufferViews'][0].update(byteStride=12.0)]
        for case, mutate in enumerate(cases):
            with self.subTest(case=case):
                doc = copy.deepcopy(original)
                mutate(doc)
                with self.assertRaises(ValueError):
                    parse_rows(doc, payload)
        # Combined start is zero, but a negative view base must never be hidden.
        doc = copy.deepcopy(original)
        doc['bufferViews'][0]['byteOffset'] = -4
        doc['bufferViews'][0]['byteLength'] += 4
        doc['accessors'][0]['byteOffset'] = 4
        with self.assertRaisesRegex(ValueError, 'Buffer view'):
            parse_rows(doc, payload)
        for index in (True, -1, len(original['accessors'])):
            with self.assertRaises(ValueError):
                checks.accessor(original, payload, index)

    def test_nonfinite_position_normal_uv_and_material_refuse(self):
        doc, original = glb_fixture()
        for attribute in ('POSITION', 'NORMAL', 'TEXCOORD_0'):
            index = doc['meshes'][0]['primitives'][0]['attributes'][attribute]
            offset = doc['bufferViews'][index]['byteOffset']
            for value in (float('nan'), float('inf'), -float('inf')):
                with self.subTest(attribute=attribute, value=value):
                    payload = bytearray(original)
                    struct.pack_into('<f', payload, offset, value)
                    with self.assertRaises(ValueError):
                        parse_rows(doc, bytes(payload))
        # Feed nonstandard JSON constants through the actual GLB parser as well.
        for value in (float('nan'), float('inf')):
            changed = copy.deepcopy(doc)
            changed['materials'][0]['pbrMetallicRoughness']['roughnessFactor'] = value
            parsed, binary = checks.read_glb(encode_glb(changed, original, allow_nonfinite=True))
            with self.assertRaises(ValueError):
                checks.triangles(parsed, binary, ['FrozenSeat'])


class FrozenReferenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.package = self.root / 'package'
        self.package.mkdir()
        source_dir = self.root / 'source'
        source_dir.mkdir()
        self.source = source_dir / 'synthetic-source.blend'
        self.source.write_bytes(b'Independent test source, never a Blender master')
        self.model = encode_glb(*glb_fixture())
        self.compressed = lzma.compress(self.model)
        self.entries = []
        self.put('models/model.xz', self.compressed)
        self.put('licenses/license.txt', b'BSD-3-Clause test fixture\n')
        members = list(checks.NODES) + [f'Independent residual member {i}' for i in range(57)]
        self.metadata = {'groups': [{'id': 'seat_lift', 'source_objects': members}]}
        self.metadata_bytes = json_bytes(self.metadata)
        self.put('metadata/wayfarer-operating-02.json', self.metadata_bytes)
        self.manifest = {'package_id': 'wayfarer-operating-02', 'files': self.entries,
                         'model': {'sha256': hash_bytes(self.model),
                                   'source_sha256': hash_bytes(self.source.read_bytes()),
                                   'bytes': len(self.model), 'chunks': ['models/model.xz']}}
        self.write_manifest()

    def tearDown(self):
        self.directory.cleanup()

    def put(self, relative, data):
        path = self.package / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        entry = {'path': relative, 'bytes': len(data), 'sha256': hash_bytes(data)}
        self.entries.append(entry)
        return entry

    def write_manifest(self):
        data = json_bytes(self.manifest)
        (self.package / 'package.json').write_bytes(data)
        return hash_bytes(data)

    def pins(self, **overrides):
        values = {'SOURCE_SHA256': hash_bytes(self.source.read_bytes()),
                  'MODEL_SHA256': hash_bytes(self.model),
                  'PACKAGE_SHA256': hash_bytes((self.package / 'package.json').read_bytes()),
                  'METADATA_SHA256': hash_bytes(self.metadata_bytes)}
        values.update(overrides)
        return mock.patch.multiple(checks, **values)

    @unittest.skipUnless(ATOMIC_AVAILABLE, 'Linux renameat2 required by producer preflight')
    def test_isolated_reference_preflight_and_bounded_decode(self):
        with self.pins():
            source, output, package, manifest, spec = checks.preflight(
                self.source, self.root / 'result', self.package)
            self.assertEqual((source, package), (self.source, self.package))
            self.assertFalse(output.exists())
            self.assertEqual(len(spec['groups'][0]['source_objects']), 64)
            self.assertEqual(checks.old_model(package, manifest), self.model)

    @unittest.skipUnless(ATOMIC_AVAILABLE, 'Linux renameat2 required by producer preflight')
    def test_independent_source_package_metadata_and_model_identity_refusals(self):
        for pin in ('SOURCE_SHA256', 'PACKAGE_SHA256', 'METADATA_SHA256', 'MODEL_SHA256'):
            with self.subTest(pin=pin), self.pins(**{pin: '0' * 64}):
                with self.assertRaises(ValueError):
                    checks.preflight(self.source, self.root / 'result', self.package)
        for field, value in (('package_id', 'different-package'),
                             ('source_sha256', '0' * 64), ('sha256', '0' * 64)):
            original = copy.deepcopy(self.manifest)
            if field == 'package_id':
                self.manifest[field] = value
            else:
                self.manifest['model'][field] = value
            self.write_manifest()
            with self.subTest(field=field), self.pins(), self.assertRaises(ValueError):
                checks.preflight(self.source, self.root / 'result', self.package)
            self.manifest = original
            self.write_manifest()

    def test_frozen_member_path_snapshot_hash_size_and_symlink_refusals(self):
        entry = self.entries[0]
        self.assertEqual(checks.verified_member(self.package, entry), self.compressed)
        for change in ({'path': '../outside'}, {'path': str(self.source)},
                       {'bytes': True}, {'bytes': 0}, {'bytes': len(self.compressed) + 1},
                       {'sha256': '0' * 64}):
            altered = dict(entry, **change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                checks.verified_member(self.package, altered)
        alias = self.package / 'alias'
        alias.symlink_to(self.package / 'models', target_is_directory=True)
        with self.assertRaises(ValueError):
            checks.verified_member(self.package, dict(entry, path='alias/model.xz'))
        link = self.package / 'direct.xz'
        link.symlink_to(self.package / entry['path'])
        with self.assertRaises(ValueError):
            checks.verified_member(self.package, dict(entry, path='direct.xz'))

    def test_decoded_size_hash_trailing_data_and_truncation_refuse(self):
        for size in (0, True, checks.MAX_BYTES + 1, len(self.model) - 1, len(self.model) + 1):
            manifest = copy.deepcopy(self.manifest)
            manifest['model']['bytes'] = size
            with self.subTest(size=size), self.pins(), self.assertRaises(ValueError):
                checks.old_model(self.package, manifest)
        with self.pins(MODEL_SHA256='0' * 64), self.assertRaises(ValueError):
            checks.old_model(self.package, self.manifest)
        for compressed in (self.compressed + b'trailing', self.compressed[:-4],
                           self.compressed + self.compressed):
            path = self.package / 'models/model.xz'
            path.write_bytes(compressed)
            manifest = copy.deepcopy(self.manifest)
            manifest['files'][0].update(bytes=len(compressed), sha256=hash_bytes(compressed))
            with self.subTest(compressed_size=len(compressed)), self.pins(), self.assertRaises(ValueError):
                checks.old_model(self.package, manifest)
        (self.package / 'models/model.xz').write_bytes(self.compressed)

    def test_decompression_bomb_stops_at_declared_output_boundary(self):
        expanded = b'A' * 1_000_000
        compressed = lzma.compress(expanded)
        (self.package / 'models/model.xz').write_bytes(compressed)
        manifest = copy.deepcopy(self.manifest)
        manifest['model']['bytes'] = 128
        manifest['files'][0].update(bytes=len(compressed), sha256=hash_bytes(compressed))
        with self.pins(MODEL_SHA256=hash_bytes(expanded)), self.assertRaises(ValueError):
            checks.old_model(self.package, manifest)

    def test_compressed_aggregate_and_chunk_count_refuse_before_decode(self):
        for chunks in ([], ['models/model.xz'] * 65):
            manifest = copy.deepcopy(self.manifest)
            manifest['model']['chunks'] = chunks
            with self.subTest(chunks=len(chunks)), self.assertRaises(ValueError):
                checks.old_model(self.package, manifest)
        manifest = copy.deepcopy(self.manifest)
        manifest['model']['chunks'] = ['models/model.xz'] * 2
        with mock.patch.object(checks, 'MAX_BYTES', 2 * len(self.compressed) - 1):
            with self.assertRaisesRegex(ValueError, 'compressed dimensions'):
                checks.old_model(self.package, manifest)

    @unittest.skipUnless(ATOMIC_AVAILABLE, 'Linux renameat2 required by producer preflight')
    def test_preflight_rejects_changed_frozen_chunks_and_licenses(self):
        for name in ('models/model.xz', 'licenses/license.txt'):
            path = self.package / name
            original = path.read_bytes()
            changed = bytes([original[0] ^ 1]) + original[1:]
            path.write_bytes(changed)
            with self.subTest(name=name), self.pins(), self.assertRaises(ValueError):
                checks.preflight(self.source, self.root / 'result', self.package)
            self.assertFalse((self.root / 'result').exists())
            path.write_bytes(original)

    def test_pinned_json_size_hash_and_symlink_refuse(self):
        path = self.package / 'package.json'
        data = path.read_bytes()
        self.assertEqual(checks.pinned_json(path, hash_bytes(data), len(data)), self.manifest)
        for pin, maximum in ((hash_bytes(data), len(data) - 1), ('0' * 64, len(data))):
            with self.assertRaises(ValueError):
                checks.pinned_json(path, pin, maximum)
        alias = self.root / 'manifest-link.json'
        alias.symlink_to(path)
        with self.assertRaises(ValueError):
            checks.pinned_json(alias, hash_bytes(data), len(data))


@unittest.skipUnless(ATOMIC_AVAILABLE, 'Linux atomic no-replace primitive unavailable')
class AtomicPublicationTests(unittest.TestCase):
    def test_raced_destination_directory_is_preserved_with_staging(self):
        for occupied in (False, True):
            with self.subTest(occupied=occupied), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                staging, destination = root / 'staging', root / 'output'
                staging.mkdir()
                (staging / 'model.glb').write_bytes(b'complete staged bytes')
                self.assertFalse(destination.exists())
                # The destination appears after the preflight observation.
                destination.mkdir()
                if occupied:
                    (destination / 'owner.txt').write_bytes(b'concurrent owner bytes')
                with self.assertRaises(FileExistsError):
                    checks.install_no_replace(staging, destination)
                self.assertEqual((staging / 'model.glb').read_bytes(), b'complete staged bytes')
                self.assertTrue(destination.is_dir())
                if occupied:
                    self.assertEqual((destination / 'owner.txt').read_bytes(), b'concurrent owner bytes')
                else:
                    self.assertEqual(list(destination.iterdir()), [])

    def test_raced_symlink_and_successful_install(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staging, destination, owner = root / 'staging', root / 'output', root / 'owner'
            staging.mkdir()
            owner.mkdir()
            (staging / 'receipt').write_bytes(b'staged')
            destination.symlink_to(owner, target_is_directory=True)
            with self.assertRaises(FileExistsError):
                checks.install_no_replace(staging, destination)
            self.assertTrue(destination.is_symlink())
            self.assertEqual(list(owner.iterdir()), [])
            self.assertEqual((staging / 'receipt').read_bytes(), b'staged')
            destination.unlink()
            checks.install_no_replace(staging, destination)
            self.assertFalse(staging.exists())
            self.assertEqual((destination / 'receipt').read_bytes(), b'staged')


if __name__ == '__main__':
    unittest.main()
