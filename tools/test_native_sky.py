#!/usr/bin/env python3
"""Two-case software sky calibration; caller supplies DISPLAY (for example Xvfb)."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile
import time


def deadline(value):
    number = float(value)
    if not math.isfinite(number) or not 1 <= number <= 60:
        raise argparse.ArgumentTypeError("timeout must be finite, 1..60 seconds")
    return number


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", required=True, type=Path)
    parser.add_argument("--output-parent", required=True, type=Path)
    parser.add_argument("--timeout", type=deadline, default=30.0)
    args = parser.parse_args()
    engine = args.godot.resolve()
    if not engine.is_file() or not os.access(engine, os.X_OK):
        parser.error("missing explicit Godot executable")
    if not os.environ.get("DISPLAY"):
        parser.error("DISPLAY is required; supply an isolated X11 display")
    repo = Path(__file__).resolve().parent.parent
    resources = {"shaders/native_planet_optics.gdshaderinc": repo / "godot/shaders/native_planet_optics.gdshaderinc",
                 "tests/native_sky_camera_test.gd": repo / "godot/tests/native_sky_camera_test.gd",
                 "shaders/native_navigation_sky.gdshader": repo / "godot/shaders/native_navigation_sky.gdshader"}
    if not all(path.is_file() for path in resources.values()):
        parser.error("missing sky calibration source")
    args.output_parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="sky-camera-", dir=args.output_parent.resolve()))
    project = work / "project"
    project.mkdir()
    pins = {name: digest(source) for name, source in resources.items()}
    for name, source in resources.items():
        target = project / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source.read_bytes())
    (project / "project.godot").write_text('config_version=5\n[display]\nwindow/size/viewport_width=3840\nwindow/size/viewport_height=2160\n[rendering]\nrenderer/rendering_method="gl_compatibility"\n')
    env = os.environ.copy()
    for key in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "DBUS_SESSION_BUS_ADDRESS"):
        env.pop(key, None)
    env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    for key in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"):
        path = work / key.lower()
        path.mkdir(mode=0o700)
        env[key] = str(path)
    command = [str(engine), "--display-driver", "x11", "--rendering-method", "gl_compatibility",
               "--audio-driver", "Dummy", "--resolution", "3840x2160", "--path", str(project),
               "--script", "res://tests/native_sky_camera_test.gd", "--", str(work)]
    report = {"scope": "Software analytic shader regression only", "source_sha256": pins, "runner_sha256": digest(Path(__file__)),
              "engine_sha256": digest(engine), "command": command, "timeout_seconds": args.timeout}
    started = time.monotonic()
    timed_out = False
    with (work / "engine.log").open("wb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                   env=env, start_new_session=True)
        try:
            code = process.wait(timeout=args.timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            timed_out = True
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            code = process.wait()
    output = (work / "engine.log").read_text(errors="replace")
    diagnostics = re.findall(r"^(?:SCRIPT ERROR|ERROR|USER ERROR|USER SCRIPT ERROR):.*$|^.*(?:instances leaked|resources still in use at exit).*$", output, re.MULTILINE)
    result = json.loads((work / "sky-result.json").read_text()) if (work / "sky-result.json").exists() else {}
    unchanged = all(digest(source) == pins[name] and digest(project / name) == pins[name]
                    for name, source in resources.items())
    passed = code == 0 and not timed_out and not diagnostics and unchanged and result.get("pass") is True
    report.update(returncode=code, timed_out=timed_out, seconds=time.monotonic() - started,
                  diagnostics=diagnostics, sources_unchanged=unchanged, capture=result, passed=passed)
    (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{'PASS' if passed else 'FAIL'} native sky camera; evidence retained in {work}", flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
