#!/usr/bin/env python3
"""Verify and atomically prepare immutable boarding attribution/candidate evidence."""
import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import shutil
import stat
import tempfile

import prepare_native_assets as base
import boarding_support_spec as spec
from boarding_support_identity import ENGINE_BINDINGS, IDENTITIES, RUNTIME_SHA256, SOURCE_SHA256

PACKAGE_ID = "boarding-support-01"
SCHEMA = "apsis.boarding-support-assets/1"
RUNTIME = "boarding-support-01.json"
PROVENANCE = "provenance.json"
SOURCE_FILES = {"sources/" + name: digest for name, digest in SOURCE_SHA256.items()}
TOOL_NAMES = ("boarding_support_identity.py", "boarding_support_spec.py", "package_boarding_support.py",
              "prepare_boarding_support.py", "prepare_native_assets.py")
SCOPE = "Immutable source attribution and candidate surface evidence; no actor, support acquisition, route or collision exemption"
LIMITS = ["All source objects retain obstacle policy.",
          "Candidate predicates establish no posture, reach, penetration or neighboring clearance.",
          "Source corrections/matrices are baked provenance; apply only admitted contact motion once.",
          "Hand-grasp candidates have no declared side; rod intervals already exclude 30 mm ends.",
          "Crop bounds describe finite rest coverage, not complete exterior collision geometry.",
          "No new render models, private character art, actor actions, save phases or clock are admitted."]


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def open_directory(path):
    path = Path(os.path.abspath(path))
    directory = os.open("/", os.O_RDONLY | os.O_DIRECTORY)
    try:
        for part in path.parts[1:]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=directory)
            os.close(directory)
            directory = child
        return directory
    except BaseException:
        os.close(directory)
        raise


def read_regular(path, maximum):
    path = Path(os.path.abspath(path))
    directory = open_directory(path.parent)
    try:
        descriptor = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
        with os.fdopen(descriptor, "rb") as stream:
            info = os.fstat(stream.fileno())
            base.require(stat.S_ISREG(info.st_mode) and 0 < info.st_size <= maximum,
                         "nonregular/oversized input: " + str(path))
            raw = stream.read(maximum + 1)
            base.require(0 < len(raw) <= maximum and len(raw) == info.st_size,
                         "changed/truncated input: " + str(path))
            return raw
    finally:
        os.close(directory)


def install_new(staging, output):
    output = base.no_symlinks(output)
    parent = open_directory(output.parent)
    try:
        rename = getattr(ctypes.CDLL(None, use_errno=True), "renameat2", None)
        base.require(rename is not None, "atomic no-replace install requires Linux renameat2")
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        base.require(Path(staging).parent == output.parent, "staging must share destination parent")
        if rename(parent, os.fsencode(Path(staging).name), parent, os.fsencode(output.name), 1) != 0:
            raise OSError(ctypes.get_errno(), "atomic no-replace installation refused", str(output))
    finally:
        os.close(parent)


def tool_hashes():
    tools = Path(__file__).resolve().parent
    return {name: digest(read_regular(tools / name, spec.MAX_SOURCE)) for name in TOOL_NAMES}


def check_sources(raw_files, repository=None):
    # Submitted provenance never defines its own approved binding policy.
    prefix = "sources/wayfarer-boarding-support-01/"
    provenance = spec.decode(raw_files[prefix + "provenance.json"], spec.MAX_SOURCE)
    base.keys(provenance, ("schema_version", "engine_bindings_sha256", "package_sha256", "extractor_sha256",
                           "artifacts_sha256", "scope", "checker_sha256"))
    spec.integer(provenance["schema_version"], 1, 1)
    base.require(provenance["engine_bindings_sha256"] == ENGINE_BINDINGS and
                 provenance["package_sha256"] == IDENTITIES["operating_package_sha256"] and
                 provenance["extractor_sha256"] == SOURCE_SHA256["tools/extract_boarding_support.py"] and
                 provenance["checker_sha256"] == SOURCE_SHA256["tools/check_boarding_support.py"],
                 "source producer/dependency identities changed")
    expected_artifacts = {name.removeprefix("wayfarer-boarding-support-01/"): value
                          for name, value in SOURCE_SHA256.items()
                          if name.startswith("wayfarer-boarding-support-01/") and not name.endswith("provenance.json")}
    base.require(provenance["artifacts_sha256"] == expected_artifacts, "source artifact roster changed")
    repo = base.no_symlinks(repository if repository is not None else Path(__file__).resolve().parent.parent)
    base.require(repo.is_dir(), "boarding dependency repository is missing")
    contact = None
    for name, selected in ENGINE_BINDINGS.items():
        raw = read_regular(repo / base.relative(name), 64 * 1024 * 1024)
        base.require(digest(raw) == selected, "existing operating dependency changed: " + name)
        if name == "assets/native/wayfarer-operating-02/metadata/contact.json":
            contact = spec.decode(raw, spec.MAX_CONTACT)
    motion = read_regular(repo / "assets/native/operating-motion-01/operating-motion-01.json", 128 * 1024)
    base.require(digest(motion) == IDENTITIES["operating_motion_sha256"], "selected motion changed")
    support = spec.decode(raw_files[prefix + "support.json"], spec.MAX_SOURCE)
    return spec.construct_runtime(support, contact)


def verify(package, repository=None):
    root = base.no_symlinks(package)
    base.require(root.is_dir(), "boarding support package is missing")
    manifest_raw = read_regular(root / "package.json", 128 * 1024)
    manifest = spec.decode(manifest_raw, 128 * 1024)
    base.keys(manifest, ("schema", "package_id", "files", "runtime", "provenance"))
    base.require(manifest["schema"] == SCHEMA and manifest["package_id"] == PACKAGE_ID and
                 manifest["runtime"] == RUNTIME and manifest["provenance"] == PROVENANCE,
                 "unsupported boarding support package")
    expected_names = set(SOURCE_FILES) | {RUNTIME, PROVENANCE}
    records = manifest["files"]
    spec.array(records, len(expected_names), len(expected_names))
    raw_files, total = {}, 0
    for record in records:
        base.keys(record, ("path", "bytes", "sha256"))
        name = record["path"]
        relative = base.relative(name)
        base.require(name in expected_names and name not in raw_files, "unknown/duplicate package path")
        base.size(record["bytes"], spec.MAX_SOURCE)
        base.digest(record["sha256"])
        raw = read_regular(root / relative, spec.MAX_RUNTIME if name == RUNTIME else spec.MAX_SOURCE)
        base.require(len(raw) == record["bytes"] and digest(raw) == record["sha256"], "package file mismatch")
        selected = SOURCE_FILES.get(name, RUNTIME_SHA256 if name == RUNTIME else None)
        base.require(selected is None or digest(raw) == selected, "repository-selected bytes changed: " + name)
        total += len(raw)
        base.require(total <= spec.MAX_TOTAL, "total package bytes exceed bound")
        raw_files[name] = raw
    actual_names, actual_directories = set(), set()
    expected_directories = {parent.as_posix() for name in expected_names
                            for parent in base.relative(name).parents if parent != Path(".")}
    entries = 0
    for entry in root.rglob("*"):
        entries += 1
        base.require(entries <= len(expected_names) + len(expected_directories) + 1, "entry roster exceeds bound")
        base.require(not entry.is_symlink() and (entry.is_file() or entry.is_dir()), "nonregular package entry")
        if entry.is_file():
            actual_names.add(entry.relative_to(root).as_posix())
        else:
            actual_directories.add(entry.relative_to(root).as_posix())
    base.require(actual_names == expected_names | {"package.json"}, "unrostered/missing file")
    base.require(actual_directories == expected_directories, "unrostered/missing directory")
    runtime = spec.validate_runtime(spec.decode(raw_files[RUNTIME], spec.MAX_RUNTIME))
    base.require(runtime == check_sources(raw_files, repository), "runtime/source geometry contract differs")
    provenance = spec.decode(raw_files[PROVENANCE], 128 * 1024)
    base.keys(provenance, ("schema", "identities", "scope", "limits", "source_artifacts_sha256", "tool_sha256"))
    base.require(provenance["schema"] == "apsis.boarding-support-provenance/1" and
                 provenance["identities"] == IDENTITIES and provenance["scope"] == SCOPE and
                 provenance["limits"] == LIMITS and provenance["source_artifacts_sha256"] == SOURCE_SHA256 and
                 provenance["tool_sha256"] == tool_hashes(), "boarding provenance/tool identity mismatch")
    raw_files["package.json"] = manifest_raw
    return raw_files, digest(manifest_raw)


def prepare(package, output, repository=None):
    output = base.no_symlinks(output)
    base.require(not output.exists(), "prepared output already exists")
    raw_files, package_sha = verify(package, repository)
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".boarding-prepare-", dir=output.parent))
    try:
        for name, raw in raw_files.items():
            path = staging / base.relative(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        receipt = {"schema": "apsis.prepared-boarding-support/1", "package_id": PACKAGE_ID,
                   "package_sha256": package_sha, "runtime": RUNTIME, "runtime_sha256": RUNTIME_SHA256,
                   "identities": IDENTITIES, "scope": SCOPE}
        (staging / "prepared.json").write_bytes(spec.encode(receipt))
        install_new(staging, output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repository", type=Path, help="Dependency repository when copied into an isolated runner")
    args = parser.parse_args()
    try:
        receipt = prepare(args.package, args.output, args.repository)
    except (ValueError, OSError, KeyError, TypeError, UnicodeError, RecursionError) as error:
        parser.exit(1, "Boarding support preparation refused: " + str(error) + "\n")
    print("Prepared", receipt["package_id"], receipt["runtime_sha256"])


if __name__ == "__main__":
    main()
