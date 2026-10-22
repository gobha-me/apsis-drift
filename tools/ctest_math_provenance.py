#!/usr/bin/env python3
"""Fingerprint an existing CTest binary without running it; BSD-3-Clause."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def missing(reason):
    return {"available": False, "reason": reason}


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def command(argv):
    try:
        with tempfile.TemporaryFile() as output:
            result = subprocess.run(argv, stdout=output, stderr=subprocess.STDOUT,
                                    timeout=5, check=False)
            output.seek(0)
            data = output.read(65537)
        if len(data) > 65536:
            return None, "output limit"
        return result.returncode, data.decode("utf-8", errors="replace")
    except (OSError, subprocess.TimeoutExpired):
        return None, "tool unavailable or timed out"


def portable(text):
    return re.sub(r"/[^\s\"'<>]+", lambda m: "<path:" + Path(m[0]).name + ">", text)


def build_id(path):
    tool = shutil.which("readelf")
    if not tool:
        return missing("readelf unavailable")
    code, text = command([tool, "-n", str(path)])
    found = re.findall(r"Build ID:\s*([0-9a-fA-F]+)", text)
    return found[0].lower() if code == 0 and len(found) == 1 else missing("no unique GNU build ID")


def file_identity(path):
    try:
        resolved = path.resolve(strict=True)
        if not resolved.is_file():
            return missing("not a regular file")
        return {"available": True, "name": path.name, "resolved_name": resolved.name,
                "sha256": digest(resolved), "gnu_build_id": build_id(resolved)}
    except OSError:
        return missing("file unavailable")


def compiler(build):
    cache = build / "CMakeCache.txt"
    if not cache.is_file() or cache.stat().st_size > 1048576:
        return missing("configured CMake cache unavailable or oversized")
    text = cache.read_text(errors="replace")
    driver = re.search(r"^CMAKE_CXX_COMPILER:[^=]+=([^\n]+)$", text, re.M)
    if not driver:
        return missing("configured compiler missing")
    path = Path(driver[1])
    result = {"configured_name": path.name, "driver": file_identity(path)}
    records = []
    for record in sorted((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))[:32]:
        if record.stat().st_size > 131072:
            continue
        data = record.read_text(errors="replace")
        values = [re.search(r'set\(' + key + r' "([^"\n]*)"\)', data)
                  for key in ("CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_ID", "CMAKE_CXX_COMPILER_VERSION")]
        if all(values) and Path(values[0][1]) == path:
            records.append((values[1][1], values[2][1]))
    unique = sorted(set(records))
    result["configured_id_version"] = dict(zip(("id", "version"), unique[0])) if len(unique) == 1 else missing("no unique compiler record")
    kind = re.search(r"^CMAKE_BUILD_TYPE:[^=]+=([^\n]*)$", text, re.M)
    result["build_type"] = kind[1] if kind else None
    result["configured_flags"] = {m[1]: portable(m[2]) for m in re.finditer(
        r"^(CMAKE_CXX_FLAGS(?:_(?:DEBUG|RELEASE|RELWITHDEBINFO|MINSIZEREL))?):[^=]+=([^\n]*)$", text, re.M)}
    result["configured_options"] = {}
    for key in ("APSIS_DRIFT_RTAUDIO", "APSIS_DRIFT_TERMINAL", "APSIS_DRIFT_GODOT_LIVE"):
        match = re.search(r"^" + key + r":BOOL=([^\n]*)$", text, re.M)
        value = match[1].upper() if match else None
        result["configured_options"][key] = (value in ("ON", "TRUE", "YES", "1") if value in
            ("ON", "TRUE", "YES", "1", "OFF", "FALSE", "NO", "0") else missing("configured boolean unavailable or invalid"))
    result["flags_scope"] = "Configured CMake cache values, not every emitted TU option"
    if not path.is_file():
        result["version_output"] = missing("configured driver unavailable")
    else:
        code, text = command([str(path), "--version"])
        result["version_output"] = portable(text[:4096]) if code == 0 else missing("compiler version command failed")
    return result


def libraries(executable):
    result = {name: missing("not listed by dynamic loader; may be static or not linked")
              for name in ("libc", "libm")}
    tool = shutil.which("ldd")
    if not tool or not executable.is_file():
        return {name: missing("ELF or ldd unavailable") for name in result}
    code, text = command([tool, str(executable)])
    if code != 0:
        return {name: missing("loader tracing unavailable; static/non-dynamic ELF possible") for name in result}
    for name in result:
        matches = re.findall(r"^\s*(" + name + r"\.so\.\d+)\s+=>\s+(\S+)", text, re.M)
        if len(matches) == 1:
            soname, location = matches[0]
            result[name] = file_identity(Path(location)) if location.startswith("/") else missing("dependency unresolved")
            result[name]["soname"] = soname
    return result


def collect(build, outcome, job_label):
    executable = build / "apsis-drift-vacuum-dynamics-tests"
    log = build / "Testing/Temporary/LastTest.log"
    code, commit = command(["git", "rev-parse", "HEAD"])
    run = {key: os.environ.get(key) for key in ("GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT")}
    run = {key: value if value and value.isdecimal() and len(value) <= 24 else None for key, value in run.items()}
    return {"schema": "apsis.ctest-math-provenance/1", "source_commit": commit.strip() if code == 0 and re.fullmatch(r"[0-9a-f]{40,64}\n?", commit) else None,
            "run": run, "job_label": job_label, "ctest": {"outcome": outcome, "outcome_source": "caller-supplied CTest step result", "representative_contract": "vacuum-dynamics-contract",
                                    "last_test_log_sha256": digest(log) if log.is_file() else missing("CTest log unavailable")},
            "compiler": compiler(build), "executable": file_identity(executable),
            "libraries": libraries(executable),
            "scope": "Dynamic-loader dependency resolution only; not observed math calls, IFUNC selection or cross-environment numerical compatibility."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    outcomes = ("success", "failure", "cancelled", "skipped", "unknown")
    parser.add_argument("--ctest-outcome", choices=outcomes, default=os.environ.get("CTEST_STEP_OUTCOME") or "unknown")
    parser.add_argument("--job-label", default=os.environ.get("CTEST_JOB_LABEL") or "unknown")
    args = parser.parse_args()
    if args.ctest_outcome not in outcomes:
        parser.error("invalid CTEST_STEP_OUTCOME")
    if len(args.job_label.encode()) > 160 or any(ord(c) < 32 or ord(c) == 127 for c in args.job_label):
        parser.error("job label must be <=160 UTF-8 bytes with no control characters")
    text = json.dumps(collect(args.build_dir.resolve(), args.ctest_outcome, args.job_label), indent=2) + "\n"
    if len(text.encode()) > 65536:
        parser.error("receipt exceeds 64 KiB")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)


if __name__ == "__main__":
    main()
