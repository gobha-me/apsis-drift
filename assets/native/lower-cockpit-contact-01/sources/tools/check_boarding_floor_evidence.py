#!/usr/bin/env python3
"""Validate an additive exact-source halo without weakening admitted contact."""
import argparse,hashlib,json,math
from pathlib import Path
import numpy as np

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def check(halo,evidence,contact):
 assert halo['schema_version']==evidence['schema_version']==contact['schema_version']==1
 assert halo['sources']==evidence['source_hashes']==contact['sources']
 assert halo['quantization_metres']==contact['quantization_metres']==1e-6
 v=np.asarray(halo['vertices_micrometres']);f=np.asarray(halo['triangles'])
 assert v.ndim==f.ndim==2 and v.shape[1]==f.shape[1]==3 and v.dtype.kind in 'iu' and f.dtype.kind in 'iu'
 assert len(v)>0 and len(f)>0 and f.min()>=0 and f.max()<len(v) and np.abs(v).max()<2**31
 tri=v[f].astype(float)*1e-6;lo,hi=np.asarray(halo['query_bounds_rest_m']);assert np.isfinite(lo).all() and np.isfinite(hi).all() and np.all(hi>lo)
 assert np.all((tri.max(1)>=lo-1e-6)&(tri.min(1)<=hi+1e-6))
 crop_lo,crop_hi=np.asarray(contact['crop_bounds_metres']['craft']);inside=np.all(tri.max(1)>=crop_lo,1)&np.all(tri.min(1)<=crop_hi,1)
 assert not inside.any(),'Proposed halo includes a triangle admitted by the old crop'
 count=0;names=[]
 for row in halo['objects']:
  assert row['owner']=='craft' and row['motion_group'] is None
  assert row['halo_triangle_start']==count and row['halo_triangle_count']>0
  ids=row['evaluated_source_triangle_indices'];assert len(ids)==row['halo_triangle_count'] and ids==sorted(set(ids))
  assert ids[0]>=0 and ids[-1]<row['source_evaluated_triangles']
  sub=tri[count:count+len(ids)];assert len(sub)==len(ids)
  bb=np.array([sub.min((0,1)),sub.max((0,1))]);assert np.max(np.abs(bb-np.asarray(row['halo_bounds_rest_m'])))<=.000001
  m=np.asarray(row['source_corrected_world_rows']);assert m.shape==(4,4) and np.isfinite(m).all()
  count+=len(ids);names.append(row['source_object'])
 assert count==len(f) and len(set(names))==len(names)
 assert evidence['counts']=={'halo_vertices':len(v),'halo_triangles':len(f),'halo_objects':len(names)}
 for probes in evidence['probes'].values():
  for probe in probes:
   hits=probe['hits'];assert [h['distance_m'] for h in hits]==sorted(h['distance_m'] for h in hits)
   for hit in hits:
    assert hit['source_triangle']>=0 and all(math.isfinite(x) for x in hit['point_m']+hit['winding_normal']) and math.isfinite(hit['distance_m']) and hit['distance_m']>=0
 cross=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]);degenerate=int((np.linalg.norm(cross,axis=1)<=1e-12).sum())
 return {'vertices':len(v),'triangles':len(f),'objects':len(names),'quantized_zero_area_triangles':degenerate,'bounds_rest_m':[tri.min((0,1)).tolist(),tri.max((0,1)).tolist()],'old_crop_overlap_triangles':int(inside.sum())}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--engine-root',type=Path,required=True);p.add_argument('--package',type=Path,required=True);a=p.parse_args()
 e=json.loads((a.package/'evidence.json').read_text());h=json.loads((a.package/'halo.json').read_text());cp=a.engine_root/'assets/native/wayfarer-operating-02/metadata/contact.json';c=json.loads(cp.read_text())
 assert sha(cp)==e['contact_sha256']==h['contact_sha256'];assert sha(a.package/'halo.json')==e['halo_sha256']
 for path,digest in e['engine_bindings_sha256'].items():assert sha(a.engine_root/path)==digest,path
 r=check(h,e,c)
 import copy
 refused=[]
 for name in ['bad-index','range-gap','moving-owner','nonfinite-transform','invalid-query-box']:
  bad=copy.deepcopy(h)
  if name=='bad-index':bad['triangles'][0][0]=len(bad['vertices_micrometres'])
  elif name=='range-gap':bad['objects'][0]['halo_triangle_start']=1
  elif name=='moving-owner':bad['objects'][0]['motion_group']='seat_carriage'
  elif name=='nonfinite-transform':bad['objects'][0]['source_corrected_world_rows'][0][0]=float('nan')
  else:bad['query_bounds_rest_m'][1][0]=bad['query_bounds_rest_m'][0][0]
  try:check(bad,e,c)
  except (AssertionError,IndexError,ValueError):refused.append(name)
  else:raise AssertionError('Negative accepted: '+name)
 r.update({'schema_version':1,'passed':True,'halo_sha256':sha(a.package/'halo.json'),'evidence_sha256':sha(a.package/'evidence.json'),'checker_sha256':sha(Path(__file__)),'negative_fixtures_refused':refused,'limits':'Independent buffer/range/finite/crop checks against admitted contact. Exact source-coordinate reproduction is recorded by the Blender extractor; no collision/support admission or missing-floor repair.'})
 (a.package/'checks.json').write_text(json.dumps(r,indent=2,allow_nan=False)+'\n');print(json.dumps(r,indent=2))
if __name__=='__main__':main()
