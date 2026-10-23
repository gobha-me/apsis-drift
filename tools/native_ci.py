#!/usr/bin/env python3
"""Verified four-way scheduling around the unchanged serial native runner."""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import time

REPO = Path(__file__).resolve().parent.parent
SELECTED = """input control_settings recorded_observations freedom_start native_shell
native_save native_asset_import native_station_view wayfarer_frame saved_flight
orbital_survey native_environment native_audio first_jump native_moon native_minor native_derelict native_recovery native_port
native_walk native_station_crew native_assembly native_start_staging native_title native_profile
native_planetary native_voyage native_roundtrip native_roundtrip_pilot
native_roundtrip_pilot_aligned native_roundtrip_pilot_offset home_navigation
native_status native_surface native_surface_walk native_selected_terrain
native_surface_detail native_surface_walk_detail native_impact native_frame_import
native_system_release native_surface_loop native_first_flight_continuation
wayfarer_operating recorded_audio_shutdown""".split()
GODOT_SUFFIXES = {".gd", ".gdshader", ".gdshaderinc", ".godot", ".tscn", ".json"}
FIXTURES = {
    "start": "src/godot/apsis-drift-freedom-start-fixture",
    "moon": "src/godot/apsis-drift-freedom-moon-fixture",
    "minor": "src/godot/apsis-drift-freedom-minor-fixture",
    "planetary": "src/godot/apsis-drift-freedom-planetary-fixture",
    "surface": "apsis-drift-landed-craft-tests",
    "impact": "apsis-drift-native-impact-tests",
    "frame_import": "apsis-drift-freedom-flight-import-tests",
    "system_release": "apsis-drift-native-system-release-tests",
    "orbital_survey": "apsis-drift-native-orbital-survey-tests",
    "derelict": "apsis-drift-native-derelict-tests",
}
EXPORTER = "src/godot/apsis-drift-godot-snapshot"
BRIDGE = "src/godot/bin/libapsis_freedom_bridge.so"
FLAGS = {"CMAKE_BUILD_TYPE": "Release", "APSIS_DRIFT_TERMINAL": "OFF",
         "APSIS_DRIFT_RTAUDIO": "OFF", "APSIS_DRIFT_GODOT_SPIKE": "ON",
         "APSIS_DRIFT_GODOT_LIVE": "ON"}
SAVES = """freedom-0.json freedom-25.json career.json flight-18.json
wayfarer-flight.json flight-trace.json flight-overflow.json port-approach.json
port-docked.json port-trace.json port-far.json journey.json journey-20.json
journey-trace.json""".split()
LAUNCHES = {
    "native_shell": "new view continue_zero continue_progressed missing_assets duplicate_assets missing ambiguous bad_seed career corrupt".split(),
    "saved_flight": "legacy_validate wayfarer_validate wayfarer_view flight_missing_assets".split(),
    "native_port": "approach_validate attached_validate attached_view".split(),
    "native_walk": "new_validate new_view continued_validate trace_validate missing_assets".split(),
    "native_title": "title_without_selection title_with_assets".split(),
}
REFUSALS = {(name, case) for name, cases in LAUNCHES.items() for case in cases
            if case in {"missing_assets", "duplicate_assets", "missing", "ambiguous",
                        "bad_seed", "career", "corrupt", "flight_missing_assets"}}
ASSET_HELPERS = """prepare_freedom_native_assets.py prepare_native_assets.py
prepare_operating_assets.py wayfarer_operating_spec.py operating_asset_identity.py
wayfarer_operating_glb_audit.py validate_operating_asset_glb.py build_boarding_qualification.py
prepare_wayfarer_stowed.py prepare_lower_cockpit_contact.py lower_cockpit_contact_spec.py
lower_cockpit_contact_identity.py wayfarer_stowed_package_checks.py stowed_asset_identity.py
wayfarer_corrected_rest_geometry.py wayfarer_restraint_export_checks.py""".split()
DIAGNOSTIC = re.compile(r"^\s*(?:SCRIPT ERROR|ERROR|USER ERROR|USER SCRIPT ERROR):.*$|"
                        r"^.*ObjectDB.*instances?.*leak.*$|^.*resources still in use at exit.*$",
                        re.MULTILINE | re.IGNORECASE)
HEX = re.compile(r"[0-9a-f]{64}\Z")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    require(path.is_file() and not path.is_symlink(), f"Not a regular file: {path}")
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def finite_tree(value):
    if isinstance(value, dict):
        for key, item in value.items():
            if key == "pass":
                require(item is True, "Failed nested acceptance check")
            if key == "timed_out":
                require(item is False, "Timed-out nested process")
            if key == "seconds":
                require(type(item) in (int, float) and 0 <= item <= 7 * 86400 and math.isfinite(item),
                        "Invalid duration")
            finite_tree(item)
    elif isinstance(value, list):
        for item in value:
            finite_tree(item)
    elif isinstance(value, float):
        require(math.isfinite(value), "Nonfinite JSON number")


def read_json(path):
    require(path.is_file() and not path.is_symlink() and path.stat().st_size <= 8 << 20,
            f"Invalid JSON file: {path}")
    result = json.loads(path.read_text(), object_pairs_hook=unique_object)
    finite_tree(result)
    return result


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    require(not path.is_symlink(), "Refusing symlink output")
    path.write_bytes(canonical(value))


def child(root, name):
    require(isinstance(name, str) and 0 < len(name) <= 4000 and "\\" not in name,
            "Invalid relative path")
    parts = PurePosixPath(name).parts
    require(not PurePosixPath(name).is_absolute() and all(p not in (".", "..") for p in parts)
            and str(PurePosixPath(name)) == name, "Unsafe relative path")
    path = root
    for part in parts:
        path /= part
        require(not path.is_symlink(), "Symlink path component")
    return path


def revision(repo):
    result = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    require(bool(re.fullmatch(r"[0-9a-f]{40,64}", result)), "Invalid checkout revision")
    require(not os.environ.get("GITHUB_SHA") or os.environ["GITHUB_SHA"] == result,
            "Actual checkout differs from GITHUB_SHA")
    return result


def groups(repo):
    value = read_json(repo / "tools/native_ci_groups.json")
    require(set(value) == {"schema_version", "groups"} and value["schema_version"] == 1,
            "Invalid groups schema")
    result = value["groups"]
    require(set(result) == {"1", "2", "3", "4"}, "Expected four groups")
    union = [name for names in result.values() for name in names]
    require(len(union) == len(SELECTED) and len(set(union)) == len(SELECTED) and set(union) == set(SELECTED),
            "Groups must cover every selected native test exactly once")
    for names in result.values():
        require(names == [name for name in SELECTED if name in names], "Group order changed")
    for pair in (("native_surface_loop", "native_first_flight_continuation"),
                 ("native_planetary", "native_voyage")):
        require(any(all(name in names for name in pair) for names in result.values()),
                "Required journey pair split")
    return result


def sources(repo):
    staged = {"project.godot": repo / "godot/project.godot",
              # CMake COPYONLY writes the identical descriptor into the producer
              # build checkout; fresh workers/aggregators never configure CMake.
              "bin/freedom.gdextension": repo / "godot/freedom.gdextension.in"}
    for folder in ("scenes", "scripts", "shaders", "settings", "tests", "studies"):
        for path in (repo / "godot" / folder).rglob("*"):
            rel = path.relative_to(repo / "godot")
            if (path.is_file() and path.suffix in GODOT_SUFFIXES and
                    not any(p.startswith(".") or p == "bin" for p in rel.parts)):
                staged[rel.as_posix()] = path
    helpers = list((repo / "tools").rglob("*.py"))
    helpers += [repo / "tools/native_ci_groups.json", repo / ".github/workflows/ci.yml"]
    return ({p.relative_to(repo).as_posix(): digest(p) for p in sorted(helpers)},
            {name: digest(path) for name, path in sorted(staged.items())})


def load_manifest(path, repo):
    envelope = read_json(path)
    require(set(envelope) == {"schema_version", "archive_sha256", "manifest"} and
            envelope["schema_version"] == 1 and HEX.fullmatch(envelope["archive_sha256"]),
            "Invalid manifest envelope")
    m = envelope["manifest"]
    require(set(m) == {"revision", "groups", "flags", "sources", "godot_sources", "files"},
            "Invalid build manifest")
    require(m["revision"] == revision(repo) and m["groups"] == groups(repo) and m["flags"] == FLAGS,
            "Mixed revision, groups or build flags")
    require((m["sources"], m["godot_sources"]) == sources(repo), "Mixed source pins")
    expected = {"engine/godot", "build/" + EXPORTER, "build/" + BRIDGE}
    expected.update("build/" + path for path in FIXTURES.values())
    require(set(m["files"]) == expected, "Wrong payload allowlist")
    for name, row in m["files"].items():
        child(Path("."), name)
        require(set(row) == {"sha256", "size", "mode"} and HEX.fullmatch(row["sha256"]) and
                type(row["size"]) is int and 0 < row["size"] <= 256 << 20 and row["mode"] == 0o755,
                "Invalid payload metadata")
    require(sum(row["size"] for row in m["files"].values()) <= 512 << 20, "Oversized payload")
    return envelope


def pack(args, repo=REPO):
    cache = (args.build_dir / "CMakeCache.txt").read_text()
    require(re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=" + re.escape(str(repo.resolve())) + r"$",
                      cache, re.MULTILINE), "Build belongs to another source checkout")
    for name, value in FLAGS.items():
        require(re.search(r"^" + re.escape(name) + r":[^=\n]+=" + value + r"$", cache, re.MULTILINE),
                f"Actual build flag mismatch: {name}")
    helper, staged = sources(repo)
    paths = {"engine/godot": args.godot}
    paths.update({"build/" + path: args.build_dir / path
                  for path in (EXPORTER, BRIDGE, *FIXTURES.values())})
    files = {}
    for name, path in paths.items():
        require(os.access(path, os.X_OK), f"Payload is not executable: {path}")
        files[name] = {"sha256": digest(path), "size": path.stat().st_size, "mode": 0o755}
    m = {"revision": revision(repo), "groups": groups(repo), "flags": FLAGS,
         "sources": helper, "godot_sources": staged, "files": files}
    require(not args.archive.exists() and not args.archive.is_symlink() and not args.manifest.exists() and
            args.archive.resolve() != args.manifest.resolve(), "Output already exists or aliases manifest")
    args.archive.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(args.archive, "w:gz") as archive:
        for name, path in paths.items():
            info = tarfile.TarInfo(name)
            info.size, info.mode = files[name]["size"], 0o755
            with path.open("rb") as stream:
                archive.addfile(info, stream)
        data = canonical(m)
        import io
        info = tarfile.TarInfo("manifest.json")
        info.size, info.mode = len(data), 0o644
        archive.addfile(info, io.BytesIO(data))
    require((helper, staged) == sources(repo) and all(digest(p) == files[n]["sha256"] for n, p in paths.items()),
            "Inputs changed while packing")
    write_json(args.manifest, {"schema_version": 1, "archive_sha256": digest(args.archive), "manifest": m})
    load_manifest(args.manifest, repo)


def unpack(archive_path, envelope, target):
    require(archive_path.stat().st_size <= 1 << 30 and digest(archive_path) == envelope["archive_sha256"],
            "Archive hash mismatch or oversized archive")
    expected = envelope["manifest"]["files"]
    with tarfile.open(archive_path, "r:gz") as archive:
        seen = set()
        for info in archive:
            require(info.name not in seen and info.isfile() and
                    info.name in {*expected, "manifest.json"}, "Unexpected archive member")
            seen.add(info.name)
            dest = child(target, info.name)
            row = expected.get(info.name)
            require(info.size == (row["size"] if row else len(canonical(envelope["manifest"]))) and
                    info.mode == (row["mode"] if row else 0o644), "Archive metadata mismatch")
            dest.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(info) as source, dest.open("xb") as output:
                shutil.copyfileobj(source, output)
            dest.chmod(info.mode)
            require(digest(dest) == row["sha256"] if row else
                    dest.read_bytes() == canonical(envelope["manifest"]), "Archive content mismatch")
        require(seen == {*expected, "manifest.json"}, "Missing archive member")


def run(args, repo=REPO):
    require(not args.receipt.exists() and not args.receipt.is_symlink(), "Invocation receipt already exists")
    receipt = {"schema_version": 1, "group": args.group, "runner_exit": None,
               "reports": [], "error": "", "seconds": 0.0}
    started = time.monotonic()
    try:
        envelope = load_manifest(args.manifest, repo)
        receipt.update(manifest_sha256=digest(args.manifest), revision=envelope["manifest"]["revision"])
        require(not args.output_parent.exists() and not args.receipt.exists(), "Worker output already exists")
        args.output_parent.mkdir(parents=True)
        relative = args.output_parent.resolve().relative_to(args.receipt.parent.resolve()).as_posix()
        payload = args.output_parent / "bundle"
        unpack(args.archive, envelope, payload)
        # The unchanged serial runner consumes CMake's COPYONLY output. A fresh
        # hosted worker has no configure step: reproduce only these verified bytes.
        descriptor = child(repo, "godot/bin/freedom.gdextension")
        expected_descriptor = envelope["manifest"]["godot_sources"]["bin/freedom.gdextension"]
        if not descriptor.exists():
            descriptor.parent.mkdir(parents=True, exist_ok=True)
            with descriptor.open("xb") as output:
                output.write((repo / "godot/freedom.gdextension.in").read_bytes())
        require(digest(descriptor) == expected_descriptor, "Conflicting generated descriptor")
        log = args.output_parent / "runner.log"
        command = [sys.executable, str(repo / "tools/test_godot_native.py"),
                   "--godot", str((payload / "engine/godot").resolve()),
                   "--build-dir", str((payload / "build").resolve()),
                   "--output-parent", str(args.output_parent.resolve()), "--timeout", "900"]
        for name in envelope["manifest"]["groups"][args.group]:
            command += ["--test", name]
        with log.open("wb") as output:
            receipt["runner_exit"] = subprocess.run(command, cwd=repo, stdout=output,
                                                      stderr=subprocess.STDOUT, check=False).returncode
        receipt["runner_log"] = {"path": relative + "/runner.log", "sha256": digest(log)}
        for path in sorted(args.output_parent.glob("native-contracts-*/report.json")):
            logs = {p.name: digest(p) for p in sorted(path.parent.glob("*.log"))}
            receipt["reports"].append({"path": relative + "/" + path.relative_to(args.output_parent).as_posix(),
                                       "sha256": digest(path), "logs": logs})
        if receipt["runner_exit"] == 0:
            shutil.rmtree(payload)
    except (OSError, ValueError, KeyError, TypeError, IndexError, tarfile.TarError, subprocess.SubprocessError) as error:
        receipt["error"] = str(error)
    finally:
        receipt["seconds"] = time.monotonic() - started
        write_json(args.receipt, receipt)
    return 0 if receipt["runner_exit"] == 0 and not receipt["error"] else 1


def runner_module(repo):
    spec = importlib.util.spec_from_file_location("serial_native_runner", repo / "tools/test_godot_native.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def layout(selected, runner):
    """Expected existing setup order and executable subset, without sharing fixtures."""
    rows, binaries = ["native_assets"], [EXPORTER]
    def add(family, role=None):
        rows.append(family)
        if role and FIXTURES[role] not in binaries:
            binaries.append(FIXTURES[role])
    if "wayfarer_operating" in selected:
        add("operating_assets")
    rows += ["snapshot:42", "snapshot:4"]
    if any(runner.TESTS[name] == "freedom_saves" for name in selected):
        rows += ["save:" + name for name in SAVES]
        binaries.append(FIXTURES["start"])
    if "native_minor" in selected:
        add("minor_voyage", "minor")
    if "native_moon" in selected:
        add("moon_voyage", "moon")
    if "native_environment" in selected:
        add("daynight_saves", "surface")
        rows += ["save:shadow-" + name + ".json" for name in ("day", "night", "limb")]
    if "native_planetary" in selected:
        add("planetary_saves", "planetary")
    if "native_surface_loop" in selected:
        add("surface_loop_saves", "planetary")
    for name in runner.ROUNDTRIP_TESTS:
        if name in selected:
            add(name, "planetary")
    for name, role in (("native_impact", "impact"), ("native_frame_import", "frame_import"),
                       ("native_system_release", "system_release"), ("orbital_survey", "orbital_survey")):
        if name in selected:
            add(role + "_saves", role)
    if any(name in selected for name in ("native_derelict", "native_station_crew")):
        add("derelict_saves", "derelict")
    for name in runner.SURFACE_TESTS:
        if name in selected:
            add(runner.TESTS[name] + ":" + name, "surface")
    rows += [name + "_launch" for name in selected if name in LAUNCHES and name != "native_shell"]
    return rows, binaries


def setup_key(row):
    if "save" in row:
        return "save:" + row["save"]
    if "seed" in row:
        return "snapshot:" + str(row["seed"])
    return row["family"] + (":" + row["test"] if "test" in row else "")


def log_check(text, refusal=False):
    matches = DIAGNOSTIC.findall(text)
    if refusal:
        require(len(matches) == 1 and matches[0].strip().startswith("ERROR: Native start rejected: "),
                "Unexpected refusal diagnostic")
    else:
        require(not matches, "Engine diagnostic in successful work")


def validate_report(report, path, metadata, selected, m, runner):
    require(report["schema_version"] == 1 and report["selected"] == selected and
            [row["name"] for row in report["tests"]] == selected, "Wrong selected/test coverage")
    files = m["files"]
    require(report["engine_sha256"] == files["engine/godot"]["sha256"] and
            report["bridge_sha256"] == files["build/" + BRIDGE]["sha256"] and
            report["exporter_sha256"] == files["build/" + EXPORTER]["sha256"] and
            report["extension_descriptor_sha256"] == m["godot_sources"]["bin/freedom.gdextension"],
            "Mixed engine, bridge, exporter or descriptor")
    project = {**m["godot_sources"], "bin/libapsis_freedom_bridge.so": report["bridge_sha256"]}
    require(report["project_sha256"] == project, "Mixed staged Godot sources")
    expected, binaries = layout(selected, runner)
    require(report["staged_executable_sha256"] ==
            {Path(p).name: files["build/" + p]["sha256"] for p in binaries}, "Mixed fixture executables")
    require([setup_key(row) for row in report["setup"]] == expected, "Missing/duplicate/foreign setup")
    refusals, launches, logs = set(), 0, metadata["logs"]
    require(set(logs) == {p.name for p in path.parent.glob("*.log")}, "Incomplete raw log inventory")
    for name, sha in logs.items():
        require(HEX.fullmatch(sha) and digest(child(path.parent, name)) == sha, "Missing or changed log")
    require(logs and all(name.endswith(".log") for name in logs), "Invalid log inventory")
    refusal_logs = set()
    shell = next((row for row in report["tests"] if row["name"] == "native_shell"), None)
    if shell:
        require(all(shell.get(key) is True for key in
                    ("source_saves_unchanged", "identity_matches", "history_matches", "geometry_matches")),
                "Missing shell ownership/save checks")
    for row in report["tests"] + report["setup"]:
        if "status" in row:
            require(row["status"] == "pass", "Failed native test")
        if "returncode" in row:
            require(type(row["returncode"]) is int and row["returncode"] == 0, "Failed process")
        if "timed_out" in row:
            require(row["timed_out"] is False, "Timed-out setup")
        if "cases" in row:
            name = row.get("name", row.get("family", "").removesuffix("_launch"))
            require([c["case"] for c in row["cases"]] == LAUNCHES[name], "Wrong launch case coverage")
            for case in row["cases"]:
                pair = (name, case["case"])
                refused = pair in REFUSALS
                require(case["pass"] is True and type(case["returncode"]) is int and
                        (case["returncode"] == 1 if refused else case["returncode"] == 0) and
                        "seconds" in case and case["log"] in logs, "Failed/invalid launch")
                launches += 1
                if refused:
                    refusals.add(pair)
                    refusal_logs.add(case["log"])
        elif "status" in row:
            require("seconds" in row and row["log"] in logs, "Missing execution evidence")
        else:
            require("returncode" in row and row.get("timed_out") is False, "Incomplete setup")
        for key in ("source_saves_unchanged", "identity_matches", "history_matches", "geometry_matches"):
            if key in row:
                require(row[key] is True, "Shell identity/history/save mismatch")
        if "fixture_sha256" in row:
            family = row["family"]
            role = ("minor" if family == "minor_voyage" else "moon" if family == "moon_voyage" else "surface" if
                    family in ("daynight_saves", "surface_saves", "surface_walk_saves") else
                    "planetary" if family in ("planetary_saves", "surface_loop_saves", *runner.ROUNDTRIP_TESTS)
                    else family.removesuffix("_saves"))
            require(row["fixture_sha256"] == files["build/" + FIXTURES[role]]["sha256"] and
                    isinstance(row.get("files_sha256"), dict) and row["files_sha256"] and
                    all(HEX.fullmatch(sha) for sha in row["files_sha256"].values()), "Foreign/incomplete fixture")
            if family == "minor_voyage":
                require(set(row["files_sha256"]) == {name + ".json" for name in
                    ("arrival", "approach", "withdraw", "home", "rendezvous-near", "reuse-captured",
                     "foreign-entry", "foreign-arrival", "foreign-home", "foreign-approach", "foreign-withdraw")},
                    "Incomplete actual minor phases")
            if family == "derelict_saves":
                require(set(row["files_sha256"]) == {name + ".json" for name in
                    ("station", "approach", "contact", "contact-recovered", "near", "inspection", "recovered")},
                    "Incomplete actual derelict phases")
            if family == "moon_voyage":
                require(set(row["files_sha256"]) == {name + ".json" for name in
                    ("arrival", "approach", "touchdown-source", "landed", "outside", "returned", "released", "ascent",
                     "service-landed", "service-requested", "service-partial600",
                     "foreign-parent", "foreign-arrival", "foreign-home",
                     "foreign-landed", "foreign-outside", "foreign-marked")},
                    "Incomplete actual Moon phases")
        if "preparer_sha256" in row:
            helper = "prepare_operating_assets.py" if row["family"] == "operating_assets" else "prepare_freedom_native_assets.py"
            require(row["preparer_sha256"] == m["sources"]["tools/" + helper], "Foreign preparer")
        if row.get("family") == "native_assets":
            require(set(row["helper_sha256"]) == set(ASSET_HELPERS) and
                    row["prepared"]["schema"] == "apsis.native-starter-assets/1" and
                    HEX.fullmatch(row["prepared"]["package_sha256"]) and
                    set(row["companions"]) == {"operating", "stowed", "crew"},
                    "Incomplete native asset provenance")
            crew = row["companions"]["crew"]
            require(type(crew) is dict and set(crew) == {"schema", "package_sha256"} and
                    crew["schema"] == "apsis.origin-crew-assets/1" and
                    type(crew["package_sha256"]) is str and HEX.fullmatch(crew["package_sha256"]),
                    "Incomplete Origin crew provenance")
        if row.get("family") == "operating_assets":
            require(row["spec_sha256"] == m["sources"]["tools/wayfarer_operating_spec.py"], "Foreign operating spec")
        if "seed" in row or "save" in row:
            require(HEX.fullmatch(row.get("snapshot_sha256", row.get("save_sha256", ""))),
                    "Missing generated source hash")
        for helper, sha in row.get("helper_sha256", {}).items():
            require(sha == m["sources"].get("tools/" + helper), "Foreign helper")
    for name, sha in logs.items():
        log = child(path.parent, name)
        require(log.stat().st_size <= 32 << 20, "Oversized log")
        log_check(log.read_text(errors="replace"), name in refusal_logs)
    cli_logs = []
    if "native_planetary" in selected:
        cli_logs += [f"planetary-refusal-{i}.log" for i in range(4)]
    if "native_surface_loop" in selected:
        cli_logs += [f"surface-loop-refusal-{i}.log" for i in range(11)]
    for name in runner.ROUNDTRIP_TESTS:
        if name in selected:
            cli_logs += [f"{name}-refusal-{i}.log" for i in range(9)]
    require(all(name in logs for name in cli_logs), "Missing CLI refusal evidence")
    if "native_first_flight_continuation" in selected:
        source = next(r for r in report["setup"] if r.get("family") == "native_first_flight_continuation")["continuation_source"]
        require(source["native_prefix_test"] == "native_surface_loop" and
                all(source[key] is True for key in ("native_prefix_selected", "fixture_preserved_bytes", "unchanged_after_native")),
                "Lost native prefix or save continuity")
        for name in (source["file"], "native_first_flight_continuation/surface-ascent.json"):
            file = child(path.parent, name)
            require(type(source["bytes"]) is int and 0 < source["bytes"] <= 1 << 20 and
                    file.stat().st_size == source["bytes"] and digest(file) == source["sha256"],
                    "Continuation save changed")
    require(refusals == {pair for pair in REFUSALS if pair[0] in selected}, "Refusal coverage mismatch")
    return {"tests": len(selected), "setup_rows": len(expected), "launch_cases": launches,
            "expected_nonzero_launches": len(refusals), "cli_refusal_logs": len(cli_logs)}


def validate_sky(path, m):
    sky = read_json(path)
    require(sky["passed"] is True and sky["returncode"] == 0 and sky["timed_out"] is False and
            sky["sources_unchanged"] is True and sky["diagnostics"] == [] and
            "seconds" in sky and sky["timeout_seconds"] == 45, "Sky gate failed")
    require(sky["engine_sha256"] == m["files"]["engine/godot"]["sha256"] and
            sky["runner_sha256"] == m["sources"]["tools/test_native_sky.py"] and
            sky["source_sha256"] == {name: m["godot_sources"][name] for name in
                ("shaders/native_planet_optics.gdshaderinc", "tests/native_sky_camera_test.gd", "shaders/native_navigation_sky.gdshader")},
            "Mixed sky provenance")
    capture = sky["capture"]
    require(capture["pass"] is True and type(capture["cpu_invalid_cases_rejected"]) is int and
            capture["cpu_invalid_cases_rejected"] == 13 and len(capture["cases"]) == 2 and
            capture["shader_sha256"] == m["godot_sources"]["shaders/native_navigation_sky.gdshader"], "Incomplete sky proof")
    for row, y in zip(capture["cases"], (0, 9)):
        require(row["camera_position"] == [0, y, 0] and row["viewport"] == [3840, 2160] and
                abs(row["error_pixels"]) <= 1 and digest(child(path.parent, row["image"])) == row["sha256"],
                "Bad sky raster proof")
    log_check((path.parent / "engine.log").read_text(errors="replace"))


def check(args, repo=REPO):
    require(len(args.dependency) == 2 and set(args.dependency) == {"producer=success", "workers=success"},
            "Producer/workers not both successful")
    envelope = load_manifest(args.manifest, repo)
    m, runner = envelope["manifest"], runner_module(repo)
    require(len(args.invocation) == 4, "Expected four invocation receipts")
    seen, rows, common_assets = set(), [], None
    for path in args.invocation:
        inv = read_json(path)
        group = inv["group"]
        require(group in m["groups"] and group not in seen, "Duplicate/foreign group")
        seen.add(group)
        require(inv["schema_version"] == 1 and type(inv["runner_exit"]) is int and inv["runner_exit"] == 0 and
                inv["error"] == "" and inv["revision"] == m["revision"] and
                inv["manifest_sha256"] == digest(args.manifest) and len(inv["reports"]) == 1,
                "Failed/incomplete/mixed worker invocation")
        raw = inv["reports"][0]
        report_path = child(path.parent, raw["path"])
        require(digest(report_path) == raw["sha256"], "Changed raw report")
        log = child(path.parent, inv["runner_log"]["path"])
        require(digest(log) == inv["runner_log"]["sha256"], "Changed runner log")
        log_check(log.read_text(errors="replace"))
        report = read_json(report_path)
        counts = validate_report(report, report_path, raw, m["groups"][group], m, runner)
        assets = {k: v for k, v in report["setup"][0].items() if k != "seconds"}
        require(common_assets is None or assets == common_assets, "Mixed asset preparation")
        common_assets = assets
        rows.append({"group": group, "invocation_sha256": digest(path), "report": raw["path"],
                     "report_sha256": raw["sha256"], "counts": counts, "seconds": inv["seconds"]})
    require(seen == set(m["groups"]), "Missing group")
    validate_sky(args.sky_report, m)
    summary = {"schema_version": 1, "passed": True, "revision": m["revision"],
               "manifest_sha256": digest(args.manifest), "groups": sorted(rows, key=lambda r: r["group"]),
               "counts": {key: sum(row["counts"][key] for row in rows) for key in rows[0]["counts"]},
               "sky": {"report_sha256": digest(args.sky_report), "cases": 2, "invalid_cases": 13}}
    write_json(args.output, summary)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("pack", "run", "check"):
        p = sub.add_parser(command)
        p.add_argument("--manifest", type=Path, required=True)
        if command in ("pack", "run"):
            p.add_argument("--archive", type=Path, required=True)
        if command == "pack":
            p.add_argument("--build-dir", type=Path, required=True)
            p.add_argument("--godot", type=Path, required=True)
        elif command == "run":
            p.add_argument("--group", choices=("1", "2", "3", "4"), required=True)
            p.add_argument("--output-parent", type=Path, required=True)
            p.add_argument("--receipt", type=Path, required=True)
        else:
            p.add_argument("--invocation", type=Path, action="append", required=True)
            p.add_argument("--dependency", action="append", required=True)
            p.add_argument("--sky-report", type=Path, required=True)
            p.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        return globals()[args.command](args) or 0
    except (OSError, ValueError, KeyError, TypeError, IndexError, tarfile.TarError,
            subprocess.SubprocessError, RecursionError) as error:
        if args.command == "check":
            write_json(args.output, {"schema_version": 1, "passed": False, "error": str(error)})
        print(f"Native CI {args.command} rejected: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
