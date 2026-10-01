#!/usr/bin/env python3
"""Read-only sampled source-triangle operating sweep diagnostics, BSD-3-Clause.

This produces raw contacts for exact-pair review. It never silently accepts a
contact as intentional and never edits/saves authoring geometry.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
import numpy as np
from mathutils import Matrix
from mathutils.bvhtree import BVHTree

sys.path.insert(0, str(Path(__file__).resolve().parent))
from export_boarding_contact import (CROPS, CONVERT, SOURCE_HASHES, ancestors,
    craft_keep, station_keep, mesh_data, station_controls, write_json)
from wayfarer_operating_spec import classify_motion_group
from wayfarer_operating_blender import OperatingPoseController
from station_clearance_blender import apply_station_clearance


def cache_scene(owner, controls=None):
    result=[]
    for obj in sorted(bpy.context.scene.objects,key=lambda o:o.name):
        if not (craft_keep(obj) if owner=='craft' else station_keep(obj)): continue
        moving=(classify_motion_group(ancestors(obj)) if owner=='craft' else
                next((controls[name] for name in ancestors(obj) if name in controls),None))
        vertices,faces=mesh_data(obj,owner)
        if moving is None:
            lo,hi=np.array(CROPS[owner]);tri=vertices[faces]
            keep=np.all(tri.max(axis=1)>=lo,axis=1)&np.all(tri.min(axis=1)<=hi,axis=1)
            faces=faces[keep]
        if not len(faces): continue
        ids,inverse=np.unique(faces,return_inverse=True)
        vertices=vertices[ids];faces=inverse.reshape(-1,3)
        result.append({'name':obj.name,'group':moving,'vertices':vertices,'faces':faces,'world_rest':obj.matrix_world.copy()})
    return result


def transformed(obj,delta):
    vertices=obj['vertices']
    if delta is not None:
        matrix=np.array(delta);vertices=vertices@matrix[:3,:3].T+matrix[:3,3]
    return vertices


def candidates(objects,deltas):
    rows=[]
    for obj in objects:
        vertices=transformed(obj,deltas.get(obj['group']))
        rows.append({'obj':obj,'vertices':vertices,'low':vertices.min(axis=0),'high':vertices.max(axis=0),'tree':None})
    return rows


def tree(row):
    if row['tree'] is None:
        row['tree']=BVHTree.FromPolygons(row['vertices'].tolist(),row['obj']['faces'].tolist(),all_triangles=True)
    return row['tree']


def contacts(rows):
    results=[]
    for index,a in enumerate(rows):
        if a['obj']['group'] is None: continue
        for b in rows:
            if a is b or a['obj']['group']==b['obj']['group']: continue
            if b['obj']['group'] is not None and a['obj']['name']>b['obj']['name']: continue
            if np.any(a['low']>b['high']) or np.any(a['high']<b['low']): continue
            pairs=tree(a).overlap(tree(b))
            if pairs:
                names=sorted((a['obj']['name'],b['obj']['name']))
                results.append((tuple(names),len(pairs)))
    return results


def accumulate(records,hits,p):
    for pair,count in hits:
        row=records.setdefault(pair,{'object_a':pair[0],'object_b':pair[1],'samples':0,'first_progress':p,'last_progress':p,'triangle_pairs_max':0,'progress_samples':[]})
        row['progress_samples'].append(p)
        row['samples']+=1;row['last_progress']=p;row['triangle_pairs_max']=max(row['triangle_pairs_max'],count)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('station','craft','closure','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--samples',type=int,default=41)
    parser.add_argument('--station-only',action='store_true')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if not 21<=args.samples<=101:raise ValueError('Bounded samples required')
    paths={'station_reference_sha256':args.station,'wayfarer_sha256':args.craft,'station_closure_sha256':args.closure}
    for key,path in paths.items():
        if hashlib.sha256(path.read_bytes()).hexdigest()!=SOURCE_HASHES[key]:raise ValueError('Stale source')
    raw={'sources':SOURCE_HASHES,'sweeps':[],'source_unchanged':False,'limits':['Sampled triangle surface contacts only, not continuous motion separation or articulated character clearance.','Raw contacts require exact-pair human/structural review; none are automatically waived.']}
    if args.station_only:
        raw=json.loads((args.output/'raw-sweeps.json').read_text())
        raw['sweeps']=[row for row in raw['sweeps'] if row['owner']=='craft']
    else:
        controller=OperatingPoseController.prepare(args.craft).freeze()
        objects=cache_scene('craft')
        print('CRAFT_CACHE',len(objects),sum(len(o['faces']) for o in objects),flush=True)
        for channel in ('roof_transfer','inner_door','seat_boarding'):
            records={}
            for i,p in enumerate(np.linspace(0,1,args.samples)):
                controller.apply(channel,float(p));hits=contacts(candidates(objects,controller.deltas()))
                accumulate(records,hits,float(p))
                if i%5==0:print('SWEEP',channel,i,'pairs',len(hits),flush=True)
            raw['sweeps'].append({'owner':'craft','channel':channel,'samples':args.samples,'method':'Sampled exact source-triangle BVH surface intersection','contacts':sorted(records.values(),key=lambda r:(r['object_a'],r['object_b']))})
            write_json(args.output/'raw-sweeps.json',raw)
    bpy.ops.wm.open_mainfile(filepath=str(args.station));bpy.context.scene.frame_set(1);bpy.context.view_layer.update()
    apply_station_clearance()
    controls={obj.name:f'station_d1_{i:02}' for i,obj in enumerate(station_controls(bpy.context.scene))}
    objects=cache_scene('station',controls)
    rests={controls[obj.name]:obj.matrix_world.copy() for obj in station_controls(bpy.context.scene)}
    bpy.ops.wm.open_mainfile(filepath=str(args.closure))
    source={obj.name:obj for obj in station_controls(bpy.context.scene)}
    records={}
    for i,p in enumerate(np.linspace(0,1,args.samples)):
        frame=110+190*float(p);bpy.context.scene.frame_set(int(frame),subframe=frame-int(frame));bpy.context.view_layer.update()
        conversion=Matrix.Translation((-.97,0,.978))@CONVERT
        deltas={controls[name]:conversion@obj.matrix_world@rests[controls[name]].inverted()@conversion.inverted() for name,obj in source.items()}
        hits=contacts(candidates(objects,deltas));accumulate(records,hits,float(p))
        if i%5==0:print('SWEEP','station_closure',i,'pairs',len(hits),flush=True)
    raw['sweeps'].append({'owner':'station','channel':'station_closure','samples':args.samples,'method':'Sampled authored kit09 source poses acting on reference triangles; exact pair BVH intersection','contacts':sorted(records.values(),key=lambda r:(r['object_a'],r['object_b']))})
    for key,path in paths.items():
        if hashlib.sha256(path.read_bytes()).hexdigest()!=SOURCE_HASHES[key]:raise ValueError('Source changed')
    raw['source_unchanged']=True
    write_json(args.output/'raw-sweeps.json',raw)
    print('RAW_SWEEPS_COMPLETE',[len(s['contacts']) for s in raw['sweeps']],flush=True)


if __name__=='__main__':main()
