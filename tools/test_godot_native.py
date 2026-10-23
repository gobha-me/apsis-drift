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
import stat
import subprocess
import sys
import tempfile
import time


# Explicit opt-in list: GPU reviews, private assets, audible captures and cadence
# diagnostics are deliberately not mistaken for headless acceptance tests.
TESTS = {
    "native_status": "none",
    "validate": "snapshot",
    "input": "none",
    "control_settings": "none",
    "recorded_observations": "none",
    "chase_camera": "none",
    "flight_status": "none",
    "planet_lighting": "none",
    "flight_plan_menu": "none",
    "flight_basics": "none",
    "home_navigation": "none",
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
    "native_asset_import": "native_assets",
    "native_station_view": "native_assets",
    "wayfarer_frame": "native_assets",
    "saved_flight": "freedom_saves",
    "orbital_survey": "orbital_survey_saves",
    "native_environment": "freedom_saves",
    "native_audio": "freedom_saves",
    "first_jump": "freedom_saves",
    "native_moon": "freedom_saves",
    "native_minor": "freedom_saves",
    "native_derelict": "freedom_saves",
    "native_recovery": "freedom_saves",
    "native_port": "freedom_saves",
    "native_walk": "freedom_saves",
    "native_station_crew": "freedom_saves",
    "native_assembly": "native_assets",
    "native_start_staging": "freedom_saves",
    "native_title": "freedom_saves",
    "native_profile": "freedom_saves",
    "native_planetary": "planetary_saves",
    "native_voyage": "voyage_saves",
    "native_roundtrip": "roundtrip_saves",
    "native_roundtrip_pilot": "roundtrip_saves",
    "native_roundtrip_pilot_aligned": "roundtrip_saves",
    "native_roundtrip_pilot_offset": "roundtrip_saves",
    "native_surface": "surface_saves",
    "native_surface_detail": "surface_saves",
    "native_selected_terrain": "freedom_saves",
    "native_impact": "impact_saves",
    "native_frame_import": "frame_import_saves",
    "native_system_release": "system_release_saves",
    "native_surface_walk": "surface_walk_saves",
    "native_surface_walk_detail": "surface_walk_saves",
    "native_surface_loop": "surface_loop_saves",
    "native_first_flight_continuation": "roundtrip_saves",
    "wayfarer_operating": "operating_assets",
    "operating_motion": "operating_motion",
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
SURFACE_TESTS = {
    "native_surface": ("surface", "--fixtures", 1),
    "native_surface_detail": ("surface-detail", "--local-detail-fixtures", 2),
    "native_surface_walk": ("surface-walk", "--walk-fixtures", 1),
    "native_surface_walk_detail": ("surface-walk-detail", "--local-detail-walk-fixtures", 2),
}
ROUNDTRIP_TESTS = tuple(name for name, family in TESTS.items() if family == "roundtrip_saves")
ERROR = re.compile(
    r"^(?:SCRIPT ERROR|ERROR|USER ERROR|USER SCRIPT ERROR):|"
    r"ObjectDB.*instances.*leak|resources still in use at exit",
    re.MULTILINE,
)
SUCCESS = re.compile(r"\b0 failures\b")


def timeout_seconds(value):
    number = float(value)
    if not math.isfinite(number) or not 1 <= number <= 900:
        raise argparse.ArgumentTypeError("timeout must be finite, 1..900 seconds")
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


def stage_helper(source, destination):
    """Reuse identical staged helpers without rewriting read-only snapshots."""
    try:
        metadata = destination.lstat()
    except FileNotFoundError:
        shutil.copy2(source, destination)
        return
    if not stat.S_ISREG(metadata.st_mode) or sha256(source) != sha256(destination):
        raise ValueError(f"refusing changed or nonregular staged helper: {destination}")


def finish_work(work, report, keep_work):
    """Compact only this completed run; failed runs remain replayable."""
    passed = (len(report["tests"]) == len(report["selected"]) and
              all(test["status"] == "pass" for test in report["tests"]))
    if keep_work or not passed:
        report["retention"] = {"mode": "full", "reason":
                               "requested" if keep_work else "tests_failed"}
        return
    disposable = ["native-assets", "operating-motion", "project/.godot", "cache",
                  "project/bin/libapsis_freedom_bridge.so"]
    for name in report["staged_executable_sha256"]:
        if Path(name).name != name or name in (".", ".."):
            raise ValueError("staged executable must be a filename")
        disposable.append(name)
    for relative in disposable:
        parent = work
        for component in Path(relative).parts[:-1]:
            parent /= component
            if parent.is_symlink():
                raise ValueError("generated artifact parent is a symlink")
    removed = []
    for relative in disposable:
        path = work / relative
        # Never follow a cache/artifact symlink out of this owned run.
        if path.is_symlink():
            size = 0
            path.unlink()
        elif path.is_dir():
            size = sum(child.stat().st_size for child in path.rglob("*")
                       if not child.is_symlink() and child.is_file())
            shutil.rmtree(path)
        elif path.is_file():
            size = path.stat().st_size
            path.unlink()
        else:
            continue
        removed.append({"path": relative, "bytes": size})
    report["retention"] = {"mode": "compact", "removed": removed,
                           "removed_bytes": sum(item["bytes"] for item in removed)}


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
    parser.add_argument("--selected-surface-loop", action="store_true",
                        help="explicit policy-2 station starter; requires --test native_surface_loop only")
    parser.add_argument("--flight-profile", choices=("assisted", "advanced"),
                        help="explicit physical profile; requires --selected-surface-loop")
    parser.add_argument("--roundtrip-from-save", type=Path,
                        help="continue one actual ascent save; requires only native_roundtrip or native_roundtrip_pilot")
    parser.add_argument("--keep-work", action="store_true",
                        help="retain successful asset/import/binary copies for replay; failures always retain them")
    args = parser.parse_args(argv)
    if args.flight_profile and not args.selected_surface_loop:
        parser.error("--flight-profile requires --selected-surface-loop")
    if args.selected_surface_loop and args.test != ["native_surface_loop"]:
        parser.error("--selected-surface-loop requires --test native_surface_loop only")
    continuation = None
    continuation_bytes = None
    if args.roundtrip_from_save is not None:
        if args.selected_surface_loop or args.flight_profile or args.test not in (
                ["native_roundtrip"], ["native_roundtrip_pilot"]):
            parser.error("--roundtrip-from-save requires only native_roundtrip or native_roundtrip_pilot")
        try:
            continuation = args.roundtrip_from_save.resolve()
            if len(os.fsencode(continuation)) > 4000 or not continuation.is_file():
                parser.error("--roundtrip-from-save requires a bounded regular save file")
            with continuation.open("rb") as stream:
                continuation_bytes = stream.read((1 << 20) + 1)
            if not 0 < len(continuation_bytes) <= (1 << 20):
                parser.error("--roundtrip-from-save requires a nonempty save within the existing 1 MiB format limit")
        except (OSError, RuntimeError) as error:
            parser.error(f"unavailable --roundtrip-from-save: {error}")
    if not sys.platform.startswith("linux"):
        parser.error("this runner currently stages the Linux .so extension only")
    repo = Path(__file__).resolve().parent.parent
    source = repo / "godot"
    build = args.build_dir.resolve() / "src/godot"
    engine = args.godot.resolve()
    exporter = build / "apsis-drift-godot-snapshot"
    bridge = build / "bin/libapsis_freedom_bridge.so"
    start_fixture = build / "apsis-drift-freedom-start-fixture"
    moon_fixture = build / "apsis-drift-freedom-moon-fixture"
    minor_fixture = build / "apsis-drift-freedom-minor-fixture"
    planetary_fixture = build / "apsis-drift-freedom-planetary-fixture"
    surface_fixture = args.build_dir.resolve() / "apsis-drift-landed-craft-tests"
    impact_fixture = args.build_dir.resolve() / "apsis-drift-native-impact-tests"
    frame_import_fixture = args.build_dir.resolve() / "apsis-drift-freedom-flight-import-tests"
    system_release_fixture = args.build_dir.resolve() / "apsis-drift-native-system-release-tests"
    survey_fixture = args.build_dir.resolve() / "apsis-drift-native-orbital-survey-tests"
    derelict_fixture = args.build_dir.resolve() / "apsis-drift-native-derelict-tests"
    for path in (engine, exporter):
        if not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f"missing executable: {path}")
    if not bridge.is_file():
        parser.error(f"missing native bridge: {bridge}; build GODOT_LIVE first")
    selected = list(dict.fromkeys(args.test or TESTS))
    if any(TESTS[name] == "freedom_saves" for name in selected):
        if not start_fixture.is_file() or not os.access(start_fixture, os.X_OK):
            parser.error(f"missing C++ save fixture: {start_fixture}")
    if "native_minor" in selected:
        if not minor_fixture.is_file() or not os.access(minor_fixture, os.X_OK):
            parser.error(f"missing actual minor route fixture: {minor_fixture}")
    if "native_moon" in selected:
        if not moon_fixture.is_file() or not os.access(moon_fixture, os.X_OK):
            parser.error(f"missing actual Moon route fixture: {moon_fixture}")
    if any(name in selected for name in ("native_planetary", "native_voyage", "native_surface_loop", *ROUNDTRIP_TESTS)):
        if not planetary_fixture.is_file() or not os.access(planetary_fixture, os.X_OK):
            parser.error(f"missing C++ planetary fixture: {planetary_fixture}")
    if "native_environment" in selected or any(name in selected for name in SURFACE_TESTS):
        if not surface_fixture.is_file() or not os.access(surface_fixture, os.X_OK):
            parser.error(f"missing C++ surface fixture: {surface_fixture}")
    if "native_impact" in selected:
        if not impact_fixture.is_file() or not os.access(impact_fixture, os.X_OK):
            parser.error(f"missing C++ impact fixture: {impact_fixture}")
    if "native_frame_import" in selected:
        if not frame_import_fixture.is_file() or not os.access(frame_import_fixture, os.X_OK):
            parser.error(f"missing C++ frame import fixture: {frame_import_fixture}")
    if "native_system_release" in selected:
        if not system_release_fixture.is_file() or not os.access(system_release_fixture, os.X_OK):
            parser.error(f"missing C++ system release fixture: {system_release_fixture}")
    if "orbital_survey" in selected:
        if not survey_fixture.is_file() or not os.access(survey_fixture, os.X_OK):
            parser.error(f"missing C++ orbital survey fixture: {survey_fixture}")
    if any(name in selected for name in ("native_derelict", "native_station_crew")):
        if not derelict_fixture.is_file() or not os.access(derelict_fixture, os.X_OK):
            parser.error(f"missing C++ derelict fixture: {derelict_fixture}")
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
                       ("scenes/native_start_shell.tscn", "scripts/native/native_start_shell.gd")):
                parser.error("missing native start shell scene or script")
        elif not (source / "tests" / f"{name}_test.gd").is_file():
            parser.error(f"missing test source: {name}")
    args.output_parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="native-contracts-",
                                 dir=args.output_parent.resolve()))
    project = work / "project"
    project.mkdir()
    # Stage presentation resources, not unrelated local asset closures or caches.
    resource_paths = [source / "project.godot"]
    for folder in ("scenes", "scripts", "shaders", "settings", "tests", "studies"):
        resource_paths.extend((source / folder).rglob("*"))
    for path in resource_paths:
        relative = path.relative_to(source)
        if any(part.startswith(".") or part == "bin" for part in relative.parts):
            continue
        if path.is_file() and path.suffix in (".gd", ".gdshader", ".gdshaderinc", ".godot", ".tscn", ".json"):
            destination = project / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    (project / "bin").mkdir()
    shutil.copy2(source / "bin/freedom.gdextension", project / "bin/freedom.gdextension")
    shutil.copy2(bridge, project / "bin/libapsis_freedom_bridge.so")
    # Stage the matching exporter too: later builds cannot replace our fixtures'
    # binary halfway through this run. The caller must finish building first.
    shutil.copy2(exporter, work / exporter.name)
    exporter = work / exporter.name
    staged_executables = [exporter]
    if any(TESTS[name] == "freedom_saves" for name in selected):
        shutil.copy2(start_fixture, work / start_fixture.name)
        start_fixture = work / start_fixture.name
        staged_executables.append(start_fixture)
    if "native_minor" in selected:
        shutil.copy2(minor_fixture, work / minor_fixture.name)
        minor_fixture = work / minor_fixture.name
        staged_executables.append(minor_fixture)
    if "native_moon" in selected:
        shutil.copy2(moon_fixture, work / moon_fixture.name)
        moon_fixture = work / moon_fixture.name
        staged_executables.append(moon_fixture)
    if any(name in selected for name in ("native_planetary", "native_voyage", "native_surface_loop", *ROUNDTRIP_TESTS)):
        shutil.copy2(planetary_fixture, work / planetary_fixture.name)
        planetary_fixture = work / planetary_fixture.name
        staged_executables.append(planetary_fixture)
    if "native_environment" in selected or any(name in selected for name in SURFACE_TESTS):
        shutil.copy2(surface_fixture, work / surface_fixture.name)
        surface_fixture = work / surface_fixture.name
        staged_executables.append(surface_fixture)
    if "native_impact" in selected:
        shutil.copy2(impact_fixture, work / impact_fixture.name)
        impact_fixture = work / impact_fixture.name
        staged_executables.append(impact_fixture)
    if "native_frame_import" in selected:
        shutil.copy2(frame_import_fixture, work / frame_import_fixture.name)
        frame_import_fixture = work / frame_import_fixture.name
        staged_executables.append(frame_import_fixture)
    if "native_system_release" in selected:
        shutil.copy2(system_release_fixture, work / system_release_fixture.name)
        system_release_fixture = work / system_release_fixture.name
        staged_executables.append(system_release_fixture)
    if "orbital_survey" in selected:
        shutil.copy2(survey_fixture, work / survey_fixture.name)
        survey_fixture = work / survey_fixture.name
        staged_executables.append(survey_fixture)
    if any(name in selected for name in ("native_derelict", "native_station_crew")):
        shutil.copy2(derelict_fixture, work / derelict_fixture.name)
        derelict_fixture = work / derelict_fixture.name
        staged_executables.append(derelict_fixture)
    env = os.environ.copy()
    env.update({"XDG_DATA_HOME": str(work / "userdata"),
                "XDG_CONFIG_HOME": str(work / "config"),
                "XDG_CACHE_HOME": str(work / "cache"),
                "GODOT_SILENCE_ROOT_WARNING": "1"})
    report = {"schema_version": 1, "scope": "headless contracts, not GPU/hardware/listening acceptance",
              "selected": selected, "tests": [], "setup": [],
              "engine_sha256": sha256(engine),
              "project_sha256": {path.relative_to(project).as_posix(): sha256(path)
                                 for path in sorted(project.rglob("*")) if path.is_file()},
              "extension_descriptor_sha256": sha256(project / "bin/freedom.gdextension"),
              "bridge_sha256": sha256(project / "bin/libapsis_freedom_bridge.so"),
              "exporter_sha256": sha256(exporter),
              "staged_executable_sha256": {path.name: sha256(path) for path in staged_executables},
              "retention": {"mode": "full", "reason": "run_not_completed"}}
    continuation_copy = None
    if continuation is not None:
        continuation_copy = work / "continuation-source.json"
        continuation_copy.write_bytes(continuation_bytes)
        report["continuation_source"] = {
            "path": str(continuation), "staged_file": continuation_copy.name,
            "sha256": hashlib.sha256(continuation_bytes).hexdigest(),
            "bytes": len(continuation_bytes),
            "scope": "Explicit saved continuation; no uninterrupted native prefix claim"}

    def save():
        (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")

    print(f"Native contracts: {work}", flush=True)
    save()
    if any(TESTS[name] in ("native_assets", "operating_assets", "operating_motion") or name in ("native_shell", "native_save", "saved_flight", "native_environment", "native_audio", "first_jump", "native_moon", "native_minor", "native_derelict", "native_recovery", "native_port", "native_walk", "native_station_crew", "native_start_staging", "native_title", "native_profile", "native_planetary", "native_voyage", "native_surface_loop", "native_impact", "native_frame_import", "native_system_release", "orbital_survey", *ROUNDTRIP_TESTS, *SURFACE_TESTS) for name in selected):
        helpers = ("prepare_freedom_native_assets.py", "prepare_native_assets.py",
                         "prepare_operating_assets.py", "wayfarer_operating_spec.py",
                         "operating_asset_identity.py", "wayfarer_operating_glb_audit.py",
                         "validate_operating_asset_glb.py", "build_boarding_qualification.py",
                         "prepare_wayfarer_stowed.py", "prepare_lower_cockpit_contact.py",
                         "lower_cockpit_contact_spec.py", "lower_cockpit_contact_identity.py",
                         "wayfarer_stowed_package_checks.py", "stowed_asset_identity.py",
                         "wayfarer_corrected_rest_geometry.py", "wayfarer_restraint_export_checks.py")
        for filename in helpers:
            stage_helper(repo / "tools" / filename, work / filename)
        preparer = work / "prepare_freedom_native_assets.py"
        log = work / "prepare-native-assets.log"
        code, timed_out, elapsed = run_logged(
            [sys.executable, str(preparer), "--repository", str(repo),
             "--output", str(work / "native-assets")], log, env, args.timeout)
        report["setup"].append({"family": "native_assets", "returncode": code,
                                "timed_out": timed_out, "seconds": elapsed,
                                "preparer_sha256": sha256(preparer),
                                "helper_sha256": {name: sha256(work / name) for name in helpers}})
        save()
        if code != 0 or timed_out:
            print(f"FAIL native asset preparation: {log}", flush=True)
            return 1
        report["setup"][-1]["prepared"] = json.loads(
            (work / "native-assets/prepared.json").read_text())
        report["setup"][-1]["companions"] = {
            name: json.loads((work / "native-assets" / name / receipt).read_text())
            for name, receipt in (("operating", "prepared.json"), ("stowed", "preparation.json"), ("crew", "prepared.json"))}
        save()
    if any(TESTS[name] in ("operating_assets", "operating_motion") for name in selected):
        for filename in ("prepare_operating_assets.py", "wayfarer_operating_spec.py", "operating_asset_identity.py",
                         "wayfarer_operating_glb_audit.py", "validate_operating_asset_glb.py",
                         "build_boarding_qualification.py"):
            stage_helper(repo / "tools" / filename, work / filename)
        preparer = work / "prepare_operating_assets.py"
        log = work / "prepare-operating-assets.log"
        code, timed_out, elapsed = run_logged(
            [sys.executable, str(preparer), "--package", str(repo / "assets/native/wayfarer-operating-02"),
             "--output", str(work / "native-assets/operating")], log, env, args.timeout)
        report["setup"].append({"family": "operating_assets", "returncode": code,
                                "timed_out": timed_out, "seconds": elapsed,
                                "preparer_sha256": sha256(preparer),
                                "spec_sha256": sha256(work / "wayfarer_operating_spec.py")})
        save()
        if code != 0 or timed_out:
            print(f"FAIL operating asset preparation: {log}", flush=True)
            return 1
        report["setup"][-1]["prepared"] = json.loads((work / "native-assets/operating/prepared.json").read_text())
        save()
    if any(TESTS[name] == "operating_motion" for name in selected):
        for filename in ("prepare_operating_motion.py", "operating_motion_spec.py",
                         "operating_motion_identity.py", "package_operating_motion.py",
                         "prepare_native_assets.py"):
            stage_helper(repo / "tools" / filename, work / filename)
        preparer = work / "prepare_operating_motion.py"
        log = work / "prepare-operating-motion.log"
        code, timed_out, elapsed = run_logged(
            [sys.executable, str(preparer), "--package", str(repo / "assets/native/operating-motion-01"),
             "--repository", str(repo), "--output", str(work / "operating-motion")],
            log, env, args.timeout)
        report["setup"].append({"family": "operating_motion", "returncode": code,
                                "timed_out": timed_out, "seconds": elapsed,
                                "preparer_sha256": sha256(preparer),
                                "spec_sha256": sha256(work / "operating_motion_spec.py")})
        save()
        if code != 0 or timed_out:
            print(f"FAIL operating motion preparation: {log}", flush=True)
            return 1
        report["setup"][-1]["prepared"] = json.loads((work / "operating-motion/prepared.json").read_text())
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
                                     ("flight-18.json", "25", "flight"),
                                     ("wayfarer-flight.json", "25", "wayfarer-flight"),
                                     ("flight-trace.json", "25", "flight-trace"),
                                     ("flight-overflow.json", "18446744073709551613", "wayfarer-flight"),
                                     ("port-approach.json", "25", "port-approach"),
                                     ("port-docked.json", "25", "port-docked"),
                                     ("port-trace.json", "25", "port-trace"),
                                     ("port-far.json", "25", "port-far"),
                                     ("journey.json", "0", "journey"),
                                     ("journey-20.json", "0", "legacy-journey"),
                                     ("journey-trace.json", "0", "journey-trace")):
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
    if "native_minor" in selected:
        phases = work / "minor-voyage"
        code, timed_out, elapsed = run_logged(
            [str(minor_fixture), str(phases)],
            work / "minor-voyage-fixture.log", env, args.timeout)
        record = {"family": "minor_voyage", "returncode": code,
                  "timed_out": timed_out, "seconds": elapsed,
                  "fixture_sha256": sha256(minor_fixture)}
        report["setup"].append(record)
        save()
        if code != 0 or timed_out:
            print("FAIL actual continuous minor route fixture", flush=True)
            return 1
        names = ("arrival", "approach", "withdraw", "home", "rendezvous-near", "reuse-captured",
                     "foreign-entry", "foreign-arrival", "foreign-home", "foreign-approach", "foreign-withdraw")
        if not all((phases / (name + ".json")).is_file() and
                   0 < (phases / (name + ".json")).stat().st_size <= 1 << 20
                   for name in names):
            print("FAIL incomplete or oversized minor route phases", flush=True)
            return 1
        record["files_sha256"] = {
            name + ".json": sha256(phases / (name + ".json")) for name in names}
        save()
    if "native_moon" in selected:
        phases = work / "moon-voyage"
        code, timed_out, elapsed = run_logged(
            [str(moon_fixture), str(phases)],
            work / "moon-voyage-fixture.log", env, args.timeout)
        record = {"family": "moon_voyage", "returncode": code,
                  "timed_out": timed_out, "seconds": elapsed,
                  "fixture_sha256": sha256(moon_fixture)}
        report["setup"].append(record)
        save()
        if code != 0 or timed_out:
            print("FAIL actual continuous Moon route fixture", flush=True)
            return 1
        names = ("arrival", "approach", "touchdown-source", "landed",
                 "outside", "returned", "released", "ascent",
                 "service-landed", "service-requested", "service-partial600",
                     "foreign-parent", "foreign-arrival", "foreign-home",
                     "foreign-landed", "foreign-outside", "foreign-marked")
        if not all((phases / (name + ".json")).is_file() and
                   0 < (phases / (name + ".json")).stat().st_size <= 1 << 20
                   for name in names):
            print("FAIL incomplete or oversized Moon route phases", flush=True)
            return 1
        record["files_sha256"] = {
            name + ".json": sha256(phases / (name + ".json")) for name in names}
        save()
    if "native_environment" in selected:
        phases = work / "daynight"
        log = work / "daynight-fixture.log"
        code, timed_out, elapsed = run_logged(
            [str(surface_fixture), "--daynight-fixtures", str(phases)],
            log, env, args.timeout)
        report["setup"].append({"family": "daynight_saves", "returncode": code,
            "timed_out": timed_out, "seconds": elapsed,
            "fixture_sha256": sha256(surface_fixture)})
        save()
        if code != 0 or timed_out:
            print(f"FAIL authoritative day/night fixture: {log}", flush=True)
            return 1
        report["setup"][-1]["files_sha256"] = {
            path.name: sha256(path) for path in sorted(phases.iterdir()) if path.is_file()}
        save()
        for filename, tick in (("shadow-day.json", "100000"),
                               ("shadow-night.json", "400000"),
                               ("shadow-limb.json", "600000")):
            path = work / filename
            code, timed_out, elapsed = run_logged(
                [str(start_fixture), str(path), "42", tick, "port-docked"],
                work / f"{filename}.log", env, args.timeout)
            report["setup"].append({"save": filename, "returncode": code,
                                    "timed_out": timed_out, "seconds": elapsed})
            save()
            if code != 0 or timed_out:
                print(f"FAIL C++ shadow fixture {filename}", flush=True)
                return 1
            report["setup"][-1]["save_sha256"] = sha256(path)
            save()
    if any(name in selected for name in ("native_planetary", "native_voyage")):
        phases = work / "planetary"
        phases.mkdir()
        log = work / "planetary-fixture.log"
        # Invalid output bounds must refuse before constructing a world or
        # generating artifacts. The expensive valid voyage runs only once.
        for index, invalid in enumerate(([], ["relative"], [str(phases), "extra"],
                                          ["/" + "x" * 4001])):
            refusal_log = work / f"planetary-refusal-{index}.log"
            code, timed_out, _ = run_logged(
                [str(planetary_fixture), *invalid], refusal_log, env, args.timeout)
            if code == 0 or timed_out or any(phases.iterdir()):
                print(f"FAIL planetary argument refusal: {refusal_log}", flush=True)
                return 1
        code, timed_out, elapsed = run_logged(
            [str(planetary_fixture), str(phases), *(["--commands"] if "native_voyage" in selected else [])], log, env, args.timeout)
        report["setup"].append({"family": "planetary_saves", "returncode": code,
                                "timed_out": timed_out, "seconds": elapsed,
                                "fixture_sha256": sha256(planetary_fixture)})
        save()
        if code != 0 or timed_out:
            print(f"FAIL planetary fixture: {log}", flush=True)
            return 1
        report["setup"][-1]["files_sha256"] = {
            path.name: sha256(path) for path in sorted(phases.iterdir()) if path.is_file()}
        save()
    if any(name in selected for name in ("native_surface_loop", "native_first_flight_continuation")):
        phases = work / "surface-loop"
        phases.mkdir()
        for index, invalid in enumerate((
                [str(phases), "--flight-profile=advanced"],
                [str(phases), "--surface-loop", "--flight-profile=assisted"],
                [str(phases), "--surface-loop", "--system-frame", "--flight-profile=unknown"],
                [str(phases), "--surface-loop", "--system-frame", "--flight-profile=advanced", "--flight-profile=assisted"],
                [str(phases), "--system-frame"],
                [str(phases), "--surface-loop", "--system-frame", "--system-frame"],
                [str(phases), "--terrain-feature", "--system-frame"],
                [str(phases), "--surface-loop", "--surface-loop"],
                [str(phases), "--surface-loop", "--neighbor"],
                [str(phases), "--surface-loop", "--pilot"],
                [str(phases), "--surface-loop", "--aligned"])):
            refusal_log = work / f"surface-loop-refusal-{index}.log"
            code, timed_out, _ = run_logged(
                [str(planetary_fixture), *invalid], refusal_log, env, args.timeout)
            if code == 0 or timed_out or any(phases.iterdir()):
                print(f"FAIL surface-loop argument refusal: {refusal_log}", flush=True)
                return 1
        log = work / "surface-loop-fixture.log"
        fixture_args = [str(planetary_fixture), str(phases), "--surface-loop", "--commands"]
        if args.selected_surface_loop:
            fixture_args.append("--system-frame")
        if args.flight_profile:
            fixture_args.append("--flight-profile=" + args.flight_profile)
        code, timed_out, elapsed = run_logged(
            fixture_args,
            log, env, args.timeout)
        report["setup"].append({"family": "surface_loop_saves", "returncode": code,
            "timed_out": timed_out, "seconds": elapsed,
            "fixture_sha256": sha256(planetary_fixture)})
        if args.selected_surface_loop:
            report["setup"][-1]["frame_policy"] = 2
        if args.flight_profile:
            report["setup"][-1]["flight_profile"] = args.flight_profile
        save()
        if code != 0 or timed_out:
            print(f"FAIL physical surface-loop fixture: {log}", flush=True)
            return 1
        report["setup"][-1]["files_sha256"] = {
            path.name: sha256(path) for path in sorted(phases.iterdir()) if path.is_file()}
        save()
    case_continuations = []
    for name in ROUNDTRIP_TESTS:
        if name not in selected:
            continue
        phases = work / name
        phases.mkdir()
        # CLI refusals precede constructing any actual world or artifacts.
        for index, invalid in enumerate(([str(phases), "--pilot"],
                [str(phases), "--neighbor", "--neighbor"],
                [str(phases), "--neighbor", "--commands", "--commands"],
                [str(phases), "--neighbor", "--aligned"],
                [str(phases), "--neighbor", "--pilot", "--aligned", "--offset"],
                [str(phases), "--continue-from=" + str(work / "missing-continuation.json")],
                [str(phases), "--neighbor", "--commands", "--continue-from=relative"],
                [str(phases), "--neighbor", "--commands", "--continue-from="],
                [str(phases), "--neighbor", "--commands", "--continue-from=" + str(work / "missing-continuation.json")])):
            refusal_log = work / f"{name}-refusal-{index}.log"
            code, timed_out, _ = run_logged(
                [str(planetary_fixture), *invalid], refusal_log, env, args.timeout)
            if code == 0 or timed_out or any(phases.iterdir()):
                print(f"FAIL round-trip argument refusal: {refusal_log}", flush=True)
                return 1
        case_source = (work / "surface-loop/space-exit.json"
                       if name == "native_first_flight_continuation" else continuation_copy)
        case_source_bytes = None
        if case_source is not None:
            try:
                with case_source.open("rb") as stream:
                    case_source_bytes = stream.read((1 << 20) + 1)
                if not 0 < len(case_source_bytes) <= (1 << 20):
                    raise ValueError("empty or oversized continuation")
                case_source_digest = hashlib.sha256(case_source_bytes).hexdigest()
            except (OSError, ValueError) as error:
                print(f"FAIL round-trip continuation source: {error}", flush=True)
                return 1
        log = work / f"{name}-fixture.log"
        code, timed_out, elapsed = run_logged(
            [str(planetary_fixture), str(phases), "--neighbor", "--commands",
             *(["--pilot"] if name != "native_roundtrip" else []),
             *(["--aligned"] if name.endswith("_aligned") else []),
             *(["--offset"] if name.endswith("_offset") else []),
             *(["--continue-from=" + str(case_source)] if case_source else [])], log, env, args.timeout)
        report["setup"].append({"family": name, "returncode": code,
            "timed_out": timed_out, "seconds": elapsed,
            "fixture_sha256": sha256(planetary_fixture)})
        if case_source is not None:
            source_report = {"file": case_source.relative_to(work).as_posix(),
                             "sha256": case_source_digest,
                             "bytes": len(case_source_bytes),
                             "scope": "Explicit saved continuation; native suffix only"}
            if name == "native_first_flight_continuation":
                source_report["native_prefix_test"] = "native_surface_loop"
                source_report["native_prefix_selected"] = "native_surface_loop" in selected
            report["setup"][-1]["continuation_source"] = source_report
        save()
        if code != 0 or timed_out:
            print(f"FAIL actual round-trip fixture: {log}", flush=True)
            return 1
        if case_source is not None:
            emitted_source = phases / "surface-ascent.json"
            try:
                identical = (case_source.stat().st_size == len(case_source_bytes) and
                             emitted_source.stat().st_size == len(case_source_bytes) and
                             case_source.read_bytes() == case_source_bytes and
                             emitted_source.read_bytes() == case_source_bytes)
            except OSError:
                identical = False
            source_report["fixture_preserved_bytes"] = identical
            save()
            if not identical:
                print("FAIL continuation changed its complete source save", flush=True)
                return 1
            case_continuations.append((case_source, emitted_source, source_report))
        report["setup"][-1]["files_sha256"] = {
            path.name: sha256(path) for path in sorted(phases.iterdir()) if path.is_file()}
        save()
    if "native_impact" in selected:
        phases = work / "impact"
        log = work / "impact-fixture.log"
        code, timed_out, elapsed = run_logged(
            [str(impact_fixture), "--fixtures", str(phases)], log, env, args.timeout)
        report["setup"].append({"family": "impact_saves", "returncode": code,
            "timed_out": timed_out, "seconds": elapsed,
            "fixture_sha256": sha256(impact_fixture)})
        save()
        if code != 0 or timed_out:
            print(f"FAIL authoritative impact fixture: {log}", flush=True)
            return 1
        report["setup"][-1]["files_sha256"] = {
            path.name: sha256(path) for path in sorted(phases.iterdir()) if path.is_file()}
        save()
    if "native_frame_import" in selected:
        phases = work / "frame-import"
        log = work / "frame-import-fixture.log"
        code, timed_out, elapsed = run_logged(
            [str(frame_import_fixture), "--fixtures", str(phases)], log, env, args.timeout)
        report["setup"].append({"family": "frame_import_saves", "returncode": code,
            "timed_out": timed_out, "seconds": elapsed,
            "fixture_sha256": sha256(frame_import_fixture)})
        save()
        if code != 0 or timed_out:
            print(f"FAIL authoritative frame import fixture: {log}", flush=True)
            return 1
        report["setup"][-1]["files_sha256"] = {
            path.name: sha256(path) for path in sorted(phases.iterdir()) if path.is_file()}
        save()
    if "native_system_release" in selected:
        phases = work / "system-release"
        log = work / "system-release-fixture.log"
        code, timed_out, elapsed = run_logged(
            [str(system_release_fixture), "--fixtures", str(phases)], log, env, args.timeout)
        report["setup"].append({"family": "system_release_saves", "returncode": code,
            "timed_out": timed_out, "seconds": elapsed,
            "fixture_sha256": sha256(system_release_fixture)})
        save()
        if code != 0 or timed_out:
            print(f"FAIL authoritative system release fixture: {log}", flush=True)
            return 1
        report["setup"][-1]["files_sha256"] = {
            path.name: sha256(path) for path in sorted(phases.iterdir()) if path.is_file()}
        save()
    if "orbital_survey" in selected:
        phases = work / "orbital-survey"
        log = work / "orbital-survey-fixture.log"
        code, timed_out, elapsed = run_logged(
            [str(survey_fixture), "--fixtures", str(phases)], log, env, args.timeout)
        report["setup"].append({"family": "orbital_survey_saves", "returncode": code,
            "timed_out": timed_out, "seconds": elapsed,
            "fixture_sha256": sha256(survey_fixture)})
        save()
        if code != 0 or timed_out:
            print(f"FAIL authoritative orbital survey fixture: {log}", flush=True)
            return 1
        required = ("survey-start", "survey-near", "survey-complete",
                    "commission-station", "commission-accepted", "commission-near",
                    "commission-complete", "commission-returned", "commission-paid",
                    "commission-repeat", "commission-expired")
        if any(not (phases / (name + ".json")).is_file() for name in required):
            print(f"FAIL incomplete orbital survey/commission fixtures: {log}", flush=True)
            return 1
        paid_required = (*required[3:], "paid-selected", "paid-fuel-station",
                         "paid-fuel-bought", "paid-retainer-station", "paid-retainer-funded")
        if any(not (phases / "paid-operations" / (name + ".json")).is_file()
               for name in paid_required):
            print(f"FAIL incomplete paid operating fixtures: {log}", flush=True)
            return 1
        sale_required = ("sale-origin-one", "sale-earning-one", "sale-enrolled",
                         "sale-origin-paid", "sale-paid-one", "sale-origin-four",
                         "sale-paid-four", "sale-remote", "sale-recovered",
                         "sale-service-source", "sale-service-paid")
        if any(not (phases / "specimen-sales" / (name + ".json")).is_file()
               for name in sale_required):
            print(f"FAIL incomplete specimen buyer fixtures: {log}", flush=True)
            return 1
        report["setup"][-1]["files_sha256"] = {
            path.relative_to(phases).as_posix(): sha256(path)
            for path in sorted(phases.rglob("*.json"))}
        save()
    if any(name in selected for name in ("native_derelict", "native_station_crew")):
        phases = work / "derelict-voyage"
        log = work / "derelict-voyage-fixture.log"
        code, timed_out, elapsed = run_logged(
            [str(derelict_fixture), "--fixtures", str(phases)], log, env, args.timeout)
        record = {"family": "derelict_saves", "returncode": code,
                  "timed_out": timed_out, "seconds": elapsed,
                  "fixture_sha256": sha256(derelict_fixture)}
        report["setup"].append(record)
        save()
        names = ("station", "approach", "contact", "contact-recovered", "near", "inspection", "recovered",
                     "lead-station", "lead-kept", "lead-confirmed", "lead-visited", "lead-recovered")
        if code != 0 or timed_out or not all(
                (phases / (name + ".json")).is_file() and
                0 < (phases / (name + ".json")).stat().st_size <= 1 << 20
                for name in names):
            print(f"FAIL authoritative derelict fixture: {log}", flush=True)
            return 1
        record["files_sha256"] = {
            name + ".json": sha256(phases / (name + ".json")) for name in names}
        save()
    for name, (stem, fixture_flag, terrain_policy) in SURFACE_TESTS.items():
        if name not in selected:
            continue
        phases = work / stem
        log = work / f"{stem}-fixture.log"
        code, timed_out, elapsed = run_logged(
            [str(surface_fixture), fixture_flag, str(phases)], log, env, args.timeout)
        report["setup"].append({"family": TESTS[name], "test": name,
            "terrain_policy": terrain_policy, "returncode": code,
            "timed_out": timed_out, "seconds": elapsed,
            "fixture_sha256": sha256(surface_fixture)})
        save()
        if code != 0 or timed_out:
            print(f"FAIL {stem} fixture: {log}", flush=True)
            return 1
        report["setup"][-1]["files_sha256"] = {
            path.name: sha256(path) for path in sorted(phases.iterdir()) if path.is_file()}
        save()
    for name in selected:
        if name == "native_title":
            cases = []
            for label, extra in (("title_without_selection", []),
                                 ("title_with_assets", [f"--assets={work / 'native-assets'}"])):
                log = work / f"{label}.log"
                command = [str(engine), "--headless", "--audio-driver", "Dummy",
                           "--path", str(project), "--scene", "res://scenes/native_start_shell.tscn",
                           "--quit-after", "3", "--", *extra]
                code, timed_out, elapsed = run_logged(command, log, env, args.timeout)
                output = log.read_text(errors="replace")
                good = (not timed_out and code == 0 and not ERROR.search(output) and
                        "Freedom native title opened: no journey selected" in output and
                        "shell opened: seed=" not in output)
                cases.append({"case": label, "pass": good, "returncode": code,
                              "seconds": elapsed, "log": log.name})
            report["setup"].append({"family": "native_title_launch", "cases": cases})
            save()
            if not all(case["pass"] for case in cases):
                report["tests"].append({"name": name, "status": "launch_failed", "cases": cases})
                save()
                print(f"FAIL {name} launch cases", flush=True)
                continue
        if name == "native_shell":
            cases = (
                ("new", [f"--continue={work / 'freedom-0.json'}", "--validate-only"],
                 "seed=42 tick=0", True),
                ("view", [f"--continue={work / 'freedom-0.json'}", f"--assets={work / 'native-assets'}"], "seed=42 tick=0", True),
                ("continue_zero", [f"--continue={work / 'freedom-0.json'}",
                                   "--validate-only"], "seed=42 tick=0", True),
                ("continue_progressed", [f"--continue={work / 'freedom-25.json'}",
                                        "--validate-only"],
                 "seed=42 tick=25", True),
                ("missing_assets", ["--new-game=42"], "", False),
                ("duplicate_assets", ["--new-game=42", f"--assets={work / 'native-assets'}", f"--assets={work / 'native-assets'}"], "", False),
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
                           "res://scenes/native_start_shell.tscn"]
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
        if name == "native_walk":
            cases = []
            for label, extra, accepted in (
                    ("new_validate", ["--new-game=42", "--validate-only"], True),
                    ("new_view", ["--new-game=42", f"--assets={work / 'native-assets'}"], True),
                    ("continued_validate", [f"--continue={work / 'journey.json'}", "--validate-only"], True),
                    ("trace_validate", [f"--continue={work / 'journey-trace.json'}", "--validate-only"], True),
                    ("missing_assets", ["--new-game=42"], False)):
                log = work / f"native_walk-{label}.log"
                command = [str(engine), "--headless", "--audio-driver", "Dummy", "--path", str(project), "--scene", "res://scenes/native_start_shell.tscn"]
                if label == "new_view":
                    command += ["--quit-after", "3"]
                command += ["--", *extra]
                code, timed_out, elapsed = run_logged(command, log, env, args.timeout)
                output = log.read_text(errors="replace")
                tick = "1920" if label == "trace_validate" else "0"
                good = (not timed_out and code == 0 and "Freedom native walking shell " in output and f"seed=42 tick={tick}" in output and not ERROR.search(output)) if accepted else (not timed_out and code != 0 and "Native start rejected:" in output)
                cases.append({"case": label, "pass": good, "returncode": code, "seconds": elapsed, "log": log.name})
            report["setup"].append({"family": "native_walk_launch", "cases": cases})
            save()
            if not all(case["pass"] for case in cases):
                report["tests"].append({"name": name, "status": "launch_failed", "cases": cases})
                save()
                print(f"FAIL {name} launch cases", flush=True)
                continue
        if name == "native_port":
            cases = []
            for label, save_name, validate in (
                    ("approach_validate", "port-approach.json", True),
                    ("attached_validate", "port-docked.json", True),
                    ("attached_view", "port-docked.json", False)):
                log = work / f"native_port-{label}.log"
                command = [str(engine), "--headless", "--audio-driver", "Dummy", "--path", str(project), "--scene", "res://scenes/native_start_shell.tscn"]
                if not validate:
                    command += ["--quit-after", "3"]
                command += ["--", f"--continue={work / save_name}"]
                command += ["--validate-only"] if validate else [f"--assets={work / 'native-assets'}"]
                code, timed_out, elapsed = run_logged(command, log, env, args.timeout)
                output = log.read_text(errors="replace")
                good = not timed_out and code == 0 and "Freedom native flight shell " in output and "seed=42 tick=25" in output and not ERROR.search(output)
                cases.append({"case": label, "pass": good, "returncode": code, "seconds": elapsed, "log": log.name})
            report["setup"].append({"family": "native_port_launch", "cases": cases})
            save()
            if not all(case["pass"] for case in cases):
                report["tests"].append({"name": name, "status": "launch_failed", "cases": cases})
                save()
                print(f"FAIL {name} launch cases", flush=True)
                continue
        if name == "saved_flight":
            case_reports = []
            for label, extra, accepted in (
                    ("legacy_validate", [f"--continue={work / 'flight-18.json'}", "--validate-only"], True),
                    ("wayfarer_validate", [f"--continue={work / 'wayfarer-flight.json'}", "--validate-only"], True),
                    ("wayfarer_view", [f"--continue={work / 'wayfarer-flight.json'}", f"--assets={work / 'native-assets'}"], True),
                    ("flight_missing_assets", [f"--continue={work / 'wayfarer-flight.json'}"], False)):
                log = work / f"saved_flight-{label}.log"
                command = [str(engine), "--headless", "--audio-driver", "Dummy", "--path", str(project), "--scene", "res://scenes/native_start_shell.tscn"]
                if label == "wayfarer_view":
                    command += ["--quit-after", "3"]
                command += ["--", *extra]
                code, timed_out, elapsed = run_logged(command, log, env, args.timeout)
                output = log.read_text(errors="replace")
                good = (not timed_out and code == 0 and "Freedom native flight shell " in output and "seed=42 tick=25" in output and not ERROR.search(output)) if accepted else (not timed_out and code != 0 and "Native start rejected:" in output)
                case_reports.append({"case": label, "pass": good, "returncode": code, "seconds": elapsed, "log": log.name})
            report["setup"].append({"family": "saved_flight_launch", "cases": case_reports})
            save()
            if not all(case["pass"] for case in case_reports):
                report["tests"].append({"name": name, "status": "launch_failed", "cases": case_reports})
                save()
                print(f"FAIL {name} launch cases", flush=True)
                continue
        arguments = []
        if TESTS[name] == "orbital_survey_saves":
            arguments = [str(work / "orbital-survey"), str(work / "native-assets")]
        elif TESTS[name] == "snapshot":
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
        elif TESTS[name] == "impact_saves":
            arguments = [str(work / path) for path in ("impact", "native-assets", "impact-native")]
        elif TESTS[name] == "frame_import_saves":
            arguments = [str(work / path) for path in ("frame-import", "native-assets", "frame-import-native")]
        elif TESTS[name] == "system_release_saves":
            arguments = [str(work / path) for path in ("system-release", "native-assets", "system-release-native")]
        elif TESTS[name] == "surface_walk_saves":
            stem = SURFACE_TESTS[name][0]
            arguments = [str(work / path) for path in (stem, "native-assets", stem + "-native")]
        elif TESTS[name] == "surface_loop_saves":
            arguments = [str(work / path) for path in
                         ("surface-loop", "native-assets", "surface-loop-native")]
        elif TESTS[name] == "surface_saves":
            stem = SURFACE_TESTS[name][0]
            arguments = [str(work / path) for path in (stem, "native-assets", stem + "-native")]
        elif TESTS[name] in ("planetary_saves", "voyage_saves"):
            arguments = [str(work / path) for path in
                         ("planetary", "native-assets", "voyage-native" if name == "native_voyage" else "planetary-native")]
        elif TESTS[name] == "roundtrip_saves":
            arguments = [str(work / path) for path in
                         (name, "native-assets", name + "-native")]
        elif TESTS[name] == "native_assets":
            arguments = [str(work / "native-assets"), str(work / "native-asset-import.json")]
        elif TESTS[name] == "operating_assets":
            arguments = [str(work / "native-assets"), str(work / "native-assets/operating")]
        elif TESTS[name] == "operating_motion":
            arguments = [str(work / path) for path in
                         ("native-assets", "native-assets/operating", "operating-motion")]
        if name == "native_assembly":
            arguments = [str(work / "native-assets")]
        if name == "native_start_staging":
            arguments = [str(work / path) for path in
                         ("native-assets", "journey-20.json", "corrupt.json")]
        if name in ("native_title", "native_profile"):
            arguments = [str(work / path) for path in
                         ("native-assets", "journey-20.json", "corrupt.json")]
        if name == "native_save":
            arguments.append(str(work / "native-assets"))
        if name == "freedom_start":
            arguments.append(str(work / "flight-18.json"))
        if name == "native_selected_terrain":
            arguments = [str(work / path) for path in ("flight-18.json", "selected-terrain")]
        if name == "native_environment":
            arguments = [str(work / path) for path in ("wayfarer-flight.json", "environment-world.json", "shadow-day.json", "shadow-night.json", "shadow-limb.json", "daynight", "native-assets")]
        if name == "saved_flight":
            arguments = [str(work / path) for path in ("wayfarer-flight.json", "flight-trace.json", "corrupt.json", "freedom-0.json", "native-assets", "flight-overflow.json")]
        if name == "native_audio":
            arguments = [str(work / path) for path in ("wayfarer-flight.json", "corrupt.json", "native-assets")]
        if name == "native_walk":
            arguments = [str(work / path) for path in ("journey.json", "journey-trace.json", "native-assets")]
        if name == "native_station_crew":
            arguments = [str(work / path) for path in ("journey.json", "native-assets", "station-crew-native", "derelict-voyage")]
        if name == "native_minor":
            arguments = [str(work / path) for path in
                         ("minor-voyage", "native-assets", "minor-native")]
        if name == "native_derelict":
            arguments = [str(work / path) for path in
                         ("derelict-voyage", "native-assets", "derelict-native")]
        if name == "native_moon":
            arguments = [str(work / path) for path in
                         ("moon-voyage", "native-assets", "moon-native")]
        if name == "native_port":
            arguments = [str(work / path) for path in ("port-approach.json", "port-docked.json", "port-trace.json", "port-far.json", "native-assets")]
        log = work / f"{name}.log"
        command = [str(engine), "--headless", "--audio-driver", "Dummy",
                   "--path", str(project), "--script", f"res://tests/{name}_test.gd",
                   "--", *arguments]
        code, timed_out, elapsed = run_logged(command, log, env, args.timeout)
        output = log.read_text(errors="replace")
        status = verdict(name, code, timed_out, output)
        report["tests"].append({"name": name, "status": status, "returncode": code,
                                "seconds": elapsed, "log": log.name})
        save()
        print(f"{status.upper()} {name} ({elapsed:.2f}s)", flush=True)
    for case_source, emitted_source, source_report in case_continuations:
        try:
            unchanged = all(path.stat().st_size == source_report["bytes"] and
                            sha256(path) == source_report["sha256"]
                            for path in (case_source, emitted_source))
        except OSError:
            unchanged = False
        source_report["unchanged_after_native"] = unchanged
        if not unchanged:
            print("FAIL native consumers changed a continuation source; retained work", flush=True)
            save()
            return 1
    if continuation is not None:
        try:
            unchanged = (continuation.stat().st_size == len(continuation_bytes) and
                         sha256(continuation) == report["continuation_source"]["sha256"] and
                         sha256(continuation_copy) == report["continuation_source"]["sha256"])
        except OSError:
            unchanged = False
        report["continuation_source"]["unchanged"] = unchanged
        if not unchanged:
            print("FAIL original or staged continuation source changed; retained work", flush=True)
            save()
            return 1
    passed = sum(test["status"] == "pass" for test in report["tests"])
    finish_work(work, report, args.keep_work)
    save()
    print(f"{passed}/{len(selected)} native contracts passed; logs retained in {work}")
    if report["retention"]["mode"] == "compact":
        print(f"Removed {report['retention']['removed_bytes']} bytes of generated assets, imports and binaries; use --keep-work for replay.")
    return 0 if passed == len(selected) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
