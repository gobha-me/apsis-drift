#!/usr/bin/env python3
"""Verify and unpack the one source-bound Freedom starter asset delivery.

No provider access, asset edits, editor import or game launch. Model bytes are
verified before an atomic directory install into explicitly selected output.
"""
import argparse
import hashlib
import json
import lzma
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import tempfile

SCHEMA = "apsis.native-starter-assets/1"
MAX_MANIFEST = 1024 * 1024
MAX_FILES = 64
MAX_CHUNK = 32 * 1024 * 1024
MAX_PAYLOAD = 512 * 1024 * 1024
MAX_MODEL = 600 * 1024 * 1024
MAX_OUTPUT = 700 * 1024 * 1024
BLOCK = 1024 * 1024
MODELS = {
    "station": ("station-reference.glb", "LicenseRef-Apsis-Station-Kit-Output"),
    "wayfarer": ("hopper-wayfarer-01.glb", "LicenseRef-Apsis-Hopper-Meshy-Output"),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def pairs_unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key: " + key)
        result[key] = value
    return result


def load_json(path):
    with path.open("rb") as stream:
        raw = stream.read(MAX_MANIFEST + 1)
    require(len(raw) <= MAX_MANIFEST, "metadata size exceeds limit")
    def invalid_constant(value):
        raise ValueError("non-finite JSON constant: " + value)
    def finite_float(value):
        result = float(value)
        require(math.isfinite(result), "non-finite JSON number")
        return result
    return json.loads(raw.decode("utf-8"), object_pairs_hook=pairs_unique,
                      parse_constant=invalid_constant, parse_float=finite_float)


def keys(value, expected):
    require(type(value) is dict and set(value) == set(expected), "unexpected metadata fields")


def size(value, limit):
    require(type(value) is int and 0 < value <= limit, "invalid byte count")


def digest(value):
    require(type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value), "invalid SHA-256")


def relative(value):
    require(type(value) is str and 0 < len(value) <= 256, "invalid relative path")
    require("\\" not in value and "\0" not in value, "invalid path separator/NUL")
    path = PurePosixPath(value)
    require(not path.is_absolute() and value == path.as_posix(), "noncanonical path")
    require(all(part not in ("", ".", "..") for part in path.parts), "path traversal")
    require(all(re.fullmatch(r"[A-Za-z0-9_.-]+", part) for part in path.parts), "unsafe path component")
    return Path(*path.parts)


def no_symlinks(path):
    path = Path(os.path.abspath(path))
    for parent in [*reversed(path.parents), path]:
        require(not parent.is_symlink(), "symlink path refused: " + str(parent))
    return path


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify(package):
    root = no_symlinks(package)
    require(root.is_dir(), "starter package directory is missing")
    manifest_path = no_symlinks(root / "package.json")
    manifest = load_json(manifest_path)
    keys(manifest, ["schema", "package_id", "files", "models", "metadata"])
    require(manifest["schema"] == SCHEMA and manifest["package_id"] == "freedom-starter-01", "unsupported starter package")
    files, models, metadata = manifest["files"], manifest["models"], manifest["metadata"]
    require(type(files) is list and 1 <= len(files) <= MAX_FILES, "invalid hash roster count")
    roster = {}
    total = 0
    for record in files:
        keys(record, ["path", "bytes", "sha256"])
        name = record["path"]
        path = relative(name)
        require(name != "package.json" and name not in roster, "duplicate/reserved roster path")
        size(record["bytes"], MAX_CHUNK)
        digest(record["sha256"])
        actual = no_symlinks(root / path)
        require(actual.is_file() and actual.stat().st_size == record["bytes"], "missing/truncated package file: " + name)
        require(sha(actual) == record["sha256"], "package hash mismatch: " + name)
        roster[name] = record
        total += record["bytes"]
    require(total <= MAX_PAYLOAD, "total package bytes exceed limit")
    actual_names = set()
    for entry in root.rglob("*"):
        require(not entry.is_symlink(), "package symlink refused")
        require(entry.is_dir() or entry.is_file(), "nonregular package entry")
        if entry.is_file():
            actual_names.add(entry.relative_to(root).as_posix())
    require(actual_names == set(roster) | {"package.json"}, "package contains unrostered/missing files")
    require(type(models) is list and len(models) == 2, "expected station and Wayfarer models")
    used, ids, decoded = set(), set(), 0
    for model in models:
        keys(model, ["id", "output", "bytes", "sha256", "source_sha256", "license", "chunks", "receipt"])
        ident = model["id"]
        require(type(ident) is str and ident in MODELS and ident not in ids, "unknown/duplicate model")
        ids.add(ident)
        require((model["output"], model["license"]) == MODELS[ident], "unrecognized model output/license")
        size(model["bytes"], MAX_MODEL)
        digest(model["sha256"])
        digest(model["source_sha256"])
        decoded += model["bytes"]
        chunks = model["chunks"]
        require(type(chunks) is list and 1 <= len(chunks) <= 32, "invalid chunk count")
        for name in chunks:
            relative(name)
            require(name in roster and name not in used and name.startswith("payloads/"), "missing/reused payload chunk")
            used.add(name)
        receipt_name = model["receipt"]
        relative(receipt_name)
        require(receipt_name in roster and receipt_name.startswith("metadata/"), "missing validation receipt")
        receipt = load_json(root / receipt_name)
        require(type(receipt) is dict and receipt.get("pass") is True, "model validation did not pass")
        require(receipt.get("glb_sha256", receipt.get("model_sha256")) == model["sha256"], "validation receipt model mismatch")
        if "source_sha256" in receipt:
            require(receipt["source_sha256"] == model["source_sha256"], "validation receipt source mismatch")
    require(decoded <= MAX_OUTPUT, "decoded model budget exceeds limit")
    require(type(metadata) is list and 1 <= len(metadata) <= 16, "invalid runtime metadata count")
    outputs = {model["output"] for model in models}
    for record in metadata:
        keys(record, ["path", "output"])
        relative(record["path"])
        output = relative(record["output"])
        require(len(output.parts) == 1 and record["output"] not in outputs, "duplicate/nonlocal runtime metadata")
        outputs.add(record["output"])
        require(record["path"] in roster and record["path"].startswith("metadata/"), "missing runtime metadata")
        require(roster[record["path"]]["bytes"] <= MAX_MANIFEST, "runtime metadata is oversized")
        if record["path"].endswith(".json"):
            load_json(root / record["path"])
    require(used == {name for name in roster if name.startswith("payloads/")}, "unconsumed payload file")
    require("licenses/LICENSE.md" in roster and "licenses/HOPPER_MESHY_TRIAL.md" in roster and
            "licenses/STATION_KIT_01.md" in roster, "required license records missing")
    return root, manifest, sha(manifest_path), roster


def unpack_model(root, model, output):
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_XZ, memlimit=256 * 1024 * 1024)
    written, hashed = 0, hashlib.sha256()
    with output.open("xb") as target:
        for name in model["chunks"]:
            with (root / relative(name)).open("rb") as source:
                while data := source.read(BLOCK):
                    require(not decoder.eof, "trailing compressed payload")
                    while True:
                        block = decoder.decompress(data, max_length=min(BLOCK, model["bytes"] - written + 1))
                        data = b""
                        written += len(block)
                        require(written <= model["bytes"], "decoded model exceeds declared size")
                        target.write(block)
                        hashed.update(block)
                        if decoder.eof:
                            require(not decoder.unused_data, "trailing compressed payload")
                            break
                        if decoder.needs_input:
                            break
    require(decoder.eof and written == model["bytes"] and hashed.hexdigest() == model["sha256"], "decoded model identity/truncation failure")
    with output.open("rb") as stream:
        header = stream.read(12)
    require(len(header) == 12 and struct.unpack("<4sII", header) == (b"glTF", 2, written), "invalid GLB header/length")


def prepared_matches(output, manifest, package_hash, roster):
    require(load_json(output / "prepared.json") == {"schema": SCHEMA, "package_sha256": package_hash}, "output belongs to a different package")
    wanted = {"prepared.json"}
    for record in manifest["models"]:
        path = no_symlinks(output / record["output"])
        require(path.is_file() and path.stat().st_size == record["bytes"] and sha(path) == record["sha256"], "prepared model changed")
        wanted.add(record["output"])
    for record in manifest["metadata"]:
        path = no_symlinks(output / record["output"])
        require(path.is_file() and sha(path) == roster[record["path"]]["sha256"], "prepared metadata changed")
        wanted.add(record["output"])
    require({p.name for p in output.iterdir()} == wanted, "prepared output contains unowned files")


def prepare(package, destination):
    root, manifest, package_hash, roster = verify(package)
    output = no_symlinks(destination)
    require(output != root and root not in output.parents and output not in root.parents, "output overlaps package")
    if output.exists():
        require(output.is_dir(), "output is not a directory")
        prepared_matches(output, manifest, package_hash, roster)
        return output
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".apsis-assets-", dir=output.parent))
    try:
        for model in manifest["models"]:
            unpack_model(root, model, staging / model["output"])
        for record in manifest["metadata"]:
            shutil.copyfile(root / record["path"], staging / record["output"])
        (staging / "prepared.json").write_text(json.dumps({"schema": SCHEMA, "package_sha256": package_hash}, indent=2) + "\n")
        prepared_matches(staging, manifest, package_hash, roster)
        require(verify(root)[2] == package_hash, "package changed during preparation")
        # No replacement of an existing output, including a concurrently created one.
        require(not output.exists() and not output.is_symlink(), "output appeared during preparation")
        os.rename(staging, output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--verify-only", action="store_true")
    mode.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.verify_only:
            root, _, package_hash, _ = verify(args.package)
            print(json.dumps({"package": str(root), "sha256": package_hash, "verified": True}))
        else:
            print(prepare(args.package, args.output))
    except (OSError, ValueError, EOFError, lzma.LZMAError, RecursionError) as error:
        parser.exit(1, "Native assets refused: " + str(error) + "\n")


if __name__ == "__main__":
    main()
