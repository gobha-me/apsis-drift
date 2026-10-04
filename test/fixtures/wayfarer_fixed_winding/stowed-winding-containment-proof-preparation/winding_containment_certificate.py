"""Private selected static material proof preparation; no project asset reads."""
from pathlib import Path
import hashlib
import importlib.util
import json
from fractions import Fraction
from dataclasses import dataclass

EMBEDDING_SHA='09b7433c3e58ac854f64ce64e03eff396ba44cf710fbdcf3586ed334bfbd3fe8'
_embedding_path=Path(__file__).resolve().parent.parent/'stowed-embeddedness-proof-preparation/embeddedness_certificate.py'
if hashlib.sha256(_embedding_path.read_bytes()).hexdigest()!=EMBEDDING_SHA:
    raise ValueError('Pinned source-free embedding code changed')
_spec=importlib.util.spec_from_file_location('private_frozen_bridge_embedding',_embedding_path)
e=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(e)
RAY=(Fraction(137),Fraction(251),Fraction(509))
KINDS=('selected_thick_ribbon','selected_buckle','selected_static_bridge')
MAX_DOCUMENT_BYTES=512*1024
_TOKEN=object()
_CODE_PATH=Path(__file__).resolve()
_CODE_SHA=hashlib.sha256(_CODE_PATH.read_bytes()).hexdigest()

def canonical(value):
    return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()
def digest(data):return hashlib.sha256(data).hexdigest()
def reject_constant(value):raise e.InvalidGeometry('Nonfinite JSON constant')
def closed_pairs(pairs):
    result={}
    for k,v in pairs:
        if k in result:raise e.InvalidGeometry('Duplicate JSON key')
        result[k]=v
    return result
def certificate_json(value):
    if isinstance(value,Fraction):return [value.numerator,value.denominator]
    if isinstance(value,dict):return {k:certificate_json(v) for k,v in value.items()}
    if isinstance(value,(tuple,list)):return [certificate_json(v) for v in value]
    return value

def numeric_doc(vertices,faces,kind):
    return {'schema':'apsis.private-selected-material-numeric/1','material_kind':kind,
            'vertices':[[[q.numerator,q.denominator] for q in v] for v in vertices],
            'faces':[list(t) for t in faces]}

@dataclass(frozen=True,slots=True,init=False)
class SelectedMaterial:
    vertices:tuple
    faces:tuple
    material_kind:str
    document_sha256:str
    numeric_sha256:str
    embedding_certificate_bytes:bytes
    embedding_certificate_sha256:str
    qualification_kind:str
    _token:object

def decode_mesh(data,expected_sha256):
    if type(data) is not bytes or len(data)>MAX_DOCUMENT_BYTES:raise e.InvalidGeometry('Numeric buffer bounds/type')
    if type(expected_sha256) is not str or len(expected_sha256)!=64 or any(c not in '0123456789abcdef' for c in expected_sha256):raise e.InvalidGeometry('Expected SHA dimensions/type')
    if digest(data)!=expected_sha256:raise e.InvalidGeometry('Numeric document identity')
    try:d=json.loads(data,parse_constant=reject_constant,object_pairs_hook=closed_pairs)
    except (ValueError,UnicodeError,RecursionError) as error:raise e.InvalidGeometry('Invalid numeric JSON') from error
    if type(d) is not dict or set(d)!={'schema','material_kind','vertices','faces'}:raise e.InvalidGeometry('Closed numeric schema')
    if d['schema']!='apsis.private-selected-material-numeric/1' or d['material_kind'] not in KINDS or type(d['material_kind']) is not str:raise e.InvalidGeometry('Selected material semantics')
    if type(d['vertices']) is not list or not 4<=len(d['vertices'])<=e.MAX_VERTICES:raise e.InvalidGeometry('Vertex count bounds')
    if type(d['faces']) is not list or not 4<=len(d['faces'])<=e.MAX_FACES:raise e.InvalidGeometry('Face count bounds')
    vertices=[]
    for point in d['vertices']:
        if type(point) is not list or len(point)!=3:raise e.InvalidGeometry('Coordinate dimensions')
        result=[]
        for pair in point:
            if type(pair) is not list or len(pair)!=2 or any(type(x) is not int for x in pair):raise e.InvalidGeometry('Rational coordinate types/dimensions')
            n,z=pair
            if z<=0 or max(n.bit_length(),z.bit_length())>e.MAX_INPUT_BITS:raise e.InvalidGeometry('Rational coordinate bit/bounds')
            q=Fraction(n,z)
            if (q.numerator,q.denominator)!=(n,z):raise e.InvalidGeometry('Canonical rational coordinate required')
            result.append(e.coordinate(q))
        vertices.append(tuple(result))
    faces=tuple(e.face_indices(t,len(vertices)) for t in d['faces'])
    return tuple(vertices),faces,d['material_kind']

def _make_handle(vertices,faces,kind,expected_sha256,proof,qualification):
    numeric=digest(canonical(numeric_doc(vertices,faces,kind)))
    cert=canonical({'schema':'apsis.private-selected-numeric-certificate/1',
                    'numeric_sha256':numeric,'embedding_code_sha256':EMBEDDING_SHA,
                    'classifier_code_sha256':_CODE_SHA,'qualification_kind':qualification,
                    'qualification':certificate_json(proof),'source_or_physical_material_authority':False})
    handle=object.__new__(SelectedMaterial)
    for key,value in {'vertices':vertices,'faces':faces,'material_kind':kind,'document_sha256':expected_sha256,'numeric_sha256':numeric,
                      'embedding_certificate_bytes':cert,'embedding_certificate_sha256':digest(cert),'qualification_kind':qualification,'_token':_TOKEN}.items():
        object.__setattr__(handle,key,value)
    return handle

def certify_selected_material(data,expected_sha256,*,operation_limit=e.MAX_OPERATIONS):
    """New bridge/embedded-body mathematics only; no accepted source semantics."""
    vertices,faces,kind=decode_mesh(data,expected_sha256)
    proof=e.certify_embedded_bridge(vertices,faces,operation_limit=operation_limit)
    if proof['status']!='CERTIFIED_CLOSED_EMBEDDED_BRIDGE':
        return {'status':proof['status'],'coverage_complete':False,'embedding':proof,'source_or_physical_material_authority':False}
    return _make_handle(vertices,faces,kind,expected_sha256,proof,'embedded_bridge')

def certify_closed_oriented_chain(data,expected_sha256,*,operation_limit=e.MAX_OPERATIONS):
    """Complete algebraic closed chain, without global embedding/positive volume."""
    vertices,faces,kind=decode_mesh(data,expected_sha256);w=e.Work(operation_limit)
    try:
        if set(i for t in faces for i in t)!=set(range(len(vertices))):raise e.InvalidGeometry('Unused chain vertex')
        balance={}
        for t in faces:
            w.tick();e.normal(tuple(vertices[i] for i in t),w)
            for a,b in zip(t,t[1:]+t[:1]):
                edge=tuple(sorted((a,b)));balance[edge]=balance.get(edge,0)+(1 if a<b else -1)
        if any(balance.values()):raise e.InvalidGeometry('Closed oriented chain edge cancellation')
        proof={'status':'CERTIFIED_CLOSED_ORIENTED_CHAIN','coverage_complete':True,'vertices':len(vertices),'faces':len(faces),'operations':w.operations,'global_embedding_claimed':False}
        return _make_handle(vertices,faces,kind,expected_sha256,proof,'closed_oriented_chain')
    except e.OperationLimit as error:return {'status':'UNRESOLVED_OPERATION_LIMIT','coverage_complete':False,'operations':w.operations,'reason':str(error),'source_or_physical_material_authority':False}


def require_material(material):
    if type(material) is not SelectedMaterial or getattr(material,'_token',None) is not _TOKEN:raise e.InvalidGeometry('Factory-certified selected material required')
    if digest(canonical(numeric_doc(material.vertices,material.faces,material.material_kind)))!=material.numeric_sha256:raise e.InvalidGeometry('Numeric mesh binding changed')
    if digest(material.embedding_certificate_bytes)!=material.embedding_certificate_sha256:raise e.InvalidGeometry('Embedding certificate identity changed')
    cert=json.loads(material.embedding_certificate_bytes)
    if digest(_CODE_PATH.read_bytes())!=_CODE_SHA or cert['classifier_code_sha256']!=_CODE_SHA or cert['qualification_kind']!=material.qualification_kind or cert['numeric_sha256']!=material.numeric_sha256 or cert['embedding_code_sha256']!=EMBEDDING_SHA or cert['qualification']['status']!={'embedded_bridge':'CERTIFIED_CLOSED_EMBEDDED_BRIDGE','closed_oriented_chain':'CERTIFIED_CLOSED_ORIENTED_CHAIN'}.get(material.qualification_kind) or cert['qualification']['coverage_complete'] is not True or cert['source_or_physical_material_authority'] is not False:
        raise e.InvalidGeometry('Complete embedding certificate binding')
    if cert['qualification']['vertices']!=len(material.vertices) or cert['qualification']['faces']!=len(material.faces):raise e.InvalidGeometry('Complete certificate mesh dimensions')
    if material.qualification_kind=='embedded_bridge' and cert['qualification']['pairs_considered']!=len(material.faces)*(len(material.faces)-1)//2:raise e.InvalidGeometry('Complete embedding pair coverage')
    return material

def face_values(t,point,w):
    n=e.normal(t,w)
    plane=e.dot(n,e.sub(point,t[0],w),w)
    sides=[e.dot(n,e.cross(e.sub(t[(i+1)%3],t[i],w),e.sub(point,t[i],w),w),w) for i in range(3)]
    return n,plane,sides

def coplanar_ray_interval(t,point,n,w):
    # Exact closed halfspaces of the finite triangle on its plane, t>=0.
    low=Fraction(0);high=None
    for i in range(3):
        edge=e.sub(t[(i+1)%3],t[i],w)
        value=e.dot(n,e.cross(edge,e.sub(point,t[i],w),w),w)
        slope=e.dot(n,e.cross(edge,RAY,w),w)
        if slope==0:
            if value<0:return None
        else:
            bound=w.number(-value/slope)
            if slope>0:low=max(low,bound)
            else:high=bound if high is None else min(high,bound)
        if high is not None and low>high:return None
    if high is None:raise e.InvalidGeometry('Unbounded nonzero ray in finite triangle')
    if high<=0:return None
    return (low,high)

def _point_classification(material,point,w):
    boundary=[]
    for index,face in enumerate(material.faces):
        w.tick();t=tuple(material.vertices[i] for i in face)
        _,plane,sides=face_values(t,point,w)
        if plane==0 and all(x>=0 for x in sides):boundary.append(index)
    if boundary:
        return {'status':'UNRESOLVED_POINT_ON_BOUNDARY','membership_complete':False,'point_on_finite_face':True,'boundary_faces':tuple(boundary)}
    hits=[];events=[]
    for index,face in enumerate(material.faces):
        w.tick();t=tuple(material.vertices[i] for i in face);n=e.normal(t,w)
        plane=e.dot(n,e.sub(point,t[0],w),w);denominator=e.dot(n,RAY,w)
        if denominator==0:
            if plane==0:
                interval=coplanar_ray_interval(t,point,n,w)
                if interval is not None:events.append({'face':index,'kind':'finite_coplanar_ray','interval':interval})
            continue
        distance=w.number(-plane/denominator)
        if distance<=0:continue
        q=tuple(w.number(point[i]+w.number(distance*RAY[i])) for i in range(3))
        _,_,sides=face_values(t,q,w)
        if any(x<0 for x in sides):continue
        if any(x==0 for x in sides):
            events.append({'face':index,'kind':'ray_vertex' if sum(x==0 for x in sides)>=2 else 'ray_edge','distance':distance,'point':q})
        else:hits.append({'face':index,'sign':1 if denominator>0 else -1,'distance':distance,'point':q})
    if events:return {'status':'UNRESOLVED_AMBIGUOUS_FIXED_RAY','membership_complete':False,'point_on_finite_face':False,'events':tuple(events),'strict_hits':tuple(hits)}
    winding=sum(hit['sign'] for hit in hits)
    if material.qualification_kind=='embedded_bridge' and winding not in (0,1):raise e.InvalidGeometry('Embedded outward winding consistency')
    return {'status':'STRICT_INTERIOR' if winding!=0 else 'STRICT_EXTERIOR','membership_complete':True,'point_on_finite_face':False,'oriented_winding':winding,'strict_hits':tuple(hits)}

def classify_point(material,point,*,operation_limit=e.MAX_OPERATIONS):
    require_material(material);q=e.vector(point);w=e.Work(operation_limit)
    try:
        result=_point_classification(material,q,w)
        return {**result,'point':q,'ray':RAY,'operations':w.operations,'numeric_sha256':material.numeric_sha256,'embedding_certificate_sha256':material.embedding_certificate_sha256,'source_or_physical_material_authority':False}
    except e.OperationLimit as error:
        return {'status':'UNRESOLVED_OPERATION_LIMIT','membership_complete':False,'operations':w.operations,'reason':str(error),'source_or_physical_material_authority':False}

def disjoint_boundary_containment(first,second,representative_first,representative_second,*,operation_limit=e.MAX_OPERATIONS):
    require_material(first);require_material(second)
    if first.qualification_kind!='embedded_bridge' or second.qualification_kind!='embedded_bridge':raise e.InvalidGeometry('Disjoint containment requires complete embedded handles')
    a=e.vector(representative_first);b=e.vector(representative_second);w=e.Work(operation_limit)
    try:
        # Explicit caller representatives must lie on their own actual finite boundary.
        for material,q in ((first,a),(second,b)):
            boundary=_point_classification(material,q,w)
            if boundary['status']!='UNRESOLVED_POINT_ON_BOUNDARY':raise e.InvalidGeometry('Representative not on own finite boundary')
        considered=0
        for i,face in enumerate(first.faces):
            t=tuple(first.vertices[k] for k in face)
            for j,other in enumerate(second.faces):
                w.tick();considered+=1;u=tuple(second.vertices[k] for k in other)
                if any(max(p[axis] for p in t)<min(p[axis] for p in u) or max(p[axis] for p in u)<min(p[axis] for p in t) for axis in range(3)):continue
                hit=e._intersection(t,u,w)
                if hit['kind']!='empty':return {'status':'UNRESOLVED_BOUNDARIES_NOT_DISJOINT','boundary_coverage_complete':False,'pairs_considered':considered,'first_face':i,'second_face':j,'intersection':hit,'source_or_physical_material_authority':False}
        ab=_point_classification(second,a,w);ba=_point_classification(first,b,w)
        if not ab['membership_complete'] or not ba['membership_complete']:
            return {'status':'UNRESOLVED_FIXED_REPRESENTATIVE','boundary_coverage_complete':True,'containment_coverage_complete':False,'first_in_second':ab,'second_in_first':ba,'source_or_physical_material_authority':False}
        contained=ab['status']=='STRICT_INTERIOR' or ba['status']=='STRICT_INTERIOR'
        return {'status':'CONTAINED_MATERIAL_INTERIOR_EXISTS' if contained else 'CERTIFIED_DISJOINT_MATERIAL_INTERIORS',
                'boundary_coverage_complete':True,'containment_coverage_complete':True,'pairs_considered':considered,
                'first_in_second':ab,'second_in_first':ba,'common_strict_interior_point_claimed':False,
                'numeric_sha256':(first.numeric_sha256,second.numeric_sha256),
                'embedding_certificate_sha256':(first.embedding_certificate_sha256,second.embedding_certificate_sha256),
                'representatives':(a,b),'ray':RAY,'operations':w.operations,'source_or_physical_material_authority':False}
    except e.OperationLimit as error:return {'status':'UNRESOLVED_OPERATION_LIMIT','boundary_coverage_complete':False,'containment_coverage_complete':False,'operations':w.operations,'reason':str(error),'source_or_physical_material_authority':False}
