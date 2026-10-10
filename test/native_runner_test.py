"""Runner control tests; no Godot, audio device, third-party asset or network."""
import argparse
import importlib.util
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
