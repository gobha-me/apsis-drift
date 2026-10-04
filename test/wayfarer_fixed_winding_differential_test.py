"""Invented fixtures only; no project geometry or buffer access."""
from pathlib import Path
from fractions import Fraction as F
import hashlib
import importlib.util
import sys
import types
import unittest

DIR = Path(__file__).resolve().parent

def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); sys.modules[name] = module
    spec.loader.exec_module(module)
    return module

b = load(DIR.parent / 'tools/wayfarer_fixed_winding.py', 'invented_fixed_broadphase')
WINDING_PATH = DIR / 'fixtures/wayfarer_fixed_winding/stowed-winding-containment-proof-preparation/winding_containment_certificate.py'
w = load(WINDING_PATH, 'invented_original_winding')
EXTRA = {'operations', 'operation_limit', 'broadphase_accounting', 'classification_scans_complete',
         'helper_sha256', 'winding_sha256', 'embedding_sha256', 'factory_calls', 'seed_calls', 'source_authority'}
FACES = ((0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3))


def material(vertices, faces=FACES, reverse=False):
    v = tuple(tuple(F(x) for x in p) for p in vertices)
    f = tuple(tuple(reversed(t)) if reverse else tuple(t) for t in faces)
    data = w.canonical(w.numeric_doc(v, f, 'selected_static_bridge'))
    result = w.certify_closed_oriented_chain(data, hashlib.sha256(data).hexdigest())
    assert type(result) is w.SelectedMaterial
    return result


def grid_cube(n):
    vertices = []; faces = []; ids = {}
    def index(point):
        if point not in ids:
            ids[point] = len(vertices); vertices.append(point)
        return ids[point]
    for axis in range(3):
        u, v = (axis + 1) % 3, (axis + 2) % 3
        for sign in (-1, 1):
            for i in range(n):
                for j in range(n):
                    quad = []
                    for a, c in ((i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)):
                        p = [F(0)] * 3; p[axis] = F(sign); p[u] = F(-1) + F(2*a, n); p[v] = F(-1) + F(2*c, n)
                        quad.append(index(tuple(p)))
                    for t in ((quad[0], quad[1], quad[2]), (quad[0], quad[2], quad[3])):
                        faces.append(t if sign == 1 else tuple(reversed(t)))
    return material(vertices, faces)


class BroadphaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tetra = material(((0, 0, 0), (2, 0, 0), (0, 2, 0), (0, 0, 2)))
        cls.reverse = material(((0, 0, 0), (2, 0, 0), (0, 2, 0), (0, 0, 2)), reverse=True)
        cls.cube = grid_cube(6)
        cls.skew = material(((0, 0, 0), tuple(x/F(1024) for x in w.RAY), (1, 0, 0), (0, 0, 1)))
        cls.tiny = material(((0, 0, 0), (F(1, 2**120), 0, 0), (0, F(1, 2**120), 0), (0, 0, F(1, 2**120))))

    def equal(self, handle, point, status=None):
        old = w.classify_point(handle, point)
        new = b.classify_point(w, handle, point)
        self.assertEqual({k:v for k,v in old.items() if k != 'operations'}, {k: v for k, v in new.items() if k not in EXTRA})
        if status is not None:
            self.assertEqual(new['status'], status)
        self.assertTrue(new['classification_scans_complete'])
        self.assertEqual(new['factory_calls'], 0); self.assertEqual(new['seed_calls'], 0)
        c = new['broadphase_accounting']
        self.assertEqual(c['box_rows_completed'], len(handle.faces))
        self.assertEqual(c['boundary_faces_visited'], len(handle.faces))
        self.assertEqual(c['boundary_candidates'] + c['boundary_box_pruned'], len(handle.faces))
        self.assertEqual(c['ray_faces_visited'], 0 if new['status'] == 'UNRESOLVED_POINT_ON_BOUNDARY' else len(handle.faces))
        return new

    def test_known_inside_outside_and_reversed(self):
        for handle in (self.tetra, self.reverse):
            inside = self.equal(handle, (F(1,4),)*3, 'STRICT_INTERIOR')
            self.assertEqual(inside['oriented_winding'], -1 if handle is self.reverse else 1)
            self.equal(handle, (3,3,3), 'STRICT_EXTERIOR')

    def test_all_face_edge_vertex_boundary_orders(self):
        for point in ((0,0,0), (2,0,0), (0,2,0), (0,0,2), (1,1,0), (F(1,2), F(1,2), 1), (F(1,2), F(1,2), 0)):
            self.equal(self.tetra, point, 'UNRESOLVED_POINT_ON_BOUNDARY')

    def test_outside_box_ray_vertex_and_edge_remain_ambiguous(self):
        for target in ((0,0,0), (1,0,0), (0,1,0), (1,1,0)):
            point = tuple(F(x)-r/F(1024) for x,r in zip(target,w.RAY))
            self.equal(self.tetra, point, 'UNRESOLVED_AMBIGUOUS_FIXED_RAY')

    def test_coplanar_ray_interval(self):
        point = tuple(-r/F(1024) for r in w.RAY)
        result = self.equal(self.skew, point, 'UNRESOLVED_AMBIGUOUS_FIXED_RAY')
        self.assertTrue(any(e['kind']=='finite_coplanar_ray' for e in result['events']))

    def test_finite_box_false_positive_uses_narrow(self):
        self.equal(self.tetra, (F(3,2), F(3,2), 0), 'STRICT_EXTERIOR')

    def test_positive_singleton_joint_miss_behind_and_zero_slabs(self):
        work = w.e.Work()
        # Box is a single point exactly along the forward fixed ray.
        point = tuple(-r/F(1024) for r in w.RAY)
        self.assertTrue(b.positive_ray_box(((F(0),F(0)),)*3, point, work))
        self.assertFalse(b.positive_ray_box(((F(0),F(0)),)*3, (F(0),)*3, work))
        self.assertFalse(b.positive_ray_box(((F(0),F(0)),)*3, tuple(r/F(1024) for r in w.RAY), work))
        # Every slab individually admits t>0, but they have no common t.
        box = ((F(137),F(274)), (F(753),F(1004)), (F(509),F(1018)))
        self.assertFalse(b.positive_ray_box(box, (F(0),)*3, work))
        self.assertEqual(work.operations, 4*22)

    def test_tiny_rational_equality_and_near_boundary(self):
        h=F(1,2**120)
        self.equal(self.tiny, (h/F(4),)*3, 'STRICT_INTERIOR')
        self.equal(self.tiny, (h,0,0), 'UNRESOLVED_POINT_ON_BOUNDARY')
        self.equal(self.tetra, (F(1,2**149),)*3, 'STRICT_INTERIOR')
        self.equal(self.tetra, (-0.0,0.0,0.0), 'UNRESOLVED_POINT_ON_BOUNDARY')

    def test_432_face_case_known_results_and_work_reduction(self):
        self.assertEqual((len(self.cube.vertices),len(self.cube.faces)),(218,432))
        for point,status in (((F(1,7),F(1,9),F(1,11)), 'STRICT_INTERIOR'), ((3,3,3),'STRICT_EXTERIOR'), ((1,F(1,7),F(1,9)),'UNRESOLVED_POINT_ON_BOUNDARY')):
            new=self.equal(self.cube,point,status)
            old=w.classify_point(self.cube,point)
            self.assertLess(new['operations'],old['operations'])

    def test_differential_fixed_grid_of_125_points(self):
        for x in (F(-1),F(0),F(1,3),F(1),F(3)):
            for y in (F(-1),F(0),F(1,3),F(1),F(3)):
                for z in (F(-1),F(0),F(1,3),F(1),F(3)):
                    self.equal(self.tetra,(x,y,z))

    def test_budgets_bounds_boundary_slab_and_completed_threshold(self):
        full=b.classify_point(w,self.tetra,(3,3,3))
        for limit in (0,1,12,13,51,52,59,83,full['operations']-1):
            result=b.classify_point(w,self.tetra,(3,3,3),operation_limit=limit)
            self.assertEqual(result['status'],'UNRESOLVED_OPERATION_LIMIT')
            self.assertFalse(result['membership_complete']); self.assertFalse(result['classification_scans_complete'])
            self.assertEqual(result['operations'],limit+1)
        result=b.classify_point(w,self.tetra,(3,3,3),operation_limit=full['operations'])
        self.assertEqual(result['status'],'STRICT_EXTERIOR')

    def test_invalid_point_and_budget_domains(self):
        for point in ((float('nan'),0,0),(float('inf'),0,0),(True,0,0),(0,0),(129,0,0),(F(1,2**256),0,0)):
            with self.assertRaises(w.e.InvalidGeometry): b.classify_point(w,self.tetra,point)
        for limit in (True,-1,16_000_001,1.0):
            with self.assertRaises(w.e.InvalidGeometry): b.classify_point(w,self.tetra,(0,0,0),operation_limit=limit)

    def test_foreign_instance_and_lookalike_callbacks_refused(self):
        foreign=load(WINDING_PATH,'invented_foreign_winding')
        with self.assertRaises(w.e.InvalidGeometry): b.classify_point(w, material_foreign(foreign), (0,0,0))
        fake=types.ModuleType('fake'); fake.__dict__.update(vars(w))
        with self.assertRaises(ValueError): b.classify_point(fake,self.tetra,(0,0,0))
        original=w.face_values
        try:
            w.face_values=lambda *args: original(*args)
            with self.assertRaises(ValueError): b.classify_point(w,self.tetra,(0,0,0))
        finally: w.face_values=original

    def test_tampered_degenerate_handle_refuses_before_filter(self):
        vertices=self.tetra.vertices
        try:
            object.__setattr__(self.tetra,'vertices',((F(0),)*3,)*4)
            with self.assertRaises(w.e.InvalidGeometry): b.classify_point(w,self.tetra,(0,0,0))
        finally: object.__setattr__(self.tetra,'vertices',vertices)


    def test_actual_embedded_qualification_kind_invented_tetra(self):
        data=w.canonical(w.numeric_doc(self.tetra.vertices,self.tetra.faces,'selected_static_bridge'))
        handle=w.certify_selected_material(data,hashlib.sha256(data).hexdigest())
        self.assertEqual(handle.qualification_kind,'embedded_bridge')
        self.equal(handle,(F(1,4),)*3,'STRICT_INTERIOR')
        self.equal(handle,(3,3,3),'STRICT_EXTERIOR')

    def test_nonconvex_closed_L_prism_known_interior_notch(self):
        polygon=((0,0),(2,0),(2,1),(1,1),(1,2),(0,2))
        vertices=[(x,y,z) for z in (0,1) for x,y in polygon]
        caps=((0,1,3),(1,2,3),(0,3,5),(3,4,5))
        faces=[tuple(reversed(t)) for t in caps]+[tuple(i+6 for i in t) for t in caps]
        for i in range(6):
            j=(i+1)%6;faces.extend(((i,j,j+6),(i,j+6,i+6)))
        handle=material(vertices,faces)
        self.equal(handle,(F(3,2),F(1,2),F(1,2)),'STRICT_INTERIOR')
        self.equal(handle,(F(1,2),F(3,2),F(1,2)),'STRICT_INTERIOR')
        self.equal(handle,(F(3,2),F(3,2),F(1,2)),'STRICT_EXTERIOR')

    def test_opposite_chain_cancellation_ordered_hits(self):
        vertices=self.tetra.vertices+self.tetra.vertices
        faces=self.tetra.faces+tuple(tuple(i+4 for i in reversed(t)) for t in self.tetra.faces)
        handle=material(vertices,faces)
        result=self.equal(handle,(F(1,4),)*3,'STRICT_EXTERIOR')
        self.assertEqual(result['oriented_winding'],0)
        self.assertEqual([hit['face'] for hit in result['strict_hits']],[3,7])

    def test_max_input_rational_and_smallest_legal_positive(self):
        self.equal(self.tetra,(F(2**254-1,2**255),F(1,4),F(1,4)),'STRICT_INTERIOR')
        self.equal(self.tetra,(F(1,2**255),F(1,4),F(1,4)),'STRICT_INTERIOR')

    def test_genuine_degenerate_and_malformed_chain_refuse(self):
        for vertices,faces in ((((F(0),)*3,)*4,FACES),(self.tetra.vertices,((0,1,True),)+FACES[1:]),(self.tetra.vertices,((0,1,4),)+FACES[1:])):
            data=w.canonical(w.numeric_doc(vertices,faces,'selected_static_bridge'))
            with self.assertRaises(w.e.InvalidGeometry):
                w.certify_closed_oriented_chain(data,hashlib.sha256(data).hexdigest())

    def test_exact_filter_charges_and_boundary_gate(self):
        work=w.e.Work();box=b.face_box(self.tetra.vertices[:3],work)
        self.assertEqual(work.operations,13)
        self.assertTrue(b.point_box(box,(F(0),)*3,work));self.assertEqual(work.operations,20)
        self.assertFalse(b.point_box(box,(F(3),)*3,work));self.assertEqual(work.operations,27)
        b.positive_ray_box(box,(F(-1),)*3,work);self.assertEqual(work.operations,49)
        result=b.classify_point(w,self.tetra,(0,0,0))
        self.assertEqual(result['broadphase_accounting']['ray_faces_visited'],0)


def material_foreign(module):
    v=((F(0),)*3,(F(2),F(0),F(0)),(F(0),F(2),F(0)),(F(0),F(0),F(2)))
    data=module.canonical(module.numeric_doc(v,FACES,'selected_static_bridge'))
    return module.certify_closed_oriented_chain(data,hashlib.sha256(data).hexdigest())

if __name__ == '__main__': unittest.main()
