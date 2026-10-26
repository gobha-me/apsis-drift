#!/usr/bin/env python3
"""Boundary tests for native CI scheduling; no game or fixture executable runs."""
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
from types import SimpleNamespace
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("native_ci", ROOT / "tools/native_ci.py")
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)
REAL_REVISION = ci.revision
SHA = "a" * 64


class NativeCiTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="native-ci-unit-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        for path in ("tools", ".github/workflows", "godot/bin", "godot/tests", "godot/shaders"):
            (self.repo / path).mkdir(parents=True)
        for name in (*ci.ASSET_HELPERS, "test_native_sky.py"):
            (self.repo / "tools" / name).write_text("# Unit source pin\n")
        for name in ("native_ci_groups.json", "test_godot_native.py", "native_ci.py"):
            shutil.copy2(ROOT / "tools" / name, self.repo / "tools" / name)
        (self.repo / ".github/workflows/ci.yml").write_text("name: unit\n")
        for name in ("project.godot", "freedom.gdextension.in", "tests/native_sky_camera_test.gd",
                     "shaders/native_navigation_sky.gdshader", "shaders/native_planet_optics.gdshaderinc"):
            (self.repo / "godot" / name).write_text("unit source " + name)
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text("\n".join(n + ":STRING=" + v for n, v in ci.FLAGS.items()) +
                                                 "\nCMAKE_HOME_DIRECTORY:INTERNAL=" + str(self.repo) + "\n")
        for name in (ci.EXPORTER, ci.BRIDGE, *ci.FIXTURES.values()):
            path = self.build / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("unit executable " + name)
            path.chmod(0o755)
        self.engine = self.root / "godot"
        self.engine.write_text("unit engine")
        self.engine.chmod(0o755)
        self.archive, self.manifest = self.root / "bundle.tar.gz", self.root / "manifest.json"
        self.revision = patch.object(ci, "revision", return_value="f" * 40)
        self.revision.start()
        self.addCleanup(self.revision.stop)
        ci.pack(SimpleNamespace(build_dir=self.build, godot=self.engine,
                                archive=self.archive, manifest=self.manifest), self.repo)
        self.envelope = ci.read_json(self.manifest)
        self.m = self.envelope["manifest"]
        self.runner = ci.runner_module(self.repo)

    def log(self, directory, name, text="0 failures\n"):
        path = directory / name
        path.write_text(text)
        return name

    def launches(self, name, directory):
        rows = []
        for case in ci.LAUNCHES[name]:
            refused = (name, case) in ci.REFUSALS
            log = self.log(directory, name + "-" + case + ".log",
                           "ERROR: Native start rejected: unit refusal\n" if refused else "accepted\n")
            rows.append({"case": case, "pass": True, "returncode": 1 if refused else 0,
                         "seconds": 0.01, "log": log})
        return rows

    def report(self, group, directory):
        directory.mkdir(parents=True)
        selected = self.m["groups"][group]
        expected, binaries = ci.layout(selected, self.runner)
        r = {"schema_version": 1, "selected": selected, "setup": [], "tests": [],
             "engine_sha256": self.m["files"]["engine/godot"]["sha256"],
             "bridge_sha256": self.m["files"]["build/" + ci.BRIDGE]["sha256"],
             "exporter_sha256": self.m["files"]["build/" + ci.EXPORTER]["sha256"],
             "extension_descriptor_sha256": self.m["godot_sources"]["bin/freedom.gdextension"],
             "staged_executable_sha256": {Path(p).name: self.m["files"]["build/" + p]["sha256"] for p in binaries}}
        r["project_sha256"] = {**self.m["godot_sources"], "bin/libapsis_freedom_bridge.so": r["bridge_sha256"]}
        for key in expected:
            if key.endswith("_launch"):
                r["setup"].append({"family": key, "cases": self.launches(key.removesuffix("_launch"), directory)})
                continue
            row = {"returncode": 0, "timed_out": False}
            if key.startswith("snapshot:"):
                row.update(seed=int(key.split(":")[1]), snapshot_sha256=SHA)
            elif key.startswith("save:"):
                row.update(save=key.split(":")[1], save_sha256=SHA)
            else:
                family, _, test = key.partition(":")
                row.update(family=family, seconds=0.01)
                if test:
                    row["test"] = test
                if family == "native_assets":
                    row.update(preparer_sha256=self.m["sources"]["tools/prepare_freedom_native_assets.py"],
                               helper_sha256={name: self.m["sources"]["tools/" + name] for name in ci.ASSET_HELPERS},
                               prepared={"schema": "apsis.native-starter-assets/1", "package_sha256": SHA},
                               companions={"operating": {"unit": True}, "stowed": {"unit": True},
                                           "crew": {"schema": "apsis.origin-crew-assets/1", "package_sha256": SHA}})
                elif family == "operating_assets":
                    row.update(preparer_sha256=self.m["sources"]["tools/prepare_operating_assets.py"],
                               spec_sha256=self.m["sources"]["tools/wayfarer_operating_spec.py"])
                else:
                    role = ("minor" if family == "minor_voyage" else "moon" if family == "moon_voyage" else "surface" if
                            family in ("daynight_saves", "surface_saves", "surface_walk_saves") else
                            "planetary" if family in ("planetary_saves", "surface_loop_saves", *self.runner.ROUNDTRIP_TESTS)
                            else family.removesuffix("_saves"))
                    row.update(fixture_sha256=self.m["files"]["build/" + ci.FIXTURES[role]]["sha256"],
                               files_sha256={"phase.json": SHA})
                    if family == "minor_voyage":
                        row["files_sha256"] = {n + ".json": SHA for n in
                            ("arrival", "approach", "withdraw", "home", "rendezvous-near", "reuse-captured",
                     "foreign-entry", "foreign-arrival", "foreign-home", "foreign-approach", "foreign-withdraw")}
                    if family == "derelict_saves":
                        row["files_sha256"] = {n + ".json": SHA for n in
                            ("station", "approach", "contact", "contact-recovered", "near", "inspection", "recovered",
                     "lead-station", "lead-kept", "lead-confirmed", "lead-visited", "lead-recovered")}
                    if family == "derelict_recovery_saves":
                        row["files_sha256"] = {n + ".json": SHA for n in
                            (*self.runner.DERELICT_RECOVERY_PHASES, "manifest")}
                    if family == "frontier_chart_saves":
                        row["files_sha256"] = {name: SHA for name in self.runner.FRONTIER_CHART_FILES}
                    if family == "moon_voyage":
                        row["files_sha256"] = {n + ".json": SHA for n in
                            ("arrival", "approach", "touchdown-source", "landed", "outside", "returned", "released", "ascent",
                     "service-landed", "service-requested", "service-partial600",
                     "foreign-parent", "foreign-arrival", "foreign-home",
                     "foreign-landed", "foreign-outside", "foreign-marked")}
                    if family == "native_first_flight_continuation":
                        data = b"unit continuity boundary, not a game save"
                        for name in ("surface-loop/space-exit.json", "native_first_flight_continuation/surface-ascent.json"):
                            p = directory / name
                            p.parent.mkdir(parents=True, exist_ok=True)
                            p.write_bytes(data)
                        row["continuation_source"] = {"file": "surface-loop/space-exit.json", "bytes": len(data),
                            "sha256": ci.digest(directory / "surface-loop/space-exit.json"), "native_prefix_test": "native_surface_loop",
                            "native_prefix_selected": True, "fixture_preserved_bytes": True, "unchanged_after_native": True}
            r["setup"].append(row)
        for name in selected:
            row = {"name": name, "status": "pass"}
            if name == "native_shell":
                row.update(cases=self.launches(name, directory), source_saves_unchanged=True,
                           identity_matches=True, history_matches=True, geometry_matches=True)
            else:
                row.update(returncode=0, seconds=0.01, log=self.log(directory, name + ".log"))
            r["tests"].append(row)
        cli = []
        if "native_planetary" in selected:
            cli += [f"planetary-refusal-{i}.log" for i in range(4)]
        if "native_surface_loop" in selected:
            cli += [f"surface-loop-refusal-{i}.log" for i in range(11)]
        for name in self.runner.ROUNDTRIP_TESTS:
            if name in selected:
                cli += [f"{name}-refusal-{i}.log" for i in range(23)]
        if "native_derelict" in selected:
            cli += [f"derelict-recovery-refusal-{i}.log" for i in range(5)]
        for name in cli:
            self.log(directory, name, "argument refused\n")
        ci.write_json(directory / "report.json", r)
        return r

    def make_check(self):
        receipts = []
        for group in self.m["groups"]:
            directory = self.root / ("work-" + group) / "native-contracts-unit"
            self.report(group, directory)
            runner_log = directory.parent / "runner.log"
            runner_log.write_text("all selected native contracts passed\n")
            inv = {"schema_version": 1, "group": group, "runner_exit": 0, "error": "", "seconds": 1.0,
                   "revision": self.m["revision"], "manifest_sha256": ci.digest(self.manifest),
                   "runner_log": {"path": runner_log.relative_to(self.root).as_posix(), "sha256": ci.digest(runner_log)},
                   "reports": [{"path": (directory / "report.json").relative_to(self.root).as_posix(),
                                "sha256": ci.digest(directory / "report.json"),
                                "logs": {p.name: ci.digest(p) for p in directory.glob("*.log")}}]}
            receipt = self.root / ("invocation-g" + group + ".json")
            ci.write_json(receipt, inv)
            receipts.append(receipt)
        sky_dir = self.root / "sky"
        sky_dir.mkdir()
        capture = {"pass": True, "cpu_invalid_cases_rejected": 13,
                   "shader_sha256": self.m["godot_sources"]["shaders/native_navigation_sky.gdshader"], "cases": []}
        for y in (0, 9):
            image = sky_dir / ("camera-y-" + str(y) + ".png")
            image.write_bytes(b"unit image bytes, not a rendered image")
            capture["cases"].append({"image": image.name, "sha256": ci.digest(image), "camera_position": [0, y, 0],
                                     "viewport": [3840, 2160], "error_pixels": 0.0})
        sky = {"passed": True, "returncode": 0, "timed_out": False, "seconds": 1.0, "timeout_seconds": 45,
               "sources_unchanged": True, "diagnostics": [], "capture": capture,
               "engine_sha256": self.m["files"]["engine/godot"]["sha256"],
               "runner_sha256": self.m["sources"]["tools/test_native_sky.py"],
               "source_sha256": {n: self.m["godot_sources"][n] for n in
                   ("shaders/native_planet_optics.gdshaderinc", "tests/native_sky_camera_test.gd", "shaders/native_navigation_sky.gdshader")}}
        self.log(sky_dir, "engine.log", "sky passed\n")
        ci.write_json(sky_dir / "report.json", sky)
        return SimpleNamespace(manifest=self.manifest, invocation=receipts, dependency=["producer=success", "workers=success"],
                               sky_report=sky_dir / "report.json", output=self.root / "summary.json")

    def change_report(self, args, group, mutate):
        inv_path = args.invocation[group - 1]
        inv = ci.read_json(inv_path)
        report_path = self.root / inv["reports"][0]["path"]
        r = json.loads(report_path.read_text())
        mutate(r)
        # allow_nan=False deliberately not used here: malformed transport is a test input.
        report_path.write_text(json.dumps(r))
        inv["reports"][0]["sha256"] = ci.digest(report_path)
        ci.write_json(inv_path, inv)

    def test_pack_and_unpack_exact_payload_modes(self):
        output = self.root / "unpacked"
        ci.unpack(self.archive, self.envelope, output)
        self.assertEqual(len(self.m["files"]), 14)
        for name, row in self.m["files"].items():
            p = output / name
            self.assertEqual(ci.digest(p), row["sha256"])
            self.assertEqual(p.stat().st_mode & 0o7777, 0o755)

    def test_fresh_worker_uses_tracked_descriptor_without_cmake(self):
        source = self.repo / "godot/freedom.gdextension.in"
        generated = self.repo / "godot/bin/freedom.gdextension"
        self.assertFalse(generated.exists())
        self.assertEqual(self.m["godot_sources"]["bin/freedom.gdextension"], ci.digest(source))
        # Producer configuration adds only a byte-identical COPYONLY output;
        # deleting that untracked output must leave the shared pins unchanged.
        shutil.copy2(source, generated)
        producer = ci.sources(self.repo)
        generated.unlink()
        self.assertEqual(ci.sources(self.repo), producer)
        self.assertEqual(ci.load_manifest(self.manifest, self.repo), self.envelope)

    def test_actual_merge_checkout_revision_allowed_foreign_refused(self):
        with patch.object(ci.subprocess, "check_output", return_value="f" * 40 + "\n"):
            with patch.dict(os.environ, {"GITHUB_SHA": "f" * 40}):
                self.assertEqual(REAL_REVISION(self.repo), "f" * 40)
            with patch.dict(os.environ, {"GITHUB_SHA": "e" * 40}):
                with self.assertRaises(ValueError):
                    REAL_REVISION(self.repo)

    def test_pack_refuses_actual_wrong_build_flags(self):
        (self.build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n")
        with self.assertRaises(ValueError):
            ci.pack(SimpleNamespace(build_dir=self.build, godot=self.engine,
                                    archive=self.root / "new.tar.gz", manifest=self.root / "new.json"), self.repo)

    def test_source_and_revision_mismatch_before_runner(self):
        args = SimpleNamespace(archive=self.archive, manifest=self.manifest, group="1",
                               output_parent=self.root / "run", receipt=self.root / "run.json")
        (self.repo / "godot/tests/native_sky_camera_test.gd").write_text("foreign source")
        with patch.object(ci.subprocess, "run") as spawn:
            self.assertEqual(ci.run(args, self.repo), 1)
            spawn.assert_not_called()
        self.assertIsNone(ci.read_json(args.receipt)["runner_exit"])

    def test_run_actual_emitted_timeout_accepted_by_unchanged_runner(self):
        args = SimpleNamespace(archive=self.archive, manifest=self.manifest, group="1",
                               output_parent=self.root / "run", receipt=self.root / "run.json")
        descriptor = self.repo / "godot/bin/freedom.gdextension"
        self.assertFalse(descriptor.exists())
        def execute(command, **kwargs):
            self.assertEqual(ci.digest(descriptor), self.m["godot_sources"]["bin/freedom.gdextension"])
            value = command[command.index("--timeout") + 1]
            self.assertEqual(value, "900")
            self.assertEqual(self.runner.timeout_seconds(value), 900)
            self.assertEqual([command[i + 1] for i, x in enumerate(command) if x == "--test"], self.m["groups"]["1"])
            out = args.output_parent / "native-contracts-unit"
            out.mkdir()
            ci.write_json(out / "report.json", {"unit": True})
            return SimpleNamespace(returncode=0)
        with patch.object(ci.subprocess, "run", side_effect=execute) as spawn:
            self.assertEqual(ci.run(args, self.repo), 0)
            spawn.assert_called_once()
        inv = ci.read_json(args.receipt)
        self.assertEqual(inv["runner_exit"], 0)
        self.assertEqual(len(inv["reports"]), 1)
        self.assertFalse((args.output_parent / "bundle").exists())

    def test_conflicting_or_symlink_descriptor_refused_before_spawn(self):
        descriptor = self.repo / "godot/bin/freedom.gdextension"
        for kind in ("different", "symlink"):
            with self.subTest(kind=kind):
                if kind == "different":
                    descriptor.write_text("wrong descriptor")
                else:
                    descriptor.symlink_to(self.repo / "godot/freedom.gdextension.in")
                args = SimpleNamespace(archive=self.archive, manifest=self.manifest, group="1",
                    output_parent=self.root / ("run-" + kind), receipt=self.root / ("run-" + kind + ".json"))
                with patch.object(ci.subprocess, "run") as spawn:
                    self.assertEqual(ci.run(args, self.repo), 1)
                    spawn.assert_not_called()
                descriptor.unlink()

    def test_identical_descriptor_reused_without_rewrite(self):
        descriptor = self.repo / "godot/bin/freedom.gdextension"
        shutil.copy2(self.repo / "godot/freedom.gdextension.in", descriptor)
        original = descriptor.stat()
        args = SimpleNamespace(archive=self.archive, manifest=self.manifest, group="1",
            output_parent=self.root / "reused", receipt=self.root / "reused.json")
        with patch.object(ci.subprocess, "run", return_value=SimpleNamespace(returncode=7)):
            self.assertEqual(ci.run(args, self.repo), 1)
        self.assertEqual(descriptor.stat().st_mtime_ns, original.st_mtime_ns)
        self.assertEqual(descriptor.stat().st_ino, original.st_ino)

    def test_failed_runner_keeps_actual_exit_and_evidence(self):
        args = SimpleNamespace(archive=self.archive, manifest=self.manifest, group="2",
                               output_parent=self.root / "run", receipt=self.root / "run.json")
        with patch.object(ci.subprocess, "run", return_value=SimpleNamespace(returncode=7)):
            self.assertEqual(ci.run(args, self.repo), 1)
        self.assertEqual(ci.read_json(args.receipt)["runner_exit"], 7)
        self.assertTrue((args.output_parent / "bundle").exists())

    def test_archive_hash_tamper_refused(self):
        with self.archive.open("ab") as stream:
            stream.write(b"changed")
        with self.assertRaises(ValueError):
            ci.unpack(self.archive, self.envelope, self.root / "bad")

    def test_archive_links_duplicates_and_traversal_refused(self):
        for label, name, kind in (("traversal", "../escape", tarfile.REGTYPE),
                                  ("link", "engine/godot", tarfile.SYMTYPE),
                                  ("duplicate", "engine/godot", tarfile.REGTYPE)):
            with self.subTest(label=label):
                archive = self.root / (label + ".tar.gz")
                with tarfile.open(self.archive) as source, tarfile.open(archive, "w:gz") as target:
                    for info in source:
                        target.addfile(info, source.extractfile(info))
                    info = tarfile.TarInfo(name)
                    info.type, info.size, info.mode = kind, 1, 0o755
                    target.addfile(info, io.BytesIO(b"x"))
                envelope = copy.deepcopy(self.envelope)
                envelope["archive_sha256"] = ci.digest(archive)
                with self.assertRaises(ValueError):
                    ci.unpack(archive, envelope, self.root / ("extract-" + label))

    def test_complete_coverage_and_expected_nonzero_refusals_pass(self):
        args = self.make_check()
        self.assertEqual(ci.check(args, self.repo), 0)
        summary = ci.read_json(args.output)
        self.assertEqual(summary["counts"], {"tests": 45, "setup_rows": 83, "launch_cases": 25,
                                           "expected_nonzero_launches": 9, "cli_refusal_logs": 135})
        self.assertEqual(len(summary["groups"]), 4)
        # Artifact extraction can relocate receipts and proof together.
        relocated = self.root.parent / (self.root.name + "-relocated")
        shutil.copytree(self.root, relocated)
        self.addCleanup(shutil.rmtree, relocated)
        args.invocation = [relocated / p.name for p in args.invocation]
        args.sky_report = relocated / "sky/report.json"
        self.assertEqual(ci.check(args, self.repo), 0)

    def test_missing_duplicate_foreign_and_failed_invocations_refused(self):
        args = self.make_check()
        original = list(args.invocation)
        for label, receipts in (("missing", original[:3]), ("duplicate", original[:3] + original[:1])):
            with self.subTest(label=label):
                args.invocation = receipts
                with self.assertRaises(ValueError):
                    ci.check(args, self.repo)
        args.invocation = original
        path = original[0]
        inv = ci.read_json(path)
        for key, value in (("group", "foreign"), ("revision", "e" * 40), ("manifest_sha256", SHA),
                           ("runner_exit", 1), ("runner_exit", None), ("error", "worker failed")):
            with self.subTest(key=key, value=value):
                changed = {**inv, key: value}
                ci.write_json(path, changed)
                with self.assertRaises((ValueError, TypeError)):
                    ci.check(args, self.repo)
        ci.write_json(path, inv)

    def test_failed_skipped_cancelled_dependency_refused(self):
        args = self.make_check()
        for state in ("failure", "skipped", "cancelled", "timed_out"):
            args.dependency = ["producer=success", "workers=" + state]
            with self.subTest(state=state), self.assertRaises(ValueError):
                ci.check(args, self.repo)

    def test_mixed_binary_source_engine_and_missing_cases_refused(self):
        args = self.make_check()
        mutations = [lambda r: r.update(bridge_sha256=SHA), lambda r: r.update(engine_sha256=SHA),
                     lambda r: r["project_sha256"].update({"project.godot": SHA}),
                     lambda r: r["staged_executable_sha256"].update({"foreign": SHA}),
                     lambda r: r["tests"].pop(), lambda r: r["tests"].append(r["tests"][0]),
                     lambda r: r["selected"].append("native_moon")]
        original = ci.read_json(self.root / "work-1/native-contracts-unit/report.json")
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index):
                self.change_report(args, 1, lambda r: (r.clear(), r.update(copy.deepcopy(original)), mutate(r)))
                with self.assertRaises(ValueError):
                    ci.check(args, self.repo)

    def test_minor_roster_requires_old_six_and_foreign_five(self):
        args = self.make_check()
        original = ci.read_json(self.root / "work-4/native-contracts-unit/report.json")
        row = next(r for r in original["setup"] if r.get("family") == "minor_voyage")
        self.assertEqual(len(row["files_sha256"]), 11)
        for name in row["files_sha256"]:
            with self.subTest(missing=name):
                def remove(r):
                    r.clear()
                    r.update(copy.deepcopy(original))
                    minor = next(s for s in r["setup"] if s.get("family") == "minor_voyage")
                    minor["files_sha256"].pop(name)
                self.change_report(args, 4, remove)
                with self.assertRaises(ValueError):
                    ci.check(args, self.repo)

    def test_crew_reuses_one_derelict_producer(self):
        for selected in (["native_station_crew"], ["native_derelict"],
                         ["native_station_crew", "native_derelict"]):
            with self.subTest(selected=selected):
                rows, binaries = ci.layout(selected, self.runner)
                self.assertEqual(rows.count("derelict_saves"), 1)
                self.assertEqual(binaries.count(ci.FIXTURES["derelict"]), 1)

    def test_derelict_roster_requires_contact_recovery_and_stellar_leads_in_selected_cohorts(self):
        args = self.make_check()
        groups = [int(group) for group, names in ci.groups(self.repo).items()
                  if "native_derelict" in names or "native_station_crew" in names]
        self.assertTrue(groups)
        for group in groups:
            original = ci.read_json(self.root / f"work-{group}/native-contracts-unit/report.json")
            row = next(r for r in original["setup"] if r.get("family") == "derelict_saves")
            self.assertEqual(len(row["files_sha256"]), 12)
            self.assertIn("contact-recovered.json", row["files_sha256"])
            self.assertIn("lead-recovered.json", row["files_sha256"])
            for name in row["files_sha256"]:
                with self.subTest(group=group, missing=name):
                    def remove(r):
                        r.clear()
                        r.update(copy.deepcopy(original))
                        phases = next(s for s in r["setup"] if s.get("family") == "derelict_saves")
                        phases["files_sha256"].pop(name)
                    self.change_report(args, group, remove)
                    with self.assertRaises(ValueError):
                        ci.check(args, self.repo)
            self.change_report(args, int(group), lambda r: (r.clear(), r.update(copy.deepcopy(original))))

    def test_frontier_chart_requires_save_packet_and_lineage(self):
        args = self.make_check()
        group = next(int(group) for group, names in ci.groups(self.repo).items()
                     if "orbital_survey" in names)
        original = ci.read_json(self.root / f"work-{group}/native-contracts-unit/report.json")
        row = next(r for r in original["setup"] if r.get("family") == "frontier_chart_saves")
        self.assertEqual(set(row["files_sha256"]), set(self.runner.FRONTIER_CHART_FILES))
        for name in self.runner.FRONTIER_CHART_FILES:
            with self.subTest(missing=name):
                def remove(report):
                    report.clear()
                    report.update(copy.deepcopy(original))
                    phases = next(r for r in report["setup"]
                                  if r.get("family") == "frontier_chart_saves")
                    phases["files_sha256"].pop(name)
                self.change_report(args, group, remove)
                with self.assertRaisesRegex(ValueError, "Incomplete actual frontier chart phases"):
                    ci.check(args, self.repo)
        self.change_report(args, group,
                           lambda report: (report.clear(), report.update(copy.deepcopy(original))))
        ci.check(args, self.repo)

    def test_bad_status_timeout_setup_helper_and_nonfinite_refused(self):
        args = self.make_check()
        original = ci.read_json(self.root / "work-1/native-contracts-unit/report.json")
        mutations = [lambda r: r["tests"][0].update(status="timeout"),
                     lambda r: r["tests"][0].update(seconds=float("nan")),
                     lambda r: r["tests"][0].update(seconds=float("inf")),
                     lambda r: r["tests"][0].update(seconds=-1),
                     lambda r: r["setup"][0].update(returncode=1),
                     lambda r: r["setup"][0].update(timed_out=True),
                     lambda r: r["setup"].pop(), lambda r: r["setup"][0].update(helper_sha256={}),
                     lambda r: r["setup"][0]["companions"].pop("crew"),
                     lambda r: r["setup"][0]["companions"]["crew"].update(schema="foreign")]
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index):
                self.change_report(args, 1, lambda r: (r.clear(), r.update(copy.deepcopy(original)), mutate(r)))
                with self.assertRaises(ValueError):
                    ci.check(args, self.repo)

    def test_lost_prefix_or_changed_continuation_refused(self):
        args = self.make_check()
        self.change_report(args, 1, lambda r: next(x for x in r["setup"] if x.get("family") ==
            "native_first_flight_continuation")["continuation_source"].update(native_prefix_selected=False))
        with self.assertRaises(ValueError):
            ci.check(args, self.repo)
        self.change_report(args, 1, lambda r: next(x for x in r["setup"] if x.get("family") ==
            "native_first_flight_continuation")["continuation_source"].update(native_prefix_selected=True))
        (self.root / "work-1/native-contracts-unit/surface-loop/space-exit.json").write_text("changed save")
        with self.assertRaises(ValueError):
            ci.check(args, self.repo)

    def test_unexpected_error_and_singular_plural_leak_diagnostics_refused(self):
        for text in ("SCRIPT ERROR: broken", "ERROR: hidden failure", "USER SCRIPT ERROR: bad",
                     "WARNING: ObjectDB instance leaked at exit", "WARNING: ObjectDB instances leaked at exit",
                     "WARNING: 2 resources still in use at exit"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                ci.log_check(text)
            with self.subTest(refusal=text), self.assertRaises(ValueError):
                ci.log_check("ERROR: Native start rejected: expected\n" + text, True)

    def test_nested_false_pass_and_timeout_are_rejected_recursively(self):
        for bad in ({"pass": False}, {"pass": 1}, {"timed_out": True}, {"timed_out": 0}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                ci.finite_tree({"setup": [{"companions": {"nested": bad}}]})
        ci.finite_tree({"setup": [{"companions": {"pass": True, "timed_out": False}}]})

    def test_expected_refusal_cannot_become_zero_or_disappear(self):
        args = self.make_check()
        self.change_report(args, 2, lambda r: next(x for x in r["tests"] if x["name"] == "native_shell")
                           ["cases"][4].update(returncode=0))
        with self.assertRaises(ValueError):
            ci.check(args, self.repo)

    def test_missing_log_or_changed_log_refused(self):
        args = self.make_check()
        log = self.root / "work-1/native-contracts-unit/native_surface_loop.log"
        log.write_text("ERROR: changed log")
        with self.assertRaises(ValueError):
            ci.check(args, self.repo)
        log.unlink()
        with self.assertRaises(ValueError):
            ci.check(args, self.repo)

    def test_last_roundtrip_cli_refusal_cannot_be_hidden(self):
        args = self.make_check()
        name = "native_roundtrip-refusal-22.log"
        for inv_path in args.invocation:
            invocation = ci.read_json(inv_path)
            row = invocation["reports"][0]
            if name not in row["logs"]:
                continue
            (self.root / row["path"]).parent.joinpath(name).unlink()
            del row["logs"][name]
            ci.write_json(inv_path, invocation)
            break
        else:
            self.fail("Roundtrip CLI refusal fixture missing")
        # The remaining inventory and hashes are self-consistent. Admission
        # must still require every old and new CLI refusal, not just a prefix.
        with self.assertRaisesRegex(ValueError, "Missing CLI refusal evidence"):
            ci.check(args, self.repo)

    def test_derelict_recovery_roster_requires_every_phase(self):
        args = self.make_check()
        group = next(group for group, names in ci.groups(self.repo).items()
                     if "native_derelict" in names)
        path = self.root / ("work-" + group + "/native-contracts-unit/report.json")
        original = ci.read_json(path)
        row = next(r for r in original["setup"] if r.get("family") == "derelict_recovery_saves")
        self.assertEqual(len(row["files_sha256"]), 18)
        for name in row["files_sha256"]:
            with self.subTest(missing=name):
                def remove(report):
                    phase = next(r for r in report["setup"] if r.get("family") == "derelict_recovery_saves")
                    del phase["files_sha256"][name]
                self.change_report(args, int(group), lambda r: (r.clear(), r.update(copy.deepcopy(original)), remove(r)))
                with self.assertRaisesRegex(ValueError, "Incomplete actual derelict recovery phases"):
                    ci.check(args, self.repo)
        self.change_report(args, int(group), lambda r: (r.clear(), r.update(copy.deepcopy(original))))

    def test_derelict_recovery_cli_refusals_cannot_be_hidden(self):
        args = self.make_check()
        name = "derelict-recovery-refusal-4.log"
        for inv_path in args.invocation:
            invocation = ci.read_json(inv_path)
            row = invocation["reports"][0]
            if name not in row["logs"]:
                continue
            (self.root / row["path"]).parent.joinpath(name).unlink()
            del row["logs"][name]
            ci.write_json(inv_path, invocation)
            break
        else:
            self.fail("Derelict recovery CLI refusal fixture missing")
        with self.assertRaisesRegex(ValueError, "Missing CLI refusal evidence"):
            ci.check(args, self.repo)

    def test_sky_failure_missing_invalid_proof_source_or_raster_refused(self):
        args = self.make_check()
        original = ci.read_json(args.sky_report)
        mutations = [lambda r: r.update(passed=False), lambda r: r.update(timed_out=True),
                     lambda r: r.update(sources_unchanged=False), lambda r: r.update(engine_sha256=SHA),
                     lambda r: r["capture"].update(cpu_invalid_cases_rejected=12),
                     lambda r: r["capture"]["cases"].pop(),
                     lambda r: r["capture"]["cases"][0].update(error_pixels=2),
                     lambda r: r["source_sha256"].update({"tests/native_sky_camera_test.gd": SHA})]
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index):
                sky = copy.deepcopy(original)
                mutate(sky)
                ci.write_json(args.sky_report, sky)
                with self.assertRaises(ValueError):
                    ci.check(args, self.repo)

    def test_unsafe_paths_duplicate_json_and_oversized_json_refused(self):
        for name in ("../secret", "/tmp/secret", "a/../secret", "a\\secret"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                ci.child(self.root, name)
        p = self.root / "bad.json"
        p.write_text('{"selected":[],"selected":[]}')
        with self.assertRaises(ValueError):
            ci.read_json(p)
        p.write_bytes(b" " * ((8 << 20) + 1))
        with self.assertRaises(ValueError):
            ci.read_json(p)

    def test_groups_exact_and_pairs_cannot_split(self):
        path = self.repo / "tools/native_ci_groups.json"
        value = ci.read_json(path)
        self.assertEqual(sum(map(len, value["groups"].values())), 45)
        value["groups"]["1"].remove("native_first_flight_continuation")
        value["groups"]["2"].append("native_first_flight_continuation")
        ci.write_json(path, value)
        with self.assertRaises(ValueError):
            ci.groups(self.repo)


if __name__ == "__main__":
    unittest.main()
