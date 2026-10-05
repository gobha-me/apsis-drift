#!/usr/bin/env python3
"""Verify and atomically prepare the frozen static stowed assembly; BSD-3-Clause.

Offline asset bytes only. No world, actor, save, authoring tool or geometry owner.
"""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import shutil
import tempfile

import prepare_native_assets as base
import prepare_lower_cockpit_contact as io
import wayfarer_stowed_package_checks as checks

SCHEMA = 'apsis.wayfarer-stowed-assets/1'
PACKAGE_ID = 'wayfarer-stowed-01'
MAX_MANIFEST = 64 * 1024
MAX_PAYLOAD = 16 * 1024 * 1024
MAX_TOTAL = 32 * 1024 * 1024
MAX_DECODER_MEMORY = 64 * 1024 * 1024
GENERATED = ('frame.json', 'replacement-contact.json', 'preservation.json')


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def encode(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'),
                      allow_nan=False).encode() + b'\n'


def closed_json(raw):
    def nonfinite(value):
        raise ValueError('Nonfinite JSON constant: ' + value)
    try:
        return json.loads(raw, object_pairs_hook=base.pairs_unique,
                          parse_constant=nonfinite)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ValueError('Malformed package JSON') from error


def packed_path(role):
    base.relative(role)
    if role.startswith('licenses/') or role == 'qualification.json':
        return role
    return 'payloads/' + role + '.xz'


def unpack(raw, size):
    """One bounded XZ stream; concatenation and trailing bytes refuse."""
    base.require(type(size) is int and 0 < size <= MAX_PAYLOAD,
                 'Decoded payload byte bound')
    base.require(type(raw) is bytes and 0 < len(raw) <= MAX_PAYLOAD,
                 'Compressed payload byte bound')
    try:
        decoder = lzma.LZMADecompressor(format=lzma.FORMAT_XZ,
                                      memlimit=MAX_DECODER_MEMORY)
        result = decoder.decompress(raw, max_length=size + 1)
    except (lzma.LZMAError, EOFError) as error:
        raise ValueError('Invalid or excessive XZ payload') from error
    base.require(len(result) == size and decoder.eof and not decoder.unused_data,
                 'Truncated, excessive or trailing XZ payload')
    return result


def verify(package):
    """Authenticate the closed container and independently selected decoded bytes."""
    root = base.no_symlinks(package)
    base.require(root.is_dir(), 'Stowed package directory is missing')
    manifest_raw = io.read_regular(root / 'package.json', MAX_MANIFEST)
    manifest = closed_json(manifest_raw)
    base.keys(manifest, ('schema', 'package_id', 'files'))
    base.require(manifest['schema'] == SCHEMA and
                 manifest['package_id'] == PACKAGE_ID, 'Stowed package schema/id')
    rows = manifest['files']
    base.require(type(rows) is list and len(rows) == len(checks.PAYLOAD_PINS),
                 'Exact stowed payload roster')
    expected_paths = {packed_path(role) for role in checks.PAYLOAD_PINS}
    io.closed_roster(root, expected_paths, manifest=True)
    payloads = {}
    packed_total, decoded_total = len(manifest_raw), 0
    for row in rows:
        base.keys(row, ('role', 'path', 'bytes', 'sha256',
                        'decoded_bytes', 'decoded_sha256'))
        role = row['role']
        base.require(type(role) is str and role in checks.PAYLOAD_PINS and
                     role not in payloads and row['path'] == packed_path(role),
                     'Duplicate, foreign or redirected stowed payload')
        selected = checks.PAYLOAD_PINS[role]
        expected_size, expected_sha = selected['bytes'], selected['sha256']
        base.require(type(row['decoded_bytes']) is int and
                     row['decoded_bytes'] == expected_size and
                     row['decoded_sha256'] == expected_sha,
                     'Independently selected decoded identity changed: ' + role)
        base.size(row['bytes'], MAX_PAYLOAD)
        base.digest(row['sha256'])
        packed_total += row['bytes']
        decoded_total += expected_size
        base.require(packed_total <= MAX_TOTAL and decoded_total <= MAX_TOTAL,
                     'Stowed aggregate byte bound')
        raw = io.read_regular(root / base.relative(row['path']), MAX_PAYLOAD)
        base.require(len(raw) == row['bytes'] and digest(raw) == row['sha256'],
                     'Compressed payload identity changed: ' + role)
        decoded = raw if row['path'] == role else unpack(raw, expected_size)
        base.require(digest(decoded) == expected_sha,
                     'Selected payload identity changed: ' + role)
        payloads[role] = decoded
    base.require(set(payloads) == set(checks.PAYLOAD_PINS), 'Missing stowed payload')
    validation = checks.validate_payloads(payloads)
    base.require(io.read_regular(root / 'package.json', MAX_MANIFEST) == manifest_raw,
                 'Manifest changed during verification')
    io.closed_roster(root, expected_paths, manifest=True)
    return {'manifest': manifest, 'manifest_sha256': digest(manifest_raw),
            'payloads': payloads, 'validation': validation}


def prepared_files(verified):
    result = dict(verified['payloads'])
    validation = verified['validation']
    result['frame.json'] = validation['frame']
    result['replacement-contact.json'] = validation['runtime_contact']
    result['preservation.json'] = encode(validation['preservation'])
    base.require(all(type(raw) is bytes and 0 < len(raw) <= MAX_PAYLOAD
                     for raw in result.values()), 'Prepared payload dimensions')
    base.require(sum(map(len, result.values())) <= MAX_TOTAL,
                 'Prepared aggregate byte bound')
    receipt = {'schema': 'apsis.wayfarer-stowed-preparation/1',
               'package_id': PACKAGE_ID,
               'package_manifest_sha256': verified['manifest_sha256'],
               'files': [{'path': name, 'bytes': len(raw), 'sha256': digest(raw)}
                         for name, raw in sorted(result.items())]}
    result['preparation.json'] = encode(receipt)
    base.require(len(result['preparation.json']) <= MAX_MANIFEST and
                 sum(map(len, result.values())) <= MAX_TOTAL,
                 'Complete prepared aggregate byte bound')
    return result


def require_prepared(root, files):
    io.closed_roster(root, files)
    for name, raw in files.items():
        base.require(io.read_regular(root / base.relative(name), MAX_PAYLOAD) == raw,
                     'Prepared stowed bytes changed: ' + name)


def prepare(package, output):
    """Refusal leaves an existing output intact; concurrent creation cannot win."""
    output = base.no_symlinks(output)
    verified = verify(package)
    files = prepared_files(verified)
    if output.exists():
        base.require(output.is_dir(), 'Prepared destination is not a directory')
        require_prepared(output, files)
        return {'package_id': PACKAGE_ID, 'reused': True,
                'manifest_sha256': verified['manifest_sha256']}
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.stowed-prepare-', dir=output.parent))
    try:
        for name, raw in files.items():
            path = staging / base.relative(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open('xb') as stream:
                stream.write(raw)
        require_prepared(staging, files)
        # The source and complete derived snapshot must still match at commit.
        fresh = verify(package)
        base.require(prepared_files(fresh) == files,
                     'Stowed package changed before preparation commit')
        io.install_new(staging, output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return {'package_id': PACKAGE_ID, 'reused': False,
            'manifest_sha256': verified['manifest_sha256']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    if args.verify_only == (args.output is not None):
        parser.error('Select exactly one of --verify-only or --output')
    try:
        if args.verify_only:
            verified = verify(args.package)
            result = {'package_id': PACKAGE_ID,
                      'manifest_sha256': verified['manifest_sha256'],
                      'preservation': verified['validation']['preservation']}
        else:
            result = prepare(args.package, args.output)
    except (ValueError, OSError, KeyError, TypeError, UnicodeError, RecursionError) as error:
        parser.exit(1, 'Stowed asset preparation refused: ' + str(error) + '\n')
    print(json.dumps(result, sort_keys=True, allow_nan=False))


if __name__ == '__main__':
    main()
