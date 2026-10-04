"""Invented exact contact-box controls; no game assets or private consumers.

closed_boxes_disjoint requires caller-validated exact, ordered closed boxes.
These controls compose it with the unchanged embeddedness reference. Source-T
validation, handle lifetime and permission evidence are separately reviewed
integration responsibilities, not authority supplied by this plain helper.
"""
from fractions import Fraction as F
from pathlib import Path
import importlib.util
import random
import sys
import unittest

DIR = Path(__file__).resolve().parent


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


b = load(DIR.parent / 'tools/wayfarer_fixed_winding.py', 'contact_bounds_helper')
e = load(DIR / 'fixtures/wayfarer_fixed_winding/stowed-embeddedness-proof-preparation/embeddedness_certificate.py',
         'contact_bounds_embeddedness_reference')


def validated(raw):
    triangle = e.triangle(raw)
    e.normal(triangle, e.Work())
    return triangle


class ContactBoundsTests(unittest.TestCase):
    def test_face_box_exact_extrema_and_thirteen_operations(self):
        triangle = validated(((F(1, 3), -2, 0), (-1, F(2, 7), 4), (3, 1, -5)))
        work = e.Work()
        self.assertEqual(b.face_box(triangle, work), ((F(-1), F(3)), (F(-2), F(1)), (F(-5), F(4))))
        self.assertEqual(work.operations, 13)

    def test_all_six_strict_axes_and_signs_charge_in_order(self):
        first = ((F(0), F(1)),) * 3
        for axis in range(3):
            second = list(first)
            second[axis] = (F(2), F(3))
            for a, c, sign, cost in ((first, tuple(second), 1, 2 * axis + 1),
                                     (tuple(second), first, -1, 2 * axis + 2)):
                work = e.Work()
                self.assertEqual(b.closed_boxes_disjoint(a, c, work), (axis, sign))
                self.assertEqual(work.operations, cost)
                # Reversing the operands changes only the direction; original
                # triangle contacts are empty for each strict interval gap.
                points = [(F(0), F(0), F(0)), (F(1), F(1), F(0)), (F(1), F(0), F(1))]
                shifted = [tuple(p[k] + (2 if k == axis else 0) for k in range(3)) for p in points]
                self.assertEqual(e.triangle_intersection(points, shifted)['kind'], 'empty')

    def test_touching_overlap_and_narrow_predicate_remain_distinct(self):
        base = validated(((0, 0, 0), (2, 0, 0), (0, 2, 0)))
        cases = (
            (((2, 0, 0), (3, 0, 0), (2, 1, 0)), 'point'),
            (((0, 0, 0), (2, 0, 0), (1, -1, 0)), 'segment'),
            (((0, 0, 0), (1, 0, 0), (0, 1, 0)), 'coplanar_polygon'),
            (((2, 2, 0), (3, 2, 0), (2, 3, 0)), 'empty'),
            (((1, 0, -1), (1, 0, 1), (1, 2, 0)), 'segment'),
        )
        for raw, kind in cases:
            other = validated(raw)
            work = e.Work()
            self.assertIsNone(b.closed_boxes_disjoint(b.face_box(base, work), b.face_box(other, work), work))
            self.assertEqual(work.operations, 32)
            result = e.triangle_intersection(base, other)
            self.assertEqual(result['status'], 'COMPLETE')
            self.assertEqual(result['kind'], kind)
            self.assertTrue(result['coverage_complete'])

    def test_exact_subnormal_and_coordinate_boundaries(self):
        for tiny in (F(1, 2**149), F(1, 2**255)):
            first = validated(((0, 0, 0), (tiny, 0, 0), (0, tiny, 0)))
            second = validated(((0, 0, tiny), (tiny, 0, tiny), (0, tiny, tiny)))
            work = e.Work()
            self.assertEqual(b.closed_boxes_disjoint(b.face_box(first, work), b.face_box(second, work), work), (2, 1))
            self.assertEqual(work.operations, 31)
            self.assertEqual(e.triangle_intersection(first, second)['kind'], 'empty')
        first = validated(((-128, -128, -128), (-127, -128, -128), (-128, -127, -128)))
        second = validated(((128, 128, 128), (127, 128, 128), (128, 127, 128)))
        work = e.Work()
        self.assertEqual(b.closed_boxes_disjoint(b.face_box(first, work), b.face_box(second, work), work), (0, 1))
        self.assertEqual(work.operations, 27)

    def test_caller_validation_rejects_invalid_triangles_before_boxes(self):
        invalid = (
            ((0, 0, 0), (1, 1, 1), (2, 2, 2)),
            ((0, 0, 0), (0, 0, 0), (0, 1, 0)),
            ((float('nan'), 0, 0), (1, 0, 0), (0, 1, 0)),
            ((float('inf'), 0, 0), (1, 0, 0), (0, 1, 0)),
            ((True, 0, 0), (1, 0, 0), (0, 1, 0)),
            ((129, 0, 0), (1, 0, 0), (0, 1, 0)),
            ((F(1, 2**256), 0, 0), (1, 0, 0), (0, 1, 0)),
            ((0, 0), (1, 0, 0), (0, 1, 0)),
        )
        for raw in invalid:
            with self.assertRaises(e.InvalidGeometry):
                validated(raw)
        for limit in (-1, True, 1.0, e.MAX_OPERATIONS + 1):
            with self.assertRaises(e.InvalidGeometry):
                e.Work(limit)

    def test_every_budget_prefix_and_first_refusal_counter(self):
        first = validated(((0, 0, 0), (1, 0, 0), (0, 1, 0)))
        for second, expected in ((validated(((0, 0, 1), (1, 0, 1), (0, 1, 1))), 31),
                                 (first, 32)):
            for limit in range(expected):
                work = e.Work(limit)
                with self.assertRaises(e.OperationLimit):
                    b.closed_boxes_disjoint(b.face_box(first, work), b.face_box(second, work), work)
                self.assertEqual(work.operations, limit + 1)
            work = e.Work(expected)
            b.closed_boxes_disjoint(b.face_box(first, work), b.face_box(second, work), work)
            self.assertEqual(work.operations, expected)
        for limit in range(13):
            work = e.Work(limit)
            with self.assertRaises(e.OperationLimit):
                b.face_box(first, work)
            self.assertEqual(work.operations, limit + 1)

    def test_two_hundred_general_exact_triangle_pairs(self):
        rng = random.Random(403)
        excluded = nonempty = nonspecial_normals = 0
        for _ in range(200):
            triangles = []
            while len(triangles) < 2:
                raw = tuple(tuple(F(rng.randint(-12, 12), rng.randint(1, 5)) for _ in range(3)) for _ in range(3))
                try:
                    triangles.append(validated(raw))
                except e.InvalidGeometry:
                    continue
            first, second = triangles
            normal = e.normal(first, e.Work())
            nonspecial_normals += all(normal)
            work = e.Work()
            a, c = b.face_box(first, work), b.face_box(second, work)
            gap = b.closed_boxes_disjoint(a, c, work)
            oracle = e.triangle_intersection(first, second)
            self.assertEqual(oracle['status'], 'COMPLETE')
            self.assertTrue(oracle['coverage_complete'])
            if gap is not None:
                axis, sign = gap
                self.assertTrue(a[axis][1] < c[axis][0] if sign == 1 else c[axis][1] < a[axis][0])
                self.assertEqual(oracle['kind'], 'empty')
                self.assertEqual(oracle['points'], ())
                excluded += 1
            else:
                self.assertEqual(work.operations, 32)
                nonempty += oracle['kind'] != 'empty'
            reverse = b.closed_boxes_disjoint(c, a, e.Work())
            self.assertEqual(reverse, None if gap is None else (gap[0], -gap[1]))
        self.assertGreater(excluded, 0)
        self.assertGreater(nonempty, 0)
        self.assertGreater(nonspecial_normals, 150)


if __name__ == '__main__':
    unittest.main(verbosity=2)
