#!/usr/bin/env python3
"""Run native contracts in an isolated Linux project with silent Dummy audio.

Consumes a prebuilt snapshot exporter/bridge; never builds, imports the editor,
touches player preferences, downloads assets, or starts a visible game window.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time


# Explicit opt-in list: GPU reviews, private assets, audible captures and cadence
# diagnostics are deliberately not mistaken for headless acceptance tests.
TESTS = {
    "validate": "snapshot",
    "input": "none",
    "chase_camera": "none",
    "flight_status": "none",
    "planet_lighting": "none",
    "flight_plan_menu": "none",
    "flight_basics": "none",
    "film": "none",
    "asset_materials": "none",
    "pilot_presentation": "none",
    "pilot_view_integration": "none",
    "pilot_motion": "none",
    "pilot_motion_input": "named_snapshot",
    "pilot_motion_integration": "named_snapshot",
    "pilot_rig_groups": "none",
    "audio_preferences": "none",
    "recorded_ship_audio": "none",
    "ship_audio": "none",
    "ship_audio_playback": "none",
    "live": "snapshot",
    "physical_world": "physical_snapshots",
    "freedom_start": "freedom_saves",
    "native_shell": "freedom_saves",
    "native_save": "freedom_saves",
    "physical_lighting_integration": "physical_snapshots",
    "thrust": "snapshot",
    "rotation_coast": "snapshot",
    "surface_start": "snapshot",
    "guidance": "snapshot",
    "stream": "snapshot",
    "orbit_assist": "two_snapshots",
    "guidance_integration": "named_snapshot",
    "ship_audio_integration": "named_snapshot",
    "recorded_audio_integration": "named_snapshot",
    "recorded_audio_shutdown": "named_snapshot",
}
ERROR = re.compile(
    r"^(?:SCRIPT ERROR|ERROR|USER ERROR|USER SCRIPT ERROR):|"
    r"ObjectDB.*instances.*leak|resources still in use at exit",
    re.MULTILINE,
)
SUCCESS = re.compile(r"\b0 failures\b")


def timeout_seconds(value):
    number = float(value)
    if not math.isfinite(number) or not 1 <= number <= 600:
        raise argparse.ArgumentTypeError("timeout must be finite, 1..600 seconds")
    return number


def verdict(name, returncode, timed_out, output):
    if timed_out:
        return "timeout"
    if returncode != 0:
        return "process_failed"
    if ERROR.search(output):
        return "engine_diagnostic"
    marker = ("Godot consumer: valid fixture," in output and
              "invalid fixtures and bounded/nonfinite head-look passed" in output
              if name == "validate" else bool(SUCCESS.search(output)))
    return "pass" if marker else "missing_completion_marker"


def run_logged(command, log, env, timeout):
    start = time.monotonic()
    timed_out = False
    # Preserve complete diagnostics even if a test aborts or times out.
    with log.open("wb") as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                   env=env, start_new_session=True)
        try:
            process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt) as error:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass # Process exited between the deadline and group cleanup.
            process.wait()
            if isinstance(error, KeyboardInterrupt):
                raise
            timed_out = True
    return process.returncode, timed_out, time.monotonic() - start


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", required=True, type=Path,
                        help="explicit Godot executable (no download)")
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--output-parent", type=Path, default=Path("build-godot"))
    parser.add_argument("--timeout", type=timeout_seconds, default=120.0,
                        help="per-process deadline in seconds, default 120")
    parser.add_argument("--test", action="append", choices=TESTS,
                        help="repeat to select contracts; default is all")
    args = parser.parse_args(argv)
    if not sys.platform.startswith("linux"):
        parser.error("this runner currently stages the Linux .so extension only")
    repo = Path(__file__).resolve().parent.parent
    source = repo / "experiments/godot-freedom"
    build = args.build_dir.resolve() / "experiments/godot-freedom"
    engine = args.godot.resolve()
    exporter = build / "apsis-drift-godot-snapshot"
    bridge = build / "bin/libapsis_freedom_bridge.so"
    start_fixture = build / "apsis-drift-freedom-start-fixture"
    for path in (engine, exporter):
        if not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f"missing executable: {path}")
    if not bridge.is_file():
        parser.error(f"missing native bridge: {bridge}; build GODOT_LIVE first")
    selected = list(dict.fromkeys(args.test or TESTS))
    if any(TESTS[name] == "freedom_saves" for name in selected):
        if not start_fixture.is_file() or not os.access(start_fixture, os.X_OK):
            parser.error(f"missing C++ save fixture: {start_fixture}")
    def prepare_physical_fixtures():
        for seed in (42, 43):
            path = work / f"physical-{seed}.json"
            log = work / f"physical-{seed}.log"
            result = run_logged([str(exporter), str(path), f"--physical-origin={seed}",
                                 "--samples=3", "--span-metres=32000", "--relief-version=1"],
                                log, env, args.timeout)
            report["setup"].append({"family": "physical_circular", "seed": seed,
                                    "returncode": result[0], "timed_out": result[1]})
            save()
            if result[0] != 0 or result[1]:
                print(f"FAIL physical fixture {seed}: {log}", flush=True)
                return 1
            report["setup"][-1]["snapshot_sha256"] = sha256(path)
            save()
        # Generate the genuine same-ID procedural counterpart, never forge its
        # descriptor or replay by editing the authored-home snapshot.
        home_seed = json.loads((work / "physical-42.json").read_text())["planet"]["planet_seed"]
        alias_path = work / "procedural-home-42.json"
        alias_log = work / "procedural-home-42.log"
        result = run_logged([str(exporter), str(alias_path), home_seed, "3", "32000", "1"],
                            alias_log, env, args.timeout)
        report["setup"].append({"family": "standalone", "seed": home_seed,
                                "returncode": result[0], "timed_out": result[1]})
        save()
        if result[0] != 0 or result[1]:
            print(f"FAIL procedural home fixture: {alias_log}", flush=True)
            return 1
        report["setup"][-1]["snapshot_sha256"] = sha256(alias_path)
        save()
        return 0

    # Preflight every source before creating output or running a subprocess.
    for name in selected:
        if name == "native_shell":
            if not all((source / path).is_file() for path in
                       ("native_start_shell.tscn", "native_start_shell.gd")):
                parser.error("missing native start shell scene or script")
        elif not (source / f"{name}_test.gd").is_file():
            parser.error(f"missing test source: {name}")
    args.output_parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="native-contracts-",
                                 dir=args.output_parent.resolve()))
    project = work / "project"
    project.mkdir()
    for path in source.iterdir():
        if path.is_file() and path.suffix in (".gd", ".gdshader", ".godot", ".tscn", ".json"):
            shutil.copy2(path, project / path.name)
    (project / "bin").mkdir()
    shutil.copy2(source / "bin/freedom.gdextension", project / "bin/freedom.gdextension")
    shutil.copy2(bridge, project / "bin/libapsis_freedom_bridge.so")
    # Stage the matching exporter too: later builds cannot replace our fixtures'
    # binary halfway through this run. The caller must finish building first.
    shutil.copy2(exporter, work / exporter.name)
    exporter = work / exporter.name
    if any(TESTS[name] == "freedom_saves" for name in selected):
        shutil.copy2(start_fixture, work / start_fixture.name)
        start_fixture = work / start_fixture.name
    env = os.environ.copy()
    env.update({"XDG_DATA_HOME": str(work / "userdata"),
                "XDG_CONFIG_HOME": str(work / "config"),
                "XDG_CACHE_HOME": str(work / "cache"),
                "GODOT_SILENCE_ROOT_WARNING": "1"})
    report = {"schema_version": 1, "scope": "headless contracts, not GPU/hardware/listening acceptance",
              "selected": selected, "tests": [], "setup": [],
              "engine_sha256": sha256(engine),
              "project_sha256": {path.name: sha256(path) for path in sorted(project.iterdir())
                                 if path.is_file()},
              "extension_descriptor_sha256": sha256(project / "bin/freedom.gdextension"),
              "bridge_sha256": sha256(project / "bin/libapsis_freedom_bridge.so"),
              "exporter_sha256": sha256(exporter)}

    def save():
        (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")

    print(f"Native contracts: {work}", flush=True)
    save()
    # Seed 42 is atmospheric; seed 4 is airless under generator v1. Both are
    # generated by the selected build, not edited/borrowed historical snapshots.
    for seed in (42, 4):
        log = work / f"snapshot-{seed}.log"
        result = run_logged([str(exporter), str(work / f"snapshot-{seed}.json"),
                             str(seed), "3", "32000", "1"], log, env, args.timeout)
        report["setup"].append({"seed": seed, "returncode": result[0],
                                "timed_out": result[1]})
        save()
        if result[0] != 0 or result[1]:
            print(f"FAIL fixture {seed}: {log}", flush=True)
            return 1
        report["setup"][-1]["snapshot_sha256"] = sha256(work / f"snapshot-{seed}.json")
        save()
    if any(TESTS[name] == "physical_snapshots" for name in selected):
        if prepare_physical_fixtures() != 0:
            return 1
    if any(TESTS[name] == "freedom_saves" for name in selected):
        for filename, tick, mode in (("freedom-0.json", "0", "freedom"),
                                     ("freedom-25.json", "25", "freedom"),
                                     ("career.json", "0", "career"),
                                     ("flight-18.json", "25", "flight")):
            path = work / filename
            log = work / f"{filename}.log"
            code, timed_out, _ = run_logged(
                [str(start_fixture), str(path), "42", tick, mode],
                log, env, args.timeout)
            report["setup"].append({"save": filename, "returncode": code,
                                    "timed_out": timed_out})
            save()
            if code != 0 or timed_out:
                print(f"FAIL C++ save fixture {filename}: {log}", flush=True)
                return 1
            report["setup"][-1]["save_sha256"] = sha256(path)
            save()
        (work / "corrupt.json").write_text("{broken json\n")
    for name in selected:
        if name == "native_shell":
            cases = (
                ("unsupported_flight", [f"--continue={work / 'flight-18.json'}",
                                        "--validate-only"], "", False),
                ("new", ["--new-game=42", "--validate-only"],
                 "seed=42 tick=0", True),
                ("view", ["--new-game=42"], "seed=42 tick=0", True),
                ("continue_zero", [f"--continue={work / 'freedom-0.json'}",
                                   "--validate-only"], "seed=42 tick=0", True),
                ("continue_progressed", [f"--continue={work / 'freedom-25.json'}",
                                        "--validate-only"],
                 "seed=42 tick=25", True),
                ("missing", ["--validate-only"], "", False),
                ("ambiguous", ["--new-game=42", f"--continue={work / 'freedom-0.json'}",
                               "--validate-only"], "", False),
                ("bad_seed", ["--new-game=01", "--validate-only"], "", False),
                ("career", [f"--continue={work / 'career.json'}",
                            "--validate-only"], "", False),
                ("corrupt", [f"--continue={work / 'corrupt.json'}",
                             "--validate-only"], "", False),
            )
            source_hashes = {path.name: sha256(path) for path in
                             (work / "freedom-0.json", work / "freedom-25.json",
                              work / "career.json", work / "corrupt.json",
                              work / "flight-18.json")}
            case_reports = []
            selected_views = {}
            spatial_views = {}
            for label, arguments, expected, accepted in cases:
                log = work / f"native_shell-{label}.log"
                command = [str(engine), "--headless", "--audio-driver", "Dummy",
                           "--path", str(project), "--scene",
                           "res://native_start_shell.tscn"]
                if label == "view":
                    command += ["--quit-after", "2"]
                command += ["--", *arguments]
                code, timed_out, elapsed = run_logged(command, log, env, args.timeout)
                output = log.read_text(errors="replace")
                view_far = re.search(r"dock_view=3d far=([0-9.]+)", output) if label == "view" else None
                if accepted:
                    marker = re.search(
                        r"Freedom native shell (?:validated|opened): seed=(\d+) tick=(\d+) "
                        r"system=(\d+) planet=(\d+) station=(\d+) craft=(\d+) "
                        r"discoveries=(\d+) deltas=(\d+)", output)
                    if marker:
                        selected_views[label] = marker.groups()
                    spatial = re.search(
                        r"radius=([0-9.]+) distance=([0-9.]+) "
                        r"phase=([-0-9.]+)", output)
                    if spatial:
                        spatial_views[label] = spatial.groups()
                good = (not timed_out and code == 0 and
                        ("Freedom native shell validated: " in output or
                         "Freedom native shell opened: " in output) and
                        expected in output and marker is not None and
                        spatial is not None and
                        (label != "view" or (view_far is not None and
                                             0 < float(view_far[1]) < 10000)) and
                        not ERROR.search(output)) if accepted else (
                        not timed_out and code != 0 and
                        "Native start rejected:" in output)
                case_reports.append({"case": label, "pass": good, "returncode": code,
                                     "seconds": elapsed, "log": log.name})
            unchanged = all(sha256(work / filename) == digest for filename, digest
                            in source_hashes.items())
            new_view = selected_views.get("new")
            identity_matches = bool(new_view) and all(
                selected_views.get(label) and
                tuple(selected_views[label][index] for index in (0, 2, 3, 4, 5)) ==
                tuple(new_view[index] for index in (0, 2, 3, 4, 5))
                for label in ("view", "continue_zero", "continue_progressed"))
            history_matches = (bool(new_view) and new_view[1] == "0" and
                               selected_views.get("continue_zero", (None,) * 8)[1] == "0" and
                               selected_views.get("continue_progressed", (None,) * 8)[1] == "25" and
                               selected_views.get("continue_progressed", (None,) * 8)[6:] ==
                               ("1", "1"))
            new_spatial = spatial_views.get("new")
            visible_spatial = spatial_views.get("view")
            geometry_matches = (bool(new_spatial) and
                                bool(visible_spatial) and
                                abs(float(visible_spatial[0]) - float(new_spatial[0])) < 1.0 and
                                abs(float(visible_spatial[1]) - float(new_spatial[1])) < 1.0 and
                                visible_spatial[2] == new_spatial[2] and
                                spatial_views.get("continue_zero") == new_spatial and
                                bool(spatial_views.get("continue_progressed")) and
                                spatial_views["continue_progressed"][0] == new_spatial[0] and
                                abs(float(spatial_views["continue_progressed"][1]) -
                                    float(new_spatial[1])) < 1.0 and
                                spatial_views["continue_progressed"][2] != new_spatial[2])
            status = "pass" if (all(case["pass"] for case in case_reports) and
                                unchanged and identity_matches and history_matches and
                                geometry_matches) else "fail"
            report["tests"].append({"name": name, "status": status,
                                    "cases": case_reports,
                                    "source_saves_unchanged": unchanged,
                                    "identity_matches": identity_matches,
                                    "history_matches": history_matches,
                                    "geometry_matches": geometry_matches})
            save()
            print(f"{status.upper()} {name} ({len(cases)} launch cases)", flush=True)
            continue
        arguments = []
        if TESTS[name] == "snapshot":
            arguments = [str(work / "snapshot-42.json")]
        elif TESTS[name] == "two_snapshots":
            arguments = [str(work / f"snapshot-{seed}.json") for seed in (42, 4)]
        elif TESTS[name] == "named_snapshot":
            arguments = [f"--snapshot={work / 'snapshot-42.json'}"]
        elif TESTS[name] == "physical_snapshots":
            arguments = [str(work / path) for path in
                         ("snapshot-42.json", "physical-42.json", "physical-43.json", "procedural-home-42.json")]
        elif TESTS[name] == "freedom_saves":
            arguments = [str(work / path) for path in
                         ("freedom-0.json", "freedom-25.json", "career.json",
                          "corrupt.json", "snapshot-42.json")]
        if name == "freedom_start":
            arguments.append(str(work / "flight-18.json"))
        log = work / f"{name}.log"
        command = [str(engine), "--headless", "--audio-driver", "Dummy",
                   "--path", str(project), "--script", f"res://{name}_test.gd",
                   "--", *arguments]
        code, timed_out, elapsed = run_logged(command, log, env, args.timeout)
        output = log.read_text(errors="replace")
        status = verdict(name, code, timed_out, output)
        report["tests"].append({"name": name, "status": status, "returncode": code,
                                "seconds": elapsed, "log": log.name})
        save()
        print(f"{status.upper()} {name} ({elapsed:.2f}s)", flush=True)
    passed = sum(test["status"] == "pass" for test in report["tests"])
    print(f"{passed}/{len(selected)} native contracts passed; logs retained in {work}")
    return 0 if passed == len(selected) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
