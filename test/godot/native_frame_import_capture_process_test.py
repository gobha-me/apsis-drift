#!/usr/bin/env python3
"""Test the capture driver process ownership with real, managed subprocesses."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

spec = importlib.util.spec_from_file_location(
    "capture_driver", Path(__file__).with_name("native_frame_import_capture_test.py"))
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


class ProcessOwnershipTest(unittest.TestCase):
    def test_completed_process_has_its_own_session(self):
        with tempfile.TemporaryDirectory() as directory:
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
        with tempfile.TemporaryDirectory() as directory:
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


class XvfbOwnershipTest(unittest.TestCase):
    # A real child connects to the real private X11 socket before reporting its
    # identity. Neither the runner nor Xvfb is replaced by a mock.
    CHILD = """
import json, os, socket, sys, time
display = os.environ["DISPLAY"]
connection = socket.socket(socket.AF_UNIX)
connection.connect("/tmp/.X11-unix/X" + display[1:])
connection.close()
print(json.dumps({"pid": os.getpid(), "group": os.getpgrp(),
                  "session": os.getsid(0), "display": display}), flush=True)
if sys.argv[1] == "timeout":
    time.sleep(60)
sys.exit(int(sys.argv[1]))
"""

    def run_child(self, outcome, timeout=30):
        directory = tempfile.mkdtemp(prefix="native-xvfb-cleanup-")
        print(f"Retained Xvfb cleanup evidence: {directory}", flush=True)
        root = Path(directory)
        env = os.environ.copy()
        for name in ("TMPDIR", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
            target = root / name.lower()
            target.mkdir()
            env[name] = str(target)
        env["LP_NUM_THREADS"] = "2"
        log = root / "xvfb-child.log"
        command = [sys.executable, "-c", self.CHILD, outcome]
        started = time.monotonic()
        with log.open("w") as output:
            if outcome == "timeout":
                with self.assertRaises(subprocess.TimeoutExpired):
                    driver.run_with_owned_xvfb(
                        command, size="800x450", cwd=root, env=env,
                        output=output, timeout=timeout)
            else:
                result = driver.run_with_owned_xvfb(
                    command, size="800x450", cwd=root, env=env,
                    output=output, timeout=timeout)
                self.assertEqual(result.returncode, int(outcome))
        elapsed = time.monotonic() - started
        lines = log.read_text().splitlines()
        servers = [line for line in lines if line.startswith("Owned Xvfb pid=")
                   and line.endswith(" started")]
        self.assertEqual(len(servers), 1, lines)
        server = int(servers[0].split("pid=")[1].split()[0])
        reaped = [line for line in lines
                  if line.startswith(f"Owned Xvfb pid={server} reaped returncode=")]
        self.assertEqual(len(reaped), 1, lines)
        children = [json.loads(line) for line in lines if line.startswith("{")]
        self.assertEqual(len(children), 1, lines)
        child = children[0]
        self.assertEqual((child["group"], child["session"]),
                         (child["pid"], child["pid"]))
        self.assertIn(f"Owned Xvfb pid={server} display={child['display']}", lines)
        for pid in (server, child["pid"]):
            with self.subTest(pid=pid):
                with self.assertRaises(ProcessLookupError):
                    os.kill(pid, 0)
                with self.assertRaises(ChildProcessError):
                    os.waitpid(pid, os.WNOHANG)
        with self.assertRaises(ProcessLookupError):
            os.killpg(child["group"], 0)
        if outcome == "timeout":
            self.assertLess(elapsed, timeout + 2)
        else:
            self.assertEqual(reaped, [f"Owned Xvfb pid={server} reaped returncode=0"])

    def test_normal_child_reaps_both_processes(self):
        self.run_child("0")

    def test_nonzero_child_retains_status_and_reaps_both_processes(self):
        self.run_child("37")

    def test_timeout_child_reaps_both_processes(self):
        self.run_child("timeout", timeout=3)


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
