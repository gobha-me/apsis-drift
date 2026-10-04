"""Independent invented controls for a single charged spatial ordering."""
from fractions import Fraction as F
import unittest
import wayfarer_prepared_winding_test as inherited

b, w, fixtures = inherited.b, inherited.w, inherited.fixtures


class SingleOrderTests(unittest.TestCase):
    def test_one_sort_exact_dominant_axis_and_contiguous_leaf_ranges(self):
        cube = fixtures.grid_cube(3)
        # Equal extents choose X; each distinct dominant axis uses the same
        # independent midpoint oracle. Reverse input order supplies spatial ties.
        for scale in ((1, 1, 1), (5, 1, 1), (1, 5, 1), (1, 1, 5)):
            h = fixtures.material(tuple(tuple(p[a]*scale[a] for a in range(3)) for p in cube.vertices),
                                  tuple(reversed(cube.faces)))
            axis = max(range(3), key=lambda a: (scale[a], -a))
            expected = tuple(sorted(range(len(h.faces)), key=lambda i:
                (min(h.vertices[v][axis] for v in h.faces[i]) +
                 max(h.vertices[v][axis] for v in h.faces[i]), i)))
            calls = []; original = b.spatial_order
            def watched(keys, faces, work):
                calls.append((keys, faces)); return original(keys, faces, work)
            b.spatial_order = watched
            try: result = b.prepare_classifier(w, h)
            finally: b.spatial_order = original
            self.assertEqual(len(calls), 1)
            p = result['prepared']
            self.assertEqual(tuple(i for node in p.nodes for i in node[3]), expected)
            def range_of(index):
                node = p.nodes[index]
                if node[3]:
                    self.assertLessEqual(len(node[3]), 8)
                    return node[3]
                left, right = range_of(node[1]), range_of(node[2])
                self.assertLessEqual(abs(len(left)-len(right)), 1)
                self.assertEqual(node[4], len(left)+len(right))
                return left+right
            self.assertEqual(range_of(0), expected)

    def test_sort_not_entered_for_single_leaf_and_no_handle_after_sort_refusal(self):
        h = fixtures.material(((0, 0, 0), (2, 0, 0), (0, 2, 0), (0, 0, 2)))
        original = b.spatial_order
        def forbidden(*args): raise AssertionError('single leaf has no sort')
        b.spatial_order = forbidden
        try: self.assertIsNotNone(b.prepare_classifier(w, h)['prepared'])
        finally: b.spatial_order = original
        cube = fixtures.grid_cube(3)
        complete = b.prepare_classifier(w, cube)
        for limit in (complete['phase_operations']['face_bounds'],
                      complete['phase_operations']['face_bounds']+complete['phase_operations']['spatial_orders']-1):
            refused = b.prepare_classifier(w, cube, operation_limit=limit)
            self.assertIsNone(refused['prepared'])
            self.assertEqual(refused['phase'], 'spatial_orders')
            self.assertEqual(refused['operations'], limit+1)

    def test_axis_selection_changes_neither_ordered_evidence_nor_fixed_ray_ambiguity(self):
        cube = fixtures.grid_cube(3)
        for scale in ((7, 1, 1), (1, 7, 1), (1, 1, 7)):
            h = fixtures.material(tuple(tuple(p[a]*scale[a] for a in range(3)) for p in cube.vertices),
                                  tuple(cube.faces[(i*37)%108] for i in range(108)))
            p = b.prepare_classifier(w, h)['prepared']
            points = ((0, 0, 0), (scale[0], F(1, 7), F(1, 9)),
                      tuple(-r/F(4096) for r in w.RAY), (9, 9, 9))
            for q in points:
                old = w.classify_point(h, q); new = b.classify_prepared_point(w, h, p, q)
                self.assertEqual({k:v for k,v in old.items() if k!='operations'},
                                 {k:v for k,v in new.items() if k not in inherited.EXTRA})
                for name in ('boundary_faces', 'events', 'strict_hits'):
                    if name in new:
                        ids = list(new[name]) if name=='boundary_faces' else [row['face'] for row in new[name]]
                        self.assertEqual(ids, sorted(ids))


if __name__ == '__main__': unittest.main(verbosity=2)
