#!/usr/bin/env python3
"""Real Godot priority/lifecycle tests and opt-in exact same-source reloads.

Run through the caller's heavy-work wrapper; never builds or copies assets.
Supply an existing native --project and authentic C++ --fixtures. --graphical
also requires --assets and runs all three output sizes unless --size is given.
All outputs/preferences/caches live in a new --work-dir. Test resources and
planet_stream.gd must already match this checkout; stage them explicitly first.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import select
import struct
import subprocess
import sys
import traceback
import zlib


SIZES = ("800x450", "1920x1080", "3840x2160")
DIAGNOSTIC = re.compile(
    r"SCRIPT ERROR:|Parse Error:|Compile Error:|Failed to load script|"
    r"ObjectDB.*instances?.*leak|resources still in use at exit|RID[^\n]*leak",
    re.IGNORECASE)
ENGINE_ERROR = re.compile(r"^\s*ERROR:", re.MULTILINE)
TESTS = ("terrain_render_priority_test.gd", "terrain_render_priority_lifecycle_test.gd",
         "terrain_same_source_reload_test.gd")
FIXTURES = ("system-import.json", "canonical-before.json", "canonical-after.json", "malformed-import.json")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stop(process):
    """Signal only an exact child PID created by this runner, and reap it."""
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)


def pixels(path, expected):
    """Independently decode lossless RGB(A) PNGs, with CRC/buffer checks."""
    encoded = path.read_bytes()
    assert encoded[:8] == b"\x89PNG\r\n\x1a\n", path
    offset, compressed, header = 8, [], None
    while offset < len(encoded):
        length = struct.unpack_from(">I", encoded, offset)[0]
        kind = encoded[offset + 4:offset + 8]
        chunk = encoded[offset + 8:offset + 8 + length]
        assert zlib.crc32(kind + chunk) & 0xffffffff == struct.unpack_from(">I", encoded, offset + 8 + length)[0], path
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", chunk)
        if kind == b"IDAT":
            compressed.append(chunk)
        offset += length + 12
    assert header is not None and offset == len(encoded), path
    width, height, depth, color, compression, filtering, interlace = header
    assert (width, height) == expected and depth == 8 and color in (2, 6), path
    assert compression == filtering == interlace == 0, path
    channels = 3 if color == 2 else 4
    stride = width * channels
    raw = zlib.decompress(b"".join(compressed))
    assert len(raw) == height * (stride + 1), path
    output, prior = bytearray(), bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        method = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        assert method in range(5), path
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            above = prior[i]
            corner = prior[i - channels] if i >= channels else 0
            predictor = 0
            if method == 1:
                predictor = left
            elif method == 2:
                predictor = above
            elif method == 3:
                predictor = (left + above) // 2
            elif method == 4:
                value = left + above - corner
                distances = (abs(value - left), abs(value - above), abs(value - corner))
                predictor = (left, above, corner)[distances.index(min(distances))]
            row[i] = (row[i] + predictor) & 255
        if channels == 4:
            output.extend(row)
        else:
            for i in range(0, stride, 3):
                output.extend(row[i:i + 3])
                output.append(255)
        prior = row
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    parser.add_argument("--project", required=True, type=Path)
    parser.add_argument("--fixtures", required=True, type=Path)
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--graphical", action="store_true")
    parser.add_argument("--size", action="append", choices=SIZES)
    args = parser.parse_args()
    if args.graphical and not args.assets:
        parser.error("--graphical requires the existing prepared --assets")
    engine, project, fixtures, work = (path.resolve() for path in
                                      (args.godot, args.project, args.fixtures, args.work_dir))
    if work.exists():
        parser.error("--work-dir must be new; keep previous evidence")
    repo = Path(__file__).resolve().parents[2]
    resources = ["scripts/world/planet_stream.gd", *["tests/" + name for name in TESTS],
                 "studies/captures/native_frame_import_capture.gd"]
    if any(not (project / name).is_file() or digest(project / name) != digest(repo / "godot" / name)
           for name in resources):
        parser.error("explicitly stage current production/test resources; staged hashes differ")
    work.mkdir(parents=True)
    env = os.environ.copy()
    for name, child in (("TMPDIR", "tmp"), ("XDG_DATA_HOME", "data"),
                        ("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache")):
        (work / child).mkdir()
        env[name] = str(work / child)
    env.update(LIBGL_ALWAYS_SOFTWARE="1", LP_NUM_THREADS="2", MAKEFLAGS="-j2",
               CMAKE_BUILD_PARALLEL_LEVEL="2", CTEST_PARALLEL_LEVEL="2")
    fixture_hashes = {p.name: digest(p) for p in fixtures.iterdir() if p.is_file()}
    cases = []

    def run(name, script, arguments=(), graphical=False, size="800x450"):
        case = {"name": name, "failures": []}
        cases.append(case)
        directory = work / name
        directory.mkdir()
        xvfb = process = None
        read_fd = write_fd = None
        local_env = env.copy()
        command = [str(engine), "--audio-driver", "Dummy", "--path", str(project)]
        try:
            if graphical:
                read_fd, write_fd = os.pipe()
                with (directory / "xvfb.log").open("w") as log:
                    xvfb = subprocess.Popen(
                        ["Xvfb", "-displayfd", str(write_fd), "-screen", "0", size + "x24", "-nolisten", "tcp", "-ac"],
                        cwd=directory, env=local_env, stdout=log, stderr=subprocess.STDOUT, pass_fds=(write_fd,))
                os.close(write_fd)
                write_fd = None
                if not select.select([read_fd], [], [], 15)[0]:
                    raise RuntimeError("Owned Xvfb startup exceeded 15 seconds")
                display = os.read(read_fd, 128).decode().strip()
                if not display.isdigit():
                    raise RuntimeError("Owned Xvfb failed to return a display")
                local_env["DISPLAY"] = ":" + display
                command += ["--display-driver", "x11", "--rendering-method", "gl_compatibility", "--resolution", size]
            else:
                command += ["--headless"]
            command += ["--script", "res://tests/" + script, "--", *map(str, arguments)]
            (directory / "command.json").write_text(json.dumps(command, indent=2) + "\n")
            with (directory / "godot.log").open("w") as log:
                process = subprocess.Popen(command, cwd=directory, env=local_env, stdout=log, stderr=subprocess.STDOUT)
            code = process.wait(timeout=900)
            text = (directory / "godot.log").read_text(errors="replace")
            if code != 0 or DIAGNOSTIC.search(text):
                raise RuntimeError(f"Actual Godot rc={code} or script/resource diagnostic: {text[-3000:]}")
            expected = "Terrain same-source reload: PASS" if graphical else "0 failures"
            if expected not in text:
                raise RuntimeError("Missing explicit test completion")
            # The deliberate invalid tick latches StreamView.error without
            # logging an engine error. Upload/engine errors always fail.
            if ENGINE_ERROR.search(text):
                raise RuntimeError("Unexpected engine ERROR")
            case["returncode"] = code
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            case["failures"].append(str(error))
        finally:
            stop(process)
            stop(xvfb)
            if read_fd is not None:
                os.close(read_fd)
            if write_fd is not None:
                os.close(write_fd)
        print(name + (": FAIL " + str(case["failures"]) if case["failures"] else ": PASS"), flush=True)
        return case

    run("properties", TESTS[0])
    save = work / "lifecycle-resaved.json"
    case = run("lifecycle", TESTS[1], [fixtures / "system-import.json", save])
    if not save.is_file() or save.read_bytes() != (fixtures / "canonical-before.json").read_bytes():
        case["failures"].append("Complete canonical bytes differ after lifecycle")
    if args.graphical and not any(case["failures"] for case in cases):
        assets = args.assets.resolve()
        for size in args.size or SIZES:
            width, height = map(int, size.split("x"))
            output = work / ("capture-" + size)
            case = run("reload-" + size, TESTS[2], [fixtures, assets, output, width, height], True, size)
            if case["failures"]:
                break
            try:
                report = json.loads((output / "reload.json").read_text())
                assert report["schema"] == "apsis.terrain-same-source-reload-test/1"
                assert report["passed"] is True and report["error"] == ""
                assert report["viewport"] == [width, height]
                assert report["display"].casefold() == "x11" and report["rendering_method"] == "gl_compatibility"
                assert report["fixture_sha256"] == {name: fixture_hashes[name] for name in FIXTURES}
                assert report["engine_sha256"] == digest(engine)
                assert report["prepared_assets_sha256"] == digest(assets / "prepared.json")
                assert all(digest(project / name) == value for name, value in report["sources_sha256"].items())
                captures = report["captures"]
                assert [(c["stage"], c["frame"]) for c in captures] == [(stage, frame) for stage in (0, 1) for frame in (0, 1, 8, 32)]
                baseline = None
                for capture in captures:
                    assert capture["source"] == "system-import.json"
                    assert capture["camera"] == captures[0]["camera"]
                    assert capture["tiles"] == captures[0]["tiles"]
                    assert capture["projection_sha256"] == captures[0]["projection_sha256"]
                    image = output / capture["image"]
                    assert digest(image) == capture["png_sha256"]
                    assert (output / capture["canonical_save"]).read_bytes() == (fixtures / "canonical-before.json").read_bytes()
                    decoded = pixels(image, (width, height))
                    assert len(decoded) == width * height * 4
                    assert decoded[3::4] == b"\xff" * (width * height), "Nonopaque native image"
                    assert any(decoded[i:i + 4] != decoded[:4] for i in range(4, len(decoded), 4)), "Vacuous uniform image"
                    if baseline is None:
                        baseline = decoded
                    assert decoded == baseline, "Independent exact decoded PNG comparison failed"
                assert captures[0]["identity"]["generation"] != captures[4]["identity"]["generation"]
            except (OSError, ValueError, KeyError, AssertionError, struct.error, zlib.error):
                case["failures"].append("Independent capture audit: " + traceback.format_exc(limit=2))
            if case["failures"]:
                break
    preservation = {"name": "authentic-fixture-preservation", "failures": []}
    if fixture_hashes != {p.name: digest(p) for p in fixtures.iterdir() if p.is_file()}:
        preservation["failures"].append("Authentic fixture bytes changed")
    cases.append(preservation)
    failed = sum(bool(case["failures"]) for case in cases)
    (work / "tests.json").write_text(json.dumps({"cases": cases, "failed": failed,
                                               "graphical_requested": args.graphical}, indent=2) + "\n")
    print(f"Terrain priority tests: {len(cases)} cases, {failed} failed; {work / 'tests.json'}", flush=True)
    return int(failed != 0)


if __name__ == "__main__":
    sys.exit(main())
