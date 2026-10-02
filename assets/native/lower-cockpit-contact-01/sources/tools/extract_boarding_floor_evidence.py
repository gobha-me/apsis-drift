#!/usr/bin/env python3
"""Read-only corrected-source floor/rim audit and separately versioned halo proposal."""
import argparse,hashlib,json,sys
from pathlib import Path
import bpy,numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def intersects(tri,lo,hi):return np.all(tri.max(axis=1)>=lo,axis=1)&np.all(tri.min(axis=1)<=hi,axis=1)
def bounds(v):return [v.min(axis=0).tolist(),v.max(axis=0).tolist()]
def intervals(ids):
 result=[]
 for i in ids:
  i=int(i)
  if result and result[-1][0]+result[-1][1]==i:result[-1][1]+=1
  else:result.append([i,1])
 return result

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 for key in ['engine-root','craft','station','support','output']:parser.add_argument('--'+key,type=Path,required=True)
 args=parser.parse_args(sys.argv[sys.argv.index('--')+1:]);engine=args.engine_root.resolve()
 if args.output.exists():raise ValueError('Use a fresh evidence output directory')
 contact_path=engine/'assets/native/wayfarer-operating-02/metadata/contact.json';contact=json.loads(contact_path.read_text());support=json.loads((args.support/'support.json').read_text())
 provenance=json.loads((args.support/'provenance.json').read_text())
 for name,digest in provenance['engine_bindings_sha256'].items():assert sha(engine/name)==digest,name
 assert sha(contact_path)==support['contact_sha256']
 assert sha(args.craft)==contact['sources']['wayfarer_sha256'] and sha(args.station)==contact['sources']['station_reference_sha256']
 sys.path.insert(0,str(engine/'tools'))
 import export_boarding_contact as src
 from wayfarer_operating_blender import OperatingPoseController
 from wayfarer_operating_spec import classify_motion_group
 from station_clearance_blender import apply_station_clearance
 records={(r['owner'],r['source_object']):r for r in support['objects']};groups={g['id']:g for g in contact['groups']}
 args.output.mkdir(parents=True)
 bindings={str(p.relative_to(engine)):sha(p) for p in [contact_path,engine/'tools/export_boarding_contact.py',engine/'tools/wayfarer_operating_blender.py',engine/'tools/wayfarer_operating_spec.py',engine/'tools/station_clearance_blender.py']}
 halo_lo=np.array([-1.05,-.85,-3.5]);halo_hi=np.array([1.05,-.20,-.50])
 vertices=[];faces=[];lookup={};owners=[];audit=[];probe_sets={};corrections=None
 for owner,path in [('craft',args.craft),('station',args.station)]:
  if owner=='craft':controller=OperatingPoseController.prepare(path).freeze();corrections=controller.derivative_corrections
  else:bpy.ops.wm.open_mainfile(filepath=str(path));bpy.context.scene.frame_set(1);bpy.context.view_layer.update();apply_station_clearance()
  original_lo,original_hi=np.array(src.CROPS[owner]);trees=[];interesting=[]
  region_lo=np.array([-1.12,-1.2,-3.55] if owner=='craft' else [-24.,-.5,-1.4]);region_hi=np.array([1.12,.35,4.30] if owner=='craft' else [-21.7,.4,1.4])
  for obj in sorted(bpy.context.scene.objects,key=lambda o:o.name):
   if not (src.craft_keep(obj) if owner=='craft' else src.station_keep(obj)):continue
   # A bounding-box rejection avoids evaluating distant high-detail hardware.
   corners=np.array([obj.matrix_world@Vector(c) for c in obj.bound_box]);corners=corners[:,[0,2,1]]*[1,1,-1]
   if owner=='station':corners+=np.array([-.97,0,.978])
   if np.any(corners.max(0)<region_lo) or np.any(corners.min(0)>region_hi):continue
   v,f=src.mesh_data(obj,owner)
   if not len(f):continue
   tri=v[f];near=intersects(tri,region_lo,region_hi)
   if not np.any(near):continue
   moving=classify_motion_group(src.ancestors(obj)) if owner=='craft' else None
   crop=np.ones(len(f),dtype=bool) if moving else intersects(tri,original_lo,original_hi)
   row={'owner':owner,'source_object':obj.name,'source_evaluated_triangles':len(f),'crop_retained_triangles':int(crop.sum()),'bounds_corrected_rest_m':bounds(tri.reshape(-1,3)),'source_corrected_world_rows':[list(r) for r in obj.matrix_world],'motion_group':moving}
   old=records.get((owner,obj.name))
   if old:
    # Verify all prior per-object kept triangle coordinates in their original order.
    g=groups[old['group']];qv=np.asarray(g['vertices_micrometres'],dtype=np.int64);qf=np.asarray(g['triangles'][old['triangle_start']:old['triangle_start']+old['triangle_count']],dtype=np.int64)
    actual=np.rint(tri[crop]*1e6).astype(np.int64);valid=np.array([len({tuple(p) for p in t})==3 for t in actual]);actual=actual[valid]
    assert np.array_equal(actual,qv[qf]),(owner,obj.name,'admitted mismatch')
    row['admitted_range']={'group':old['group'],'triangle_start':old['triangle_start'],'triangle_count':old['triangle_count'],'exact_coordinate_match':True}
   else:row['admitted_range']=None
   # Full evaluated source triangles, no old vertical crop, in an isolated query BVH.
   ids=np.flatnonzero(near);tr=BVHTree.FromPolygons(v.tolist(),f[ids].tolist(),all_triangles=True)
   trees.append((obj.name,tr,ids))
   focus=any(w in obj.name.lower() for w in ['pressure tub','transition','walking tile','pressure floor','ladder','landing','deck','hatch','rim'])
   if focus:
    row['evaluated_source_triangle_indices_in_region']=intervals(ids);interesting.append(row)
   if owner=='craft' and moving is None:
    extra=intersects(tri,halo_lo,halo_hi)&~crop
    ids_extra=np.flatnonzero(extra)
    if len(ids_extra):
     start=len(faces);source_ids=[]
     for i in ids_extra:
      ids_out=[]
      for p in tri[i]:
       key=tuple(int(round(float(x)*1000000)) for x in p)
       if key not in lookup:lookup[key]=len(vertices);vertices.append(key)
       ids_out.append(lookup[key])
      if len(set(ids_out))==3:faces.append(ids_out);source_ids.append(int(i))
     if len(faces)>start:
      owners.append({**row,'halo_triangle_start':start,'halo_triangle_count':len(faces)-start,'evaluated_source_triangle_indices':source_ids,'halo_bounds_rest_m':bounds(tri[source_ids].reshape(-1,3))})
  def cast(a,b):
   a,b=Vector(a),Vector(b);direction=b-a;hits=[]
   for name,tr,ids in trees:
    p,n,index,distance=tr.ray_cast(a,direction.normalized(),direction.length)
    if p is not None:hits.append({'source_object':name,'source_triangle':int(ids[index]),'point_m':list(p),'winding_normal':list(n),'distance_m':distance})
   return sorted(hits,key=lambda h:h['distance_m'])
  if owner=='craft':
   grid=[]
   for x in [-.48,-.32,-.16,0,.16,.32,.48]:
    for k in range(321):
     z=-.40-k*.01;hits=cast([x,.15,z],[x,-1.0,z]);grid.append({'x_m':x,'z_m':z,'hits':hits})
   seams=[]
   for k in range(7):
    z=3.6475-k*.595
    for delta in [-.009,-.007,0,.007,.009]:seams.append({'nominal_seam_z_m':z,'offset_m':delta,'hits':cast([0,.05,z+delta],[0,-.5,z+delta])})
   probe_sets['cockpit_and_step_grid']=grid;probe_sets['cabin_tile_seams']=seams
  else:
   grid=[]
   for i in range(45):
    x=-23.90+i*.05
    for j in range(45):
     z=-1.1+j*.05;grid.append({'x_m':x,'z_m':z,'hits':cast([x,.30,z],[x,-.5,z])})
   probe_sets['station_top_landing_grid']=grid
  audit.extend(interesting)
  print('FLOOR_AUDIT',owner,'trees',len(trees),'objects',len(interesting),flush=True)
 halo={'schema_version':1,'id':'wayfarer-cockpit-contact-halo-01','status':'Exact-source additive proposal; not admitted contact or support permission','contact_sha256':sha(contact_path),'sources':contact['sources'],'coordinate_contract':contact['coordinate_contracts']['craft'],'quantization_metres':1e-6,'query_bounds_rest_m':[halo_lo.tolist(),halo_hi.tolist()],'selection':'Static source triangles intersecting this bounded box and excluded by the existing craft contact crop; complete triangles retained, no clipping/filler/remeshing.','vertices_micrometres':vertices,'triangles':faces,'objects':owners,'derivative_corrections_baked_once':corrections,'motion_contract':'Source-rest craft coordinates. Apply only the existing craft root/world placement; this static halo has no extra local correction or moving group.','policy':'All proposed triangles are obstacles. No new boot/grasp permissions, missing floor replacement, posture/step acceptance or standing-policy relaxation.'}
 write(args.output/'halo.json',halo)
 report={'schema_version':1,'id':'wayfarer-floor-evidence-01','status':'Bounded read-only source evidence; Engine owns qualification','source_hashes':contact['sources'],'contact_sha256':sha(contact_path),'support_sha256':sha(args.support/'support.json'),'engine_bindings_sha256':bindings,'extractor_sha256':sha(Path(__file__)),'coordinate_contracts':contact['coordinate_contracts'],'source_objects':audit,'probes':probe_sets,'halo_sha256':sha(args.output/'halo.json'),'counts':{'halo_vertices':len(vertices),'halo_triangles':len(faces),'halo_objects':len(owners)},'limits':['Nearest vertical hits are source surfaces, not accepted walking supports. Winding normal is not a permission or reach result.','All probes use corrected rest geometry. Station moving deck hatches and craft seat are in admitted source-rest pose; deployed placement/motion remains caller-owned.','Sampling can reveal gaps but does not prove continuous support, foot shape, actor posture or route clearance.','No source master, admitted operating/motion/contact/support artifact modified.']}
 write(args.output/'evidence.json',report)
 assert sha(args.craft)==contact['sources']['wayfarer_sha256'] and sha(args.station)==contact['sources']['station_reference_sha256']
 print('FLOOR_EVIDENCE',report['counts'],flush=True)
if __name__=='__main__':main()
