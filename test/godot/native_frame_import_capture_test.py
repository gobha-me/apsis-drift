#!/usr/bin/env python3
"""Real-Godot capture contracts; optional staged native integration, no builds.

Run through the caller's heavy-work wrapper. --work-dir must be a new directory.
Without --project, copy only small source resources into an isolated project.
Use --graphical for Xvfb rejection checks, --fixture-exporter for real C++ saves,
or supply --project/--fixtures/--assets from test_godot_native.py --keep-work.
--capture additionally runs the actual production capture and audits its files.
All outputs, engine preferences and caches stay below --work-dir.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys


CAPTURE = "res://studies/captures/native_frame_import_capture.gd"
HELPERS = "res://tests/native_frame_import_capture_test.gd"
PAIR_HELPERS = "res://tests/native_frame_import_capture_fixture_test.gd"
SIZES = ((800, 450), (1920, 1080), (3840, 2160))
SCRIPT_ERROR = re.compile(r"SCRIPT ERROR:|Parse Error:|Compile Error:|Failed to load script")
ENGINE_DIAGNOSTIC = re.compile(
    r"^\s*(?:ERROR:|SCRIPT ERROR:)|ObjectDB.*instances?.*leak|"
    r"resources still in use at exit|RID[^\n]*leak", re.IGNORECASE | re.MULTILINE)


def run_owned_process(command, *, cwd, env, output, timeout):
    """Own one session so an xvfb-run timeout also terminates its children."""
    process = subprocess.Popen(command, cwd=cwd, env=env, stdout=output,
                               stderr=subprocess.STDOUT, start_new_session=True)
    try:
        code = process.wait(timeout=timeout)
    except BaseException:
        # Only this newly created process group is signalled. Always reap the
        # direct child, including on interruption, before propagating the error.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        raise
    return subprocess.CompletedProcess(command, code)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--fixtures", type=Path)
    parser.add_argument("--fixture-exporter", type=Path)
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--graphical", action="store_true")
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--size", action="append", choices=[f"{w}x{h}" for w, h in SIZES])
    args = parser.parse_args()
    if args.fixtures and args.fixture_exporter:
        parser.error("choose --fixtures or --fixture-exporter")
    if args.capture and not (args.graphical and args.project and args.assets
                             and (args.fixtures or args.fixture_exporter)):
        parser.error("--capture requires --graphical, --project, real fixtures and --assets")
    engine = args.godot.resolve()
    if not engine.is_file():
        parser.error("--godot must name an installed engine")
    work = args.work_dir.resolve()
    if work.exists():
        parser.error("--work-dir must be new; previous evidence is retained")
    work.mkdir(parents=True)
    repo = Path(__file__).resolve().parents[2]
    project = args.project.resolve() if args.project else work / "project"
    if not args.project:
        project.mkdir()
        for folder in ("scripts", "scenes", "shaders", "settings", "studies", "tests"):
            for source in (repo / "godot" / folder).rglob("*"):
                if source.is_file() and source.suffix in (".gd", ".gdshader", ".gdshaderinc", ".tscn", ".json"):
                    destination = project / source.relative_to(repo / "godot")
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, destination)
        shutil.copy2(repo / "godot/project.godot", project / "project.godot")
    if not (project / "tests/native_frame_import_capture_test.gd").is_file():
        parser.error("staged project must include the independent helper test")
    env = os.environ.copy()
    for name, directory in (("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"),
                            ("XDG_CACHE_HOME", "cache"), ("TMPDIR", "tmp")):
        (work / directory).mkdir()
        env[name] = str(work / directory)
    env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    cases = []
    skipped = []

    def run(name, command, timeout=30):
        log = work / f"{len(cases):03d}-{name}.log"
        try:
            with log.open("w") as output:
                result = run_owned_process(list(map(str, command)), cwd=work, env=env,
                                           output=output, timeout=timeout)
            code = result.returncode
        except subprocess.TimeoutExpired:
            code = None
        text = log.read_text(errors="replace")
        case = {"name": name, "returncode": code, "log": str(log), "failures": []}
        cases.append(case)
        return case, text

    def check(case, condition, reason):
        if not condition:
            case["failures"].append(reason)
            print(f"FAIL {case['name']}: {reason} ({case['log']})", flush=True)

    def finish():
        failed = sum(bool(case["failures"]) for case in cases)
        (work / "tests.json").write_text(json.dumps({"cases": cases, "failed": failed, "skipped": skipped}, indent=2) + "\n")
        print(f"Native frame import capture: {len(cases)} cases, {failed} failed; {work / 'tests.json'}", flush=True)
        return int(failed != 0)

    def command(script=CAPTURE, graphical=False, size=(800, 450)):
        base = [engine, "--audio-driver", "Dummy", "--path", project]
        if graphical:
            w, h = size
            return ["xvfb-run", "--auto-servernum", f"--server-args=-screen 0 {w}x{h}x24",
                    *base, "--display-driver", "x11", "--rendering-method", "gl_compatibility",
                    "--resolution", f"{w}x{h}", "--script", script]
        return [*base, "--headless", "--script", script]

    case, text = run("production-parse", [*command(), "--check-only"])
    check(case, case["returncode"] == 0 and not SCRIPT_ERROR.search(text), "production script must compile in actual Godot")
    if case["failures"]:
        # Keep independent tests and exact parser diagnostics; never patch the
        # production source or mask its failure with a substitute implementation.
        print(text, flush=True)
        return finish()
    case, text = run("helpers", command(HELPERS))
    check(case, case["returncode"] == 0 and "0 failures" in text
          and not SCRIPT_ERROR.search(text), "real helper assertions failed")

    fixtures = args.fixtures.resolve() if args.fixtures else work / "missing-fixtures"
    assets = args.assets.resolve() if args.assets else work / "missing-assets"

    def refused(name, arguments, diagnostic, output, graphical=False):
        case, text = run(name, [*command(graphical=graphical), "--", *arguments])
        check(case, case["returncode"] == 1, "expected explicit exit 1, without crash or timeout")
        check(case, diagnostic in text and not SCRIPT_ERROR.search(text), "expected guard diagnostic, without script errors")
        check(case, not output.exists(), "refused invocation created its output")
        check(case, "Native frame import capture: PASS" not in text, "refusal reported success")
        return case

    def arguments(output, width="800", height="450"):
        return [str(fixtures), str(assets), str(output), width, height]

    base = arguments(work / "arity-output")
    for count in range(5):
        refused(f"arity-{count}", base[:count], "Expected absolute fixture/assets/new output directories, width and height", work / "arity-output")
    refused("arity-extra", [*base, "extra"], "Expected absolute fixture/assets/new output directories, width and height", work / "arity-output")
    # Godot trims outer argv whitespace before OS.get_cmdline_user_args(); use
    # embedded whitespace to exercise malformed numbers that reach the tool.
    bad_numbers = ("0", "-1", "", " ", "800.0", "8e2", "nan", "NaN", "inf", "-inf",
                   "Infinity", "1e309", "+800", "0800", "8 00", "80\t0", "800x", "0x320",
                   "9223372036854775808", "-9223372036854775809", "18446744073709551616")
    for axis in (0, 1):
        for index, value in enumerate(bad_numbers):
            pair = ["800", "450"]
            pair[axis] = value
            output = work / f"bad-number-{axis}-{index}"
            refused(output.name, arguments(output, *pair), "Unsupported capture dimensions", output)
    for index, (width, height) in enumerate(((799, 450), (800, 451), (1280, 720), (450, 800), (1920, 2160), (0, 0), (-800, -450))):
        output = work / f"unsupported-{index}"
        refused(output.name, arguments(output, str(width), str(height)), "Unsupported capture dimensions", output)
    for width, height in SIZES:
        for shift in (-4294967296, 4294967296):
            for axis in (0, 1):
                pair = [width, height]
                pair[axis] += shift
                output = work / f"wrapped-{width}-{shift}-{axis}"
                refused(output.name, arguments(output, *map(str, pair)), "Unsupported capture dimensions", output)
        output = work / f"headless-{width}"
        refused(output.name, arguments(output, str(width), str(height)), "Capture requires an isolated rendering display", output)
    for index in range(3):
        output = work / f"relative-{index}"
        values = arguments(output)
        values[index] = "relative-directory"
        refused(output.name, values, "Capture directories must be absolute", output)

    if args.fixture_exporter:
        fixtures = work / "fixtures"
        case, text = run("authoritative-fixtures", [args.fixture_exporter.resolve(), "--fixtures", fixtures], timeout=900)
        check(case, case["returncode"] == 0 and all((fixtures / name).is_file() for name in ("system-import.json", "canonical-before.json", "malformed-import.json")), "real C++ fixture export failed")
        if case["failures"]:
            return finish()
    if fixtures.is_dir():
        case, text = run("fixture-pair-helpers", [*command(PAIR_HELPERS), "--",
                          fixtures / "system-import.json", fixtures / "canonical-before.json"])
        check(case, case["returncode"] == 0 and "0 failures" in text
              and not SCRIPT_ERROR.search(text), "real fixture-pair helper assertions failed")
        check(case, not ENGINE_DIAGNOSTIC.search(text), "fixture-pair helpers emitted engine/leak diagnostics")
    else:
        skipped.append("fixture-pair helpers require real fixtures")

    if args.graphical:
        if not shutil.which("xvfb-run"):
            parser.error("--graphical requires installed xvfb-run")
        output = work / "missing-first"
        refused(output.name, [str(work / "absent"), str(assets), str(output), "800", "450"], "Missing authoritative fixture: system-import.json", output, True)
        if fixtures.is_dir():
            before = {path.name: digest(path) for path in fixtures.iterdir() if path.is_file()}
            partial = work / "partial-fixtures"
            partial.mkdir()
            shutil.copy2(fixtures / "system-import.json", partial / "system-import.json")
            output = work / "missing-second"
            refused(output.name, [str(partial), str(assets), str(output), "800", "450"], "Missing authoritative fixture: canonical-before.json", output, True)
            # Test file kinds with untouched exporter bytes under each required
            # name, rather than inventing JSON saves to reach the preflight.
            for name, system_name, canonical_name in (
                    ("duplicated-canonical", "canonical-before.json", "canonical-before.json"),
                    ("duplicated-system", "system-import.json", "system-import.json"),
                    ("reversed-fixture-kinds", "canonical-before.json", "system-import.json")):
                altered = work / name
                altered.mkdir()
                for destination, source in (("system-import.json", system_name),
                                            ("canonical-before.json", canonical_name)):
                    shutil.copy2(fixtures / source, altered / destination)
                copies = {path.name: digest(path) for path in altered.iterdir()}
                output = work / f"refused-{name}"
                case = refused(name, [str(altered), str(work / "absent-assets"), str(output), "800", "450"],
                               "Expected distinct format35 system-frame and format31 canonical planetary fixtures", output, True)
                check(case, copies == {path.name: digest(path) for path in altered.iterdir()},
                      "fixture-kind preflight changed authentic file copies")

            for name in ("system-import.json", "canonical-before.json"):
                nonfile = work / f"nonfile-{Path(name).stem}"
                nonfile.mkdir()
                for source in ("system-import.json", "canonical-before.json"):
                    if source == name:
                        (nonfile / source).mkdir()
                    else:
                        shutil.copy2(fixtures / source, nonfile / source)
                output = work / f"refused-{nonfile.name}"
                refused(nonfile.name, [str(nonfile), str(assets), str(output), "800", "450"],
                        "Missing authoritative fixture: " + name, output, True)

                # Whitespace padding preserves the exported JSON document and
                # its world. Test the exact inclusive byte boundary on each file.
                for length in (1048576, 1048577):
                    padded = work / f"size-{Path(name).stem}-{length}"
                    padded.mkdir()
                    for source in ("system-import.json", "canonical-before.json"):
                        shutil.copy2(fixtures / source, padded / source)
                    path = padded / name
                    with path.open("ab") as stream:
                        stream.write(b" " * (length - path.stat().st_size))
                    copies = {path.name: digest(path) for path in padded.iterdir()}
                    output = work / f"refused-{padded.name}"
                    reason = ("Prepared native assets are required" if length == 1048576 else
                              "Fixture unavailable or larger than 1 MiB: " + name)
                    case = refused(padded.name, [str(padded), str(work / "absent-assets"), str(output), "800", "450"],
                                   reason, output, True)
                    check(case, path.stat().st_size == length, "fixture boundary test used the wrong byte length")
                    check(case, copies == {path.name: digest(path) for path in padded.iterdir()},
                          "fixture size preflight changed its input bytes")

            output = work / "missing-prepared-assets"
            refused(output.name, [str(fixtures), str(work / "absent-assets"), str(output), "800", "450"], "Prepared native assets are required", output, True)
            if args.assets:
                for kind in ("file", "directory"):
                    used = work / f"used-output-{kind}"
                    if kind == "directory":
                        used.mkdir()
                    sentinel = used / "sentinel" if kind == "directory" else used
                    sentinel.write_bytes(b"preserve existing output\x00\xff\n")
                    case, text = run(used.name, [*command(graphical=True), "--", *arguments(used)])
                    check(case, case["returncode"] == 1 and "Capture output must be new" in text and not SCRIPT_ERROR.search(text), "used output was not explicitly refused")
                    check(case, sentinel.read_bytes() == b"preserve existing output\x00\xff\n", "used output bytes changed")
                    if kind == "directory":
                        check(case, list(used.iterdir()) == [sentinel], "used directory acquired capture files")
                if not args.project:
                    output = work / "missing-bridge"
                    refused(output.name, arguments(output), "Native bridge unavailable", output, True)
            else:
                skipped.append("used-output/bridge guards require real --assets")
                print("SKIP used-output/bridge guards: supply real --assets from a native stage", flush=True)
            case = {"name": "fixture-preservation", "failures": []}
            cases.append(case)
            check(case, before == {path.name: digest(path) for path in fixtures.iterdir() if path.is_file()}, "rejected commands changed real fixtures")
        else:
            skipped.append("later graphical guards require real fixtures")
            print("SKIP later graphical guards: supply real --fixtures or --fixture-exporter", flush=True)
    else:
        skipped.append("graphical guards require --graphical")

    if args.capture:
        names = ("system-import.json", "canonical-before.json")
        originals = {name: digest(fixtures / name) for name in names}
        blocked = work / "blocked-parent"
        blocked.write_bytes(b"preserve parent file\n")
        output = blocked / "capture"
        case = refused("output-creation-failure", arguments(output), "Cannot create capture output", output, True)
        check(case, blocked.read_bytes() == b"preserve parent file\n", "output creation failure changed blocking file")

        # Exercise failures after output creation with unmodified, authentic
        # exporter saves. Renaming copies selects real malformed/wrong-tick
        # fixtures; no JSON document or saved universe is manufactured.
        for name, source, canonical, reason in (
                ("malformed-owner", "malformed-import.json", "canonical-before.json", "Continue refused:"),
                ("wrong-canonical-tick", "system-import.json", "canonical-after.json", "Canonical Save As differs after capture")):
            altered = work / name
            altered.mkdir()
            shutil.copy2(fixtures / source, altered / "system-import.json")
            shutil.copy2(fixtures / canonical, altered / "canonical-before.json")
            before = {path.name: digest(path) for path in altered.iterdir()}
            output = work / f"failed-{name}"
            case, text = run(name, [*command(graphical=True), "--", str(altered), str(assets), str(output), "800", "450"], timeout=900)
            check(case, case["returncode"] == 1 and reason in text and "Native frame import capture: FAIL" in text and not SCRIPT_ERROR.search(text), "native failure must terminate with its original diagnostic")
            report_path = output / "capture.json"
            check(case, report_path.is_file(), "native failure lost its diagnostic report")
            if report_path.is_file():
                report = json.loads(report_path.read_text())
                check(case, report.get("passed") is False and report.get("pixels_equal") is False and reason in report.get("error", ""), "native failure report claims success or lost its first error")
                check(case, report.get("captures") == [], "failed first import claimed a completed capture")
            check(case, before == {path.name: digest(path) for path in altered.iterdir()}, "native failure changed authentic input copies")

        for size in args.size or ["800x450"]:
            width, height = map(int, size.split("x"))
            output = work / f"capture-{size}"
            case, text = run(output.name, [*command(graphical=True, size=(width, height)), "--", *arguments(output, str(width), str(height))], timeout=900)
            check(case, case["returncode"] == 0 and "Native frame import capture: PASS" in text and not SCRIPT_ERROR.search(text), "actual native capture failed")
            check(case, not ENGINE_DIAGNOSTIC.search(text), "positive capture emitted engine errors or resource-leak diagnostics")
            report_path = output / "capture.json"
            check(case, report_path.is_file(), "capture report missing")
            if not report_path.is_file():
                continue
            report = json.loads(report_path.read_text())
            check(case, report.get("schema") == "apsis.native-frame-import-capture/1" and report.get("passed") is True and report.get("pixels_equal") is True and report.get("error") == "", "report does not prove successful comparison")
            check(case, report.get("display") != "headless" and report.get("viewport") == [width, height], "report has wrong display or dimensions")
            check(case, report.get("fixture_sha256") == originals, "report lost authoritative fixture hashes")
            check(case, report.get("prepared_assets_sha256") == digest(assets / "prepared.json"), "report lost prepared-asset receipt hash")
            check(case, bool(report.get("sources_sha256")) and all(digest(project / name) == value for name, value in report.get("sources_sha256", {}).items()), "report source/bridge hashes differ from staged project")
            captures = report.get("captures", [])
            check(case, [item.get("source") for item in captures] == list(names), "report must contain both real imports in order")
            for item in captures:
                source = item["source"]
                stem = Path(source).stem
                png = output / f"{stem}.png"
                saved = output / f"{stem}-resaved.json"
                check(case, png.is_file() and item.get("image") == png.name and item.get("image_sha256") == digest(png), "PNG absent or hash differs")
                check(case, saved.is_file() and saved.read_bytes() == (fixtures / "canonical-before.json").read_bytes() and item.get("canonical_save_sha256") == originals["canonical-before.json"], "complete canonical Save As differs")
                check(case, item.get("tick") == "25" and item.get("terrain_policy") == 2, "real fixture clock/policy changed")
                for camera in ("near", "terrain"):
                    check(case, item.get("camera", {}).get(camera, {}).get("viewport") == [width, height], "internal camera viewport differs from requested full resolution")
            if len(captures) == 2:
                check(case, captures[0].get("camera") == captures[1].get("camera"), "same-pose cameras differ")
                pngs = [output / f"{Path(name).stem}.png" for name in names]
                if all(path.is_file() for path in pngs):
                    audit, text = run(f"png-audit-{size}", [*command(HELPERS), "--", *pngs, str(width), str(height)], timeout=900)
                    check(audit, audit["returncode"] == 0 and "0 failures" in text and not SCRIPT_ERROR.search(text), "independent Godot PNG visibility/dimension audit failed")
                    check(audit, not ENGINE_DIAGNOSTIC.search(text), "PNG audit emitted engine errors or resource-leak diagnostics")
            check(case, originals == {name: digest(fixtures / name) for name in names}, "capture changed authoritative input bytes")
    else:
        skipped.append("native failure reports and positive captures require --capture and a staged native project")
    return finish()


if __name__ == "__main__":
    sys.exit(main())
