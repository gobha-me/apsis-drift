"""Seal the fixed corrected-closed derivative; BSD-3-Clause.

No Blender dependency, source save or runtime asset admission. The exporter
owns its temporary staging directory and removes it on any failed check.
"""
import hashlib
import json
from pathlib import Path

import wayfarer_corrected_rest_checks as checks
import wayfarer_corrected_rest_geometry as geometry
import wayfarer_restraint_export_checks as original

TOOLS = Path(__file__).resolve().parent
TOOL_NAMES = (
    'export_wayfarer_corrected_rest.py',
    'wayfarer_corrected_rest_geometry.py',
    'wayfarer_corrected_rest_proof.py',
    'wayfarer_corrected_rest_checks.py',
    'wayfarer_corrected_rest_package.py',
    'wayfarer_operating_blender.py',
    'wayfarer_operating_spec.py',
    'wayfarer_flight_export_base.py',
    'wayfarer_operating_glb_audit.py',
    'wayfarer_gltf_checks.py',
    'wayfarer_restraint_export_checks.py',
)
PAYLOADS = (
    'model.glb', 'producer.json', 'contact.json', 'face-attribution.json',
    'verification.json', 'static-composite.json', 'finite-volume-proof.json',
    'finite-attachment-proof.json', 'evidence/original-rest.glb',
    'evidence/original-rest.json', 'evidence/original-posed.glb',
    'evidence/original-posed.json',
)


def dependency_snapshot():
    """Identify the actual producer and helpers before any source capture."""
    return {name: original.sha(TOOLS / name) for name in TOOL_NAMES}


def require_dependencies(snapshot):
    original.require(type(snapshot) is dict and set(snapshot) == set(TOOL_NAMES),
                     'Exact producer dependency roster required')
    original.require(dependency_snapshot() == snapshot,
                     'Producer dependency changed during export')


def write_new(path, value):
    """Never replace another staging payload, including a dangling symlink."""
    data = (json.dumps(value, indent=2, allow_nan=False) + '\n').encode()
    with Path(path).open('xb') as stream:
        stream.write(data)


def file_id(path, relative):
    data = checks.read_member(path, relative)
    return {'path': relative, 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest()}


def build_provenance(staging, producer, baselines, dependency_hashes):
    snapshots = {'corrected_closed': producer, **baselines}
    original.require(producer.get('mode') == 'corrected_closed' and
                     set(baselines) == {'original_rest', 'original_posed'} and
                     all(report.get('mode') == mode and
                         report.get('source_sha256') == original.SOURCE_SHA256 and
                         report.get('source_file_unchanged') is True and
                         report.get('all_1746_original_signatures_restored') is True
                         for mode, report in snapshots.items()),
                     'All three source snapshot identities/restorations required')
    original.require(type(producer.get('blender_version')) is str and
                     producer['blender_version'] == '5.2.2',
                     'Qualified Blender 5.2.2 producer required')
    reference_ids = {}
    for mode, name in (('original_rest', 'original-rest'),
                       ('original_posed', 'original-posed')):
        model = file_id(staging, 'evidence/' + name + '.glb')
        metadata = file_id(staging, 'evidence/' + name + '.json')
        reference_ids[mode] = {
            'model': {'file': model['path'], 'bytes': model['bytes'],
                      'sha256': model['sha256']},
            'metadata': {'file': metadata['path'], 'bytes': metadata['bytes'],
                         'sha256': metadata['sha256']},
            'tuple': [0, 0, 0, 0] if mode == 'original_rest' else [1, 1, 1, 0],
        }
    return {
        'schema': 'apsis.corrected-closed-provenance/1',
        'id': 'wayfarer-corrected-closed-01',
        'source': {'file': 'assets/visual/hopper-craft-09.blend',
                   'sha256': original.SOURCE_SHA256},
        'old_package': {'id': 'wayfarer-operating-02',
                        'package_sha256': original.PACKAGE_SHA256,
                        'model_sha256': original.MODEL_SHA256,
                        'metadata_sha256': original.METADATA_SHA256},
        'producer': {'file': 'tools/export_wayfarer_corrected_rest.py',
                     'sha256': dependency_hashes['export_wayfarer_corrected_rest.py'],
                     'blender_version': producer['blender_version']},
        'helpers': {name: value for name, value in dependency_hashes.items()
                    if name != 'export_wayfarer_corrected_rest.py'},
        'baselines': reference_ids,
        'registered_corrections': {'ribbons': producer['declared_edits'],
                                   'manifold': producer['manifold'],
                                   'connectors': producer['connector_records']},
        'source_restoration': {
            'snapshots': {mode: report['restoration'] for mode, report in snapshots.items()},
            'source_file_sha256_before': original.SOURCE_SHA256,
            'source_file_sha256_after': original.SOURCE_SHA256,
            'source_master_saved': False,
        },
        'licenses': {name: {'bytes': size, 'sha256': identity}
                     for name, (size, identity) in checks.LICENSES.items()},
        'namespace': checks.NAMESPACE,
        'limits': geometry.LIMITS,
    }


def finalize(staging, output, source, reference_package, reference_manifest,
             dependency_hashes):
    """Validate the complete private staging snapshot, then install once.

    Source/reference/output preflight is deliberately repeated immediately
    before publication. No metadata pass flag replaces independent buffer and
    finite geometry validation. No existing output is removed or replaced.
    """
    staging, output = Path(staging), Path(output)
    original.require(staging.is_dir() and not staging.is_symlink() and
                     staging.parent.resolve() == output.parent.resolve() and
                     staging.resolve() != output.resolve(),
                     'Private staging must be a separate destination sibling')
    source, output, reference_package, manifest, _ = original.preflight(
        source, output, reference_package)
    original.require(manifest == reference_manifest,
                     'Frozen reference manifest changed during export')
    require_dependencies(dependency_hashes)
    actual = {str(p.relative_to(staging)) for p in staging.rglob('*')
              if p.is_file() or p.is_symlink()}
    original.require(actual == set(PAYLOADS),
                     'Exact pre-package snapshot inventory required')
    # Snapshot bytes are read once for provenance and later read independently
    # against the sealed manifest. Neither source paths nor host paths enter it.
    producer = checks.closed_json(checks.read_member(staging, 'producer.json'),
                                  maximum=original.MAX_BYTES)
    baselines = {
        mode: checks.closed_json(checks.read_member(staging, 'evidence/' + name + '.json'),
                                 maximum=original.MAX_BYTES)
        for mode, name in (('original_rest', 'original-rest'),
                           ('original_posed', 'original-posed'))
    }
    original.require(producer.get('source_sha256') == original.SOURCE_SHA256 and
                     producer.get('all_1746_original_signatures_restored') is True,
                     'Source snapshot/restoration identity required')
    inherited = {entry['path']: entry for entry in manifest['files']
                 if entry['path'].startswith('licenses/')}
    original.require(set(inherited) == set(checks.LICENSES),
                     'Exact inherited five-license roster required')
    for name, entry in sorted(inherited.items()):
        data = original.verified_member(reference_package, entry)
        size, expected = checks.LICENSES[name]
        original.require(len(data) == size and
                         hashlib.sha256(data).hexdigest() == expected,
                         'Inherited license identity changed')
        path = staging / name
        path.parent.mkdir(exist_ok=True)
        with path.open('xb') as stream:
            stream.write(data)
    provenance = build_provenance(staging, producer, baselines, dependency_hashes)
    write_new(staging / 'provenance.json', provenance)
    paths = (*PAYLOADS, 'provenance.json', *sorted(inherited))
    files = [file_id(staging, name) for name in sorted(paths)]
    model = next(entry for entry in files if entry['path'] == 'model.glb')
    package = {
        'schema': 'apsis.wayfarer-corrected-closed-assets/1',
        'id': 'wayfarer-corrected-closed-01',
        'source_sha256': original.SOURCE_SHA256,
        'model': {'file': model['path'], 'bytes': model['bytes'],
                  'sha256': model['sha256']},
        'files': files,
        'limits': provenance['limits'],
    }
    write_new(staging / 'package.json', package)
    sealed_manifest = checks.read_member(staging, 'package.json',
                                         maximum=2 * 1024 * 1024)
    validation = checks.validate_derivative(staging, reference_package)
    original.require(validation.get('pass') is True,
                     'Independent corrected-closed package validation failed')
    # Re-read all payload and license identities after the complete proof. This
    # catches a changed staging snapshot before the atomic destination handoff.
    checks.validate_files(staging, package, paths)
    original.require(checks.read_member(staging, 'package.json',
                                       maximum=2 * 1024 * 1024) == sealed_manifest,
                     'Sealed package manifest changed during validation')
    require_dependencies(dependency_hashes)
    original.require(original.sha(source) == original.SOURCE_SHA256,
                     'Source master changed during export')
    original.install_no_replace(staging, output)
    return validation
