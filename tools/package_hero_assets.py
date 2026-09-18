#!/usr/bin/env python3
"""Assemble a self-contained hero source/asset kit in a new temporary folder."""
import hashlib
import json
import shutil
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
source=json.loads((ROOT/'assets/provenance.json').read_text())
records=[a for a in source['assets'] if a['id'].startswith('visual/hero-')]
package=Path(tempfile.mkdtemp(prefix='apsis-hero-v2-'))
for record in records:
    for encoded in record['files']:
        relative=Path(encoded)
        if relative.is_absolute() or '..' in relative.parts:raise ValueError(encoded)
        target=package/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(ROOT/relative,target)
shutil.copy2(ROOT/'LICENSE.md',package/'LICENSE.md')
shutil.copy2(ROOT/'docs/HERO_ASSET_REVIEW.md',package/'README.md')
for name in ('hero-design-board.png','hero-lod-board.png'):
    shutil.copy2(ROOT/'assets/visual'/name,package/name)
(package/'assets/provenance.json').write_text(json.dumps({'schema_version':1,'assets':records},indent=2)+'\n')
lines=[]
for path in sorted(p for p in package.rglob('*') if p.is_file()):
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    lines.append(f'{digest}  {path.relative_to(package).as_posix()}')
(package/'SHA256SUMS').write_text('\n'.join(lines)+'\n')
print(package)
