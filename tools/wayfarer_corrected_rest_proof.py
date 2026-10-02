"""Exact finite corrected-closed webbing/connector proof; BSD-3-Clause.
Only selected thick webbing and buckle have winding-defined material semantics.
A closed pressure shell is never interpreted as filled cabin air.
"""
import itertools
import math
from collections import Counter
from fractions import Fraction as F

from wayfarer_corrected_rest_geometry import NAMESPACE, require
NAMES = ['Shoulder restraint','Shoulder restraint.001','Lap restraint','Lap restraint.001','Anti-submarining strap']

def sub(a,b): return tuple(x-y for x,y in zip(a,b))

def dot(a,b): return sum(x*y for x,y in zip(a,b))

def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])

def plane(points, tri):
    a,b,c = (points[i] for i in tri)
    n = cross(sub(b,a), sub(c,a))
    if not any(n): raise ValueError('Degenerate triangle')
    d = dot(n,a)
    g = math.gcd(*n, d)
    return tuple(x//g for x in n)+(d//g,)

def oriented_key(tri):
    t = tuple(tri); key = tuple(sorted(t))
    inversions = sum(t[i]>t[j] for i in range(3) for j in range(i+1,3))
    return key, (-1 if inversions%2 else 1)

def chain(tris):
    c = Counter()
    for t in tris:
        k,sign=oriented_key(t); c[k]+=sign
    return +c, +Counter({k:-v for k,v in c.items()})

def topology(points,tris):
    edges=Counter(); oriented=Counter()
    for t in tris:
        plane(points,t)
        for a,b in zip(t,t[1:]+t[:1]):
            edges[tuple(sorted((a,b)))]+=1
            oriented[tuple(sorted((a,b)))]+=(1 if a<b else -1)
    vol6=sum(dot(points[t[0]],cross(points[t[1]],points[t[2]])) for t in tris)
    return {'edges':len(edges),'edge_incidence_two':all(v==2 for v in edges.values()),
            'oriented_edge_cancellation':all(v==0 for v in oriented.values()),
            'signed_volume_six_scaled_integer':str(vol6), 'positive_volume':vol6>0}

def hull_planes(points):
    result=set()
    for tri in itertools.combinations(range(len(points)),3):
        a,b,c=(points[i] for i in tri)
        n=cross(sub(b,a),sub(c,a))
        if not any(n):continue
        d=dot(n,a); signs=[dot(n,p)-d for p in points]
        if max(signs)>0 and min(signs)<0:continue
        if max(signs)>0:n=tuple(-x for x in n);d=-d
        if all(s==0 for s in signs):continue
        g=math.gcd(*n,d);result.add(tuple(x//g for x in n)+(d//g,))
    if len(result)<4:raise ValueError('Non-volume panel hull')
    return sorted(result)

def make_cells(geom,scale):
    points=[tuple(int(F(x)*scale) for x in p) for p in geom['vertices']]
    tris=geom['triangles']; n=len(points)//2
    if len(points)%4 or n<6:raise ValueError('Not selected paired-row ribbon topology')
    cells=[]; boundary=[]
    for i in range(n//2-1):
        ids=[2*i,2*i+1,2*i+2,2*i+3,2*i+n,2*i+n+1,2*i+n+2,2*i+n+3]
        subset=set(ids); ct=[list(t) for t in tris if set(t)<=subset]
        centre=tuple(sum(points[j][k] for j in ids) for k in range(3))
        for row in (i,i+1):
            if row in (0,n//2-1):continue
            a,b=2*row,2*row+1
            for tri in ([a,b,b+n],[a,b+n,a+n]):
                normal=cross(sub(points[tri[1]],points[tri[0]]),sub(points[tri[2]],points[tri[0]]))
                # Eight times the centre; no division or tolerance.
                if dot(normal,sub(centre,tuple(8*x for x in points[tri[0]])))>0:
                    tri=[tri[0],tri[2],tri[1]]
                ct.append(tri)
        topo=topology(points,ct)
        if not all(topo[k] for k in ('edge_incidence_two','oriented_edge_cancellation','positive_volume')):
            raise ValueError(('Panel boundary closure refused',i,topo))
        boundary.extend(ct)
        cellpoints=[points[j] for j in ids]
        cells.append({'index':i,'source_vertex_ids':ids,'triangles':ct,'points':cellpoints,
                      'planes':hull_planes(cellpoints),'topology':topo,
                      'bounds':[(min(p[k] for p in cellpoints),max(p[k] for p in cellpoints)) for k in range(3)]})
    exact=chain(boundary)==chain(tris)
    if not exact:raise ValueError('Panel chains do not equal entire original oriented source mesh')
    return cells,{'vertices':len(points),'triangles':len(tris),'cells':len(cells),
                  'source_boundary_equals_sum_of_closed_cells':exact,'source_topology':topology(points,tris)}

def intersection_vertices(a,b):
    if any(x[1]<y[0] or y[1]<x[0] for x,y in zip(a['bounds'],b['bounds'])):return [],'exact_disjoint_AABB'
    planes=sorted(set(a['planes']+b['planes']));vertices=set()
    for p,q,r in itertools.combinations(planes,3):
        n,m,l=p[:3],q[:3],r[:3]; ml=cross(m,l);det=dot(n,ml)
        if det==0:continue
        ln=cross(l,n);nm=cross(n,m)
        num=tuple(p[3]*ml[i]+q[3]*ln[i]+r[3]*nm[i] for i in range(3))
        if det<0:det=-det;num=tuple(-v for v in num)
        if any(dot(t[:3],num)>t[3]*det for t in planes):continue
        vertices.add(tuple(F(v,det) for v in num))
    return sorted(vertices),'all_exact_halfspace_triples'

def centre_ray_control(points,tris,centre,direction):
    hits=[]; boundary=[]
    for index,t in enumerate(tris):
        a,b,c=(points[i] for i in t); e1=sub(b,a);e2=sub(c,a)
        p=cross(direction,e2);det=dot(e1,p)
        if det==0:continue
        q=sub(centre,a);u=F(dot(q,p),det); qq=cross(q,e1)
        v=F(dot(direction,qq),det);distance=F(dot(e2,qq),det)
        if distance>0 and u>=0 and v>=0 and u+v<=1:
            h={'face':index,'t':str(distance),'u':str(u),'v':str(v),'exit_orientation':dot(cross(e1,e2),direction)>0}
            (boundary if u==0 or v==0 or u+v==1 else hits).append(h)
    return {'direction':list(direction),'strict_hits':hits,'ambiguous_boundary_hits':boundary,
            'one_strict_outward_exit_no_boundary_hits':len(hits)==1 and hits[0]['exit_orientation'] and not boundary}

def frac(v):return {'numerator':str(v.numerator),'denominator':str(v.denominator),'metres_or_dimensionless':float(v)}

def vec(v,scale):return [float(x/scale) for x in v]

def assess(label,geometries,scale,kernel):
    cells={};metadata={}
    for name in NAMES:cells[name],metadata[name]=make_cells(geometries[name],scale)
    records=[];minimum=None;counterexamples=[];checks=0
    for left,right in itertools.combinations(NAMES,2):
        pairs=[]
        for a in cells[left]:
            for b in cells[right]:
                vv,method=intersection_vertices(a,b);checks+=1
                rec={'left_cell':a['index'],'right_cell':b['index'],'method':method,'extreme_vertices':len(vv)}
                if vv:
                    slack=min(F(p[3])-dot(p[:3],v) for v in vv for p in kernel)
                    gap=min((F(p[3])-dot(p[:3],v))/(scale*sum(abs(x) for x in p[:3])) for v in vv for p in kernel)
                    rec.update({'every_extreme_vertex_strictly_inside_actual_buckle_face_kernel':slack>0,
                                'minimum_conservative_face_distance_metres':frac(gap),
                                'exact_extreme_vertices_scaled':[ [str(x) for x in v] for v in vv]})
                    if minimum is None or gap<minimum:minimum=gap
                    if slack<=0:
                        v,p=min(((v,p) for v in vv for p in kernel),key=lambda vp:(F(vp[1][3])-dot(vp[1][:3],vp[0]))/(scale*sum(abs(x) for x in vp[1][:3])))
                        counterexamples.append({'objects':[left,right],'cells':[a['index'],b['index']],
                            'point_current_craft_metres':vec(v,scale),'buckle_plane':list(p),
                            'signed_inside_distance_bound_metres':frac((F(p[3])-dot(p[:3],v))/(scale*sum(abs(x) for x in p[:3])))})
                pairs.append(rec)
        records.append({'objects':[left,right],'cell_pairs':pairs})
    return {'label':label,'ribbon_metadata':metadata,'cross_ribbon_cell_pairs':checks,
            'mutual_hull_intersections':sum(p['extreme_vertices']>0 for r in records for p in r['cell_pairs']),
            'all_overlap_enclosures_in_actual_buckle_kernel':not counterexamples,
            'minimum_conservative_face_distance_metres':frac(minimum) if minimum is not None else None,
            'counterexamples':counterexamples,'pairs':records}

def orient(a,b,p):return (b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0])

def polygon_area(p):return abs(sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(p,p[1:]+p[:1])))/2 if len(p)>=3 else F(0)

def clip(subject,clipper):
    p=subject
    sign=1 if orient(clipper[0],clipper[1],clipper[2])>0 else -1
    for a,b in zip(clipper,clipper[1:]+clipper[:1]):
        q=[]
        if not p:break
        for u,v in zip(p,p[1:]+p[:1]):
            du=sign*orient(a,b,u);dv=sign*orient(a,b,v)
            if du>=0:q.append(u)
            if (du<0)!=(dv<0):
                t=du/(du-dv);q.append(tuple(x+t*(y-x) for x,y in zip(u,v)))
        p=q
    return p


def checked_mesh(geometry, maximum=256):
    require(type(geometry) is dict, 'Proof mesh must be an object')
    vertices, triangles = geometry.get('vertices'), geometry.get('triangles')
    require(type(vertices) is list and 4 <= len(vertices) <= maximum and
            type(triangles) is list and 4 <= len(triangles) <= maximum*2, 'Finite proof mesh dimensions')
    require(all(type(p) in (list, tuple) and len(p) == 3 and all(
        type(x) in (int, float) and math.isfinite(x) and abs(x) <= 20 for x in p) for p in vertices),
        'Nonfinite or out-of-domain proof coordinates')
    require(all(type(t) in (list, tuple) and len(t) == 3 and all(
        type(i) is int and 0 <= i < len(vertices) for i in t) for t in triangles), 'Proof indices')
    return vertices, triangles


def finite_patch(first_geom, first_face_ids, second_geom, second_face_ids):
    """Exact opposed finite coplanar end-face patch; selected axis-aligned planes.

    These actual q0 connector faces are axis aligned after emitted float32 bake.
    Unsupported tilted planes refuse instead of using an approximate area.
    """
    first_vertices, first_triangles = checked_mesh(first_geom)
    second_vertices, second_triangles = checked_mesh(second_geom)
    for ids, triangles in ((first_face_ids, first_triangles), (second_face_ids, second_triangles)):
        require(type(ids) in (list, tuple) and 1 <= len(ids) <= 8 and len(set(ids)) == len(ids) and
                all(type(i) is int and 0 <= i < len(triangles) for i in ids), 'Finite patch face selection')
    # Summed pair areas equal the actual patch union only for non-overlapping
    # selected face partitions. Distinct IDs are not a geometric partition proof.
    for vertices, triangles, ids in ((first_vertices, first_triangles, first_face_ids),
                                     (second_vertices, second_triangles, second_face_ids)):
        projected = []
        reference = None
        for index in ids:
            points = [tuple(F(x) for x in vertices[i]) for i in triangles[index]]
            normal = cross(sub(points[1],points[0]),sub(points[2],points[0]))
            axes = [i for i,x in enumerate(normal) if x != 0]
            require(len(axes) == 1, 'Patch partition plane must be axis aligned')
            axis = axes[0]
            if reference is None:
                reference = (axis,points[0][axis],normal)
            require(axis == reference[0] and all(p[axis] == reference[1] for p in points) and
                    dot(normal,reference[2]) > 0, 'Selected patch faces do not share one oriented plane')
            projection = [i for i in range(3) if i != axis]
            projected.append([tuple(p[i] for i in projection) for p in points])
        require(all(polygon_area(clip(a,b)) == 0 for a,b in itertools.combinations(projected,2)),
                'Selected finite patch face interiors overlap')
    parts, total = [], F(0)
    for first in first_face_ids:
        cp = [tuple(F(x) for x in first_vertices[i]) for i in first_triangles[first]]
        cn = cross(sub(cp[1], cp[0]), sub(cp[2], cp[0]))
        axes = [i for i, x in enumerate(cn) if x != 0]
        require(len(axes) == 1, 'Patch plane must be nondegenerate axis aligned')
        axis = axes[0]; projection = [i for i in range(3) if i != axis]
        for second in second_face_ids:
            op = [tuple(F(x) for x in second_vertices[i]) for i in second_triangles[second]]
            on = cross(sub(op[1], op[0]), sub(op[2], op[0]))
            require(all(p[axis] == cp[0][axis] for p in cp+op) and
                    cross(cn, on) == (0,0,0) and dot(cn, on) < 0,
                    'Patch normals/gap must be exactly opposed/coplanar')
            polygon = clip([tuple(p[i] for i in projection) for p in cp],
                           [tuple(p[i] for i in projection) for p in op])
            area = polygon_area(polygon); total += area
            points = []
            for p in polygon:
                q = [None]*3; q[axis] = cp[0][axis]
                for i, value in zip(projection, p): q[i] = value
                points.append([str(x) for x in q])
            parts.append({'connector_source_face': first, 'other_source_face': second,
                          'exact_common_polygon_metres': points,
                          'area_square_metres': {'numerator': str(area.numerator),
                                                'denominator': str(area.denominator),
                                                'reported_double': float(area)},
                          'exactly_opposed_normals_and_zero_plane_gap': True})
    require(total > 0, 'No positive finite source attachment patch')
    return {'contact_parts': parts, 'total_exact_area_square_metres': {
        'numerator': str(total.numerator), 'denominator': str(total.denominator),
        'reported_double': float(total)}}


def prove_static_composite(contact_doc):
    """Purpose-specific finite q0 certificate; no caller collision exemptions."""
    require(type(contact_doc) is dict and contact_doc.get('schema') ==
            'apsis.corrected-closed-exact-source-contact/1' and
            contact_doc.get('coordinate_namespace') == NAMESPACE and
            type(contact_doc.get('fixed_operating_tuple')) is list and
            all(type(q) is int for q in contact_doc['fixed_operating_tuple']) and
            contact_doc['fixed_operating_tuple'] == [1,1,1,0] and
            contact_doc.get('actor_load_motion_or_runtime_admission') is False, 'Wrong fixed contact namespace/state')
    objects = contact_doc.get('objects')
    require(type(objects) is list and len(objects) == 66 and
            all(type(o) is dict and type(o.get('source_object')) is str and
                {'vertices_metres','triangles'} <= set(o) for o in objects) and
            len({o['source_object'] for o in objects}) == 66, 'Complete proof source roster')
    geo = {o['source_object']: {'vertices': o['vertices_metres'], 'triangles': o['triangles']} for o in objects}
    selected = NAMES+['Five-point buckle','Buckle release','Harness shoulder anchor',
                     'Harness shoulder anchor.001','Corrected restraint shoulder connector port',
                     'Corrected restraint shoulder connector starboard']
    expected_faces = {n: 36 if n.startswith('Shoulder') else 20 for n in NAMES}
    expected_faces.update({n: 12 if n.startswith('Corrected restraint') else 108 for n in selected if n not in NAMES})
    for name in selected:
        require(name in geo, 'Missing finite proof source object: ' + name)
        _, triangles = checked_mesh(geo[name])
        require(len(triangles) == expected_faces[name], 'Finite source topology differs: ' + name)
    scale = max(F(x).denominator for n in selected for p in geo[n]['vertices'] for x in p)
    require(scale & (scale-1) == 0, 'Source coordinate denominator must be binary')
    topology_checks = {}
    for name in selected:
        points = [tuple(int(F(x)*scale) for x in p) for p in geo[name]['vertices']]
        check = topology(points, geo[name]['triangles'])
        require(all(check[k] for k in ('edge_incidence_two','oriented_edge_cancellation','positive_volume')),
                'Finite source mesh not closed/oriented: ' + name)
        topology_checks[name] = check
    bp = [tuple(int(F(x)*scale) for x in p) for p in geo['Five-point buckle']['vertices']]
    bt = geo['Five-point buckle']['triangles']
    kernel = sorted(set(plane(bp, t) for t in bt))
    centre = tuple(F(sum(p[k] for p in bp),len(bp)) for k in range(3))
    require(all(dot(p[:3],centre) < p[3] for p in kernel), 'Buckle inward kernel centre refused')
    rays = [centre_ray_control(bp,bt,centre,d) for d in ((137,251,509),(331,127,613))]
    require(all(r['one_strict_outward_exit_no_boundary_hits'] for r in rays), 'Buckle exact interior ray refused')
    overlap = assess('actual emitted corrected-closed GLB-bound contact faces',geo,scale,kernel)
    require(overlap['cross_ribbon_cell_pairs'] == 76 and
            overlap['all_overlap_enclosures_in_actual_buckle_kernel'], 'Mutual ribbon volume escapes actual buckle')
    volume = {'schema': 'apsis.corrected-closed-emitted-finite-volume-proof/1',
              'model_sha256': contact_doc['model_sha256'], 'coordinate_namespace': NAMESPACE,
              'no_quantization_or_penetration_tolerance': True, 'exact_binary_coordinate_scale': str(scale),
              'buckle_actual_faces': len(bt), 'buckle_kernel_actual_face_planes': len(kernel),
              'buckle_exact_triangle_centre_ray_controls': rays, 'selected_mesh_topology': topology_checks,
              'proof': overlap,
              'material_semantics': 'Nonzero oriented winding for selected finite thick ribbons/buckle only; never pressure-shell cabin air.',
              'opening_strength_actor_load_runtime_admission': False}
    connections = []
    for connector,anchor,ribbon in [
            ('Corrected restraint shoulder connector port','Harness shoulder anchor','Shoulder restraint'),
            ('Corrected restraint shoulder connector starboard','Harness shoulder anchor.001','Shoulder restraint.001')]:
        for other,first,second in ((anchor,(4,5),(104,105)),(ribbon,(6,7),(18,19))):
            patch = finite_patch(geo[connector],first,geo[other],second)
            connections.append({'connector': connector,'other': other,
                                'fixed_state': 'corrected_closed_q0', **patch})
    attachments = {'schema': 'apsis.corrected-closed-emitted-finite-attachment-proof/1',
                   'model_sha256': contact_doc['model_sha256'], 'coordinate_namespace': NAMESPACE,
                   'connections': connections, 'no_epsilon': True,
                   'geometric_static_attachment_only_no_strength': True}
    release = [tuple(int(F(x)*scale) for x in p) for p in geo['Buckle release']['vertices']]
    outside = sum(any(dot(p[:3],v) > p[3] for p in kernel) for v in release)
    require(outside > 0, 'Unexpected release whole-kernel containment; source geometry changed')
    semantics = {'schema': 'apsis.corrected-closed-static-composite-semantics/1',
                 'state': 'corrected_closed_q0','model_sha256': contact_doc['model_sha256'],
                 'coordinate_namespace': NAMESPACE,'operating_tuple': [1,1,1,0],
                 'connector_fixed_connection_patches': connections,
                 'finite_buckle_coupler': {
                     'source_object': 'Five-point buckle','actual_contact_source_faces': list(range(len(bt))),
                     'material_definition': 'Actual closed finite buckle nonzero oriented winding; exact centre/kernel controls.',
                     'shared_webbing_objects': NAMES,
                     'restriction': 'Only actual mutual shared material proven inside buckle at q0; no whole-pair exemption or occupied-body contact permission.'},
                 'retained_buckle_release_assembly': {
                     'source_objects': ['Five-point buckle','Buckle release'],
                     'unchanged_same_tuple_baseline_required': True,
                     'finite_retained_overlap_region': 'Intersection of actual buckle and release material at fixed q0 only.',
                     'entire_release_inside_buckle_claimed': False,
                     'actual_release_vertices_outside_kernel': outside,
                     'new_motion_or_whole_pair_collision_exemption': False},
                 'all_source_surfaces_retained_as_actor_obstacles': True,
                 'pressure_shell_semantics': 'Unchanged surface boundaries enclosing cabin air, not filled solids.',
                 'attachment_semantics': 'Explicit fixed kinematic connection at exact opposed finite patches; no weld/fastener/strength/latch/pressure/load claim.',
                 'release_disaggregation_motion_channels': None,
                 'runtime_admission': False}
    return {'volume': volume, 'attachments': attachments, 'semantics': semantics}
