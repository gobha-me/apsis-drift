"""Copied portable source references plus invented closed chains only."""
from pathlib import Path
from fractions import Fraction as F
import hashlib
import importlib.util
import sys
DIR=Path(__file__).resolve().parent
def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec); sys.modules[name] = module
    spec.loader.exec_module(module)
    return module
b=load(DIR/'fixtures/wayfarer_fixed_winding/flat_reference.py','prototype_flat_reference')
WINDING_PATH=DIR/'fixtures/wayfarer_fixed_winding/stowed-winding-containment-proof-preparation/winding_containment_certificate.py'
w=load(WINDING_PATH,'prototype_original_winding')
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
