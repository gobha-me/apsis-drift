#!/usr/bin/env python3
"""Archive the exact approved floor evidence and independently bind its static halo."""
import argparse
from pathlib import Path
import shutil
import tempfile

import prepare_native_assets as base
import lower_cockpit_contact_spec as spec
import prepare_lower_cockpit_contact as assets
from lower_cockpit_contact_identity import IDENTITIES, POLICY, POLICY_SHA256, RUNTIME_SHA256, SOURCE_SHA256


def build(source, destination, repository=None, source_tools=None):
    source,destination=map(base.no_symlinks,(source,destination))
    base.require(not destination.exists(),'lower-contact package destination exists')
    base.require(source_tools is not None,'explicit source-tools directory required')
    source_tools=base.no_symlinks(source_tools)
    source_names={name.removeprefix('wayfarer-floor-evidence-01/') for name in SOURCE_SHA256
                  if name.startswith('wayfarer-floor-evidence-01/')}
    assets.closed_roster(source,source_names)
    files={}
    for name,selected in SOURCE_SHA256.items():
        path=source/base.relative(name.removeprefix('wayfarer-floor-evidence-01/')) if name.startswith('wayfarer-floor-evidence-01/') else source_tools/base.relative(name.removeprefix('tools/'))
        raw=assets.read_regular(path,spec.MAX_SOURCE)
        base.require(assets.digest(raw)==selected,'selected source changed: '+name)
        files['sources/'+name]=raw
    assets.check_sources(files,repository)
    files[assets.RUNTIME]=files['sources/wayfarer-floor-evidence-01/halo.json']
    files[assets.POLICY_FILE]=spec.encode(POLICY)
    base.require(assets.digest(files[assets.RUNTIME])==RUNTIME_SHA256 and
                 assets.digest(files[assets.POLICY_FILE])==POLICY_SHA256,'selected runtime/policy changed')
    files[assets.PROVENANCE]=spec.encode({'schema':'apsis.lower-cockpit-contact-provenance/1',
        'identities':IDENTITIES,'scope':assets.SCOPE,'limits':assets.LIMITS,
        'source_artifacts_sha256':SOURCE_SHA256,'tool_sha256':assets.tool_hashes()})
    manifest={'schema':assets.SCHEMA,'package_id':assets.PACKAGE_ID,
        'files':[{'path':name,'bytes':len(raw),'sha256':assets.digest(raw)} for name,raw in sorted(files.items())],
        'runtime':assets.RUNTIME,'policy':assets.POLICY_FILE,'provenance':assets.PROVENANCE}
    destination.parent.mkdir(parents=True,exist_ok=True)
    staging=Path(tempfile.mkdtemp(prefix='.lower-contact-package-',dir=destination.parent))
    try:
        for name,raw in files.items():
            path=staging/base.relative(name);path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw)
        (staging/'package.json').write_bytes(spec.encode(manifest))
        assets.verify(staging,repository)
        assets.install_new(staging,destination)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return assets.digest(spec.encode(manifest))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',required=True,type=Path,help='Frozen wayfarer-floor-evidence-01 directory')
    parser.add_argument('--source-tools',required=True,type=Path,help='Directory of exact supplied checker/extractor')
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--repository',type=Path,help='Explicit existing contact/motion/model dependency repository')
    args=parser.parse_args()
    try:
        package_sha=build(args.source,args.output,args.repository,args.source_tools)
    except (ValueError,OSError,KeyError,TypeError,UnicodeError,RecursionError) as error:
        parser.exit(1,'Lower-cockpit packaging refused: '+str(error)+'\n')
    print('Packaged',assets.PACKAGE_ID,package_sha)


if __name__=='__main__':
    main()
