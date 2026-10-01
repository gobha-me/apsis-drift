#!/usr/bin/env python3
"""Archive exact delivered motion evidence and publish a separate mesh-free recipe."""
import argparse
from pathlib import Path
import shutil
import tempfile

import prepare_native_assets as base
import operating_motion_spec as spec
import prepare_operating_motion as assets
from operating_motion_identity import IDENTITIES, RECIPE_SHA256, SOURCE_SHA256


def build(source, destination):
    source, destination = map(base.no_symlinks, (source, destination))
    base.require(not destination.exists(), "motion package destination exists")
    files = {}
    for name, selected in SOURCE_SHA256.items():
        raw = assets.read_regular(source / base.relative(name), spec.MAX_SOURCE)
        base.require(assets.digest(raw) == selected, "selected source changed: " + name)
        files["sources/" + name] = raw
    recipe = assets.check_sources(files)
    files[assets.RECIPE] = spec.encode(recipe)
    base.require(assets.digest(files[assets.RECIPE]) == RECIPE_SHA256, "selected recipe digest changed")
    files[assets.PROVENANCE] = spec.encode({"schema": "apsis.operating-motion-provenance/1",
                                         "identities": IDENTITIES, "scope": assets.SCOPE,
                                         "limits": assets.LIMITS, "source_artifacts_sha256": SOURCE_SHA256,
                                         "tool_sha256": assets.tool_hashes()})
    manifest = {"schema": assets.SCHEMA, "package_id": assets.PACKAGE_ID,
                "files": [{"path": name, "bytes": len(raw), "sha256": assets.digest(raw)}
                          for name, raw in sorted(files.items())],
                "runtime": assets.RECIPE, "provenance": assets.PROVENANCE}
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".motion-package-", dir=destination.parent))
    try:
        for name, raw in files.items():
            path = staging / base.relative(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        (staging / "package.json").write_bytes(spec.encode(manifest))
        assets.verify(staging)
        assets.install_new(staging, destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return assets.digest(spec.encode(manifest))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Directory containing the two delivered source packages")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        digest = build(args.source, args.output)
    except (ValueError, OSError, KeyError, TypeError, UnicodeError, RecursionError) as error:
        parser.exit(1, "Motion packaging refused: " + str(error) + "\n")
    print("Packaged", assets.PACKAGE_ID, digest)


if __name__ == "__main__":
    main()
