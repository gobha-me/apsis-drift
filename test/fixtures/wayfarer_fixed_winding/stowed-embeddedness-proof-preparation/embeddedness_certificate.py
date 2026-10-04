"""Private source-free static bridge proof preparation. No asset/file authority."""
from fractions import Fraction
import math

MAX_VERTICES=256
MAX_FACES=512
MAX_COORDINATE=128
MAX_INPUT_BITS=256
MAX_RESULT_BITS=4096
MAX_OPERATIONS=16_000_000

class InvalidGeometry(ValueError):
    pass
class OperationLimit(ValueError):
    pass
class Work:
    def __init__(self, limit=MAX_OPERATIONS):
        if type(limit) is not int or not 0<=limit<=MAX_OPERATIONS:
            raise InvalidGeometry('Invalid operation budget')
        self.limit=limit;self.operations=0
    def tick(self, count=1):
        self.operations+=count
        if self.operations>self.limit:raise OperationLimit('Exact operation budget exhausted')
    def number(self, x):
        self.tick()
        if max(x.numerator.bit_length(),x.denominator.bit_length())>MAX_RESULT_BITS:
            raise OperationLimit('Exact rational bit budget exhausted')
        return x

def coordinate(x):
    if type(x) not in (int,float,Fraction):raise InvalidGeometry('Coordinate type')
    if type(x) is float and not math.isfinite(x):raise InvalidGeometry('Nonfinite coordinate')
    q=Fraction(x)
    if abs(q)>MAX_COORDINATE:raise InvalidGeometry('Coordinate bounds')
    if max(q.numerator.bit_length(),q.denominator.bit_length())>MAX_INPUT_BITS:
        raise InvalidGeometry('Input rational bit bounds')
    return q

def vector(v):
    if type(v) not in (list,tuple) or len(v)!=3:raise InvalidGeometry('Coordinate dimensions')
    return tuple(coordinate(x) for x in v)

def sub(a,b,w):return tuple(w.number(x-y) for x,y in zip(a,b))
def total(values,w):
    result=Fraction(0)
    for x in values:result=w.number(result+x)
    return result
def dot(a,b,w):return total((w.number(x*y) for x,y in zip(a,b)),w)
def cross(a,b,w):
    return tuple(w.number(w.number(a[i]*b[j])-w.number(a[j]*b[i])) for i,j in ((1,2),(2,0),(0,1)))
def normal(t,w):
    n=cross(sub(t[1],t[0],w),sub(t[2],t[0],w),w)
    if not any(n):raise InvalidGeometry('Zero-area triangle')
    return n

def triangle(t):
    if type(t) not in (list,tuple) or len(t)!=3:raise InvalidGeometry('Triangle dimensions')
    return tuple(vector(p) for p in t)

def section(t,n,d,w):
    values=[w.number(dot(n,p,w)-d) for p in t];points=[]
    for i,p in enumerate(t):
        if values[i]==0:points.append(p)
        j=(i+1)%3
        if (values[i]<0<values[j]) or (values[j]<0<values[i]):
            f=w.number(values[i]/w.number(values[i]-values[j]))
            points.append(tuple(w.number(p[k]+w.number(f*w.number(t[j][k]-p[k]))) for k in range(3)))
    return list(dict.fromkeys(points))

def area2(poly,w):
    return total((w.number(w.number(p[0]*q[1])-w.number(p[1]*q[0])) for p,q in zip(poly,poly[1:]+poly[:1])),w)
def orient(a,b,p,w):
    return w.number(w.number(w.number(b[0]-a[0])*w.number(p[1]-a[1]))-w.number(w.number(b[1]-a[1])*w.number(p[0]-a[0])))
def coplanar(t,u,n,w):
    axis=next(i for i,x in enumerate(n) if x);dims=[i for i in range(3) if i!=axis]
    project=lambda p:tuple(p[i] for i in dims)
    clip=[project(p) for p in u];poly=[project(p) for p in t]
    if area2(clip,w)<0:clip.reverse()
    for a,b in zip(clip,clip[1:]+clip[:1]):
        if not poly:break
        new=[]
        for p,q in zip(poly,poly[1:]+poly[:1]):
            fp=orient(a,b,p,w);fq=orient(a,b,q,w)
            if fp>=0:new.append(p)
            if (fp<0<=fq) or (fq<0<=fp):
                r=w.number(fp/w.number(fp-fq))
                new.append(tuple(w.number(p[k]+w.number(r*w.number(q[k]-p[k]))) for k in range(2)))
        poly=list(dict.fromkeys(new))
    if not poly:return {'kind':'empty','points':()}
    def lift(p):
        result=[Fraction(0)]*3
        for i,x in zip(dims,p):result[i]=x
        d=dot(n,t[0],w)
        result[axis]=w.number(w.number(d-total((w.number(n[i]*result[i]) for i in dims),w))/n[axis])
        return tuple(result)
    if len(poly)==1:return {'kind':'point','points':(lift(poly[0]),)}
    if area2(poly,w)==0:
        points=sorted(lift(p) for p in poly)
        return {'kind':'segment','points':(points[0],points[-1])} if points[0]!=points[-1] else {'kind':'point','points':(points[0],)}
    while len(poly)>3:
        index=next((i for i in range(len(poly)) if orient(poly[i-1],poly[i],poly[(i+1)%len(poly)],w)==0),None)
        if index is None:break
        poly.pop(index)
    if area2(poly,w)<0:poly.reverse()
    points=[lift(p) for p in poly];start=min(range(len(points)),key=points.__getitem__)
    return {'kind':'coplanar_polygon','points':tuple(points[start:]+points[:start])}

def _intersection(t,u,w):
    n=normal(t,w);m=normal(u,w);direction=cross(n,m,w)
    if not any(direction):
        return coplanar(t,u,n,w) if dot(n,sub(u[0],t[0],w),w)==0 else {'kind':'empty','points':()}
    left=section(t,m,dot(m,u[0],w),w);right=section(u,n,dot(n,t[0],w),w)
    if not left or not right:return {'kind':'empty','points':()}
    axis=next(i for i,x in enumerate(direction) if x)
    left.sort(key=lambda p:p[axis]);right.sort(key=lambda p:p[axis])
    low=max(left[0],right[0],key=lambda p:p[axis]);high=min(left[-1],right[-1],key=lambda p:p[axis])
    if low[axis]>high[axis]:return {'kind':'empty','points':()}
    if low[axis]==high[axis]:
        if low!=high:raise InvalidGeometry('Inconsistent exact intersection line')
        return {'kind':'point','points':(low,)}
    return {'kind':'segment','points':tuple(sorted((low,high)))}

def triangle_intersection(first,second,*,operation_limit=MAX_OPERATIONS):
    w=Work(operation_limit)
    try:
        hit=_intersection(triangle(first),triangle(second),w)
        return {'status':'COMPLETE','coverage_complete':True,'operations':w.operations,**hit}
    except OperationLimit as error:
        return {'status':'UNRESOLVED_OPERATION_LIMIT','coverage_complete':False,'operations':w.operations,'reason':str(error)}

def face_indices(face,count):
    if type(face) not in (list,tuple) or len(face)!=3:raise InvalidGeometry('Face dimensions')
    if any(type(i) is not int or not 0<=i<count for i in face):raise InvalidGeometry('Index bounds/type')
    if len(set(face))!=3:raise InvalidGeometry('Repeated triangle index')
    return tuple(face)

def _pair(vertices,a,b,w):
    hit=_intersection(tuple(vertices[i] for i in a),tuple(vertices[i] for i in b),w)
    common=set(a)&set(b);expected=tuple(sorted(vertices[i] for i in common))
    kind={0:'empty',1:'point',2:'segment'}.get(len(common))
    equal=kind is not None and hit['kind']==kind and tuple(sorted(hit['points']))==expected
    return {'permitted_shared_vertex_ids':tuple(sorted(common)),'expected_kind':kind,'intersection':hit,'equals_shared_simplex':equal}

def shared_simplex_check(vertices,first,second,*,operation_limit=MAX_OPERATIONS):
    if type(vertices) not in (list,tuple) or not 3<=len(vertices)<=MAX_VERTICES:raise InvalidGeometry('Vertex count bounds')
    v=tuple(vector(p) for p in vertices);a=face_indices(first,len(v));b=face_indices(second,len(v));w=Work(operation_limit)
    try:
        result=_pair(v,a,b,w)
        return {'status':'COMPLETE','coverage_complete':True,'operations':w.operations,**result}
    except OperationLimit as error:return {'status':'UNRESOLVED_OPERATION_LIMIT','coverage_complete':False,'operations':w.operations,'reason':str(error)}

def certify_embedded_bridge(vertices,faces,*,operation_limit=MAX_OPERATIONS):
    w=Work(operation_limit)
    try:
        if type(vertices) not in (list,tuple) or not 4<=len(vertices)<=MAX_VERTICES:raise InvalidGeometry('Vertex count bounds')
        if type(faces) not in (list,tuple) or not 4<=len(faces)<=MAX_FACES:raise InvalidGeometry('Face count bounds')
        v=tuple(vector(p) for p in vertices);f=tuple(face_indices(t,len(v)) for t in faces)
        if len({tuple(sorted(t)) for t in f})!=len(f):raise InvalidGeometry('Duplicate face')
        if set(i for t in f for i in t)!=set(range(len(v))):raise InvalidGeometry('Unused vertex')
        edges={};links={i:{} for i in range(len(v))};graph={i:set() for i in range(len(f))}
        for index,t in enumerate(f):
            w.tick();normal(tuple(v[i] for i in t),w)
            for a,b in zip(t,t[1:]+t[:1]):edges.setdefault(tuple(sorted((a,b))),[]).append((a,b,index))
            for i in range(3):
                center=t[i];a=t[(i+1)%3];b=t[(i+2)%3]
                links[center].setdefault(a,[]).append(b);links[center].setdefault(b,[]).append(a)
        for uses in edges.values():
            w.tick()
            if len(uses)!=2 or uses[0][:2]!=uses[1][:2][::-1]:raise InvalidGeometry('Closed directed edge incidence')
            a,b=uses[0][2],uses[1][2];graph[a].add(b);graph[b].add(a)
        for link in links.values():
            w.tick()
            if any(len(neighbors)!=2 for neighbors in link.values()):raise InvalidGeometry('Vertex link degree')
            visited=set();todo=[next(iter(link))]
            while todo:
                w.tick();a=todo.pop()
                if a not in visited:visited.add(a);todo.extend(link[a])
            if visited!=set(link):raise InvalidGeometry('Vertex link is not one cycle')
        visited=set();todo=[0]
        while todo:
            w.tick();a=todo.pop()
            if a not in visited:visited.add(a);todo.extend(graph[a])
        if len(visited)!=len(f):raise InvalidGeometry('Unexpected disconnected bridge')
        volume=total((dot(v[t[0]],cross(v[t[1]],v[t[2]],w),w) for t in f),w)
        if volume<=0:raise InvalidGeometry('Outward positive orientation')
        pairs=0
        for i,a in enumerate(f):
            for j in range(i+1,len(f)):
                b=f[j];w.tick();pairs+=1
                # Inclusive exact rational AABBs. No floating BVH removes pairs.
                if any(max(v[k][axis] for k in a)<min(v[k][axis] for k in b) or max(v[k][axis] for k in b)<min(v[k][axis] for k in a) for axis in range(3)):continue
                result=_pair(v,a,b,w)
                if not result['equals_shared_simplex']:
                    return {'status':'ACTUAL_EMBEDDEDNESS_REFUSAL','coverage_complete':False,'first_face':i,'second_face':j,'pairs_considered':pairs,'operations':w.operations,**result}
        return {'status':'CERTIFIED_CLOSED_EMBEDDED_BRIDGE','coverage_complete':True,'vertices':len(v),'faces':len(f),'pairs_considered':pairs,'operations':w.operations,'signed_volume_six':volume}
    except OperationLimit as error:
        return {'status':'UNRESOLVED_OPERATION_LIMIT','coverage_complete':False,'operations':w.operations,'reason':str(error)}
    except InvalidGeometry as error:
        return {'status':'INVALID_GEOMETRY','coverage_complete':False,'operations':w.operations,'reason':str(error)}
