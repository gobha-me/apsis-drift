#!/usr/bin/env python3
"""Verify and atomically stage approved lower-cockpit obstacle evidence only."""
import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import shutil
import stat
import tempfile

import prepare_native_assets as base
import lower_cockpit_contact_spec as spec
from lower_cockpit_contact_identity import ENGINE_BINDINGS, IDENTITIES, POLICY, POLICY_SHA256, RUNTIME_SHA256, SOURCE_SHA256

PACKAGE_ID = 'lower-cockpit-contact-01'
SCHEMA = 'apsis.lower-cockpit-contact-assets/1'
RUNTIME = PACKAGE_ID + '.json'
POLICY_FILE = 'contact-policy.json'
PROVENANCE = 'provenance.json'
SOURCE_FILES = {'sources/'+name: selected for name, selected in SOURCE_SHA256.items()}
TOOL_NAMES = ('lower_cockpit_contact_identity.py', 'lower_cockpit_contact_spec.py',
              'package_lower_cockpit_contact.py', 'prepare_lower_cockpit_contact.py', 'prepare_native_assets.py')
SCOPE = 'Additive immutable lower-cockpit obstacle geometry and exact source attribution; no actor/standing/seat permission'
LIMITS = POLICY['limits']


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def open_directory(path):
    path = Path(os.path.abspath(path))
    directory = os.open('/', os.O_RDONLY | os.O_DIRECTORY)
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
        with os.fdopen(descriptor, 'rb') as stream:
            before = os.fstat(stream.fileno())
            base.require(stat.S_ISREG(before.st_mode) and 0 < before.st_size <= maximum,
                         'nonregular/oversized input: '+str(path))
            raw = stream.read(maximum+1)
            after = os.fstat(stream.fileno())
            base.require(0 < len(raw) <= maximum and len(raw) == before.st_size and
                         (before.st_size, before.st_mtime_ns, before.st_ctime_ns) ==
                         (after.st_size, after.st_mtime_ns, after.st_ctime_ns), 'changed/truncated input: '+str(path))
            return raw
    finally:
        os.close(directory)


def install_new(staging, output):
    output = base.no_symlinks(output)
    parent = open_directory(output.parent)
    try:
        rename = getattr(ctypes.CDLL(None, use_errno=True), 'renameat2', None)
        base.require(rename is not None, 'atomic no-replace install requires Linux renameat2')
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        base.require(Path(staging).parent == output.parent, 'staging must share destination parent')
        if rename(parent, os.fsencode(Path(staging).name), parent, os.fsencode(output.name), 1) != 0:
            raise OSError(ctypes.get_errno(), 'atomic no-replace installation refused', str(output))
    finally:
        os.close(parent)


def tool_hashes():
    tools = Path(__file__).resolve().parent
    return {name: digest(read_regular(tools/name, spec.MAX_SOURCE)) for name in TOOL_NAMES}


def closed_roster(root, names, manifest=False):
    expected_dirs = {parent.as_posix() for name in names for parent in base.relative(name).parents
                     if parent != Path('.')}
    files, directories, count = set(), set(), 0
    for entry in root.rglob('*'):
        count += 1
        base.require(count <= len(names)+len(expected_dirs)+int(manifest), 'entry roster exceeds bound')
        base.require(not entry.is_symlink() and (entry.is_file() or entry.is_dir()), 'nonregular package entry')
        relative = entry.relative_to(root).as_posix()
        if entry.is_file():
            files.add(relative)
        else:
            directories.add(relative)
    base.require(files == set(names) | ({'package.json'} if manifest else set()), 'unrostered/missing file')
    base.require(directories == expected_dirs, 'unrostered/missing directory')


def dependencies(repository=None):
    repo = base.no_symlinks(repository if repository is not None else Path(__file__).resolve().parent.parent)
    base.require(repo.is_dir(), 'lower-contact dependency repository is missing')
    documents = {}
    for name, selected in ENGINE_BINDINGS.items():
        raw = read_regular(repo/base.relative(name), 64*1024*1024)
        base.require(digest(raw) == selected, 'independently selected dependency changed: '+name)
        if name.endswith('.json'):
            documents[name] = spec.decode(raw, spec.MAX_CONTACT)
    contact = documents['assets/native/wayfarer-operating-02/metadata/contact.json']
    support = documents['assets/native/boarding-support-01/boarding-support-01.json']
    motion = documents['assets/native/operating-motion-01/operating-motion-01.json']
    base.require(motion['identities']['craft_model_sha256'] == IDENTITIES['craft_model_sha256'] and
                 motion['identities']['contact_sha256'] == IDENTITIES['contact_sha256'], 'model/motion binding changed')
    return contact, support


def check_sources(files, repository=None):
    prefix = 'sources/wayfarer-floor-evidence-01/'
    for name, selected in SOURCE_FILES.items():
        base.require(digest(files[name]) == selected, 'independently selected source changed: '+name)
    provenance = spec.decode(files[prefix+'provenance.json'], spec.MAX_SOURCE)
    base.keys(provenance, ('schema_version','id','source_kind','parents','engine_bindings_sha256',
                           'artifacts_sha256','tools_sha256','scope'))
    spec.integer(provenance['schema_version'],1,1)
    base.require(provenance['id']=='wayfarer-floor-evidence-01' and provenance['source_kind']=='derived',
                 'source provenance type changed')
    artifacts = {name.removeprefix('wayfarer-floor-evidence-01/'): selected
                 for name,selected in SOURCE_SHA256.items()
                 if name.startswith('wayfarer-floor-evidence-01/') and not name.endswith('provenance.json')}
    base.require(provenance['artifacts_sha256']==artifacts and
                 provenance['tools_sha256']=={name:selected for name,selected in SOURCE_SHA256.items() if name.startswith('tools/')} and
                 provenance['engine_bindings_sha256']=={name:ENGINE_BINDINGS[name] for name in provenance['engine_bindings_sha256']},
                 'source dependency/tool/artifact roster changed')
    evidence = spec.decode(files[prefix+'evidence.json'], spec.MAX_SOURCE)
    base.keys(evidence, ('schema_version','id','status','source_hashes','contact_sha256','support_sha256',
                        'engine_bindings_sha256','extractor_sha256','coordinate_contracts','source_objects',
                        'probes','halo_sha256','counts','limits'))
    spec.integer(evidence['schema_version'],1,1)
    base.require(evidence['contact_sha256']==IDENTITIES['contact_sha256'] and
                 evidence['support_sha256']==IDENTITIES['support_source_sha256'] and
                 evidence['halo_sha256']==RUNTIME_SHA256 and evidence['extractor_sha256']==IDENTITIES['extractor_sha256'] and
                 evidence['engine_bindings_sha256']==provenance['engine_bindings_sha256'], 'evidence binding mismatch')
    spec.exact(evidence['counts'], {'halo_vertices':4598,'halo_triangles':8100,'halo_objects':75})
    checks = spec.decode(files[prefix+'checks.json'], spec.MAX_SOURCE)
    base.keys(checks, ('vertices','triangles','objects','quantized_zero_area_triangles','bounds_rest_m',
                      'old_crop_overlap_triangles','schema_version','passed','halo_sha256','evidence_sha256',
                      'checker_sha256','negative_fixtures_refused','limits'))
    base.require(checks['passed'] is True and checks['quantized_zero_area_triangles']==0 and
                 checks['old_crop_overlap_triangles']==0 and checks['halo_sha256']==RUNTIME_SHA256 and
                 checks['evidence_sha256']==IDENTITIES['evidence_sha256'] and
                 checks['checker_sha256']==IDENTITIES['checker_sha256'], 'archived source checks failed')
    findings = spec.decode(files[prefix+'findings.json'], spec.MAX_SOURCE)
    base.keys(findings, ('schema_version','evidence_sha256','halo_sha256','centerline_transition_samples',
                        'sampled_station_landing_surfaces','findings','limits'))
    base.require(findings['evidence_sha256']==IDENTITIES['evidence_sha256'] and
                 findings['halo_sha256']==RUNTIME_SHA256, 'findings binding mismatch')
    contact, support = dependencies(repository)
    halo = spec.validate_halo(spec.decode(files[prefix+'halo.json'], spec.MAX_RUNTIME), contact, support)
    base.require(halo['sources']==evidence['source_hashes'], 'source identities differ')
    return halo, contact, support


def verify(package, repository=None):
    root = base.no_symlinks(package)
    base.require(root.is_dir(), 'lower-contact package is missing')
    manifest_raw = read_regular(root/'package.json',128*1024)
    manifest = spec.decode(manifest_raw,128*1024)
    base.keys(manifest, ('schema','package_id','files','runtime','policy','provenance'))
    base.require(manifest['schema']==SCHEMA and manifest['package_id']==PACKAGE_ID and
                 manifest['runtime']==RUNTIME and manifest['policy']==POLICY_FILE and manifest['provenance']==PROVENANCE,
                 'unsupported lower-contact package')
    names = set(SOURCE_FILES) | {RUNTIME,POLICY_FILE,PROVENANCE}
    spec.array(manifest['files'],len(names))
    files, total = {}, 0
    for record in manifest['files']:
        base.keys(record, ('path','bytes','sha256'))
        name = record['path']; relative = base.relative(name)
        base.require(name in names and name not in files, 'unknown/duplicate package path')
        base.size(record['bytes'],spec.MAX_SOURCE);base.digest(record['sha256'])
        raw = read_regular(root/relative,spec.MAX_RUNTIME if name in (RUNTIME,POLICY_FILE) else spec.MAX_SOURCE)
        base.require(len(raw)==record['bytes'] and digest(raw)==record['sha256'], 'package file hash mismatch')
        selected = SOURCE_FILES.get(name)
        base.require(selected is None or digest(raw)==selected, 'independently selected archive changed: '+name)
        total += len(raw)
        base.require(total<=spec.MAX_TOTAL,'total package byte bound')
        files[name]=raw
    closed_roster(root,names,True)
    source_halo, contact, support = check_sources(files,repository)
    halo = spec.validate_halo(spec.decode(files[RUNTIME],spec.MAX_RUNTIME),contact,support)
    policy = spec.validate_policy(spec.decode(files[POLICY_FILE],128*1024))
    base.require(halo==source_halo and files[RUNTIME]==files['sources/wayfarer-floor-evidence-01/halo.json'] and
                 digest(files[RUNTIME])==RUNTIME_SHA256 and digest(files[POLICY_FILE])==POLICY_SHA256,
                 'independently selected runtime/policy bytes changed')
    provenance = spec.decode(files[PROVENANCE],128*1024)
    base.keys(provenance,('schema','identities','scope','limits','source_artifacts_sha256','tool_sha256'))
    base.require(provenance['schema']=='apsis.lower-cockpit-contact-provenance/1' and
                 provenance['identities']==IDENTITIES and provenance['scope']==SCOPE and provenance['limits']==LIMITS and
                 provenance['source_artifacts_sha256']==SOURCE_SHA256 and provenance['tool_sha256']==tool_hashes(),
                 'lower-contact provenance/tool binding changed')
    files['package.json']=manifest_raw
    return files,digest(manifest_raw)


def prepare(package,output,repository=None):
    output = base.no_symlinks(output)
    base.require(not output.exists(),'prepared output already exists')
    files, package_sha = verify(package,repository)
    output.parent.mkdir(parents=True,exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.lower-contact-prepare-',dir=output.parent))
    try:
        for name,raw in files.items():
            path=staging/base.relative(name);path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw)
        receipt={'schema':'apsis.prepared-lower-cockpit-contact/1','package_id':PACKAGE_ID,
                 'package_sha256':package_sha,'runtime':RUNTIME,'runtime_sha256':RUNTIME_SHA256,
                 'policy':POLICY_FILE,'policy_sha256':POLICY_SHA256,'identities':IDENTITIES,'scope':SCOPE}
        (staging/'prepared.json').write_bytes(spec.encode(receipt))
        install_new(staging,output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return receipt


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--repository',type=Path,help='Explicit dependency repository for copied tools')
    args=parser.parse_args()
    try:
        receipt=prepare(args.package,args.output,args.repository)
    except (ValueError,OSError,KeyError,TypeError,UnicodeError,RecursionError) as error:
        parser.exit(1,'Lower-cockpit preparation refused: '+str(error)+'\n')
    print('Prepared',receipt['package_id'],receipt['runtime_sha256'],receipt['policy_sha256'])


if __name__=='__main__':
    main()
