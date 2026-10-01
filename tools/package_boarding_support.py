#!/usr/bin/env python3
"""Archive exact source evidence and normalize selected boarding candidate geometry."""
import argparse
from pathlib import Path
import shutil
import tempfile

import prepare_native_assets as base
import boarding_support_spec as spec
import prepare_boarding_support as assets
from boarding_support_identity import IDENTITIES, RUNTIME_SHA256, SOURCE_SHA256


def build(source, destination, repository=None):
    source, destination = map(base.no_symlinks, (source, destination))
    base.require(not destination.exists(), "boarding package destination exists")
    files = {}
    for name, selected in SOURCE_SHA256.items():
        raw = assets.read_regular(source / base.relative(name), spec.MAX_SOURCE)
        base.require(assets.digest(raw) == selected, "selected source changed: " + name)
        files["sources/" + name] = raw
    runtime = assets.check_sources(files, repository)
    files[assets.RUNTIME] = spec.encode(runtime)
    base.require(assets.digest(files[assets.RUNTIME]) == RUNTIME_SHA256, "selected normalization changed")
    files[assets.PROVENANCE] = spec.encode({"schema": "apsis.boarding-support-provenance/1",
                                         "identities": IDENTITIES, "scope": assets.SCOPE, "limits": assets.LIMITS,
                                         "source_artifacts_sha256": SOURCE_SHA256, "tool_sha256": assets.tool_hashes()})
    manifest = {"schema": assets.SCHEMA, "package_id": assets.PACKAGE_ID,
                "files": [{"path": name, "bytes": len(raw), "sha256": assets.digest(raw)}
                          for name, raw in sorted(files.items())],
                "runtime": assets.RUNTIME, "provenance": assets.PROVENANCE}
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".boarding-package-", dir=destination.parent))
    try:
        for name, raw in files.items():
            path = staging / base.relative(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(raw)
        (staging / "package.json").write_bytes(spec.encode(manifest))
        assets.verify(staging, repository)
        assets.install_new(staging, destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return assets.digest(spec.encode(manifest))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Directory containing wayfarer-boarding-support-01/ and tools/")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repository", type=Path, help="Existing operating/motion dependency repository")
    args = parser.parse_args()
    try:
        package_sha = build(args.source, args.output, args.repository)
    except (ValueError, OSError, KeyError, TypeError, UnicodeError, RecursionError) as error:
        parser.exit(1, "Boarding support packaging refused: " + str(error) + "\n")
    print("Packaged", assets.PACKAGE_ID, package_sha)


if __name__ == "__main__":
    main()
