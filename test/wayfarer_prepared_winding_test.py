"""Invented synthetic geometry only; no project geometry or owner pipeline."""
from pathlib import Path
import importlib.util
import sys
from fractions import Fraction as F
import json
import unittest
from dataclasses import replace

DIR = Path(__file__).resolve().parent
ROOT = DIR.parents[1]
def load(path, name):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec); sys.modules[name]=m; spec.loader.exec_module(m)
    return m
fixtures=load(DIR/'wayfarer_prepared_winding_fixtures.py','prepared_invented_fixtures')
b=load(DIR.parent/'tools/wayfarer_fixed_winding.py','prepared_prototype')
w=fixtures.w
EXTRA=fixtures.EXTRA | {'prepared_accounting','phase','index_integrity_operations','prepared_identity_accounting'}

class PreparedTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tetra=fixtures.material(((0,0,0),(2,0,0),(0,2,0),(0,0,2)))
        cls.reverse=fixtures.material(((0,0,0),(2,0,0),(0,2,0),(0,0,2)), reverse=True)
        cls.skew=fixtures.material(((0,0,0),tuple(x/F(1024) for x in w.RAY),(1,0,0),(0,0,1)))
        cls.tiny=fixtures.material(((0,0,0),(F(1,2**120),0,0),(0,F(1,2**120),0),(0,0,F(1,2**120))))
        cls.cube=fixtures.grid_cube(6)
        cls.shuffled=fixtures.material(cls.cube.vertices, tuple(cls.cube.faces[(i*137)%432] for i in range(432)))
        cls.indices={id(h):b.prepare_classifier(w,h) for h in (cls.tetra,cls.reverse,cls.skew,cls.tiny,cls.cube,cls.shuffled)}
    def prep(self,h): return self.indices[id(h)]['prepared']
    def equal(self,h,p):
        old=w.classify_point(h,p)
        new=b.classify_prepared_point(w,h,self.prep(h),p)
        self.assertEqual({k:v for k,v in old.items() if k!='operations'}, {k:v for k,v in new.items() if k not in EXTRA})
        self.assertTrue(new['classification_scans_complete'])
        counts=new['prepared_accounting']
        self.assertEqual(counts['boundary_faces_pruned']+counts['boundary_candidates'],len(h.faces))
        if new['status']!='UNRESOLVED_POINT_ON_BOUNDARY':
            self.assertEqual(counts['ray_faces_pruned']+counts['ray_candidates'],len(h.faces))
        else: self.assertEqual(counts['ray_nodes_visited'],0)
        return new
    def test_tetra_grid_reversal_and_boundaries(self):
        for h in (self.tetra,self.reverse):
            for x in (F(-1),F(0),F(1,3),F(1),F(3)):
                for y in (F(0),F(1,3),F(1)):
                    for z in (F(0),F(1,3),F(1)): self.equal(h,(x,y,z))
    def test_edge_vertex_rays_and_coplanarity(self):
        for target in ((0,0,0),(1,0,0),(0,1,0),(1,1,0)):
            self.equal(self.tetra,tuple(F(x)-r/F(1024) for x,r in zip(target,w.RAY)))
        self.equal(self.skew,tuple(-r/F(1024) for r in w.RAY))
    def test_tiny_exact_rational_and_signed_zero(self):
        h=F(1,2**120)
        for p in ((h/4,h/4,h/4),(h,0,0),(-h,0,0)): self.equal(self.tiny,p)
        self.equal(self.tetra,(-0.0,0.0,0.0))
    def test_cube_and_shuffled_face_order(self):
        for h in (self.cube,self.shuffled):
            for p in ((F(1,7),F(1,9),F(1,11)),(3,3,3),(1,F(1,7),F(1,9)),(-1,-1,-1),(0,0,0)):
                self.equal(h,p)
            leaves=[x for row in self.prep(h).nodes for x in row[3]]
            self.assertEqual(sorted(leaves),list(range(432)))
            self.assertLessEqual(len(self.prep(h).nodes),b.MAX_NODES)
    def test_build_and_query_budget_thresholds(self):
        build=self.indices[id(self.tetra)]
        for limit in (0,1,12,13,build['operations']-1):
            r=b.prepare_classifier(w,self.tetra,operation_limit=limit)
            self.assertEqual(r['status'],'UNRESOLVED_OPERATION_LIMIT'); self.assertIsNone(r['prepared'])
            self.assertEqual(r['operations'],limit+1)
        self.assertIsNotNone(b.prepare_classifier(w,self.tetra,operation_limit=build['operations'])['prepared'])
        full=self.equal(self.tetra,(3,3,3))
        for limit in (0,1,full['operations']//2,full['operations']-1):
            r=b.classify_prepared_point(w,self.tetra,self.prep(self.tetra),(3,3,3),operation_limit=limit)
            self.assertEqual(r['status'],'UNRESOLVED_OPERATION_LIMIT'); self.assertFalse(r['membership_complete'])
            self.assertFalse(r['classification_scans_complete']); self.assertEqual(r['operations'],limit+1)
        self.assertEqual(b.classify_prepared_point(w,self.tetra,self.prep(self.tetra),(3,3,3),operation_limit=full['operations'])['status'],'STRICT_EXTERIOR')
    def test_hierarchy_sort_partition_and_scan_refusal_phases(self):
        full=self.indices[id(self.cube)]; phases=full['phase_operations']
        for limit,phase in ((phases['face_bounds']-1,'face_bounds'),
                            (phases['face_bounds']+100,'spatial_orders'),
                            (phases['face_bounds']+phases['spatial_orders']+100,'hierarchy')):
            r=b.prepare_classifier(w,self.cube,operation_limit=limit)
            self.assertIsNone(r['prepared']); self.assertEqual(r['status'],'UNRESOLVED_OPERATION_LIMIT')
            self.assertEqual(r['phase'],phase); self.assertEqual(r['operations'],limit+1)
        full_query=b.classify_prepared_point(w,self.cube,self.prep(self.cube),(3,3,3))
        for limit in (0,len(self.cube.faces),full_query['operations']-1):
            r=b.classify_prepared_point(w,self.cube,self.prep(self.cube),(3,3,3),operation_limit=limit)
            self.assertEqual(r['status'],'UNRESOLVED_OPERATION_LIMIT'); self.assertFalse(r['classification_scans_complete'])
            self.assertEqual(r['operations'],limit+1)
    def test_complete_original_candidate_order_known_rosters(self):
        result=self.equal(self.tetra,(0,0,0))
        self.assertEqual(result['boundary_faces'],(0,1,2))
        result=self.equal(self.tetra,tuple(-r/F(1024) for r in w.RAY))
        self.assertEqual([event['face'] for event in result['events']], sorted(event['face'] for event in result['events']))
        self.assertEqual([hit['face'] for hit in result['strict_hits']], sorted(hit['face'] for hit in result['strict_hits']))
    def test_wrong_equal_handle_foreign_and_malformed(self):
        equivalent=fixtures.material(self.tetra.vertices,self.tetra.faces)
        with self.assertRaisesRegex(ValueError,'Exact selected'): b.classify_prepared_point(w,equivalent,self.prep(self.tetra),(3,3,3))
        for point in ((True,0,0),(float('nan'),0,0),(float('inf'),0,0),(129,0,0),(0,0)):
            with self.assertRaises(w.e.InvalidGeometry): b.classify_prepared_point(w,self.tetra,self.prep(self.tetra),point)
        for limit in (True,-1,16_000_001,1.0):
            with self.assertRaises(w.e.InvalidGeometry): b.prepare_classifier(w,self.tetra,operation_limit=limit)
        for field,value in (('nodes',()),('nodes',(self.prep(self.tetra).nodes[0],)*1024),('bounds',()),('index_sha256','0'*64),('token',object())):
            damaged=object.__new__(b.PreparedClassifier)
            for key,val in vars(self.prep(self.tetra)).items(): object.__setattr__(damaged,key,val)
            object.__setattr__(damaged,field,value)
            with self.assertRaises(ValueError): b.classify_prepared_point(w,self.tetra,damaged,(3,3,3))
    def test_index_encoding_guards_before_serialization(self):
        p=self.prep(self.tetra)
        for bounds,nodes in (((),p.nodes),(p.bounds,()),(p.bounds,(p.nodes[0],)*1024),(((True,)*6,)*4,p.nodes)):
            with self.assertRaises(ValueError): b.index_digest(self.tetra,bounds,nodes,{})
    def test_legal_max_bits_and_binary32_style(self):
        for h in (F(1,2**255), F(1,2**149), F(2**255-1,2**255)):
            handle=fixtures.material(((0,0,0),(h,0,0),(0,h,0),(0,0,h)))
            prepared=b.prepare_classifier(w,handle)['prepared']
            for point in ((h/4,h/4,h/4),(h,0,0),(-h,0,0)):
                # h/4 can exceed256 inputbits for the smallest legal h;
                # use an exact vertex or legal exterior in that case.
                if any(max(F(x).numerator.bit_length(), F(x).denominator.bit_length())>256 for x in point): continue
                old=w.classify_point(handle,point); new=b.classify_prepared_point(w,handle,prepared,point)
                self.assertEqual({k:v for k,v in old.items() if k!='operations'}, {k:v for k,v in new.items() if k not in EXTRA})
    def test_full256vertex512face_domain(self):
        def diamond(n):
            corners=((1,0),(0,1),(-1,0),(0,-1))
            return [tuple(F(corners[edge][a])+(F(corners[(edge+1)%4][a])-corners[edge][a])*F(i,n) for a in range(2)) for edge in range(4) for i in range(n)]
        us=diamond(2); vs=diamond(8)
        vertices=[((3+x)*u,(3+x)*v,z) for u,v in us for x,z in vs]
        faces=[]
        for i in range(8):
            for j in range(32):
                a=i*32+j; c=((i+1)%8)*32+j; d=((i+1)%8)*32+(j+1)%32; e=i*32+(j+1)%32
                faces.extend(((a,c,d),(a,d,e)))
        handle=fixtures.material(vertices,faces)
        prepared=b.prepare_classifier(w,handle)
        self.assertEqual((len(handle.vertices),len(handle.faces)),(256,512))
        self.assertLessEqual(prepared['identity_accounting']['encoded_bytes'],b.MAX_INDEX_BYTES)
        for point in ((6,6,6),(3,0,0)):
            old=w.classify_point(handle,point); new=b.classify_prepared_point(w,handle,prepared['prepared'],point)
            self.assertEqual({k:v for k,v in old.items() if k!='operations'}, {k:v for k,v in new.items() if k not in EXTRA})
    def test_foreign_same_bytes_winding_instance(self):
        other=fixtures.load(fixtures.WINDING_PATH,'prepared_foreign_winding')
        with self.assertRaises(other.e.InvalidGeometry):
            b.classify_prepared_point(other,self.tetra,self.prep(self.tetra),(3,3,3))
    def test_frozen_constructor_no_caller_created_index(self):
        with self.assertRaises(TypeError): b.PreparedClassifier(w,self.tetra,(),(),'0'*64,object(),())
        from dataclasses import FrozenInstanceError
        with self.assertRaises(FrozenInstanceError): self.prep(self.tetra).nodes=()
    def test_flat_api_unchanged(self):
        for p in ((F(1,4),)*3,(3,3,3),(0,0,0)):
            old=fixtures.b.classify_point(w,self.tetra,p); new=b.classify_point(w,self.tetra,p)
            self.assertEqual({k:v for k,v in old.items() if k!='helper_sha256'}, {k:v for k,v in new.items() if k!='helper_sha256'})
    def test_mutation_during_query_is_refused(self):
        # All mutations occur in invented callback control; original source method restored.
        p=b.prepare_classifier(w,self.tetra)['prepared']; old=b.point_box
        def changed(box,q,work):
            object.__setattr__(p,'nodes',tuple(list(p.nodes)))
            return old(box,q,work)
        b.point_box=changed
        try:
            with self.assertRaisesRegex(ValueError,'during query'): b.classify_prepared_point(w,self.tetra,p,(3,3,3))
        finally: b.point_box=old
    def test_shared_remainder_accounting(self):
        remaining=16_000_000
        build=b.prepare_classifier(w,self.tetra,operation_limit=remaining); remaining-=build['operations']
        for p in ((3,3,3),(F(1,4),)*3):
            r=b.classify_prepared_point(w,self.tetra,build['prepared'],p,operation_limit=remaining)
            self.assertTrue(r['classification_scans_complete']); remaining-=r['operations']
        self.assertGreater(remaining,0)

if __name__=='__main__': unittest.main(verbosity=2)
