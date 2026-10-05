#!/usr/bin/env python3
"""Exercise composite publication failures with tiny constituent fixtures."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import prepare_freedom_native_assets as assets


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repository = self.root / 'repository'
        self.output = self.root / 'prepared'
        self.model = b'finite-fixture-model'
        self.manifest = {'models': [{'output': 'starter.glb',
                                    'bytes': len(self.model),
                                    'sha256': hashlib.sha256(self.model).hexdigest()}],
                         'metadata': []}
        self.verified = (self.repository, self.manifest, 'a' * 64, {})
        self.receipt = {'schema': assets.starter.SCHEMA, 'package_sha256': 'a' * 64}
        self.addCleanup(patch.stopall)
        patch.object(assets.starter, 'verify', return_value=self.verified).start()
        patch.object(assets.starter, 'prepare', side_effect=self.make_starter).start()
        patch.object(assets.operating, 'prepare', side_effect=self.companion).start()
        patch.object(assets.stowed, 'prepare', side_effect=self.companion).start()

    def make_starter(self, package, destination):
        destination.mkdir()
        (destination / 'starter.glb').write_bytes(self.model)
        (destination / 'prepared.json').write_text(json.dumps(self.receipt))

    def companion(self, package, destination):
        if destination.exists():
            assets.starter.require(destination.is_dir() and
                                   set(destination.iterdir()) == {destination / 'model'} and
                                   (destination / 'model').read_bytes() == b'companion',
                                   'Changed companion')
        else:
            destination.mkdir()
            (destination / 'model').write_bytes(b'companion')

    def snapshot(self):
        return {path.relative_to(self.output).as_posix(): path.read_bytes()
                for path in self.output.rglob('*') if path.is_file()}

    def test_complete_install_and_read_only_reuse(self):
        assets.prepare(self.repository, self.output)
        before = self.snapshot()
        assets.prepare(self.repository, self.output)
        self.assertEqual(before, self.snapshot())
        self.assertEqual(set(self.output.iterdir()), {
            self.output / name for name in ('starter.glb', 'prepared.json', 'operating', 'stowed')})

    def test_late_companion_failure_publishes_nothing(self):
        with patch.object(assets.stowed, 'prepare', side_effect=ValueError('late failure')):
            with self.assertRaisesRegex(ValueError, 'late failure'):
                assets.prepare(self.repository, self.output)
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.root.glob('.freedom-native-*')), [])

    def test_concurrent_destination_is_not_replaced(self):
        install = assets.operating.install_new

        def race(candidate, destination):
            destination.mkdir()
            (destination / 'winner').write_bytes(b'preserve me')
            install(candidate, destination)

        with patch.object(assets.operating, 'install_new', side_effect=race):
            with self.assertRaises((ValueError, OSError)):
                assets.prepare(self.repository, self.output)
        self.assertEqual(self.snapshot(), {'winner': b'preserve me'})
        self.assertEqual(list(self.root.glob('.freedom-native-*')), [])

    def test_changed_existing_companion_is_not_repaired(self):
        assets.prepare(self.repository, self.output)
        (self.output / 'stowed/model').write_bytes(b'changed')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Changed companion'):
            assets.prepare(self.repository, self.output)
        self.assertEqual(before, self.snapshot())

    def test_missing_companion_is_not_installed_into_existing_output(self):
        assets.prepare(self.repository, self.output)
        (self.output / 'stowed/model').unlink()
        (self.output / 'stowed').rmdir()
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Unexpected native asset entry'):
            assets.prepare(self.repository, self.output)
        self.assertEqual(before, self.snapshot())

    def test_changed_starter_and_unknown_entries_refuse_without_writes(self):
        assets.prepare(self.repository, self.output)
        (self.output / 'starter.glb').write_bytes(b'wrong')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'starter model changed'):
            assets.prepare(self.repository, self.output)
        self.assertEqual(before, self.snapshot())
        (self.output / 'starter.glb').write_bytes(self.model)
        (self.output / 'unknown').write_bytes(b'extra')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'Unexpected native asset entry'):
            assets.prepare(self.repository, self.output)
        self.assertEqual(before, self.snapshot())


if __name__ == '__main__':
    unittest.main()
