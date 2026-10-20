#!/usr/bin/env python3
"""Prepare the fixed native starter, operating base and stowed companion offline.

The directory locates authenticated assets; the C++ saved selection chooses the
assembly. A complete first install is atomic and reuse never repairs changed
files. Each constituent retains its existing provenance and license records.
"""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

import prepare_native_assets as starter
import prepare_operating_assets as operating
import prepare_wayfarer_stowed as stowed

CREW_PACKAGE_SHA256 = 'f1a452efd4e1e4d9dfa4643e378455e36f533090c4339de5b1a11316f0920a4d'
CREW_OUTPUTS = ('casual-male-idle.png', 'idle-frames.json')


def verify_crew(package):
    root = starter.no_symlinks(package)
    manifest_path = starter.no_symlinks(root / 'package.json')
    starter.require(manifest_path.is_file() and
                    starter.sha(manifest_path) == CREW_PACKAGE_SHA256,
                    'Origin crew package identity changed')
    manifest = starter.load_json(manifest_path)
    starter.keys(manifest, ['schema', 'package_id', 'files'])
    starter.require(manifest['schema'] == 'apsis.origin-crew-assets/1' and
                    manifest['package_id'] == 'origin-crew-01' and
                    type(manifest['files']) is list and
                    1 <= len(manifest['files']) <= 16, 'Invalid crew package')
    roster, total = {}, 0
    for record in manifest['files']:
        starter.keys(record, ['path', 'bytes', 'sha256'])
        name = record['path']
        relative = starter.relative(name)
        starter.require(name != 'package.json' and name not in roster,
                        'Duplicate crew file')
        starter.size(record['bytes'], 2 * 1024 * 1024)
        starter.digest(record['sha256'])
        path = starter.no_symlinks(root / relative)
        starter.require(path.is_file() and path.stat().st_size == record['bytes']
                        and starter.sha(path) == record['sha256'],
                        'Crew package file changed: ' + name)
        roster[name] = record
        total += record['bytes']
    starter.require(total <= 3 * 1024 * 1024, 'Crew package exceeds byte bound')
    names = set()
    for path in root.rglob('*'):
        starter.require(not path.is_symlink() and
                        (path.is_file() or path.is_dir()), 'Linked/nonregular crew entry')
        if path.is_file():
            names.add(path.relative_to(root).as_posix())
    starter.require(names == set(roster) | {'package.json'} and
                    all(name in roster for name in CREW_OUTPUTS),
                    'Unrostered/missing crew file')
    return root, roster


def prepare_crew(package, output):
    root, roster = verify_crew(package)
    output = starter.no_symlinks(output)
    receipt = {'schema': 'apsis.origin-crew-assets/1',
               'package_sha256': CREW_PACKAGE_SHA256}

    def verify_output(path):
        starter.require(path.is_dir() and
                        {p.name for p in path.iterdir()} == set(CREW_OUTPUTS) | {'prepared.json'},
                        'Unexpected crew companion entry')
        starter.require(starter.load_json(starter.no_symlinks(path / 'prepared.json')) == receipt,
                        'Crew preparation receipt changed')
        for name in CREW_OUTPUTS:
            selected = starter.no_symlinks(path / name)
            starter.require(selected.is_file() and
                            selected.stat().st_size == roster[name]['bytes'] and
                            starter.sha(selected) == roster[name]['sha256'],
                            'Prepared crew bytes changed')

    if output.exists():
        verify_output(output)
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='.origin-crew-', dir=output.parent))
    try:
        for name in CREW_OUTPUTS:
            shutil.copyfile(root / name, temporary / name)
        (temporary / 'prepared.json').write_text(json.dumps(receipt, indent=2) + '\n')
        verify_output(temporary)
        verify_crew(package)
        operating.install_new(temporary, output)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def verify_starter(output, verified, allow_missing_crew=False):
    _, manifest, package_hash, roster = verified
    starter.require(starter.load_json(output / 'prepared.json') == {
        'schema': starter.SCHEMA, 'package_sha256': package_hash},
        'Starter receipt changed')
    expected = {'prepared.json', 'operating', 'stowed'}
    if not allow_missing_crew or (output / 'crew').exists():
        expected.add('crew')
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
    verify_crew(packages / 'origin-crew-01')
    if output.exists():
        starter.require(output.is_dir(), 'Native asset destination is not a directory')
        verify_starter(output, verified, allow_missing_crew=True)
        # Both preparers validate their complete existing outputs before reuse.
        operating.prepare(packages / 'wayfarer-operating-02', output / 'operating')
        stowed.prepare(packages / 'wayfarer-stowed-01', output / 'stowed')
        # A previously verified starter may acquire this new companion once;
        # existing crew bytes are authenticated, never silently repaired.
        prepare_crew(packages / 'origin-crew-01', output / 'crew')
        verify_starter(output, verified)
        return output
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.freedom-native-', dir=output.parent))
    candidate = staging / 'assets'
    try:
        starter.prepare(packages / 'freedom-starter-01', candidate)
        operating.prepare(packages / 'wayfarer-operating-02', candidate / 'operating')
        stowed.prepare(packages / 'wayfarer-stowed-01', candidate / 'stowed')
        prepare_crew(packages / 'origin-crew-01', candidate / 'crew')
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
