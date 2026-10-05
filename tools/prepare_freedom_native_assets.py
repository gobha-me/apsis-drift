#!/usr/bin/env python3
"""Prepare the fixed native starter, operating base and stowed companion offline.

The directory locates authenticated assets; the C++ saved selection chooses the
assembly. A complete first install is atomic and reuse never repairs changed
files. Each constituent retains its existing provenance and license records.
"""
import argparse
from pathlib import Path
import shutil
import tempfile

import prepare_native_assets as starter
import prepare_operating_assets as operating
import prepare_wayfarer_stowed as stowed


def verify_starter(output, verified):
    _, manifest, package_hash, roster = verified
    starter.require(starter.load_json(output / 'prepared.json') == {
        'schema': starter.SCHEMA, 'package_sha256': package_hash},
        'Starter receipt changed')
    expected = {'prepared.json', 'operating', 'stowed'}
    for record in manifest['models']:
        path = starter.no_symlinks(output / record['output'])
        starter.require(path.is_file() and path.stat().st_size == record['bytes']
                        and starter.sha(path) == record['sha256'],
                        'Prepared starter model changed')
        expected.add(record['output'])
    for record in manifest['metadata']:
        path = starter.no_symlinks(output / record['output'])
        starter.require(path.is_file() and
                        starter.sha(path) == roster[record['path']]['sha256'],
                        'Prepared starter metadata changed')
        expected.add(record['output'])
    starter.require({path.name for path in output.iterdir()} == expected,
                    'Unexpected native asset entry')


def prepare(repository, destination):
    packages = repository / 'assets/native'
    output = starter.no_symlinks(destination)
    starter.require(output != packages and packages not in output.parents and
                    output not in packages.parents, 'Output overlaps packages')
    verified = starter.verify(packages / 'freedom-starter-01')
    if output.exists():
        starter.require(output.is_dir(), 'Native asset destination is not a directory')
        verify_starter(output, verified)
        # Both preparers validate their complete existing outputs before reuse.
        operating.prepare(packages / 'wayfarer-operating-02', output / 'operating')
        stowed.prepare(packages / 'wayfarer-stowed-01', output / 'stowed')
        return output
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.freedom-native-', dir=output.parent))
    candidate = staging / 'assets'
    try:
        starter.prepare(packages / 'freedom-starter-01', candidate)
        operating.prepare(packages / 'wayfarer-operating-02', candidate / 'operating')
        stowed.prepare(packages / 'wayfarer-stowed-01', candidate / 'stowed')
        verify_starter(candidate, verified)
        starter.require(starter.verify(packages / 'freedom-starter-01')[2] == verified[2],
                        'Starter package changed before install')
        operating.install_new(candidate, output)
    finally:
        shutil.rmtree(staging)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', type=Path,
                        default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        print(prepare(args.repository.resolve(), args.output))
    except (OSError, ValueError, EOFError) as error:
        parser.exit(1, 'Native asset preparation refused: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
