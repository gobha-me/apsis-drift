#!/usr/bin/env python3
"""Copy selected Hero presentation resources unchanged for local film integration."""

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile

ENTRIES = (
    "actors/pixel_pilot.tscn",
    "trials/walk-04/pilot_presentation.gd",
    "trials/encounter-01/actor_view.tscn",
    "trials/casual-male-01/pilot.gd",
    "trials/flight-pair-01/pilot.gd",
    "trials/casual-actions-01/pilot.gd",
)
CHECKPOINT = "d932d4939d87a9d6551bcfc407141eafca5aba26"
RESOURCE = re.compile(r'res://([^\s"\'\)\],]+)')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def publish_new(staging, output):
    # Match the native package installer: even a concurrently created empty
    # destination must never be replaced by this local resource staging tool.
    rename = getattr(ctypes.CDLL(None, use_errno=True), "renameat2", None)
    if rename is None:
        raise ValueError("No-replace resource publication requires Linux renameat2")
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(staging), -100, os.fsencode(output), 1) != 0:
        raise OSError(ctypes.get_errno(), "No-replace resource publication refused", str(output))


def source_file(root, name):
    relative = PurePosixPath(name)
    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
        raise ValueError(f"Unsafe resource path: {name}")
    candidate = root.joinpath(*relative.parts)
    if candidate.is_symlink() or not candidate.is_file():
        raise ValueError(f"Missing or linked resource: {name}")
    if not candidate.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"Resource escapes source root: {name}")
    return candidate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hero-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="New resource directory; never replaces a Godot project")
    parser.add_argument("--expected-checkpoint", default=CHECKPOINT,
                        help="Explicit stable Hero checkpoint to verify against")
    args = parser.parse_args()
    root = args.hero_root.resolve()
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("Output already exists; source resources must not be overwritten")
    pending, files = list(ENTRIES), set()
    while pending:
        name = pending.pop()
        if name in files:
            continue
        path = source_file(root, name)
        files.add(name)
        if path.suffix in (".gd", ".tscn", ".tres", ".json"):
            pending.extend(RESOURCE.findall(path.read_text()))
    runtime = sorted(files)
    # Provenance text is archived without following image-generation references.
    metadata = {name for name in ("README.md", "provenance.json") if (root / name).is_file()}
    for directory in {str(PurePosixPath(name).parent) for name in runtime}:
        for basename in ("README.md", "request.json", "requests.json", "receipt.json", "measurements.json"):
            name = basename if directory == "." else f"{directory}/{basename}"
            if root.joinpath(name).is_file():
                metadata.add(name)
    worktree_checkpoint = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    checkpoint = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "--verify", args.expected_checkpoint + "^{commit}"], text=True).strip()
    repository = Path(subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "--show-toplevel"], text=True).strip())
    source_bytes = {}
    for name in sorted(files | metadata):
        source = source_file(root, name)
        content = source.read_bytes()
        committed = subprocess.check_output(
            ["git", "-C", str(repository), "show", f"{checkpoint}:{source.relative_to(repository).as_posix()}"])
        if name in files and content != committed:
            raise ValueError(f"Source differs from selected checkpoint: {name}")
        # Other sessions may update their handoff prose while we work. Archive
        # that prose from the selected checkpoint instead of mislabelling it.
        source_bytes[name] = content if name in files else committed
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".hero-resources-", dir=args.output.parent))
    try:
        roster = []
        for name in sorted(files | metadata):
            destination = temporary / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(source_bytes[name])
            roster.append({"path": name, "sha256": digest(destination),
                           "bytes": destination.stat().st_size,
                           "role": "runtime" if name in files else "source_metadata"})
        receipt = {"schema": "apsis-reel-character-resources-v1",
                   "source_checkpoint": checkpoint, "entry_resources": list(ENTRIES),
                   "source_worktree_checkpoint_at_copy": worktree_checkpoint,
                   "source_policy": "Every runtime and archived metadata byte matches the selected Git checkpoint",
                   "path_policy": "Original res:// paths; unchanged source bytes; no project.godot or import caches",
                   "purpose": "Owner-requested local scripted Godot film integration",
                   "art_rights": "Inherited references and derived output redistribution uncleared; isolated local study",
                   "files": roster}
        (temporary / "character-resources.json").write_text(json.dumps(receipt, indent=2) + "\n")
        # Recheck before publication; output is an ignored local resource bundle.
        if args.output.exists() or args.output.is_symlink():
            raise ValueError("Output appeared while copying; refusing replacement")
        publish_new(temporary, args.output)
        print(json.dumps({"output": str(args.output), "runtime_files": len(runtime),
                          "textures": sum(name.endswith(".png") for name in runtime),
                          "source_checkpoint": checkpoint}))
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


if __name__ == "__main__":
    main()
