#!/usr/bin/env python3
"""Verify and atomically prepare the selected, mesh-free operating-motion sidecar."""
import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import shutil
import stat
import tempfile

import prepare_native_assets as base
import operating_motion_spec as spec
from operating_motion_identity import IDENTITIES, RECIPE_SHA256, SOURCE_SHA256

PACKAGE_ID = "operating-motion-01"
SCHEMA = "apsis.operating-motion-assets/1"
RECIPE = "operating-motion-01.json"
PROVENANCE = "provenance.json"
SOURCE_FILES = {"sources/" + name: digest for name, digest in SOURCE_SHA256.items()}
TOOL_NAMES = ("operating_motion_identity.py", "operating_motion_spec.py",
              "package_operating_motion.py", "prepare_operating_motion.py", "prepare_native_assets.py")
SCOPE = "Source-local craft/D1 transform evaluation; no geometry, actor, clock, pressure, save or collision admission"
LIMITS = ["Source matrices are sampled transform evidence, not continuous clearance.",
          "Existing operating-02 corrections are already baked; do not apply twice.",
          "Craft outputs are full world-rest deltas; D1 presentation outputs are node-local.",
          "Canonical D1 contact conversion includes the station offset.",
          "No meshes, supported boarding route, interlocks or occupied seat are admitted."]


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def open_directory(path):
    """Hold a directory descriptor reached without any symlink component."""
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
    """Open each path component without symlink traversal, then bound regular bytes."""
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


def tool_hashes():
    tools = Path(__file__).resolve().parent
    return {name: digest(read_regular(tools / name, spec.MAX_SOURCE)) for name in TOOL_NAMES}


def install_new(staging, output):
    """Linux no-replace rename refuses even a concurrently created empty output."""
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


def check_sources(raw_files, repository=None):
    documents = {name: spec.decode(raw_files["sources/" + name], spec.MAX_SOURCE)
                 for name in SOURCE_SHA256 if name.endswith(".json")}
    craft = documents["wayfarer-motion-proposal/recipe.json"]
    station = documents["operating-motion-fixtures-01/d1-recipe-source-poses.json"]
    recipe = spec.construct_recipe(craft, station)
    base.require(digest(spec.encode(recipe)) == RECIPE_SHA256, "source scalar extraction changed")
    base.require(len(documents["wayfarer-motion-proposal/source-poses.json"]["source_poses"]) == 71 and
                 len(documents["operating-motion-fixtures-01/craft-combined.json"]["poses"]) == 8 and
                 len(station["source_poses"]) == 41, "independent source pose roster changed")
    repo = base.no_symlinks(repository if repository is not None else Path(__file__).resolve().parent.parent)
    base.require(repo.is_dir(), "operating dependency repository is missing")
    for name in ("wayfarer-motion-proposal/recipe.json", "operating-motion-fixtures-01/provenance.json"):
        for relative, expected in documents[name]["engine_bindings_sha256"].items():
            path = base.relative(relative)
            base.require(digest(read_regular(repo / path, 64 * 1024 * 1024)) == expected,
                         "existing operating dependency changed: " + relative)
    return recipe


def verify(package, repository=None):
    root = base.no_symlinks(package)
    base.require(root.is_dir(), "motion package is missing")
    manifest_raw = read_regular(root / "package.json", 128 * 1024)
    manifest = spec.decode(manifest_raw, 128 * 1024)
    base.keys(manifest, ("schema", "package_id", "files", "runtime", "provenance"))
    base.require(manifest["schema"] == SCHEMA and manifest["package_id"] == PACKAGE_ID and
                 manifest["runtime"] == RECIPE and manifest["provenance"] == PROVENANCE,
                 "unsupported motion package")
    expected_names = set(SOURCE_FILES) | {RECIPE, PROVENANCE}
    records = manifest["files"]
    base.require(type(records) is list and len(records) == len(expected_names), "package roster count changed")
    raw_files, total = {}, 0
    for record in records:
        base.keys(record, ("path", "bytes", "sha256"))
        name = record["path"]
        relative = base.relative(name)
        base.require(name in expected_names and name not in raw_files, "unknown/duplicate package path")
        base.size(record["bytes"], spec.MAX_SOURCE)
        base.digest(record["sha256"])
        raw = read_regular(root / relative, spec.MAX_RECIPE if name == RECIPE else spec.MAX_SOURCE)
        base.require(len(raw) == record["bytes"] and digest(raw) == record["sha256"], "package file mismatch")
        selected = SOURCE_FILES.get(name, RECIPE_SHA256 if name == RECIPE else None)
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
        base.require(entries <= len(expected_names) + len(expected_directories) + 1,
                     "package entry roster exceeds bound")
        base.require(not entry.is_symlink() and (entry.is_file() or entry.is_dir()), "nonregular package entry")
        if entry.is_file():
            actual_names.add(entry.relative_to(root).as_posix())
        else:
            actual_directories.add(entry.relative_to(root).as_posix())
    base.require(actual_names == expected_names | {"package.json"}, "unrostered/missing file")
    base.require(actual_directories == expected_directories, "unrostered/missing package directory")
    # Cached verified bytes are used for admission AND later staging, closing a
    # separate hash/read/copy race without reopening authoring files.
    recipe = spec.validate_recipe(spec.decode(raw_files[RECIPE], spec.MAX_RECIPE))
    base.require(recipe == check_sources(raw_files, repository), "runtime/source scalar contract differs")
    provenance = spec.decode(raw_files[PROVENANCE], spec.MAX_RECIPE)
    base.keys(provenance, ("schema", "identities", "scope", "limits", "source_artifacts_sha256", "tool_sha256"))
    base.require(provenance["schema"] == "apsis.operating-motion-provenance/1" and
                 provenance["identities"] == IDENTITIES and provenance["scope"] == SCOPE and
                 provenance["limits"] == LIMITS and provenance["source_artifacts_sha256"] == SOURCE_SHA256 and
                 provenance["tool_sha256"] == tool_hashes(), "motion provenance/tool identity mismatch")
    raw_files["package.json"] = manifest_raw
    return raw_files, digest(manifest_raw)


def prepare(package, output, repository=None):
    output = base.no_symlinks(output)
    base.require(not output.exists(), "prepared output already exists")
    raw_files, package_sha = verify(package, repository)
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".motion-prepare-", dir=output.parent))
    try:
        for name, raw in raw_files.items():
            path = staging / base.relative(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        receipt = {"schema": "apsis.prepared-operating-motion/1", "package_id": PACKAGE_ID,
                   "package_sha256": package_sha, "runtime": RECIPE, "recipe_sha256": RECIPE_SHA256,
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
    parser.add_argument("--repository", type=Path,
                        help="Source dependency repository when this tool is copied into an isolated runner")
    args = parser.parse_args()
    try:
        receipt = prepare(args.package, args.output, args.repository)
    except (ValueError, OSError, KeyError, TypeError, UnicodeError, RecursionError) as error:
        parser.exit(1, "Motion preparation refused: " + str(error) + "\n")
    print("Prepared", receipt["package_id"], receipt["recipe_sha256"])


if __name__ == "__main__":
    main()
