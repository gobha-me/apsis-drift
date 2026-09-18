#!/usr/bin/env python3
"""Render all inspection cameras from a saved hero master without rebuilding it.

blender -b --factory-startup -t 8 --python tools/render_hero_views.py -- ship 2 3
"""
import argparse
import sys
from pathlib import Path
import bpy

OUT = Path(__file__).resolve().parents[1] / 'assets' / 'visual'
parser=argparse.ArgumentParser()
parser.add_argument('asset',choices=['cockpit','ship','station'])
parser.add_argument('views',nargs='+',type=int)
parser.add_argument('--width',type=int,default=2560)
parser.add_argument('--samples',type=int,default=72)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
if not 320<=args.width<=8192 or not 1<=args.samples<=4096:
    raise ValueError('Render dimensions or samples out of bounds')
bpy.ops.wm.open_mainfile(filepath=str(OUT/f'hero-{args.asset}.blend'))
scene=bpy.context.scene
scene.render.resolution_x=args.width;scene.render.resolution_y=args.width*9//16
scene.cycles.samples=args.samples
cameras=sorted((o for o in scene.objects if o.type=='CAMERA'),key=lambda o:o.name)
for view in args.views:
    if not 1<=view<=len(cameras):raise ValueError('Unknown camera')
    scene.camera=cameras[view-1]
    scene.render.filepath=str(OUT/f'hero-{args.asset}-view-{view:02}.png')
    bpy.ops.render.render(write_still=True)
