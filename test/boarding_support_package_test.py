#!/usr/bin/env python3
"""Real source attribution, geometry controls and immutable atomic package admission."""
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
import boarding_support_spec as spec
import package_boarding_support as builder
import prepare_boarding_support as assets
from boarding_support_identity import ENGINE_BINDINGS, IDENTITIES, RUNTIME_SHA256


class BoardingSupportPackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="apsis-boarding-test-")
        cls.root = Path(cls.temp.name)
        cls.source = ROOT / "assets/native/boarding-support-01"
        assets.verify(cls.source)
        cls.runtime = spec.decode((cls.source / assets.RUNTIME).read_bytes(), spec.MAX_RUNTIME)
        cls.support = spec.decode((cls.source / "sources/wayfarer-boarding-support-01/support.json").read_bytes(), spec.MAX_SOURCE)
        cls.contact = spec.decode((ROOT / "assets/native/wayfarer-operating-02/metadata/contact.json").read_bytes(), spec.MAX_CONTACT)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def package(self, name):
        output = self.root / name
        shutil.copytree(self.source, output)
        return output

    def refused(self, callback):
        with self.assertRaises((ValueError, OSError, TypeError)):
            callback()

    def rewrite_record(self, package, name, raw):
        (package / name).write_bytes(raw)
        manifest = json.loads((package / "package.json").read_text())
        for record in manifest["files"]:
            if record["path"] == name:
                record.update(bytes=len(raw), sha256=assets.digest(raw))
        (package / "package.json").write_bytes(spec.encode(manifest))

    def test_real_source_geometry_and_once_only_partition(self):
        value = spec.construct_runtime(self.support, self.contact)
        self.assertEqual(value, self.runtime)
        self.assertEqual(assets.digest(spec.encode(value)), RUNTIME_SHA256)
        covered = 0
        for gi, group in enumerate(value["groups"]):
            intervals = value["objects"][group["object_start"]:group["object_start"] + group["object_count"]]
            cursor = 0
            for obj in intervals:
                self.assertEqual(obj["group"], gi)
                self.assertEqual(obj["triangle_start"], cursor)
                cursor += obj["triangle_count"]
            self.assertEqual(cursor, group["triangle_count"])
            covered += cursor
        self.assertEqual(covered, 475212)
        self.assertEqual(len(value["faces"]), 1322)
        self.assertEqual(len(value["vertices"]), 1217)
        refs = [i for p in value["patches"] for i in p["selected_faces"]]
        self.assertEqual(len(refs), 1474)
        self.assertLess(len(set(refs)), len(refs))  # Different role patches intentionally overlap.
        self.assertEqual(sum(p["opposite_winding_triangles"] or 0 for p in value["patches"]), 233)
        self.assertEqual(sum(p["contact_side_direction_rest"] is None for p in value["patches"]), 20)
        # Source matrices remain affine provenance; scaled columns are not another runtime transform.
        actual_scales = [o["source_corrected_world_rows"][2][2] for o in self.support["objects"]
                         if abs(o["source_corrected_world_rows"][2][2] - 1) > 1e-5 and
                         o["group"] in ("craft_seat_lift", "craft_seat_swivel")]
        self.assertTrue(any(abs(x - 1.7142857313156128) < 1e-12 for x in actual_scales))
        self.assertTrue(any(abs(x - 1.5714285373687744) < 1e-12 for x in actual_scales))
        self.assertNotIn("source_corrected_world_rows", value["objects"][0])

    def test_real_prepare_unchanged_sources_and_existing_output(self):
        before = {p.relative_to(self.source): assets.digest(p.read_bytes()) for p in self.source.rglob("*") if p.is_file()}
        output = self.root / "positive"
        receipt = assets.prepare(self.source, output)
        self.assertEqual(receipt["runtime_sha256"], RUNTIME_SHA256)
        self.assertEqual((output / assets.RUNTIME).read_bytes(), (self.source / assets.RUNTIME).read_bytes())
        after = {p.relative_to(self.source): assets.digest(p.read_bytes()) for p in self.source.rglob("*") if p.is_file()}
        self.assertEqual(before, after)
        inode = output.stat().st_ino
        self.refused(lambda: assets.prepare(self.source, output))
        self.assertEqual(inode, output.stat().st_ino)

    def test_rehashed_runtime_source_provenance_refuse(self):
        mutations = ((assets.RUNTIME, lambda v: v["objects"][0].update(source_object="finite renamed object")),
                     ("sources/wayfarer-boarding-support-01/support.json", lambda v: v["objects"][0].update(default_collision="permitted")),
                     (assets.PROVENANCE, lambda v: v["tool_sha256"].update(boarding_support_spec="0" * 64)))
        for index, (name, mutate) in enumerate(mutations):
            with self.subTest(name=name):
                package = self.package("tamper-" + str(index))
                value = json.loads((package / name).read_bytes())
                mutate(value)
                self.rewrite_record(package, name, spec.encode(value))
                output = self.root / ("tamper-output-" + str(index))
                self.refused(lambda: assets.prepare(package, output))
                self.assertFalse(output.exists())
        self.assertFalse(list(self.root.glob(".boarding-prepare-*")))

    def test_closed_structural_and_selected_geometry_refusals(self):
        changes = [lambda v: v.update(version=True), lambda v: v.update(unknown=0),
                   lambda v: v["counts"].update(selected_faces=True),
                   lambda v: v["groups"][0].update(motion_group="ladder_base"),
                   lambda v: v["objects"][0].update(triangle_start=1),
                   lambda v: v["objects"][0].update(triangle_count=2**64),
                   lambda v: v["objects"][0].update(default_collision="allowed"),
                   lambda v: v["objects"][1].update(source_object=v["objects"][0]["source_object"]),
                   lambda v: v["vertices"][0].update(source_vertex=True),
                   lambda v: v["vertices"][0].update(position_micrometres=[0, 0]),
                   lambda v: v["vertices"][0]["position_micrometres"].__setitem__(0, .1),
                   lambda v: v["faces"][0]["vertices"].__setitem__(0, len(v["vertices"])),
                   lambda v: v["faces"][1].update(source_triangle=v["faces"][0]["source_triangle"]),
                   lambda v: v["faces"][0].update(vertices=[0, 0, 0]),
                   lambda v: v["patches"][0].update(body_contact="boot sole"),
                   lambda v: v["patches"][0].update(surface_area_m2=1),
                   lambda v: v["patches"][0]["bounds_rest_m"][0].__setitem__(0, 0),
                   lambda v: v["patches"][0].update(representative_triangle=2**64),
                   lambda v: v["patches"][0].update(representative_barycentric=[2, -1, 0]),
                   lambda v: v["patches"][0].update(contact_side_direction_rest=[0, 1, 0]),
                   lambda v: v["patches"][0]["required_point_predicate"].update(end_margin_m=0),
                   lambda v: v["patches"][0]["required_point_predicate"].update(unit_axis_rest=[0, 0, 0]),
                   lambda v: v["patches"][0]["required_point_predicate"].update(inclusive_interval_m=[1, -1]),
                   lambda v: v["identities"].update(operating_motion_sha256="0" * 64)]
        for index, change in enumerate(changes):
            with self.subTest(index=index):
                value = copy.deepcopy(self.runtime)
                change(value)
                self.refused(lambda: spec.validate_runtime(value))
        value = copy.deepcopy(self.runtime)
        patch_value = value["patches"][0]
        obj = value["objects"][patch_value["object"]]
        patch_value["triangle_ranges"][0][0] = obj["triangle_start"] + obj["triangle_count"]
        self.refused(lambda: spec.validate_runtime(value))

    def test_published_raw_source_controls_and_scaled_provenance(self):
        patch_owner = next(i for i, obj in enumerate(self.support["objects"]) if obj["candidate_patches"])
        changes = [lambda s: s["objects"][0].update(triangle_start=1),
                   lambda s: s["objects"][0]["source_corrected_world_rows"][0].__setitem__(0, float("nan")),
                   lambda s: s["objects"][patch_owner]["candidate_patches"][0]["triangle_ranges"][0].__setitem__(0, s["objects"][patch_owner]["triangle_start"] + s["objects"][patch_owner]["triangle_count"]),
                   lambda s: s["objects"][patch_owner]["candidate_patches"][0]["representative_barycentric"].__setitem__(0, 2),
                   lambda s: s["objects"][0].update(default_collision="whole-group permitted contact"),
                   lambda s: s["objects"][0].update(unrecognized=1),
                   lambda s: s["checks"].update(objects=True),
                   lambda s: s["objects"][0]["source_corrected_world_rows"][3].__setitem__(0, 1)]
        for index, change in enumerate(changes):
            with self.subTest(index=index):
                value = copy.deepcopy(self.support)
                change(value)
                self.refused(lambda: spec.construct_runtime(value, self.contact))
        # Contact indices are checked independently before any candidate points are read.
        contact = copy.deepcopy(self.contact)
        contact["groups"][0]["triangles"][0][0] = contact["groups"][0]["vertices_micrometres"].__len__()
        self.refused(lambda: spec.construct_runtime(self.support, contact))

    def test_parser_dimensions_depth_duplicate_and_nonfinite(self):
        for raw in (b'{"a":1,"a":2}', b'{"x":NaN}', b'{"x":Infinity}', b'{"x":1e9999}',
                    b'[' * 17 + b'0' + b']' * 17, b'\xff', b' ' * (spec.MAX_RUNTIME + 1)):
            with self.subTest(raw=raw[:30]):
                self.refused(lambda: spec.decode(raw, spec.MAX_RUNTIME))
        self.assertEqual(spec.decode(b'{"a":"[[[\\\"]]]"}', 128), {"a": '[[["]]]'})

    def test_all_independent_identity_and_dependency_controls(self):
        for key in IDENTITIES:
            with self.subTest(identity=key):
                value = copy.deepcopy(self.runtime)
                value["identities"][key] = "0" * 64
                self.refused(lambda: spec.validate_runtime(value))
        repository = self.root / "dependency-repository"
        motion_name = "assets/native/operating-motion-01/operating-motion-01.json"
        for name in (*ENGINE_BINDINGS, motion_name):
            target = repository / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        # A complete independently copied binding set is accepted before mutations.
        assets.verify(self.source, repository)
        for name in ("assets/native/wayfarer-operating-02/metadata/contact.json",
                     "assets/native/wayfarer-operating-02/payloads/model-00.xz-part", motion_name):
            with self.subTest(dependency=name):
                target = repository / name
                original = target.read_bytes()
                # Same length finite byte change cannot be authorized by the submitted manifest.
                changed = bytes([original[0] ^ 1]) + original[1:]
                target.write_bytes(changed)
                self.refused(lambda: assets.verify(self.source, repository))
                target.write_bytes(original)

    def test_manifest_roster_paths_sizes_and_extra_entries(self):
        changes = [lambda m: m["files"][0].update(path="../escape"),
                   lambda m: m["files"][0].update(bytes=True),
                   lambda m: m["files"].append(m["files"][0]), lambda m: m.update(extra=1)]
        for index, change in enumerate(changes):
            with self.subTest(index=index):
                package = self.package("manifest-" + str(index))
                manifest = json.loads((package / "package.json").read_bytes())
                change(manifest)
                (package / "package.json").write_bytes(spec.encode(manifest))
                self.refused(lambda: assets.verify(package))
        package = self.package("extra-directory")
        (package / "unrostered").mkdir()
        self.refused(lambda: assets.verify(package))

    def test_symlink_fifo_ancestor_and_nonregular_refusals(self):
        package = self.package("runtime-link")
        (package / assets.RUNTIME).unlink()
        (package / assets.RUNTIME).symlink_to(self.source / assets.RUNTIME)
        self.refused(lambda: assets.verify(package))
        ancestor = self.root / "linked-package"
        ancestor.symlink_to(self.source, target_is_directory=True)
        self.refused(lambda: assets.read_regular(ancestor / assets.RUNTIME, spec.MAX_RUNTIME))
        fifo = self.root / "fifo"
        os.mkfifo(fifo)
        self.refused(lambda: assets.read_regular(fifo, spec.MAX_RUNTIME))
        self.refused(lambda: assets.read_regular(self.root, spec.MAX_RUNTIME))

    def test_concurrent_output_preserved(self):
        output = self.root / "racing-output"
        original, marker = assets.install_new, {}
        def race(staging, destination):
            destination.mkdir()
            marker["inode"] = destination.stat().st_ino
            return original(staging, destination)
        with patch.object(assets, "install_new", race):
            self.refused(lambda: assets.prepare(self.source, output))
        self.assertEqual(output.stat().st_ino, marker["inode"])
        self.assertEqual(list(output.iterdir()), [])
        self.assertFalse(list(self.root.glob(".boarding-prepare-*")))

    def test_verified_snapshot_prevents_reopen_copy_race(self):
        package = self.package("snapshot")
        original = assets.verify
        def change_after_verify(source, repository=None):
            result = original(source, repository)
            (source / assets.RUNTIME).write_bytes(b"changed after verified snapshot")
            return result
        output = self.root / "snapshot-output"
        with patch.object(assets, "verify", change_after_verify):
            assets.prepare(package, output)
        self.assertEqual(assets.digest((output / assets.RUNTIME).read_bytes()), RUNTIME_SHA256)

    def test_packager_roundtrip_source_refusal_and_producer_race(self):
        output = self.root / "rebuilt"
        builder.build(self.source / "sources", output)
        assets.verify(output)
        source = self.root / "changed-authoring-input"
        shutil.copytree(self.source / "sources", source)
        (source / "wayfarer-boarding-support-01/support.json").write_bytes(b"{}")
        failed = self.root / "failed-build"
        self.refused(lambda: builder.build(source, failed))
        self.assertFalse(failed.exists())
        original, raced = assets.install_new, self.root / "producer-race"
        def race(staging, destination):
            destination.mkdir()
            (destination / "owner-marker").write_bytes(b"preserve")
            return original(staging, destination)
        with patch.object(assets, "install_new", race):
            self.refused(lambda: builder.build(self.source / "sources", raced))
        self.assertEqual((raced / "owner-marker").read_bytes(), b"preserve")
        self.assertFalse(list(self.root.glob(".boarding-package-*")))

    def test_copied_cli_explicit_repository_and_optimized_validation(self):
        copied = self.root / "copied-tools"
        copied.mkdir()
        for name in assets.TOOL_NAMES:
            shutil.copyfile(ROOT / "tools" / name, copied / name)
        output = self.root / "copied-output"
        command = [sys.executable, "-O", str(copied / "prepare_boarding_support.py"),
                   "--package", str(self.source), "--output", str(output)]
        refused = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertNotEqual(refused.returncode, 0)
        self.assertFalse(output.exists())
        passed = subprocess.run(command + ["--repository", str(ROOT)], capture_output=True, text=True, timeout=30)
        self.assertEqual(passed.returncode, 0, passed.stdout + passed.stderr)
        self.assertEqual(assets.digest((output / assets.RUNTIME).read_bytes()), RUNTIME_SHA256)
        linked = self.root / "linked-repository"
        linked.symlink_to(ROOT, target_is_directory=True)
        self.refused(lambda: assets.verify(self.source, linked))
        self.refused(lambda: assets.verify(self.source, self.root / "missing-repository"))
        mutated = self.package("optimized-negative")
        changed = copy.deepcopy(self.runtime)
        changed["objects"][0]["triangle_start"] += 1
        self.rewrite_record(mutated, assets.RUNTIME, spec.encode(changed))
        failed = subprocess.run([sys.executable, "-O", str(copied / "prepare_boarding_support.py"),
                                 "--package", str(mutated), "--output", str(self.root / "optimized-refused"),
                                 "--repository", str(ROOT)], capture_output=True, text=True, timeout=30)
        self.assertNotEqual(failed.returncode, 0)


if __name__ == "__main__":
    unittest.main()
