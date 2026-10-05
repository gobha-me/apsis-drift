"""Portable #411 package controls using invented binary32 GLB buffers only."""
from contextlib import ExitStack
import copy
import ctypes
import hashlib
import json
import lzma
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


def byte_pin(raw):
    return {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def glb_bytes(document, payload):
    """Independent glTF2 GLB encoder, with one padded JSON and BIN chunk."""
    text = encoded(document)
    text += b' ' * (-len(text) % 4)
    binary = payload + b'\0' * (-len(payload) % 4)
    return (struct.pack('<III', 0x46546C67, 2, 28 + len(text) + len(binary)) +
            struct.pack('<II', len(text), 0x4E4F534A) + text +
            struct.pack('<II', len(binary), 0x004E4942) + binary)


def triangle_fixture(*, duplicate_face=False):
    """One flat root; repeated coincident faces remain distinct occurrences."""
    positions = [[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]]
    normals = [[0., 0., 1.]] * 3
    uvs = [[0., 0.], [1., 0.], [0., 1.]]
    indices = [0, 1, 2] * (2 if duplicate_face else 1)
    chunks = [struct.pack('<9f', *[c for v in positions for c in v]),
              struct.pack('<9f', *[c for v in normals for c in v]),
              struct.pack('<6f', *[c for v in uvs for c in v]),
              struct.pack('<' + 'H' * len(indices), *indices)]
    views = []
    offset = 0
    for raw in chunks:
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(raw)})
        offset += len(raw)
    material = {'name': 'FixtureMetal',
                'pbrMetallicRoughness': {'baseColorFactor': [.5, .5, .5, 1.],
                                       'metallicFactor': .5, 'roughnessFactor': .75}}
    document = {
        'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
        'nodes': [{'name': 'FixtureRoot', 'mesh': 0}],
        'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1,
                                                   'TEXCOORD_0': 2},
                                    'indices': 3, 'material': 0, 'mode': 4}]}],
        'materials': [material], 'buffers': [{'byteLength': offset}],
        'bufferViews': views,
        'accessors': [{'bufferView': 0, 'componentType': 5126, 'type': 'VEC3', 'count': 3},
                      {'bufferView': 1, 'componentType': 5126, 'type': 'VEC3', 'count': 3},
                      {'bufferView': 2, 'componentType': 5126, 'type': 'VEC2', 'count': 3},
                      {'bufferView': 3, 'componentType': 5123, 'type': 'SCALAR',
                       'count': len(indices)}]}
    return document, b''.join(chunks), positions, normals, uvs


def two_part_glb():
    """Two source triangles in one render primitive, with distinct vertex IDs."""
    document, _, positions, normals, uvs = triangle_fixture()
    positions += [[2., 0., 0.], [3., 0., 0.], [2., 1., 0.]]
    normals += [[0., 0., 1.]] * 3
    uvs += [[0., 0.], [1., 0.], [0., 1.]]
    indices = [0, 1, 2, 3, 4, 5]
    chunks = [struct.pack('<18f', *[c for v in positions for c in v]),
              struct.pack('<18f', *[c for v in normals for c in v]),
              struct.pack('<12f', *[c for v in uvs for c in v]),
              struct.pack('<6H', *indices)]
    offset = 0
    for view, raw in zip(document['bufferViews'], chunks):
        view.update(byteOffset=offset, byteLength=len(raw))
        offset += len(raw)
    document['buffers'][0]['byteLength'] = offset
    for accessor in document['accessors']:
        accessor['count'] = 6
    return document, b''.join(chunks), positions, normals, uvs


def rewrite_float(payload, document, view_index, scalar_index, value):
    raw = bytearray(payload)
    struct.pack_into('<f', raw, document['bufferViews'][view_index]['byteOffset'] +
                     scalar_index * 4, value)
    return bytes(raw)


class GeometryFixture:
    """Explicit small profile, enrolled once; GLB bytes are independently assembled."""
    def __init__(self, *, duplicate=False):
        import wayfarer_stowed_package_checks as checks
        self.checks = checks
        self.document, self.payload, positions, normals, uvs = two_part_glb()
        if duplicate:
            # Equal corners at distinct emitted/source face occurrences.
            self.payload = rewrite_float(self.payload, self.document, 0, 9, 0.)
            self.payload = rewrite_float(self.payload, self.document, 0, 12, 1.)
            self.payload = rewrite_float(self.payload, self.document, 0, 15, 0.)
            positions[3:] = copy.deepcopy(positions[:3])
        self.baseline_model = glb_bytes(self.document, self.payload)
        self.sources = {}
        for index, name in enumerate(('Keep', 'Changed')):
            start = 3 * index
            self.sources[name] = {
                'source_type': 'MESH', 'source_parent': None,
                'vertices': copy.deepcopy(positions[start:start + 3]),
                'triangles': [[0, 1, 2]], 'triangle_materials': ['FixtureMetal'],
                'triangle_uv_gltf': [copy.deepcopy(uvs[start:start + 3])],
                'corner_normals_blender': copy.deepcopy(normals[start:start + 3]),
                # Deliberately nonidentity historical matrix: vertices already world-baked.
                'source_world_canonical': [[1., 0., 0., 0.], [0., 1., 0., 0.],
                                           [0., 0., 1., 0.], [7., 0., 0., 1.]],
                'introduced_connector': name == 'Changed'}
        self.contact = {
            'schema': 'apsis.restraint-stowed-prototype-contact/1',
            'model_sha256': hashlib.sha256(self.baseline_model).hexdigest(),
            'source_sha256': '1' * 64,
            'coordinate_namespace': checks.identity.NAMESPACE,
            'objects': self.sources, 'actual_emitted_buffer_bits_verified': True,
            'runtime_actor_attachment_motion_admitted': False}
        self.attribution = {'model_sha256': self.contact['model_sha256'], 'faces': [
            {'node': 'FixtureRoot', 'node_face': i, 'source_object': name, 'source_face': 0,
             'actual_primitive': 0, 'actual_primitive_triangle': i}
            for i, name in enumerate(self.sources)]}
        self.baseline_attribution = encoded(self.attribution)
        self.profile = {'source_sha256': '1' * 64,
                        'coordinate_namespace': checks.identity.NAMESPACE,
                        'groups': [{'node': 'FixtureRoot', 'source_objects': list(self.sources)}],
                        'objects': {}, 'retained_objects': ['Keep'],
                        'replacement_objects': ['Changed'], 'baseline_objects': 2,
                        'frame': copy.deepcopy(checks.FRAME)}
        self.enroll()

    def model(self):
        return glb_bytes(self.document, self.payload)

    def rebind_model(self):
        pin = hashlib.sha256(self.model()).hexdigest()
        self.contact['model_sha256'] = pin
        self.attribution['model_sha256'] = pin

    def enroll(self):
        rows = self.checks.geometry.node_faces(self.checks.geometry.decode_glb(self.model()),
                                              'FixtureRoot')
        self.profile['objects'] = {
            name: self.checks.describe_object(source, 'FixtureRoot', [rows[i]])
            for i, (name, source) in enumerate(self.sources.items())}

    def validate(self):
        return self.checks.validate_geometry(self.model(), encoded(self.contact),
            encoded(self.attribution), self.baseline_model, self.baseline_attribution,
            profile=self.profile)


class GeometryTests(unittest.TestCase):
    def test_complete_occurrence_preservation_and_world_baked_runtime_contact(self):
        f = GeometryFixture()
        result = f.validate()
        self.assertEqual(result['preservation']['complete_source_faces'], 2)
        self.assertEqual(result['preservation']['retained_faces'], 1)
        self.assertTrue(result['preservation']['retained_complete_corner_material_bits_match'])
        self.assertFalse(result['preservation']['runtime_actor_admitted'])
        runtime = json.loads(result['runtime_contact'])
        self.assertEqual(len(runtime['objects']), 1)
        self.assertEqual(runtime['objects'][0]['source_object'], 'Changed')
        self.assertEqual(runtime['objects'][0]['vertices'], [[2., 0., 0.], [3., 0., 0.], [2., 1., 0.]])
        self.assertEqual(runtime['objects'][0]['triangles'], [[0, 1, 2]])
        self.assertEqual(runtime['objects'][0]['source_faces'], [0])
        self.assertFalse(runtime['retained_originals_and_halo_added_again'])
        self.assertEqual(json.loads(result['frame'])['operating_tuple'], [0, 0, 0, 0])

    def test_coincident_faces_require_both_distinct_occurrences(self):
        f = GeometryFixture(duplicate=True)
        self.assertEqual(f.validate()['preservation']['complete_source_faces'], 2)
        f.attribution['faces'][1]['node_face'] = 0
        with self.assertRaises(ValueError):
            f.validate()

    def test_unused_attribute_rows_refuse_and_shared_rows_union_across_primitives(self):
        f = GeometryFixture()
        # Append one unreferenced row to all three attributes without changing any face.
        positions = [p for source in f.sources.values() for p in source['vertices']] + [[-2., 0., 0.]]
        normals = [[0., 0., 1.]] * 7
        uvs = [[0., 0.], [1., 0.], [0., 1.]] * 2 + [[.5, .5]]
        chunks = [struct.pack('<21f', *[v for p in positions for v in p]),
                  struct.pack('<21f', *[v for p in normals for v in p]),
                  struct.pack('<14f', *[v for p in uvs for v in p]),
                  struct.pack('<6H', 0, 1, 2, 3, 4, 5)]
        offset = 0
        for view, raw in zip(f.document['bufferViews'], chunks):
            view.update(byteOffset=offset, byteLength=len(raw))
            offset += len(raw)
        f.document['buffers'][0]['byteLength'] = offset
        for accessor in f.document['accessors'][:3]: accessor['count'] = 7
        f.payload = b''.join(chunks)
        f.rebind_model()
        with self.assertRaisesRegex(ValueError, 'attribute'):
            f.validate()

        # The same six rows can be shared by two primitives covering three rows each.
        f = GeometryFixture()
        old = f.document['bufferViews'][3]
        old['byteLength'] = 6
        f.document['bufferViews'].append(dict(old, byteOffset=old['byteOffset'] + 6))
        f.document['accessors'][3]['count'] = 3
        f.document['accessors'].append(dict(f.document['accessors'][3], bufferView=4))
        primitive = f.document['meshes'][0]['primitives'][0]
        f.document['meshes'][0]['primitives'].append(dict(primitive, indices=4))
        f.attribution['faces'][1].update(actual_primitive=1, actual_primitive_triangle=0)
        f.rebind_model()
        self.assertEqual(f.validate()['preservation']['complete_source_faces'], 2)

    def test_source_dimensions_indices_and_true_types_refuse(self):
        changes = ('empty-points', 'dimension', 'nonfinite', 'bool-coordinate', 'not-binary32',
                   'empty-faces', 'short-face', 'oob', 'negative', 'bool-index', 'float-index',
                   'short-uv', 'uv-width', 'normal-width', 'short-normals', 'material-count')
        for change in changes:
            with self.subTest(change=change):
                f = GeometryFixture()
                source = f.sources['Keep']
                if change == 'empty-points': source['vertices'] = []
                elif change == 'dimension': source['vertices'][0].pop()
                elif change == 'nonfinite': source['vertices'][0][0] = 1e100
                elif change == 'bool-coordinate': source['vertices'][0][0] = False
                elif change == 'not-binary32': source['vertices'][0][0] = .1
                elif change == 'empty-faces': source['triangles'] = []
                elif change == 'short-face': source['triangles'][0].pop()
                elif change == 'oob': source['triangles'][0][0] = 3
                elif change == 'negative': source['triangles'][0][0] = -1
                elif change == 'bool-index': source['triangles'][0][0] = True
                elif change == 'float-index': source['triangles'][0][0] = 0.
                elif change == 'short-uv': source['triangle_uv_gltf'][0].pop()
                elif change == 'uv-width': source['triangle_uv_gltf'][0][0].append(0.)
                elif change == 'normal-width': source['corner_normals_blender'][0].pop()
                elif change == 'short-normals': source['corner_normals_blender'].pop()
                else: source['triangle_materials'] = []
                with self.assertRaises(ValueError): f.validate()

    def test_binary_nonfinite_and_glb_boundaries_refuse(self):
        for view in (0, 1, 2):
            for value in (float('nan'), float('inf'), -float('inf')):
                with self.subTest(view=view, value=value):
                    f = GeometryFixture()
                    f.payload = rewrite_float(f.payload, f.document, view, 0, value)
                    f.rebind_model()
                    with self.assertRaises(ValueError): f.validate()
        for change in ('bool-count', 'negative-count', 'empty-count', 'huge-count',
                       'view-range', 'index-oob', 'truncated'):
            with self.subTest(change=change):
                f = GeometryFixture()
                if change == 'bool-count': f.document['accessors'][0]['count'] = True
                elif change == 'negative-count': f.document['accessors'][0]['count'] = -1
                elif change == 'empty-count': f.document['accessors'][0]['count'] = 0
                elif change == 'huge-count': f.document['accessors'][0]['count'] = 1000000
                elif change == 'view-range': f.document['bufferViews'][0]['byteLength'] = 1000000
                elif change == 'index-oob':
                    raw = bytearray(f.payload)
                    struct.pack_into('<H', raw, f.document['bufferViews'][3]['byteOffset'], 6)
                    f.payload = bytes(raw)
                else: f.payload = f.payload[:-4]
                f.rebind_model()
                with self.assertRaises(ValueError): f.validate()

    def test_attribution_missing_duplicate_foreign_and_primitive_slots_refuse(self):
        for change in ('missing', 'extra', 'duplicate-source', 'duplicate-emitted', 'foreign',
                       'source-gap', 'primitive', 'triangle', 'bool-index', 'extra-row-key'):
            with self.subTest(change=change):
                f = GeometryFixture()
                rows = f.attribution['faces']
                if change == 'missing': rows.pop()
                elif change == 'extra': rows.append(copy.deepcopy(rows[0]))
                elif change == 'duplicate-source': rows[1]['source_object'] = 'Keep'
                elif change == 'duplicate-emitted': rows[1]['node_face'] = 0
                elif change == 'foreign': rows[1]['source_object'] = 'Unlisted'
                elif change == 'source-gap': rows[1]['source_face'] = 1
                elif change == 'primitive': rows[1]['actual_primitive'] = 1
                elif change == 'triangle': rows[1]['actual_primitive_triangle'] = 0
                elif change == 'bool-index': rows[0]['node_face'] = False
                else: rows[0]['trusted'] = True
                with self.assertRaises(ValueError): f.validate()

    def test_retained_normal_signed_zero_uv_and_material_drift_refuse_reenrollment(self):
        for change in ('normal-zero', 'normal-value', 'uv-zero', 'uv-value', 'material-name',
                       'material-factor', 'position'):
            with self.subTest(change=change):
                f = GeometryFixture()
                if change.startswith('normal'):
                    f.payload = rewrite_float(f.payload, f.document, 1, 0,
                                               -0. if change == 'normal-zero' else .5)
                elif change.startswith('uv'):
                    value = -0. if change == 'uv-zero' else .5
                    f.payload = rewrite_float(f.payload, f.document, 2, 0, value)
                    f.sources['Keep']['triangle_uv_gltf'][0][0][0] = value
                elif change == 'position':
                    f.payload = rewrite_float(f.payload, f.document, 0, 0, .5)
                    f.sources['Keep']['vertices'][0][0] = .5
                elif change == 'material-name':
                    f.document['materials'][0]['name'] = 'ChangedName'
                    for source in f.sources.values(): source['triangle_materials'] = ['ChangedName']
                else: f.document['materials'][0]['pbrMetallicRoughness']['roughnessFactor'] = .5
                f.rebind_model()
                # Deliberately re-enroll the current fixture profile: frozen baseline still binds it.
                f.enroll()
                with self.assertRaisesRegex(ValueError, 'Retained'):
                    f.validate()

    def test_joint_cyclic_rotation_allowed_but_winding_reversal_refused(self):
        f = GeometryFixture()
        raw = bytearray(f.payload)
        index_offset = f.document['bufferViews'][3]['byteOffset']
        struct.pack_into('<3H', raw, index_offset, 1, 2, 0)
        f.payload = bytes(raw)
        f.rebind_model()
        self.assertEqual(f.validate()['preservation']['retained_faces'], 1)
        struct.pack_into('<3H', raw, index_offset, 0, 2, 1)
        f.payload = bytes(raw)
        f.rebind_model()
        f.enroll()
        with self.assertRaises(ValueError): f.validate()

    def test_flat_identity_roots_static_attributes_and_frames_only(self):
        for change in ('translation', 'rotation', 'scale', 'matrix', 'children', 'skin',
                       'animation', 'morph', 'frame', 'namespace', 'source-pin', 'scope'):
            with self.subTest(change=change):
                f = GeometryFixture()
                if change in ('translation', 'rotation', 'scale', 'matrix', 'children', 'skin'):
                    f.document['nodes'][0][change] = {
                        'translation': [1., 0., 0.], 'rotation': [0., 0., 0., 1.],
                        'scale': [1., 1., 1.], 'matrix': [1.] * 16,
                        'children': [0], 'skin': 0}[change]
                    f.rebind_model()
                elif change == 'animation':
                    f.document['animations'] = [{'samplers': [], 'channels': []}]
                    f.rebind_model()
                elif change == 'morph':
                    f.document['meshes'][0]['primitives'][0]['targets'] = [{'POSITION': 0}]
                    f.rebind_model()
                elif change == 'frame': f.profile['frame']['operating_tuple'][0] = 1
                elif change == 'namespace': f.contact['coordinate_namespace'] = 'guessed axis remapping'
                elif change == 'source-pin': f.contact['source_sha256'] = '2' * 64
                else: f.contact['runtime_actor_attachment_motion_admitted'] = True
                with self.assertRaises(ValueError): f.validate()

    def test_normal_metadata_and_emitted_profile_are_separate_fixed_domains(self):
        f = GeometryFixture()
        f.sources['Changed']['corner_normals_blender'][0][0] = -0.
        with self.assertRaises(ValueError): f.validate()
        f = GeometryFixture()
        f.profile['objects']['Changed']['emitted_sha256'] = '0' * 64
        with self.assertRaises(ValueError): f.validate()
        f = GeometryFixture()
        f.sources['Changed'].pop('corner_normals_blender')
        f.enroll()
        self.assertIsNone(f.profile['objects']['Changed']['normal_sha256'])
        self.assertFalse(f.validate()['preservation']['runtime_actor_admitted'])

    def test_duplicate_nonfinite_json_depth_and_input_output_caps(self):
        f = GeometryFixture()
        checks = f.checks
        for raw in (b'{"x":0,"x":1}', b'{"x":NaN}', b'{"x":1e999}',
                    b'[' * 33 + b'0' + b']' * 33):
            with self.subTest(raw=raw[:20]), self.assertRaises(ValueError):
                checks.closed_json(raw)
        total = sum(len(raw) for raw in (f.model(), encoded(f.contact), encoded(f.attribution),
                                        f.baseline_model, f.baseline_attribution))
        with mock.patch.object(checks, 'MAX_TOTAL', total): f.validate()
        with mock.patch.object(checks, 'MAX_TOTAL', total - 1), self.assertRaises(ValueError): f.validate()
        with mock.patch.object(checks, 'MAX_FILE', 8), self.assertRaises(ValueError): f.validate()
        output = f.validate()
        exact = len(output['frame']) + len(output['runtime_contact'])
        with mock.patch.object(checks, 'MAX_OUTPUT', exact): f.validate()
        with mock.patch.object(checks, 'MAX_OUTPUT', exact - 1), self.assertRaises(ValueError): f.validate()


class ProductionSelectionTests(unittest.TestCase):
    def test_self_described_rehashed_substitutes_do_not_enroll_production(self):
        import wayfarer_stowed_package_checks as checks
        inputs = {name: b'Invented substitute ' + name.encode() for name in checks.identity.PAYLOAD_PINS}
        with mock.patch.object(checks, 'validate_geometry') as geometry:
            with self.assertRaises(ValueError): checks.validate_payloads(inputs)
            geometry.assert_not_called()

    def test_license_and_qualification_pins_are_checked_before_geometry(self):
        import wayfarer_stowed_package_checks as checks
        inputs = {name: b'Invented substitute ' + name.encode() for name in checks.identity.PAYLOAD_PINS}
        fixture_pins = {name: byte_pin(raw) for name, raw in inputs.items()}
        # Explicit synthetic identity enrollment isolates each production gate.
        for role in [n for n in inputs if n.startswith('licenses/')] + ['qualification.json']:
            with self.subTest(role=role):
                changed = dict(inputs)
                changed[role] += b'changed'
                with mock.patch.object(checks.identity, 'PAYLOAD_PINS', fixture_pins), \
                        mock.patch.object(checks, 'validate_geometry') as geometry:
                    with self.assertRaises(ValueError): checks.validate_payloads(changed)
                    geometry.assert_not_called()
        evidence = {'schema': 'invented-fixed-qualification', 'runtime_actor_admitted': False}
        inputs['qualification.json'] = encoded(dict(evidence, runtime_actor_admitted=True))
        fixture_pins['qualification.json'] = byte_pin(inputs['qualification.json'])
        with mock.patch.object(checks.identity, 'PAYLOAD_PINS', fixture_pins), \
                mock.patch.object(checks.identity, 'EVIDENCE', evidence), \
                mock.patch.object(checks, 'validate_geometry') as geometry:
            with self.assertRaises(ValueError): checks.validate_payloads(inputs)
            geometry.assert_not_called()


class StorageFixture:
    """Explicit isolated storage enrollment, never a geometry admission fixture."""
    def __enter__(self):
        import prepare_wayfarer_stowed as assets
        import package_wayfarer_stowed as package
        self.assets, self.package = assets, package
        self.stack = ExitStack()
        self.root = Path(self.stack.enter_context(tempfile.TemporaryDirectory()))
        self.output, self.installed = self.root / 'package', self.root / 'installed'
        self.payloads = {
            'model.glb': b'Invented storage bytes, not a GLB.',
            'contact.json': encoded({'fixture': 'contact'}),
            'face-attribution.json': encoded({'fixture': 'attribution'}),
            'baseline/model.glb': b'Invented baseline storage bytes, not a GLB.',
            'baseline/face-attribution.json': encoded({'fixture': 'baseline-attribution'}),
            'qualification.json': encoded({'fixture_storage_only': True, 'runtime_admitted': False})}
        for name in ('HOPPER_GENERATED_CONCEPTS.md', 'HOPPER_MESHY_TRIAL.md', 'LICENSE.md',
                     'MESHY_QUALIFICATION_OUTPUT.md', 'STATION_KIT_01.md'):
            self.payloads['licenses/' + name] = ('Invented test license ' + name + '\n').encode()
        self.pins = {name: byte_pin(raw) for name, raw in self.payloads.items()}
        self.validation = {'frame': encoded({'fixture_frame': True}),
                           'runtime_contact': encoded({'fixture_contact': True}),
                           'preservation': {'fixture_storage_only': True}}
        self.stack.enter_context(mock.patch.object(assets.checks, 'PAYLOAD_PINS', self.pins))
        self.gate = self.stack.enter_context(mock.patch.object(
            assets.checks, 'validate_payloads', return_value=self.validation))
        package.build(self.payloads, self.output)
        self.snapshot = self.files(self.output)
        self.gate.reset_mock()
        return self

    def __exit__(self, *args):
        return self.stack.__exit__(*args)

    @staticmethod
    def files(root):
        return {str(path.relative_to(root)): path.read_bytes()
                for path in root.rglob('*') if path.is_file()}

    def manifest(self):
        return json.loads((self.output / 'package.json').read_bytes())

    def save_manifest(self, manifest):
        (self.output / 'package.json').write_bytes(encoded(manifest))

    def replace_stream(self, raw):
        manifest = self.manifest()
        row = next(row for row in manifest['files'] if row['path'] != row['role'])
        (self.output / row['path']).write_bytes(raw)
        row.update(byte_pin(raw))
        self.save_manifest(manifest)


ATOMIC_SUPPORTED = sys.platform.startswith('linux') and hasattr(ctypes.CDLL(None), 'renameat2')


class CompressionTests(unittest.TestCase):
    def test_one_exact_stream_and_no_trailing_or_concatenated_data(self):
        import prepare_wayfarer_stowed as assets
        raw = b'bounded invented payload' * 50
        stream = lzma.compress(raw)
        self.assertEqual(assets.unpack(stream, len(raw)), raw)
        for bad in (stream[:-1], stream + b'junk', stream + lzma.compress(b'other'), b'not XZ'):
            with self.subTest(kind=len(bad)), self.assertRaises(ValueError):
                assets.unpack(bad, len(raw))
        for size in (len(raw) - 1, len(raw) + 1, 0, -1, True, 1.0, assets.MAX_PAYLOAD + 1):
            with self.subTest(size=size), self.assertRaises(ValueError):
                assets.unpack(stream, size)

    def test_decoded_reservation_stops_expansion(self):
        import prepare_wayfarer_stowed as assets
        stream = lzma.compress(b'A' * 100000)
        with self.assertRaises(ValueError):
            assets.unpack(stream, 10)
        with mock.patch.object(assets, 'MAX_DECODER_MEMORY', 1), self.assertRaises(ValueError):
            assets.unpack(stream, 100000)


@unittest.skipUnless(ATOMIC_SUPPORTED, 'Linux atomic no-replace installation is unavailable')
class StorageTests(unittest.TestCase):
    def test_lossless_build_prepare_and_exact_reuse(self):
        with StorageFixture() as f:
            verified = f.assets.verify(f.output)
            self.assertEqual(verified['payloads'], f.payloads)
            expected = dict(f.payloads, **{'frame.json': f.validation['frame'],
                                         'replacement-contact.json': f.validation['runtime_contact'],
                                         'preservation.json': f.assets.encode(f.validation['preservation'])})
            result = f.assets.prepare(f.output, f.installed)
            self.assertFalse(result['reused'])
            files = f.files(f.installed)
            self.assertEqual(set(files), set(expected) | {'preparation.json'})
            for name, raw in expected.items():
                self.assertEqual(files[name], raw)
            receipt = json.loads(files['preparation.json'])
            self.assertEqual(receipt['files'], [dict(path=name, **byte_pin(raw))
                                              for name, raw in sorted(expected.items())])
            self.assertTrue(f.assets.prepare(f.output, f.installed)['reused'])
            self.assertEqual(f.files(f.output), f.snapshot)

    def test_raw_qualification_and_five_licenses_are_readable_and_closed(self):
        with StorageFixture() as f:
            rows = {row['role']: row for row in f.manifest()['files']}
            raw_roles = {'qualification.json'} | {name for name in f.payloads if name.startswith('licenses/')}
            self.assertEqual(len(raw_roles), 6)
            expected_paths = {'package.json'}
            for role, raw in f.payloads.items():
                row = rows[role]
                if role in raw_roles:
                    self.assertEqual(row['path'], role)
                    self.assertEqual((f.output / role).read_bytes(), raw)
                    self.assertEqual(row['bytes'], row['decoded_bytes'])
                    self.assertEqual(row['sha256'], row['decoded_sha256'])
                else:
                    self.assertEqual(row['path'], 'payloads/' + role + '.xz')
                    self.assertEqual(lzma.decompress((f.output / row['path']).read_bytes()), raw)
                expected_paths.add(row['path'])
            self.assertEqual(set(f.files(f.output)), expected_paths)
            qualification = json.loads((f.output / 'qualification.json').read_bytes())
            self.assertFalse(qualification['runtime_admitted'])
            real_unpack = f.assets.unpack
            with mock.patch.object(f.assets, 'unpack', wraps=real_unpack) as unpack:
                self.assertEqual(f.assets.verify(f.output)['payloads'], f.payloads)
                self.assertEqual(unpack.call_count, 5)

    def test_raw_license_and_qualification_rehash_cannot_change_fixed_identity(self):
        for role in ('qualification.json', 'licenses/LICENSE.md'):
            with self.subTest(role=role), StorageFixture() as f:
                manifest = f.manifest()
                row = next(row for row in manifest['files'] if row['role'] == role)
                changed = b'X' * len(f.payloads[role])
                (f.output / role).write_bytes(changed)
                row.update(byte_pin(changed))
                row['decoded_sha256'] = hashlib.sha256(changed).hexdigest()
                f.save_manifest(manifest)
                with self.assertRaises(ValueError): f.assets.verify(f.output)
                f.gate.assert_not_called()

    def test_raw_role_cannot_be_redirected_to_compressed_old_layout(self):
        with StorageFixture() as f:
            manifest = f.manifest()
            row = next(row for row in manifest['files'] if row['role'] == 'qualification.json')
            source = f.output / row['path']
            source.unlink()
            packed = lzma.compress(f.payloads[row['role']])
            row['path'] = 'payloads/qualification.json.xz'
            path = f.output / row['path']
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(packed)
            row.update(byte_pin(packed))
            f.save_manifest(manifest)
            with self.assertRaises(ValueError): f.assets.verify(f.output)
            f.gate.assert_not_called()

    def test_exact_manifest_bytes_bind_receipt_even_for_equivalent_json(self):
        with StorageFixture() as f:
            first = f.assets.verify(f.output)
            first_manifest = (f.output / 'package.json').read_bytes()
            f.save_manifest(f.manifest())  # Same values, remove the original terminal newline.
            second = f.assets.verify(f.output)
            second_manifest = (f.output / 'package.json').read_bytes()
            self.assertNotEqual(first_manifest, second_manifest)
            self.assertEqual(first['manifest'], second['manifest'])
            self.assertEqual(first['manifest_sha256'], hashlib.sha256(first_manifest).hexdigest())
            self.assertEqual(second['manifest_sha256'], hashlib.sha256(second_manifest).hexdigest())
            self.assertNotEqual(first['manifest_sha256'], second['manifest_sha256'])
            f.assets.prepare(f.output, f.installed)
            receipt = json.loads((f.installed / 'preparation.json').read_bytes())
            self.assertEqual(receipt['package_manifest_sha256'], second['manifest_sha256'])
            (f.output / 'package.json').write_bytes(first_manifest)
            installed_before = f.files(f.installed)
            with self.assertRaises(ValueError): f.assets.prepare(f.output, f.installed)
            self.assertEqual(f.files(f.installed), installed_before)

    def test_build_closed_roster_types_and_caps_deny_before_geometry(self):
        for change in ('missing', 'extra', 'mutable-bytes', 'empty', 'oversized', 'aggregate'):
            with self.subTest(change=change), StorageFixture() as f:
                payloads = dict(f.payloads)
                target = f.root / 'new-package'
                with ExitStack() as bounds:
                    if change == 'missing': payloads.pop('model.glb')
                    elif change == 'extra': payloads['extra.bin'] = b'foreign'
                    elif change == 'mutable-bytes': payloads['model.glb'] = bytearray(payloads['model.glb'])
                    elif change == 'empty': payloads['model.glb'] = b''
                    elif change == 'oversized':
                        bounds.enter_context(mock.patch.object(f.assets, 'MAX_PAYLOAD', 8))
                    else:
                        bounds.enter_context(mock.patch.object(f.assets, 'MAX_TOTAL',
                            sum(map(len, payloads.values())) - 1))
                    with self.assertRaises(ValueError): f.package.build(payloads, target)
                f.gate.assert_not_called()
                self.assertFalse(target.exists())
                self.assertEqual(f.files(f.output), f.snapshot)

    def test_build_existing_destination_preserves_owner_bytes(self):
        for kind in ('file', 'directory', 'dangling-symlink'):
            with self.subTest(kind=kind), StorageFixture() as f:
                target = f.root / 'owned'
                if kind == 'file': target.write_bytes(b'owner')
                elif kind == 'directory':
                    target.mkdir()
                    (target / 'owner.bin').write_bytes(b'owner')
                else: target.symlink_to(f.root / 'not-created')
                with self.assertRaises(ValueError): f.package.build(f.payloads, target)
                f.gate.assert_not_called()
                if kind == 'file': self.assertEqual(target.read_bytes(), b'owner')
                elif kind == 'directory': self.assertEqual(f.files(target), {'owner.bin': b'owner'})
                else: self.assertEqual(target.readlink(), f.root / 'not-created')

    def test_manifest_rehash_cannot_substitute_selected_payload(self):
        with StorageFixture() as f:
            manifest = f.manifest()
            row = manifest['files'][0]
            raw = b'X' * row['decoded_bytes']
            packed = lzma.compress(raw)
            (f.output / row['path']).write_bytes(packed)
            row.update(byte_pin(packed), decoded_sha256=hashlib.sha256(raw).hexdigest())
            f.save_manifest(manifest)
            with self.assertRaises(ValueError):
                f.assets.verify(f.output)
            f.gate.assert_not_called()

    def test_rehashed_invalid_streams_refuse(self):
        for change in ('truncated', 'trailing', 'second-stream'):
            with self.subTest(change=change), StorageFixture() as f:
                row = f.manifest()['files'][0]
                packed = (f.output / row['path']).read_bytes()
                bad = {'truncated': packed[:-2], 'trailing': packed + b'X',
                       'second-stream': packed + lzma.compress(b'next')}[change]
                f.replace_stream(bad)
                with self.assertRaises(ValueError):
                    f.assets.verify(f.output)
                f.gate.assert_not_called()

    def test_manifest_roster_paths_types_and_pin_limits_refuse(self):
        changes = ('duplicate', 'missing', 'foreign-role', 'parent-path', 'absolute-path',
                   'bool-bytes', 'float-decoded', 'over-cap', 'wrong-sha', 'extra-key')
        for change in changes:
            with self.subTest(change=change), StorageFixture() as f:
                manifest = f.manifest()
                row = manifest['files'][0]
                if change == 'duplicate':
                    manifest['files'][1] = copy.deepcopy(row)
                elif change == 'missing':
                    manifest['files'].pop()
                elif change == 'foreign-role':
                    row['role'] = 'unexpected.json'
                elif change == 'parent-path':
                    row['path'] = '../escaped.xz'
                elif change == 'absolute-path':
                    row['path'] = '/escaped.xz'
                elif change == 'bool-bytes':
                    row['bytes'] = True
                elif change == 'float-decoded':
                    row['decoded_bytes'] = float(row['decoded_bytes'])
                elif change == 'over-cap':
                    row['bytes'] = f.assets.MAX_PAYLOAD + 1
                elif change == 'wrong-sha':
                    row['sha256'] = 'G' * 64
                else:
                    row['authority'] = True
                f.save_manifest(manifest)
                with self.assertRaises(ValueError):
                    f.assets.verify(f.output)
                f.gate.assert_not_called()

    def test_duplicate_nonfinite_and_oversized_manifest_json_refuse(self):
        for raw in (b'{"schema":1,"schema":2}', b'{"schema":NaN}', b'{"schema":Infinity}'):
            with self.subTest(raw=raw), StorageFixture() as f:
                (f.output / 'package.json').write_bytes(raw)
                with self.assertRaises(ValueError):
                    f.assets.verify(f.output)
        with StorageFixture() as f:
            with mock.patch.object(f.assets, 'MAX_MANIFEST', 8), self.assertRaises(ValueError):
                f.assets.verify(f.output)

    def test_aggregate_limit_refuses_before_decompressing_over_budget_member(self):
        with StorageFixture() as f:
            manifest = f.manifest()
            first = manifest['files'][0]
            limit = max(first['bytes'], first['decoded_bytes'])
            real = f.assets.unpack
            with mock.patch.object(f.assets, 'MAX_TOTAL', limit), \
                    mock.patch.object(f.assets, 'unpack', wraps=real) as unpack:
                with self.assertRaises(ValueError):
                    f.assets.verify(f.output)
                self.assertLessEqual(unpack.call_count, 1)

    def test_extra_missing_and_symlink_members_refuse(self):
        for change in ('extra', 'missing', 'symlink', 'dangling', 'extra-directory'):
            with self.subTest(change=change), StorageFixture() as f:
                path = f.output / f.manifest()['files'][0]['path']
                if change == 'extra':
                    (f.output / 'foreign.bin').write_bytes(b'owner')
                elif change == 'extra-directory':
                    (f.output / 'unlisted').mkdir()
                else:
                    path.unlink()
                    if change != 'missing':
                        outside = f.root / 'outside'
                        outside.write_bytes(b'owner')
                        path.symlink_to(outside if change == 'symlink' else f.root / 'absent')
                with self.assertRaises(ValueError):
                    f.assets.verify(f.output)
                f.gate.assert_not_called()

    def test_source_and_destination_symlink_ancestors_refuse(self):
        with StorageFixture() as f:
            alias = f.root / 'alias'
            alias.symlink_to(f.output, target_is_directory=True)
            with self.assertRaises(ValueError):
                f.assets.verify(alias)
            parent = f.root / 'destination-alias'
            parent.symlink_to(f.root, target_is_directory=True)
            with self.assertRaises(ValueError):
                f.assets.prepare(f.output, parent / 'installed')
            self.assertFalse(f.installed.exists())

    def test_existing_changed_output_is_preserved(self):
        for change in ('file', 'directory', 'dangling', 'decoded-change', 'extra-file'):
            with self.subTest(change=change), StorageFixture() as f:
                if change in ('decoded-change', 'extra-file'):
                    f.assets.prepare(f.output, f.installed)
                    path = f.installed / ('model.glb' if change == 'decoded-change' else 'owner.bin')
                    path.write_bytes(b'owner bytes')
                elif change == 'file':
                    f.installed.write_bytes(b'owner bytes')
                elif change == 'directory':
                    f.installed.mkdir()
                    (f.installed / 'owner.bin').write_bytes(b'owner bytes')
                else:
                    f.installed.symlink_to(f.root / 'absent')
                before = (f.installed.readlink() if f.installed.is_symlink() else
                          f.files(f.installed) if f.installed.is_dir() else f.installed.read_bytes())
                with self.assertRaises(ValueError):
                    f.assets.prepare(f.output, f.installed)
                after = (f.installed.readlink() if f.installed.is_symlink() else
                         f.files(f.installed) if f.installed.is_dir() else f.installed.read_bytes())
                self.assertEqual(after, before)
                self.assertEqual(f.files(f.output), f.snapshot)

    def test_concurrent_destination_is_not_replaced(self):
        for operation in ('build', 'prepare'):
            with self.subTest(operation=operation), StorageFixture() as f:
                destination = f.root / 'new-output'
                real = f.assets.io.install_new
                def race(staging, output):
                    Path(output).mkdir()
                    (Path(output) / 'owner.bin').write_bytes(b'concurrent owner')
                    real(staging, output)
                with mock.patch.object(f.assets.io, 'install_new', side_effect=race), \
                        self.assertRaises(OSError):
                    if operation == 'build':
                        f.package.build(f.payloads, destination)
                    else:
                        f.assets.prepare(f.output, destination)
                self.assertEqual(f.files(destination), {'owner.bin': b'concurrent owner'})
                self.assertEqual(f.files(f.output), f.snapshot)
                self.assertFalse(any(path.name.startswith('.stowed-') for path in f.root.iterdir()))

    def test_package_change_during_preparation_never_installs(self):
        with StorageFixture() as f:
            calls = 0
            def mutate(_payloads):
                nonlocal calls
                calls += 1
                if calls == 2:
                    (f.output / 'package.json').write_bytes(b'changed during final verification')
                return f.validation
            with mock.patch.object(f.assets.checks, 'validate_payloads', side_effect=mutate), \
                    self.assertRaises(ValueError):
                f.assets.prepare(f.output, f.installed)
            self.assertFalse(f.installed.exists())

    def test_invalid_build_and_validation_failure_leave_destination_untouched(self):
        with StorageFixture() as f:
            new = f.root / 'new-package'
            wrong = dict(f.payloads)
            wrong['model.glb'] = b'X' * len(wrong['model.glb'])
            with self.assertRaises(ValueError):
                f.package.build(wrong, new)
            self.assertFalse(new.exists())
            with mock.patch.object(f.assets.checks, 'validate_payloads',
                                   side_effect=ValueError('independent fixture refusal')), \
                    self.assertRaises(ValueError):
                f.assets.prepare(f.output, f.installed)
            self.assertFalse(f.installed.exists())
            self.assertEqual(f.files(f.output), f.snapshot)

    def test_prepared_aggregate_bound_includes_generated_receipt(self):
        with StorageFixture() as f:
            verified = f.assets.verify(f.output)
            files = f.assets.prepared_files(verified)
            exact_total = sum(map(len, files.values()))
            with mock.patch.object(f.assets, 'MAX_TOTAL', exact_total):
                self.assertEqual(f.assets.prepared_files(verified), files)
            with mock.patch.object(f.assets, 'MAX_TOTAL', exact_total - 1), \
                    self.assertRaises(ValueError):
                f.assets.prepared_files(verified)


if __name__ == "__main__":
    unittest.main()
