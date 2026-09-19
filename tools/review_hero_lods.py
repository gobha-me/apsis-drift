#!/usr/bin/env python3
"""Render round-tripped GLBs through each master's camera and light rig.

blender -b --factory-startup -t 8 --python tools/review_hero_lods.py -- ship
"""
import argparse
import sys
from pathlib import Path
import bpy

OUT=Path(__file__).resolve().parents[1]/'assets'/'visual'
p=argparse.ArgumentParser();p.add_argument('asset',choices=['cockpit','ship','station'])
p.add_argument('--tier',choices=['near','mid','far'])
a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
for tier in ((a.tier,) if a.tier else ('near','mid','far')):
    bpy.ops.wm.open_mainfile(filepath=str(OUT/f'hero-{a.asset}.blend'))
    scene=bpy.context.scene
    cameras=sorted((o for o in scene.objects if o.type=='CAMERA'),key=lambda o:o.name)
    scene.camera=cameras[0]
    scene.render.resolution_x=800;scene.render.resolution_y=450
    scene.cycles.samples=16
    for o in list(scene.objects):
        if o.type in ('MESH','CURVE','FONT') and o.get('detail_tier',9) in (-1,0,1,2):
            bpy.data.objects.remove(o,do_unlink=True)
    bpy.ops.import_scene.gltf(filepath=str(OUT/f'hero-{a.asset}-{tier}.glb'))
    scene.render.filepath=str(OUT/f'hero-{a.asset}-lod-{tier}.png')
    bpy.ops.render.render(write_still=True)
