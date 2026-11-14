#!/usr/bin/env python3
"""Test the capture driver process ownership with real, managed subprocesses."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "capture_driver", Path(__file__).with_name("native_frame_import_capture_test.py"))
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


class ProcessOwnershipTest(unittest.TestCase):
    def test_completed_process_has_its_own_session(self):
        with tempfile.TemporaryDirectory(dir=os.environ["LANE_TMP"]) as directory:
            log = Path(directory) / "success.log"
            with log.open("w") as output:
                result = driver.run_owned_process(
                    [sys.executable, "-c", "import os; print(os.getpid(), os.getpgrp(), os.getsid(0))"],
                    cwd=directory, env=os.environ.copy(), output=output, timeout=30)
            self.assertEqual(result.returncode, 0)
            pid, group, session = map(int, log.read_text().split())
            self.assertEqual((group, session), (pid, pid))
            with self.assertRaises(ChildProcessError):
                os.waitpid(pid, os.WNOHANG)

    def test_timeout_terminates_owned_group_and_reaps_direct_child(self):
        # This probe creates no descendants. The timed-out process and its group
        # are both owned here; no owner or unrelated process is ever signalled.
        with tempfile.TemporaryDirectory(dir=os.environ["LANE_TMP"]) as directory:
            log = Path(directory) / "timeout.log"
            with log.open("w") as output:
                with self.assertRaises(subprocess.TimeoutExpired):
                    driver.run_owned_process(
                        [sys.executable, "-c", "import os, time; print(os.getpid(), os.getpgrp(), flush=True); time.sleep(60)"],
                        cwd=directory, env=os.environ.copy(), output=output, timeout=1)
            pid, group = map(int, log.read_text().split())
            self.assertEqual(group, pid)
            with self.assertRaises(ProcessLookupError):
                os.killpg(group, 0)
            with self.assertRaises(ChildProcessError):
                os.waitpid(pid, os.WNOHANG)


class EngineDiagnosticsTest(unittest.TestCase):
    def test_positive_capture_diagnostics_are_detected(self):
        for diagnostic in (
                "ERROR: Failed to create rendering resource", "SCRIPT ERROR: Invalid call",
                "WARNING: ObjectDB instances leaked at exit (run with --verbose for details).",
                "ERROR: 1 resources still in use at exit (run with --verbose for details).",
                "WARNING: 1 RID allocations of type Viewport were leaked at exit."):
            with self.subTest(diagnostic=diagnostic):
                self.assertIsNotNone(driver.ENGINE_DIAGNOSTIC.search(diagnostic))

    def test_ordinary_completion_is_clean(self):
        self.assertIsNone(driver.ENGINE_DIAGNOSTIC.search(
            "Godot Engine v4.7.2.stable.official\nNative frame import capture: PASS\n"))


if __name__ == "__main__":
    unittest.main()
