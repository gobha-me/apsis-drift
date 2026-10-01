#!/usr/bin/env python3
"""Quantify source pair crossings, separately from BVH contact candidates."""
import argparse,hashlib,json,math,sys
from pathlib import Path
import numpy as np
import bpy
from mathutils import Matrix,Vector
from mathutils.geometry import intersect_ray_tri
sys.path.insert(0,str(Path(__file__).resolve().parent))
from export_boarding_contact import CONVERT,SOURCE_HASHES,station_controls,write_json
from qualify_boarding_contact import cache_scene,candidates,tree
from wayfarer_operating_blender import OperatingPoseController
from station_clearance_blender import apply_station_clearance

TOLERANCE=.000001

def pair_details(a,b):
 pairs=tree(a).overlap(tree(b));crossings=0;coplanar=0;depth=0.;points=[]
 for ia,ib in pairs:
  aa=a['vertices'][a['obj']['faces'][ia]];bb=b['vertices'][b['obj']['faces'][ib]]
  na=np.cross(aa[1]-aa[0],aa[2]-aa[0]);nb=np.cross(bb[1]-bb[0],bb[2]-bb[0])
  la=np.linalg.norm(na);lb=np.linalg.norm(nb)
  if min(la,lb)<1e-12:continue
  da=(bb-aa[0])@(na/la);db=(aa-bb[0])@(nb/lb)
  crossing=(da.min() < -TOLERANCE and da.max()>TOLERANCE and db.min() < -TOLERANCE and db.max()>TOLERANCE)
  if np.max(np.abs(da))<=TOLERANCE and np.max(np.abs(db))<=TOLERANCE:coplanar+=1
  if crossing:
   crossings+=1;depth=max(depth,min(-float(da.min()),float(da.max()),-float(db.min()),float(db.max())))
  if crossing and len(points)<100:
   for edges,tri in ((aa,bb),(bb,aa)):
    vectors=[Vector(v) for v in tri]
    for k in range(3):
     start=Vector(edges[k]);end=Vector(edges[(k+1)%3]);direction=end-start;length=direction.length
     if length<1e-9:continue
     hit=intersect_ray_tri(*vectors,direction/length,start,True)
     if hit is not None and (hit-start).length<=length+TOLERANCE:points.append(list(hit))
 return {'bvh_triangle_pairs':len(pairs),'strict_crossing_pairs':crossings,'coplanar_pairs':coplanar,
         'maximum_triangle_plane_crossing_depth_m':depth,'crossing_points_owner_m':points}


def primitive(name,offset):
 vertices=np.array([[x,y,z] for x in (0.,1.) for y in (0.,1.) for z in (0.,1.)])+offset
 faces=np.array([[0,4,6],[0,6,2],[1,3,7],[1,7,5],[0,1,5],[0,5,4],[2,6,7],[2,7,3],[0,2,3],[0,3,1],[4,5,7],[4,7,6]])
 return {'obj':{'name':name,'faces':faces},'vertices':vertices,'tree':None}


def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('station','craft','closure','output'):p.add_argument('--'+name,type=Path,required=True)
 args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
 raw=json.loads((args.output/'raw-sweeps.json').read_text())
 result={'schema_version':1,'sources':SOURCE_HASHES,'tolerance_metres':TOLERANCE,'sweeps':[],
 'negative_controls':[],'inner_opening':{},'limits':['Triangle-plane crossing depth is a local sampled surface metric, not whole-solid penetration distance.','Coplanar or touching candidates are not automatically waived; exact designed contact pairs still require review.','Samples do not establish continuous separation or articulated character motion.']}
 a=primitive('reference',np.array([0.,0.,0.]))
 for name,offset in [('separated',[1.01,0,0]),('touching',[1.,0,0]),('crossing',[.90,.05,.03])]:
  d=pair_details(a,primitive(name,np.array(offset)))
  result['negative_controls'].append({'name':name,'result':d})
 assert result['negative_controls'][0]['result']['bvh_triangle_pairs']==0
 assert result['negative_controls'][1]['result']['strict_crossing_pairs']==0
 assert result['negative_controls'][2]['result']['strict_crossing_pairs']>0
 for owner in ('craft','station'):
  if owner=='craft':
   controller=OperatingPoseController.prepare(args.craft).freeze();objects=cache_scene('craft')
  else:
   bpy.ops.wm.open_mainfile(filepath=str(args.station));bpy.context.scene.frame_set(1);bpy.context.view_layer.update()
   apply_station_clearance()
   controls={o.name:f'station_d1_{i:02}' for i,o in enumerate(station_controls(bpy.context.scene))}
   objects=cache_scene('station',controls);rests={controls[o.name]:o.matrix_world.copy() for o in station_controls(bpy.context.scene)}
   bpy.ops.wm.open_mainfile(filepath=str(args.closure));source={o.name:o for o in station_controls(bpy.context.scene)}
  for sweep in (s for s in raw['sweeps'] if s['owner']==owner):
   rows={}
   for progress in np.linspace(0,1,sweep['samples']):
    progress=float(progress)
    if owner=='craft':controller.apply(sweep['channel'],progress);deltas=controller.deltas()
    else:
     frame=110+190*progress;bpy.context.scene.frame_set(int(frame),subframe=frame-int(frame));bpy.context.view_layer.update()
     conversion=Matrix.Translation((-.97,0,.978))@CONVERT
     deltas={controls[n]:conversion@o.matrix_world@rests[controls[n]].inverted()@conversion.inverted() for n,o in source.items()}
    current={r['obj']['name']:r for r in candidates(objects,deltas)}
    for contact in sweep['contacts']:
     names=(contact['object_a'],contact['object_b']);detail=pair_details(current[names[0]],current[names[1]])
     row=rows.setdefault(names,{'object_a':names[0],'object_b':names[1], 'first_strict_crossing_progress':None,'last_strict_crossing_progress':None,'strict_crossing_samples':0,'maximum_triangle_plane_crossing_depth_m':0.,'representative_crossing_points_owner_m':[]})
     if detail['strict_crossing_pairs']:
      if row['first_strict_crossing_progress'] is None:row['first_strict_crossing_progress']=progress
      row['last_strict_crossing_progress']=progress;row['strict_crossing_samples']+=1
      if detail['maximum_triangle_plane_crossing_depth_m']>row['maximum_triangle_plane_crossing_depth_m']:
       row['maximum_triangle_plane_crossing_depth_m']=detail['maximum_triangle_plane_crossing_depth_m']
       row['representative_crossing_points_owner_m']=detail['crossing_points_owner_m'][:12]
   for row in rows.values():
    points=row['representative_crossing_points_owner_m']
    row['representative_crossing_points_blender_m']=[[v[0],-v[2],v[1]] if owner=='craft' else [v[0]+.97,.978-v[2],v[1]] for v in points]
   result['sweeps'].append({'owner':owner,'channel':sweep['channel'],'samples':sweep['samples'],'pairs':list(rows.values())})
   write_json(args.output/'crossing-analysis.json',result)
   print('CROSSING_ANALYSIS',sweep['channel'],sum(bool(r['strict_crossing_samples']) for r in rows.values()),flush=True)
   if owner=='craft' and sweep['channel']=='inner_door':
    for progress in (0.,1.):
     controller.apply('inner_door',progress)
     selected=[o for o in bpy.context.scene.objects if o.name.startswith('AFT01 | inner pressure leaf')]
     right=[];left=[]
     for o in selected:
      points=np.array([o.matrix_world@Vector(v) for v in o.bound_box]);x=points[:,0]
      if x.mean()>0:right.append(x.min())
      else:left.append(x.max())
     gap=min(right)-max(left)
     result['inner_opening'][str(progress)]={'central_panel_gap_m':float(gap),'width_reservation_m':.64}
 for key,path in [('station_reference_sha256',args.station),('wayfarer_sha256',args.craft),('station_closure_sha256',args.closure)]:
  if hashlib.sha256(path.read_bytes()).hexdigest()!=SOURCE_HASHES[key]:raise ValueError('Changed source')
 write_json(args.output/'crossing-analysis.json',result)
 print('CROSSING_ANALYSIS_COMPLETE',flush=True)


if __name__=='__main__':main()
