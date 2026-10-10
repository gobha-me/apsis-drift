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
                "APSIS_DRIFT_TERMINAL:BOOL=OFF\nAPSIS_DRIFT_GODOT_SPIKE:BOOL=ON\n"
                "APSIS_DRIFT_GODOT_LIVE:BOOL=ON\n")
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

    def test_native_launcher_rejects_ambiguous_or_relative_selection(self):
        launcher = Path(__file__).resolve().parents[1] / "tools/run_godot_native.sh"
        for arguments in (("--headless-validate",), ("--new-game=",),
                          ("--continue=relative.json",),
                          ("--new-game=42", "--continue=/tmp/freedom.json"),
                          ("--new-game=42", "--headless-validate", "--headless-validate")):
            result = subprocess.run(["bash", str(launcher), *arguments],
                                    capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 2, arguments)

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
