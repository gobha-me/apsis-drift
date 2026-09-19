#!/usr/bin/env python3
"""Re-export exchange tiers without rerendering or altering the saved master."""
import argparse
import sys
from pathlib import Path
import bpy

TOOLS=Path(__file__).resolve().parent
sys.path.insert(0,str(TOOLS))
import build_hero_assets as hero

p=argparse.ArgumentParser();p.add_argument('asset',choices=['cockpit','ship','station'])
a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
stem='hero-'+a.asset
bpy.ops.wm.open_mainfile(filepath=str(hero.OUT/(stem+'.blend')))
hero.export_lods(stem)
