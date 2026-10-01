#!/usr/bin/env python3
"""Real selected motion bytes, malformed contracts and transactional preparation."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import operating_motion_spec as spec
import package_operating_motion as builder
import prepare_operating_motion as assets
from operating_motion_identity import RECIPE_SHA256


class MotionPackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="apsis-motion-test-")
        cls.root = Path(cls.temp.name)
        cls.source = ROOT / "assets/native/operating-motion-01"
        assets.verify(cls.source)
        cls.recipe = spec.decode((cls.source / assets.RECIPE).read_bytes(), spec.MAX_RECIPE)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def package(self, name):
        path = self.root / name
        shutil.copytree(self.source, path)
        return path

    def rejected(self, callback):
        with self.assertRaises((ValueError, OSError, TypeError)):
            callback()

    def rewrite_record(self, package, name, raw):
        (package / name).write_bytes(raw)
        manifest = json.loads((package / "package.json").read_text())
        for record in manifest["files"]:
            if record["path"] == name:
                record.update(bytes=len(raw), sha256=assets.digest(raw))
        (package / "package.json").write_bytes(spec.encode(manifest))

    def test_real_prepare_and_immutable_sources(self):
        before = {p.relative_to(self.source): assets.digest(p.read_bytes()) for p in self.source.rglob("*") if p.is_file()}
        output = self.root / "prepared-positive"
        receipt = assets.prepare(self.source, output)
        self.assertEqual(receipt["recipe_sha256"], RECIPE_SHA256)
        self.assertEqual((output / assets.RECIPE).read_bytes(), (self.source / assets.RECIPE).read_bytes())
        after = {p.relative_to(self.source): assets.digest(p.read_bytes()) for p in self.source.rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        inode = output.stat().st_ino
        self.rejected(lambda: assets.prepare(self.source, output))
        self.assertEqual(inode, output.stat().st_ino)

    def test_recipe_tamper_even_with_rehashed_receipt(self):
        package = self.package("changed-recipe")
        changed = copy.deepcopy(self.recipe)
        changed["craft"]["tracks"][0]["knots"][1][1] += .001
        self.rewrite_record(package, assets.RECIPE, spec.encode(changed))
        output = self.root / "refused-output"
        self.rejected(lambda: assets.prepare(package, output))
        self.assertFalse(output.exists())
        self.assertFalse(list(self.root.glob(".motion-prepare-*")))

    def test_source_oracle_and_tool_provenance_tamper(self):
        for name in ("sources/operating-motion-fixtures-01/craft-combined.json", assets.PROVENANCE):
            with self.subTest(name=name):
                package = self.package("changed-" + Path(name).name)
                value = json.loads((package / name).read_text())
                if name == assets.PROVENANCE:
                    value["tool_sha256"]["operating_motion_spec.py"] = "0" * 64
                else:
                    value["poses"][0]["groups"]["roof_port"]["godot_world_delta_rows"][0][3] = 10
                self.rewrite_record(package, name, spec.encode(value))
                self.rejected(lambda: assets.verify(package))

    def test_manifest_paths_sizes_and_roster(self):
        for index, transform in enumerate((lambda m: m["files"][0].update(path="../escape"),
                                           lambda m: m["files"][0].update(bytes=True),
                                           lambda m: m["files"].append(m["files"][0]),
                                           lambda m: m.update(extra="untrusted"))):
            with self.subTest(index=index):
                package = self.package("manifest-" + str(index))
                manifest = json.loads((package / "package.json").read_text())
                transform(manifest)
                (package / "package.json").write_bytes(spec.encode(manifest))
                self.rejected(lambda: assets.verify(package))
        package = self.package("extra-entry")
        (package / "unknown").mkdir()
        self.rejected(lambda: assets.verify(package))

    def test_symlinks_fifo_and_ancestor_links(self):
        package = self.package("linked-source")
        (package / assets.RECIPE).unlink()
        (package / assets.RECIPE).symlink_to(self.source / assets.RECIPE)
        self.rejected(lambda: assets.verify(package))
        linked = self.root / "ancestor-link"
        linked.symlink_to(self.source, target_is_directory=True)
        self.rejected(lambda: assets.read_regular(linked / assets.RECIPE, spec.MAX_RECIPE))
        fifo = self.root / "fifo"
        os.mkfifo(fifo)
        self.rejected(lambda: assets.read_regular(fifo, spec.MAX_RECIPE))

    def test_parser_depth_bytes_duplicates_and_nonfinite(self):
        for raw in (b'{"a":1,"a":2}', b'{"x":NaN}', b'{"x":Infinity}', b'{"x":1e9999}',
                    b'[' * 17 + b'0' + b']' * 17, b'\xff', b' ' * (spec.MAX_RECIPE + 1)):
            with self.subTest(raw=raw[:30]):
                self.rejected(lambda: spec.decode(raw, spec.MAX_RECIPE))
        # Quotes/escapes in a scalar do not count as nested structure.
        self.assertEqual(spec.decode(b'{"a":"[[[\\\"]]]"}', 128), {"a": '[[["]]]'})

    def test_structural_numeric_and_ancestry_refusals(self):
        mutations = [lambda r: r.update(version=True),
                     lambda r: r["craft"]["nodes"][0].update(parent=0),
                     lambda r: r["craft"]["nodes"][0].update(parent=True),
                     lambda r: r["craft"]["nodes"][0].update(group="missing"),
                     lambda r: r["craft"]["nodes"][1].update(group=r["craft"]["nodes"][0]["group"]),
                     lambda r: r["craft"]["nodes"][0]["rest_world"][0].__setitem__(0, -1),
                     lambda r: r["craft"]["nodes"][0]["rest_world"][0].__setitem__(0, 0),
                     lambda r: r["craft"]["nodes"][0].update(location_metres=[0, 0]),
                     lambda r: r["craft"]["nodes"][0]["location_metres"].__setitem__(0, float("nan")),
                     lambda r: r["craft"]["tracks"][0].update(axis=True),
                     lambda r: r["craft"]["tracks"][0]["knots"][0].__setitem__(0, True),
                     lambda r: r["craft"]["tracks"][0]["knots"][1].__setitem__(0, 0),
                     lambda r: r["craft"]["tracks"][0].update(extra=0),
                     lambda r: r["station"]["tracks"].pop()]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                value = copy.deepcopy(self.recipe)
                mutate(value)
                self.rejected(lambda: spec.validate_recipe(value))

    def test_concurrent_empty_destination_not_replaced(self):
        output = self.root / "racing-output"
        original = assets.install_new
        marker = {}
        def race(staging, destination):
            destination.mkdir()
            marker["inode"] = destination.stat().st_ino
            return original(staging, destination)
        with patch.object(assets, "install_new", race):
            self.rejected(lambda: assets.prepare(self.source, output))
        self.assertEqual(marker["inode"], output.stat().st_ino)
        self.assertEqual(list(output.iterdir()), [])
        self.assertFalse(list(self.root.glob(".motion-prepare-*")))

    def test_cached_snapshot_prevents_hash_copy_race(self):
        package = self.package("snapshot-source")
        original_verify = assets.verify
        def changed_after_verify(source, repository=None):
            result = original_verify(source, repository)
            (source / assets.RECIPE).write_bytes(b"changed after verified snapshot")
            return result
        output = self.root / "snapshot-output"
        with patch.object(assets, "verify", changed_after_verify):
            assets.prepare(package, output)
        self.assertEqual(assets.digest((output / assets.RECIPE).read_bytes()), RECIPE_SHA256)

    def test_packager_round_trip_and_source_refusal(self):
        output = self.root / "rebuilt-package"
        builder.build(self.source / "sources", output)
        assets.verify(output)
        source = self.root / "changed-builder-source"
        shutil.copytree(self.source / "sources", source)
        (source / "wayfarer-motion-proposal/recipe.json").write_bytes(b"{}")
        target = self.root / "failed-build"
        self.rejected(lambda: builder.build(source, target))
        self.assertFalse(target.exists())
        self.assertFalse(list(self.root.glob(".motion-package-*")))

    def test_copied_tools_require_explicit_dependency_repository(self):
        copied = self.root / "copied-tools"
        copied.mkdir()
        for name in (*assets.TOOL_NAMES, "wayfarer_operating_spec.py"):
            shutil.copyfile(ROOT / "tools" / name, copied / name)
        output = self.root / "portable-output"
        command = [sys.executable, str(copied / "prepare_operating_motion.py"),
                   "--package", str(self.source), "--output", str(output)]
        refused = subprocess.run(command, capture_output=True, text=True, timeout=20)
        self.assertNotEqual(refused.returncode, 0)
        self.assertFalse(output.exists())
        completed = subprocess.run(command + ["--repository", str(ROOT)],
                                   capture_output=True, text=True, timeout=20)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertEqual(assets.digest((output / assets.RECIPE).read_bytes()), RECIPE_SHA256)
        linked = self.root / "linked-repository"
        linked.symlink_to(ROOT, target_is_directory=True)
        self.rejected(lambda: assets.verify(self.source, linked))
        self.rejected(lambda: assets.verify(self.source, self.root / "missing-repository"))


if __name__ == "__main__":
    unittest.main()
