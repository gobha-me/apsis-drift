"""Private exact fixed-ray broad phase; no factories, assets, or admission."""
from fractions import Fraction
from pathlib import Path
import hashlib
import types
import os
import stat

WINDING_SHA = '748c307f3395f7c31751866871c23cdda895829cdaba8ec6c1677605aac04127'
EMBEDDING_SHA = '09b7433c3e58ac854f64ce64e03eff396ba44cf710fbdcf3586ed334bfbd3fe8'
RAY = (Fraction(137), Fraction(251), Fraction(509))
CODE_PATH = Path(__file__).resolve()
MAX_SOURCE_BYTES = 131072


def bounded_source(path, cap, *, exact_length=None):
    """Bound allocation and require stable regular FD/final-name identity."""
    if type(cap) is not int or not 0 <= cap <= MAX_SOURCE_BYTES:
        raise ValueError('Source read cap')
    if exact_length is not None and (type(exact_length) is not int or not 0 <= exact_length <= cap):
        raise ValueError('Source exact length')
    def identity(value):
        if not stat.S_ISREG(value.st_mode):
            raise ValueError('Regular source file required')
        return (value.st_dev, value.st_ino, value.st_mode, value.st_size,
                value.st_mtime_ns, value.st_ctime_ns)
    path = Path(path)
    before_name = identity(path.lstat())
    if before_name[3] > cap or (exact_length is not None and before_name[3] != exact_length):
        raise ValueError('Source byte bound before read')
    with path.open('rb') as stream:
        before_fd = identity(os.fstat(stream.fileno()))
        if before_fd != before_name or identity(path.lstat()) != before_fd:
            raise ValueError('Source FD/name changed before read')
        data = stream.read(cap + 1)
        if len(data) > cap or (exact_length is not None and len(data) != exact_length):
            raise ValueError('Source byte bound after read')
        after_fd = identity(os.fstat(stream.fileno()))
        if after_fd != before_fd or identity(path.lstat()) != before_fd or len(data) != before_fd[3]:
            raise ValueError('Source FD/name changed during read')
        return data


CODE_SHA = hashlib.sha256(bounded_source(CODE_PATH, MAX_SOURCE_BYTES)).hexdigest()


def source_codes(path, expected, length):
    data = bounded_source(path, length, exact_length=length)
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError('Frozen classifier dependency changed')
    code = compile(data, str(path), 'exec')
    return {value.co_name: value for value in code.co_consts if type(value) is types.CodeType}


def require_callable_semantics(function, defaults=None, kwdefaults=None):
    if type(function) is not types.FunctionType or function.__closure__ is not None:
        raise ValueError('Frozen callable closure semantics changed')
    actual = function.__defaults__
    if defaults is None:
        if actual is not None:
            raise ValueError('Frozen callable defaults changed')
    elif (type(actual) is not tuple or len(actual) != len(defaults)
          or any(type(a) is not type(z) or a != z for a, z in zip(actual, defaults))):
        raise ValueError('Frozen typed callable defaults changed')
    actual = function.__kwdefaults__
    if kwdefaults is None:
        if actual is not None:
            raise ValueError('Frozen callable keyword defaults changed')
    elif (type(actual) is not dict or set(actual) != set(kwdefaults)
          or any(type(actual[k]) is not type(v) or actual[k] != v for k, v in kwdefaults.items())):
        raise ValueError('Frozen typed callable keyword defaults changed')


def require_module(module, path, expected, codes, names):
    if type(module) is not types.ModuleType or getattr(module, '__file__', None) != str(path):
        raise ValueError('Exact source-backed winding module required')
    if hashlib.sha256(bounded_source(path, 15130 if expected == WINDING_SHA else 10644,
                                       exact_length=15130 if expected == WINDING_SHA else 10644)).hexdigest() != expected:
        raise ValueError('Pinned source identity changed')
    for name in names:
        function = getattr(module, name, None)
        if (type(function) is not types.FunctionType or function.__globals__ is not vars(module)
                or function.__code__ != codes[name] or function.__code__.co_filename != str(path)):
            raise ValueError('Frozen exact predicate function changed')
        require_callable_semantics(function, kwdefaults={'operation_limit': 16_000_000}
                                   if name == 'classify_point' else None)


def require_winding(w):
    # Only the supplied instance's class/token can authenticate its own handle.
    if type(w) is not types.ModuleType or type(getattr(w, '__file__', None)) is not str:
        raise ValueError('Exact source-backed winding module required')
    winding_path = Path(w.__file__).resolve()
    if type(getattr(w, 'e', None)) is not types.ModuleType or type(getattr(w.e, '__file__', None)) is not str:
        raise ValueError('Exact source-backed embedding module required')
    embedding_path = Path(w.e.__file__).resolve()
    if winding_path.stat().st_size != 15130 or embedding_path.stat().st_size != 10644:
        raise ValueError('Frozen dependency byte bounds')
    w_codes = source_codes(winding_path, WINDING_SHA, 15130)
    e_codes = source_codes(embedding_path, EMBEDDING_SHA, 10644)
    require_module(w, winding_path, WINDING_SHA, w_codes,
                   ('require_material', 'numeric_doc', 'canonical', 'digest', 'face_values',
                    'coplanar_ray_interval', 'classify_point', '_point_classification'))
    require_module(w.e, embedding_path, EMBEDDING_SHA, e_codes,
                   ('coordinate', 'vector', 'normal', 'sub', 'total', 'dot', 'cross'))
    if (w.RAY != RAY or w.EMBEDDING_SHA != EMBEDDING_SHA or w._CODE_SHA != WINDING_SHA
            or w.e.MAX_VERTICES != 256 or w.e.MAX_FACES != 512
            or w.e.MAX_INPUT_BITS != 256 or w.e.MAX_RESULT_BITS != 4096
            or w.e.MAX_OPERATIONS != 16_000_000 or w.e.MAX_COORDINATE != 128):
        raise ValueError('Frozen exact numeric domain changed')
    checked_work_class(w, embedding_path, e_codes)
    if hashlib.sha256(bounded_source(CODE_PATH, MAX_SOURCE_BYTES)).hexdigest() != CODE_SHA:
        raise ValueError('Broadphase source changed')


def checked_work_class(w, embedding_path, e_codes):
    cls = w.e.Work
    if (type(cls) is not type or cls.__bases__ != (object,) or cls.__mro__ != (cls, object)
            or cls.__new__ is not object.__new__ or cls.__getattribute__ is not object.__getattribute__
            or cls.__setattr__ is not object.__setattr__ or cls.__delattr__ is not object.__delattr__
            or not {'__module__', '__init__', 'tick', 'number', '__dict__', '__weakref__', '__doc__'} <= set(vars(cls))
            or set(vars(cls)) - {'__module__', '__init__', 'tick', 'number', '__dict__', '__weakref__', '__doc__',
                                '__firstlineno__', '__static_attributes__'}
            or cls.__module__ != w.e.__name__):
        raise ValueError('Frozen plain work class required')
    if ('__firstlineno__' in vars(cls) and (type(cls.__firstlineno__) is not int or
            cls.__firstlineno__ != e_codes['Work'].co_firstlineno)) or ('__static_attributes__' in vars(cls) and
            cls.__static_attributes__ != ('limit', 'operations')):
        raise ValueError('Frozen compiler class metadata changed')
    for name in ('__init__', 'tick', 'number'):
        function = vars(cls)[name]
        original = next(value for value in e_codes['Work'].co_consts
                        if type(value) is types.CodeType and value.co_name == name)
        if (type(function) is not types.FunctionType or function.__globals__ is not vars(w.e)
                or function.__code__ != original or function.__code__.co_filename != str(embedding_path)):
            raise ValueError('Frozen work implementation changed')
        require_callable_semantics(function, defaults={'__init__': (16_000_000,), 'tick': (1,), 'number': None}[name])
    return cls


def require_live_work(work, cls, limit, *, initial=False):
    if (type(work) is not cls or type(work.limit) is not int or work.limit != limit
            or type(work.operations) is not int or work.operations < 0
            or set(vars(work)) != {'limit', 'operations'} or (initial and work.operations != 0)):
        raise ValueError('Actual exact work object changed')
    for name in ('tick', 'number'):
        method = getattr(work, name)
        if (type(method) is not types.MethodType or method.__self__ is not work
                or method.__func__ is not vars(cls)[name]):
            raise ValueError('Actual bound work method changed')
        require_callable_semantics(method.__func__, defaults=(1,) if name == 'tick' else None)


def face_box(t, work):
    box = []
    for axis in range(3):
        low = high = t[0][axis]
        for point in t[1:]:
            work.tick(); low = min(low, point[axis])
            work.tick(); high = max(high, point[axis])
        box.append((low, high))
    work.tick()  # Install one bounded ordered row; no measurement prepass.
    return tuple(box)


def point_box(box, point, work):
    outside = []
    for axis, (low, high) in enumerate(box):
        work.tick(); outside.append(point[axis] < low)
        work.tick(); outside.append(point[axis] > high)
    work.tick()
    return not any(outside)


def positive_ray_box(box, point, work):
    low = Fraction(0); high = None; empty = False
    for axis, (minimum, maximum) in enumerate(box):
        start = work.number(work.number(minimum - point[axis]) / RAY[axis])
        end = work.number(work.number(maximum - point[axis]) / RAY[axis])
        work.tick(); low = max(low, start)
        work.tick(); high = end if high is None else min(high, end)
        work.tick(); separated = high < low; empty = empty or separated
    work.tick()
    nonpositive = high <= 0
    return not (empty or nonpositive)


def classify_point(w, material, point, *, operation_limit=16_000_000):
    """Same frozen instance; local bounds only; original finite predicates."""
    require_winding(w)
    w.require_material(material)
    if (type(material.vertices) is not tuple or type(material.faces) is not tuple
            or not 4 <= len(material.vertices) <= 256 or not 4 <= len(material.faces) <= 512):
        raise w.e.InvalidGeometry('Broadphase tuple mesh dimensions')
    q = w.e.vector(point)
    work_class = w.e.Work
    work = work_class(operation_limit)
    require_live_work(work, work_class, operation_limit, initial=True)
    counts = {'box_rows_completed': 0, 'boundary_faces_visited': 0,
              'boundary_box_pruned': 0, 'boundary_candidates': 0,
              'ray_faces_visited': 0, 'ray_box_pruned': 0, 'ray_candidates': 0}
    try:
        boxes = []
        for face in material.faces:
            boxes.append(face_box(tuple(material.vertices[i] for i in face), work))
            counts['box_rows_completed'] += 1
        boundary = []
        for index, face in enumerate(material.faces):
            work.tick(); counts['boundary_faces_visited'] += 1
            if not point_box(boxes[index], q, work):
                counts['boundary_box_pruned'] += 1
                continue
            counts['boundary_candidates'] += 1
            t = tuple(material.vertices[i] for i in face)
            _, plane, sides = w.face_values(t, q, work)
            if plane == 0 and all(x >= 0 for x in sides):
                boundary.append(index)
        if boundary:
            result = {'status': 'UNRESOLVED_POINT_ON_BOUNDARY', 'membership_complete': False,
                      'point_on_finite_face': True, 'boundary_faces': tuple(boundary)}
        else:
            hits = []; events = []
            for index, face in enumerate(material.faces):
                work.tick(); counts['ray_faces_visited'] += 1
                if not positive_ray_box(boxes[index], q, work):
                    counts['ray_box_pruned'] += 1
                    continue
                counts['ray_candidates'] += 1
                t = tuple(material.vertices[i] for i in face); n = w.e.normal(t, work)
                plane = w.e.dot(n, w.e.sub(q, t[0], work), work); denominator = w.e.dot(n, RAY, work)
                if denominator == 0:
                    if plane == 0:
                        interval = w.coplanar_ray_interval(t, q, n, work)
                        if interval is not None:
                            events.append({'face': index, 'kind': 'finite_coplanar_ray', 'interval': interval})
                    continue
                distance = work.number(-plane / denominator)
                if distance <= 0:
                    continue
                hit_point = tuple(work.number(q[i] + work.number(distance * RAY[i])) for i in range(3))
                _, _, sides = w.face_values(t, hit_point, work)
                if any(x < 0 for x in sides):
                    continue
                if any(x == 0 for x in sides):
                    events.append({'face': index, 'kind': 'ray_vertex' if sum(x == 0 for x in sides) >= 2 else 'ray_edge',
                                   'distance': distance, 'point': hit_point})
                else:
                    hits.append({'face': index, 'sign': 1 if denominator > 0 else -1,
                                 'distance': distance, 'point': hit_point})
            if events:
                result = {'status': 'UNRESOLVED_AMBIGUOUS_FIXED_RAY', 'membership_complete': False,
                          'point_on_finite_face': False, 'events': tuple(events), 'strict_hits': tuple(hits)}
            else:
                winding = sum(hit['sign'] for hit in hits)
                if material.qualification_kind == 'embedded_bridge' and winding not in (0, 1):
                    raise w.e.InvalidGeometry('Embedded outward winding consistency')
                result = {'status': 'STRICT_INTERIOR' if winding != 0 else 'STRICT_EXTERIOR',
                          'membership_complete': True, 'point_on_finite_face': False,
                          'oriented_winding': winding, 'strict_hits': tuple(hits)}
        result.update({'point': q, 'ray': RAY, 'numeric_sha256': material.numeric_sha256,
                       'embedding_certificate_sha256': material.embedding_certificate_sha256,
                       'source_or_physical_material_authority': False})
        scans_complete = True
    except w.e.OperationLimit as error:
        result = {'status': 'UNRESOLVED_OPERATION_LIMIT', 'membership_complete': False,
                  'reason': str(error), 'source_or_physical_material_authority': False}
        scans_complete = False
    require_winding(w)
    if w.e.Work is not work_class:
        raise ValueError('Selected work class changed during query')
    require_live_work(work, work_class, operation_limit)
    w.require_material(material)
    return {**result, 'operations': work.operations, 'operation_limit': work.limit,
            'broadphase_accounting': counts, 'classification_scans_complete': scans_complete,
            'helper_sha256': CODE_SHA, 'winding_sha256': WINDING_SHA,
            'embedding_sha256': EMBEDDING_SHA,
            'factory_calls': 0, 'seed_calls': 0, 'source_authority': False}
