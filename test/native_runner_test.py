"""Runner control tests; no Godot, audio device, third-party asset or network."""
import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "native_runner", Path(__file__).resolve().parents[1] / "tools/test_godot_native.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class NativeRunnerTests(unittest.TestCase):
    def test_successful_work_compacts_generated_payloads_only(self):
        with tempfile.TemporaryDirectory(prefix="native-retention-") as directory:
            root = Path(directory)
            work = root / "run"
            preserved = ("report.json", "input.log", "snapshot-42.json",
                         "surface-native/save.json", "project/tests/input_test.gd",
                         "project/bin/freedom.gdextension", "userdata/save.json")
            disposable = ("native-assets/station.glb", "operating-motion/motion.glb",
                          "project/.godot/imported/station.scn", "cache/shader.bin",
                          "project/bin/libapsis_freedom_bridge.so", "snapshot-exporter")
            for name in (*preserved, *disposable):
                path = work / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
            outside = root / "source-study.glb"
            outside.write_bytes(b"original source")
            (work / "native-assets/source-link").symlink_to(outside)
            expected_bytes = sum((work / name).stat().st_size for name in disposable)
            report = {"selected": ["input"], "tests": [{"status": "pass"}],
                      "staged_executable_sha256": {"snapshot-exporter": "hash"}}
            runner.finish_work(work, report, False)
            self.assertEqual(report["retention"]["mode"], "compact")
            self.assertEqual(report["retention"]["removed_bytes"], expected_bytes)
            for name in preserved:
                self.assertEqual((work / name).read_bytes(), name.encode())
            for name in disposable:
                self.assertFalse((work / name).exists())
            self.assertEqual(outside.read_bytes(), b"original source")

    def test_failures_and_explicit_replay_retain_work(self):
        for statuses, keep, reason in ((["pass"], True, "requested"),
                                       (["engine_diagnostic"], False, "tests_failed"),
                                       ([], False, "tests_failed")):
            with tempfile.TemporaryDirectory(prefix="native-retention-") as directory:
                work = Path(directory)
                asset = work / "native-assets/station.glb"
                asset.parent.mkdir()
                asset.write_bytes(b"replay source")
                report = {"selected": ["input"],
                          "tests": [{"status": status} for status in statuses],
                          "staged_executable_sha256": {}}
                runner.finish_work(work, report, keep)
                self.assertEqual(report["retention"], {"mode": "full", "reason": reason})
                self.assertEqual(asset.read_bytes(), b"replay source")

    def test_cache_symlink_is_unlinked_without_following_it(self):
        with tempfile.TemporaryDirectory(prefix="native-retention-") as directory:
            root = Path(directory)
            work = root / "run"
            work.mkdir()
            outside = root / "source"
            outside.mkdir()
            (outside / "study.bin").write_bytes(b"keep")
            (work / "cache").symlink_to(outside, target_is_directory=True)
            report = {"selected": ["input"], "tests": [{"status": "pass"}],
                      "staged_executable_sha256": {}}
            runner.finish_work(work, report, False)
            self.assertFalse((work / "cache").is_symlink())
            self.assertEqual((outside / "study.bin").read_bytes(), b"keep")

    def test_cleanup_refuses_external_binary_and_symlinked_parent(self):
        with tempfile.TemporaryDirectory(prefix="native-retention-") as directory:
            root = Path(directory)
            work = root / "run"
            work.mkdir()
            outside = root / "source"
            outside.mkdir()
            original = outside / "exporter"
            original.write_bytes(b"keep")
            report = {"selected": ["input"], "tests": [{"status": "pass"}],
                      "staged_executable_sha256": {str(original): "hash"}}
            with self.assertRaises(ValueError):
                runner.finish_work(work, report, False)
            report["staged_executable_sha256"] = {}
            (work / "project").symlink_to(outside, target_is_directory=True)
            with self.assertRaises(ValueError):
                runner.finish_work(work, report, False)
            self.assertEqual(original.read_bytes(), b"keep")

    def test_runner_wires_retention_after_process_completion(self):
        with tempfile.TemporaryDirectory(prefix="native-retention-cli-") as directory:
            root = Path(directory)
            build = root / "build"
            binary = build / "src/godot/bin/libapsis_freedom_bridge.so"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"prebuilt bridge")
            exporter = build / "src/godot/apsis-drift-godot-snapshot"
            exporter.write_text("#!/usr/bin/env python3\nfrom pathlib import Path\n"
                                "import sys\nPath(sys.argv[1]).write_text('{}')\n")
            exporter.chmod(0o755)
            engine = root / "engine"
            for index, (code, extra, expected) in enumerate(
                    ((0, [], "compact"), (0, ["--keep-work"], "full"), (1, [], "full"))):
                engine.write_text(f"#!/bin/sh\necho '0 failures'\nexit {code}\n")
                engine.chmod(0o755)
                output = root / f"output-{index}"
                with contextlib.redirect_stdout(io.StringIO()):
                    result = runner.main(["--godot", str(engine), "--build-dir", str(build),
                                          "--output-parent", str(output), "--test", "input", *extra])
                self.assertEqual(result, code)
                work, = output.iterdir()
                report = json.loads((work / "report.json").read_text())
                self.assertEqual(report["retention"]["mode"], expected)
                self.assertEqual(report["bridge_sha256"], runner.sha256(binary))
                self.assertEqual(report["staged_executable_sha256"][exporter.name],
                                 runner.sha256(exporter))
                self.assertTrue((work / "input.log").exists())
                self.assertTrue((work / "snapshot-42.json").exists())
                self.assertTrue((work / "project/tests/input_test.gd").exists())
                self.assertEqual((work / "project/bin/libapsis_freedom_bridge.so").exists(),
                                 expected == "full")
                self.assertEqual((work / exporter.name).exists(), expected == "full")
            self.assertEqual(binary.read_bytes(), b"prebuilt bridge")

    def test_native_launcher_passes_title_without_an_empty_selection(self):
        source = Path(__file__).resolve().parents[1] / "tools/run_godot_native.sh"
        with tempfile.TemporaryDirectory(prefix="native-title-launch-") as directory:
            root = Path(directory)
            tools = root / "tools"
            tools.mkdir()
            launcher = tools / source.name
            launcher.write_bytes(source.read_bytes())
            (tools / "prepare_freedom_native_assets.py").write_text("pass\n")
            build = root / "build-native"
            build.mkdir()
            (build / "CMakeCache.txt").write_text(
                "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(root) + "\n"
                "APSIS_DRIFT_TERMINAL:BOOL=OFF\nAPSIS_DRIFT_GODOT_SPIKE:BOOL=ON\n"
                "APSIS_DRIFT_GODOT_LIVE:BOOL=ON\n")
            library = build / "src/godot/bin/libapsis_freedom_bridge.so"
            library.parent.mkdir(parents=True)
            library.write_bytes(b"selected default bridge")
            cmake = tools / "cmake"
            cmake.write_text("#!/bin/sh\nexit 0\n")
            cmake.chmod(0o755)
            engine = tools / "capture-engine"
            engine.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\"\n")
            engine.chmod(0o755)
            env = os.environ.copy()
            env["GODOT_BIN"] = str(engine)
            env["PATH"] = str(tools) + os.pathsep + env["PATH"]
            result = subprocess.run(["bash", str(launcher)], env=env,
                                    capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            arguments = result.stdout.splitlines()
            self.assertEqual(arguments, ["--path", str(root / "godot"), "--scene",
                             "res://scenes/native_start_shell.tscn", "--",
                             "--assets=" + str(build / "native-freedom-assets")])
            self.assertEqual((root / "godot/bin/libapsis_freedom_bridge.so").read_bytes(),
                             library.read_bytes())

    def test_native_launcher_rejects_ambiguous_or_relative_selection(self):
        launcher = Path(__file__).resolve().parents[1] / "tools/run_godot_native.sh"
        for arguments in (("--headless-validate",), ("--new-game=",),
                          ("--continue=relative.json",),
                          ("--new-game=42", "--continue=/tmp/freedom.json"),
                          ("--new-game=42", "--headless-validate", "--headless-validate"),
                          ("--build-dir=",), ("--build-dir=one", "--build-dir=two"),
                          ("--build-dir=.",), ("--build-dir=godot/build",),
                          ("--build-dir=src/build",), ("--build-dir=.git/nope",),
                          ("--build-dir=/",), ("--build-dir=bad\npath",)):
            result = subprocess.run(["bash", str(launcher), *arguments],
                                    capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 2, arguments)

    def test_native_launcher_reuses_cache_and_replaces_stale_bridge(self):
        source = Path(__file__).resolve().parents[1] / "tools/run_godot_native.sh"
        with tempfile.TemporaryDirectory(prefix="native-build-reuse-") as directory:
            outside = Path(directory)
            root = outside / "checkout with spaces"
            tools = root / "tools"
            tools.mkdir(parents=True)
            launcher = tools / source.name
            launcher.write_bytes(source.read_bytes())
            (tools / "prepare_freedom_native_assets.py").write_text("pass\n")
            build = root / "build qualified"
            build.mkdir()
            cache = build / "CMakeCache.txt"
            cache.write_text("CMAKE_HOME_DIRECTORY:INTERNAL=" + str(root) + "\n"
                             "APSIS_DRIFT_TERMINAL:BOOL=OFF\n"
                             "APSIS_DRIFT_GODOT_SPIKE:BOOL=ON\n"
                             "APSIS_DRIFT_GODOT_LIVE:BOOL=ON\n")
            library = build / "src/godot/bin/libapsis_freedom_bridge.so"
            library.parent.mkdir(parents=True)
            library.write_bytes(b"qualified chosen bridge")
            installed = root / "godot/bin/libapsis_freedom_bridge.so"
            installed.parent.mkdir(parents=True)
            cmake = tools / "cmake"
            cmake.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$APSIS_TEST_CMAKE_LOG"\n')
            cmake.chmod(0o755)
            engine = tools / "capture-engine"
            engine.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
            engine.chmod(0o755)
            log = outside / "cmake.log"
            env = {**os.environ, "GODOT_BIN": str(engine),
                   "PATH": str(tools) + os.pathsep + os.environ["PATH"],
                   "APSIS_TEST_CMAKE_LOG": str(log)}
            for selection in (build.name, str(build)):
                installed.write_bytes(b"stale unrelated bridge")
                result = subprocess.run(["bash", str(launcher), "--build-dir=" + selection],
                                        cwd=outside, env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(log.read_text().splitlines(),
                                 ["--build", str(build), "--target", "apsis_freedom_bridge",
                                  "--parallel", "4"])
                self.assertIn("--assets=" + str(build / "native-freedom-assets"),
                              result.stdout.splitlines())
                self.assertEqual(installed.read_bytes(), library.read_bytes())
                self.assertFalse((root / "build-native").exists())
                self.assertEqual(list(installed.parent.glob(installed.name + ".launch.*.new")), [])
            cmake.write_text("#!/bin/sh\nexit 27\n")
            installed.write_bytes(b"retain on failed build")
            result = subprocess.run(["bash", str(launcher), "--build-dir=" + str(build)],
                                    cwd=outside, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 27, result.stderr)
            self.assertEqual(installed.read_bytes(), b"retain on failed build")
            cmake.write_text("#!/bin/sh\nexit 0\n")
            copier = tools / "cp"
            copier.write_text('#!/bin/sh\nprintf "partial" > "$3"\nexit 23\n')
            copier.chmod(0o755)
            result = subprocess.run(["bash", str(launcher), "--build-dir=" + str(build)],
                                    cwd=outside, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 23, result.stderr)
            self.assertEqual(installed.read_bytes(), b"retain on failed build")
            self.assertEqual(list(installed.parent.glob(installed.name + ".launch.*.new")), [])
            copier.unlink()
            cache.write_text(cache.read_text().replace(str(root), str(outside / "other checkout")))
            log.unlink()
            installed.write_bytes(b"retain on refusal")
            result = subprocess.run(["bash", str(launcher), "--build-dir=" + str(build)],
                                    cwd=outside, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertFalse(log.exists())
            self.assertEqual(installed.read_bytes(), b"retain on refusal")

    def test_exit_and_markers(self):
        self.assertEqual(runner.verdict("input", 0, False, "Player input: 0 failures"), "pass")
        for code in (-6, -11, 1, 124, 134):
            self.assertEqual(runner.verdict("input", code, False, "0 failures"), "process_failed")
        self.assertEqual(runner.verdict("input", 0, True, "0 failures"), "timeout")
        for log in ("", "engine banner", "10 failures", "1 failures"):
            self.assertEqual(runner.verdict("input", 0, False, log), "missing_completion_marker")
        self.assertEqual(runner.verdict("validate", 0, False,
                         "Godot consumer: valid fixture, 20 invalid fixtures and bounded/nonfinite head-look passed"), "pass")
        self.assertNotEqual(runner.verdict("validate", 0, False, "0 failures"), "pass")

    def test_zero_exit_does_not_hide_engine_errors(self):
        for diagnostic in ("SCRIPT ERROR: bad parse", "ERROR: failed load",
                           "USER ERROR: bad fixture", "USER SCRIPT ERROR: assertion",
                           "WARNING: ObjectDB instances leaked at exit",
                           "WARNING: 6 ObjectDB instances were leaked at exit",
                           "ERROR: 3 resources still in use at exit"):
            self.assertEqual(runner.verdict("input", 0, False, "0 failures\n" + diagnostic),
                             "engine_diagnostic")
        # Shutdown contract intentionally exercises its bounded drain warning.
        self.assertEqual(runner.verdict("recorded_audio_shutdown", 0, False,
                         "WARNING: Audio shutdown reached its bounded drain deadline\n0 failures"), "pass")

    def test_invalid_deadlines(self):
        for value in ("0", "-1", "601", "nan", "inf", "-inf"):
            with self.assertRaises(argparse.ArgumentTypeError):
                runner.timeout_seconds(value)
        self.assertEqual(runner.timeout_seconds("1"), 1.0)
        self.assertEqual(runner.timeout_seconds("600"), 600.0)

    def test_process_logs_and_timeout(self):
        with tempfile.TemporaryDirectory(prefix="native-runner-test-") as directory:
            log = Path(directory) / "process.log"
            code, timed_out, _ = runner.run_logged(
                [sys.executable, "-c", "print('fixture'); raise SystemExit(7)"],
                log, os.environ.copy(), 2)
            self.assertEqual(code, 7)
            self.assertFalse(timed_out)
            self.assertIn("fixture", log.read_text())
            code, timed_out, elapsed = runner.run_logged(
                [sys.executable, "-c", "import time; time.sleep(30)"],
                log, os.environ.copy(), 0.1)
            self.assertTrue(timed_out)
            self.assertLess(code, 0)
            self.assertLess(elapsed, 3)


if __name__ == "__main__":
    unittest.main()
