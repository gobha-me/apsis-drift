#!/usr/bin/env python3
"""Package independently selected static stowed bytes; BSD-3-Clause.

No source regeneration, external services, geometry owners or runtime admission.
"""
import argparse
import lzma
from pathlib import Path
import shutil
import tempfile

import prepare_wayfarer_stowed as assets


def build(payloads, output):
    """Seal a frozen memory snapshot and install once without replacing any path."""
    output = assets.base.no_symlinks(output)
    assets.base.require(not output.exists(), 'Stowed package destination exists')
    assets.base.require(type(payloads) is dict, 'Stowed input dictionary required')
    snapshot = dict(payloads)
    assets.base.require(set(snapshot) == set(assets.checks.PAYLOAD_PINS) and
                        all(type(raw) is bytes and 0 < len(raw) <= assets.MAX_PAYLOAD
                            for raw in snapshot.values()) and
                        sum(map(len, snapshot.values())) <= assets.MAX_TOTAL,
                        'Exact bounded stowed input snapshot required')
    assets.checks.validate_payloads(snapshot)
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.stowed-package-', dir=output.parent))
    try:
        rows = []
        for role, raw in sorted(snapshot.items()):
            relative = assets.packed_path(role)
            packed = raw if relative == role else lzma.compress(
                raw, format=lzma.FORMAT_XZ, preset=6)
            assets.base.require(len(packed) <= assets.MAX_PAYLOAD,
                                'Compressed stowed input exceeds bound')
            path = staging / assets.base.relative(relative)
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open('xb') as stream:
                stream.write(packed)
            rows.append({'role': role, 'path': relative,
                         'bytes': len(packed), 'sha256': assets.digest(packed),
                         'decoded_bytes': len(raw), 'decoded_sha256': assets.digest(raw)})
        manifest = {'schema': assets.SCHEMA, 'package_id': assets.PACKAGE_ID,
                    'files': rows}
        with (staging / 'package.json').open('xb') as stream:
            stream.write(assets.encode(manifest))
        verified = assets.verify(staging)
        assets.base.require(verified['payloads'] == snapshot,
                            'Staged package differs from frozen input snapshot')
        assets.io.install_new(staging, output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return {'package_id': assets.PACKAGE_ID,
            'manifest_sha256': verified['manifest_sha256'], 'files': len(rows)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-dir', type=Path, required=True,
                        help='Closed directory of the exact decoded delivery roles')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        source = assets.base.no_symlinks(args.input_dir)
        assets.io.closed_roster(source, assets.checks.PAYLOAD_PINS)
        payloads = {role: assets.io.read_regular(source / assets.base.relative(role),
                                               assets.MAX_PAYLOAD)
                    for role in assets.checks.PAYLOAD_PINS}
        result = build(payloads, args.output)
    except (ValueError, OSError, KeyError, TypeError, UnicodeError, RecursionError) as error:
        parser.exit(1, 'Stowed asset packaging refused: ' + str(error) + '\n')
    print(assets.encode(result).decode(), end='')


if __name__ == '__main__':
    main()
