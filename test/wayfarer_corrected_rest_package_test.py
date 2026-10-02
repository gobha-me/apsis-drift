"""Portable package-publication controls; heavy geometry gate is isolated.

The real member readers, frozen-reference preflight, file validator, and Linux
atomic no-replace installation remain active. Synthetic files have real hashes;
these fixtures are not geometry, Blender, or runtime admission evidence.
"""
from contextlib import ExitStack
import copy
import ctypes
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import wayfarer_corrected_rest_package as package


def raw_json(value):
    return (json.dumps(value, sort_keys=True, allow_nan=False) + '\n').encode()


def identity(relative, data):
    return {'path': relative, 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest()}


class Fixture:
    """An isolated source, reference, dependencies, and twelve staging payloads."""
    def __enter__(self):
        self.stack = ExitStack()
        self.root = Path(self.stack.enter_context(tempfile.TemporaryDirectory()))
        self.source = self.root / 'authoring' / 'source.blend'
        self.reference = self.root / 'reference'
        self.tools = self.root / 'dependencies'
        self.parent = self.root / 'delivery'
        for directory in (self.source.parent, self.reference, self.tools, self.parent):
            directory.mkdir()
        self.staging = self.parent / 'private-stage'
        self.staging.mkdir()
        self.output = self.parent / 'corrected-package'
        self.source_bytes = b'Fixture immutable source bytes; not a Blender model.\n'
        self.source.write_bytes(self.source_bytes)
        source_sha = hashlib.sha256(self.source_bytes).hexdigest()
        self.licenses = {('licenses/LICENSE-%d.md' % index):
                         ('Synthetic inherited license %d\n' % index).encode()
                         for index in range(5)}
        members = list(package.original.NODES) + ['Preserved fixture %02d' % n
                                                 for n in range(57)]
        metadata = raw_json({'groups': [{'id': 'seat_lift', 'source_objects': members}]})
        reference_files = dict(self.licenses)
        reference_files['metadata/wayfarer-operating-02.json'] = metadata
        reference_files['model/fixture.chunk'] = b'Pinned reference chunk; no decompression needed.\n'
        reference_model_sha = hashlib.sha256(b'Fixture decoded reference model').hexdigest()
        self.manifest = {
            'package_id': 'wayfarer-operating-02',
            'model': {'sha256': reference_model_sha, 'source_sha256': source_sha,
                      'chunks': ['model/fixture.chunk']},
            'files': [identity(name, data) for name, data in sorted(reference_files.items())],
        }
        for name, data in reference_files.items():
            path = self.reference / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        manifest_bytes = raw_json(self.manifest)
        (self.reference / 'package.json').write_bytes(manifest_bytes)
        pins = {'SOURCE_SHA256': source_sha, 'MODEL_SHA256': reference_model_sha,
                'PACKAGE_SHA256': hashlib.sha256(manifest_bytes).hexdigest(),
                'METADATA_SHA256': hashlib.sha256(metadata).hexdigest()}
        for name, value in pins.items():
            self.stack.enter_context(mock.patch.object(package.original, name, value))
            self.stack.enter_context(mock.patch.object(package.checks, name, value, create=True))
        license_pins = {name: (len(data), hashlib.sha256(data).hexdigest())
                        for name, data in self.licenses.items()}
        self.stack.enter_context(mock.patch.object(package.checks, 'LICENSES', license_pins))
        self.stack.enter_context(mock.patch.object(package, 'TOOLS', self.tools))
        for name in package.TOOL_NAMES:
            (self.tools / name).write_bytes(('Synthetic frozen dependency: ' + name).encode())
        self.dependencies = package.dependency_snapshot()
        self.reports = {}
        for mode in ('corrected_closed', 'original_rest', 'original_posed'):
            self.reports[mode] = {
                'mode': mode, 'source_sha256': source_sha,
                'source_file_unchanged': True,
                'all_1746_original_signatures_restored': True,
                'restoration': {'source_bytes_preserved': True, 'master_saved': False},
            }
        self.reports['corrected_closed'].update({
            'blender_version': '5.2.2',
            'declared_edits': {'registered_ribbon_recipe': 'fixed source-specific fixture'},
            'manifold': {'registered_transform': 'fixed fixture'},
            'connector_records': [{'source_object': 'fixed fixture connector'}],
        })
        payload_data = {name: raw_json({'fixture': name, 'runtime_admission': False})
                        for name in package.PAYLOADS}
        for name in package.PAYLOADS:
            if name.endswith('.glb'):
                payload_data[name] = ('Nonempty isolated model payload ' + name).encode()
        payload_data['producer.json'] = raw_json(self.reports['corrected_closed'])
        payload_data['evidence/original-rest.json'] = raw_json(self.reports['original_rest'])
        payload_data['evidence/original-posed.json'] = raw_json(self.reports['original_posed'])
        for name, data in payload_data.items():
            path = self.staging / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.payload_bytes = payload_data
        return self

    def __exit__(self, *arguments):
        return self.stack.__exit__(*arguments)

    def finalize(self, validator=None):
        if validator is None:
            validator = lambda _stage, _reference: {'pass': True, 'isolated_test_gate': True}
        with mock.patch.object(package.checks, 'validate_derivative', side_effect=validator) as gate:
            self.gate = gate
            return package.finalize(self.staging, self.output, self.source, self.reference,
                                    copy.deepcopy(self.manifest), self.dependencies)


ATOMIC_SUPPORTED = sys.platform.startswith('linux') and hasattr(ctypes.CDLL(None), 'renameat2')


@unittest.skipUnless(ATOMIC_SUPPORTED, 'Linux atomic no-replace installation is unavailable')
class PackagePublicationTests(unittest.TestCase):
    def assert_unpublished(self, fixture, source_unchanged=True):
        self.assertFalse(os.path.lexists(fixture.output))
        self.assertTrue(fixture.staging.is_dir())
        if source_unchanged:
            self.assertEqual(fixture.source.read_bytes(), fixture.source_bytes)

    def test_success_installs_complete_sealed_package_atomically(self):
        with Fixture() as f:
            real_install = package.original.install_no_replace
            transitions = []

            def observe_install(staging, output):
                self.assertTrue(Path(staging).is_dir())
                self.assertFalse(os.path.lexists(output))
                self.assertTrue((Path(staging) / 'package.json').is_file())
                transitions.append('before')
                real_install(staging, output)
                self.assertFalse(Path(staging).exists())
                self.assertTrue(Path(output).is_dir())
                transitions.append('after')

            with mock.patch.object(package.original, 'install_no_replace', side_effect=observe_install):
                result = f.finalize()
            self.assertEqual(transitions, ['before', 'after'])
            self.assertTrue(result['pass'])
            f.gate.assert_called_once()
            manifest = json.loads((f.output / 'package.json').read_bytes())
            expected = set(package.PAYLOADS) | {'provenance.json'} | set(f.licenses)
            self.assertEqual({entry['path'] for entry in manifest['files']}, expected)
            self.assertEqual(len(manifest['files']), 18)
            actual = {str(p.relative_to(f.output)) for p in f.output.rglob('*') if p.is_file()}
            self.assertEqual(actual, expected | {'package.json'})
            package.checks.validate_files(f.output, manifest, expected)
            for name, data in {**f.payload_bytes, **f.licenses}.items():
                self.assertEqual((f.output / name).read_bytes(), data)
            provenance = json.loads((f.output / 'provenance.json').read_bytes())
            self.assertEqual(provenance['helpers'], {name: value for name, value in f.dependencies.items()
                                                    if name != 'export_wayfarer_corrected_rest.py'})
            self.assertEqual(provenance['source']['file'], 'assets/visual/hopper-craft-09.blend')
            self.assertFalse(provenance['source_restoration']['source_master_saved'])
            self.assertEqual(provenance['namespace'], package.checks.NAMESPACE)
            self.assertEqual(provenance['limits'], package.geometry.LIMITS)
            self.assertIn('No release/opening curve, occupied body, seat load, reach or boarding route.',
                          provenance['limits'])
            self.assertNotIn(str(f.root), json.dumps(provenance))
            for name in ('contact.json', 'finite-volume-proof.json', 'static-composite.json'):
                self.assertFalse(json.loads((f.output / name).read_bytes())['runtime_admission'])
            self.assertEqual(f.source.read_bytes(), f.source_bytes)

    def test_rejecting_or_raising_independent_gate_never_installs(self):
        for outcome in ('false', 'raise', 'integer-pass'):
            with self.subTest(outcome=outcome), Fixture() as f:
                def gate(_stage, _reference):
                    if outcome == 'raise':
                        raise ValueError('Independent fixture proof refusal')
                    return {'pass': 1 if outcome == 'integer-pass' else False}
                with self.assertRaises(ValueError):
                    f.finalize(gate)
                self.assert_unpublished(f)

    def test_payload_or_provenance_change_during_gate_refuses(self):
        for relative in ('model.glb', 'contact.json', 'finite-volume-proof.json',
                         'evidence/original-rest.glb', 'provenance.json'):
            with self.subTest(payload=relative), Fixture() as f:
                def gate(stage, _reference):
                    path = Path(stage) / relative
                    raw = path.read_bytes()
                    path.write_bytes(bytes([raw[0] ^ 1]) + raw[1:])
                    return {'pass': True}
                with self.assertRaisesRegex(ValueError, 'byte identity changed'):
                    f.finalize(gate)
                self.assert_unpublished(f)

    def test_staging_license_change_during_gate_refuses(self):
        with Fixture() as f:
            name = next(iter(f.licenses))
            def gate(stage, _reference):
                path = Path(stage) / name
                raw = path.read_bytes()
                path.write_bytes(bytes([raw[0] ^ 1]) + raw[1:])
                return {'pass': True}
            with self.assertRaisesRegex(ValueError, 'byte identity changed'):
                f.finalize(gate)
            self.assert_unpublished(f)

    def test_dependency_or_source_change_during_gate_refuses(self):
        for target in ('dependency', 'source'):
            with self.subTest(target=target), Fixture() as f:
                def gate(_stage, _reference):
                    path = f.source if target == 'source' else f.tools / package.TOOL_NAMES[0]
                    raw = path.read_bytes()
                    path.write_bytes(bytes([raw[0] ^ 1]) + raw[1:])
                    return {'pass': True}
                with self.assertRaisesRegex(ValueError, 'changed during export'):
                    f.finalize(gate)
                self.assert_unpublished(f, source_unchanged=target != 'source')
                if target == 'source':
                    self.assertNotEqual(f.source.read_bytes(), f.source_bytes)

    def test_manifest_change_during_gate_refuses_even_with_valid_payloads(self):
        with Fixture() as f:
            def gate(stage, _reference):
                path = Path(stage) / 'package.json'
                manifest = json.loads(path.read_bytes())
                manifest['id'] = 'concurrent-unvalidated-package'
                path.write_bytes(raw_json(manifest))
                return {'pass': True}
            with self.assertRaisesRegex(ValueError, 'Sealed package manifest changed'):
                f.finalize(gate)
            self.assert_unpublished(f)

    def test_missing_or_tampered_reference_license_refuses_before_gate(self):
        for change in ('missing', 'same-size-tamper'):
            with self.subTest(change=change), Fixture() as f:
                path = f.reference / next(iter(f.licenses))
                if change == 'missing':
                    path.unlink()
                else:
                    raw = path.read_bytes()
                    path.write_bytes(bytes([raw[0] ^ 1]) + raw[1:])
                with self.assertRaises(ValueError):
                    f.finalize()
                self.assert_unpublished(f)
                f.gate.assert_not_called()

    def test_missing_extra_or_symlink_initial_payload_refuses(self):
        for change in ('missing', 'extra', 'dangling', 'symlink'):
            with self.subTest(change=change), Fixture() as f:
                path = f.staging / 'model.glb'
                if change == 'missing':
                    path.unlink()
                elif change == 'extra':
                    (f.staging / 'unlisted.bin').write_bytes(b'Not part of the snapshot')
                else:
                    path.unlink()
                    path.symlink_to(f.root / 'absent' if change == 'dangling' else f.source)
                with self.assertRaises(ValueError):
                    f.finalize()
                self.assert_unpublished(f)
                f.gate.assert_not_called()

    def test_symlink_dangling_or_unlisted_change_during_gate_refuses(self):
        for change in ('dangling', 'symlink', 'unlisted'):
            with self.subTest(change=change), Fixture() as f:
                def gate(stage, _reference):
                    if change == 'unlisted':
                        (Path(stage) / 'late.bin').write_bytes(b'Unvalidated late file')
                    else:
                        path = Path(stage) / 'model.glb'
                        path.unlink()
                        path.symlink_to(f.root / 'missing' if change == 'dangling' else f.source)
                    return {'pass': True}
                with self.assertRaises(ValueError):
                    f.finalize(gate)
                self.assert_unpublished(f)

    def test_destination_race_preserves_directory_file_or_symlink(self):
        for kind in ('empty-directory', 'occupied-directory', 'file', 'dangling-symlink'):
            with self.subTest(kind=kind), Fixture() as f:
                def gate(_stage, _reference):
                    if kind.endswith('directory'):
                        f.output.mkdir()
                        if kind == 'occupied-directory':
                            (f.output / 'owner.bin').write_bytes(b'Concurrent owner data')
                    elif kind == 'file':
                        f.output.write_bytes(b'Concurrent owner data')
                    else:
                        f.output.symlink_to(f.root / 'missing-owner-target')
                    return {'pass': True}
                with self.assertRaises(FileExistsError):
                    f.finalize(gate)
                self.assertTrue(os.path.lexists(f.output))
                self.assertTrue(f.staging.is_dir())
                self.assertEqual(f.source.read_bytes(), f.source_bytes)
                if kind == 'occupied-directory':
                    self.assertEqual((f.output / 'owner.bin').read_bytes(), b'Concurrent owner data')
                elif kind == 'empty-directory':
                    self.assertEqual(list(f.output.iterdir()), [])
                elif kind == 'file':
                    self.assertEqual(f.output.read_bytes(), b'Concurrent owner data')
                else:
                    self.assertTrue(f.output.is_symlink())
                    self.assertEqual(f.output.readlink(), f.root / 'missing-owner-target')

    def test_preexisting_output_refuses_without_gate_or_overwrite(self):
        for kind in ('directory', 'file', 'dangling-symlink'):
            with self.subTest(kind=kind), Fixture() as f:
                if kind == 'directory':
                    f.output.mkdir()
                    (f.output / 'owner.bin').write_bytes(b'Existing owner data')
                elif kind == 'file':
                    f.output.write_bytes(b'Existing owner data')
                else:
                    f.output.symlink_to(f.root / 'absent-owner-target')
                with self.assertRaisesRegex(ValueError, 'Output already exists'):
                    f.finalize()
                f.gate.assert_not_called()
                self.assertTrue(f.staging.is_dir())
                self.assertEqual(f.source.read_bytes(), f.source_bytes)
                if kind == 'directory':
                    self.assertEqual((f.output / 'owner.bin').read_bytes(), b'Existing owner data')
                elif kind == 'file':
                    self.assertEqual(f.output.read_bytes(), b'Existing owner data')
                else:
                    self.assertEqual(f.output.readlink(), f.root / 'absent-owner-target')

    def test_staging_must_be_distinct_non_symlink_sibling(self):
        for change in ('same-output', 'non-sibling', 'symlink'):
            with self.subTest(change=change), Fixture() as f:
                if change == 'same-output':
                    f.output = f.staging
                elif change == 'non-sibling':
                    moved = f.root / 'other-stage'
                    f.staging.rename(moved)
                    f.staging = moved
                else:
                    real = f.parent / 'actual-stage'
                    f.staging.rename(real)
                    f.staging.symlink_to(real, target_is_directory=True)
                with self.assertRaisesRegex(ValueError, 'Private staging must be'):
                    f.finalize()
                f.gate.assert_not_called()
                self.assertEqual(f.source.read_bytes(), f.source_bytes)

    def test_incoherent_source_snapshot_or_blender_version_refuses(self):
        for field, value, baseline in (
            ('mode', 'wrong_snapshot', True),
            ('source_file_unchanged', False, True),
            ('all_1746_original_signatures_restored', False, True),
            ('source_sha256', '0' * 64, False),
            ('blender_version', 'unqualified-version', False),
        ):
            with self.subTest(field=field, baseline=baseline), Fixture() as f:
                relative = 'evidence/original-rest.json' if baseline else 'producer.json'
                data = json.loads((f.staging / relative).read_bytes())
                data[field] = value
                (f.staging / relative).write_bytes(raw_json(data))
                with self.assertRaises(ValueError):
                    f.finalize()
                self.assert_unpublished(f)
                f.gate.assert_not_called()

    def test_wrong_reference_manifest_or_dependency_roster_refuses(self):
        for change in ('manifest', 'missing-dependency', 'changed-dependency'):
            with self.subTest(change=change), Fixture() as f:
                if change == 'manifest':
                    f.manifest['unregistered'] = True
                elif change == 'missing-dependency':
                    f.dependencies.pop(package.TOOL_NAMES[-1])
                else:
                    (f.tools / package.TOOL_NAMES[-1]).write_bytes(b'Changed before source capture')
                with self.assertRaises(ValueError):
                    f.finalize()
                self.assert_unpublished(f)
                f.gate.assert_not_called()


class WriteNewTests(unittest.TestCase):
    def test_exclusive_json_write_never_overwrites_file_or_dangling_symlink(self):
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            existing = root / 'existing.json'
            existing.write_bytes(b'Owner bytes')
            dangling = root / 'dangling.json'
            dangling.symlink_to(root / 'missing.json')
            for path in (existing, dangling):
                with self.subTest(path=path.name), self.assertRaises(FileExistsError):
                    package.write_new(path, {'unvalidated': True})
            self.assertEqual(existing.read_bytes(), b'Owner bytes')
            self.assertTrue(dangling.is_symlink())
            self.assertFalse((root / 'missing.json').exists())
            nonfinite = root / 'nonfinite.json'
            with self.assertRaises(ValueError):
                package.write_new(nonfinite, {'value': float('nan')})
            self.assertFalse(nonfinite.exists())
            fresh = root / 'fresh.json'
            package.write_new(fresh, {'runtime_admission': False})
            self.assertEqual(json.loads(fresh.read_bytes()), {'runtime_admission': False})


if __name__ == '__main__':
    unittest.main()
