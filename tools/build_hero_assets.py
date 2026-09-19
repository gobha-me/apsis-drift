#!/usr/bin/env python3
"""Original Apsis Drift hero geometry, authored in metres. Blender 5.2.

blender -b --factory-startup --python tools/build_hero_assets.py -- cockpit
Use --draft for iteration, --final for 2560px renders, --export for GLB/LODs.
Each invocation constructs one independent editable scene. No downloaded art.
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import flight_cell
import build_flight_cell
OUT = ROOT / 'assets' / 'visual'
TAU = math.tau
MAT = {}
CACHE = {}
CURRENT = None
LEVEL = 0


def assembly(name, level=0):
    global CURRENT, LEVEL
    CURRENT = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(CURRENT)
    CURRENT['reduction_level'] = level
    LEVEL = level


def obj(name, mesh, loc=(0, 0, 0), scale=None, rot=None, mat=None):
    o = bpy.data.objects.new(name, mesh)
    CURRENT.objects.link(o)
    o.location = loc
    if scale is not None:
        o.scale = scale
    if rot is not None:
        o.rotation_euler = rot
    if mat is not None and mesh is not None:
        if len(mesh.materials) == 0:
            mesh.materials.append(MAT[mat])
        o.material_slots[0].link = 'OBJECT'
        o.material_slots[0].material = MAT[mat]
    o['detail_tier'] = LEVEL
    return o


def mesh(name, verts, faces):
    m = bpy.data.meshes.new(name)
    m.from_pydata(verts, [], faces)
    m.update()
    return m


def box(name, loc, size, mat='ivory', bevel=.025, rot=None):
    if len(size)!=3 or any(not math.isfinite(v) or v<=0 for v in size):
        raise ValueError(f'{name}: box dimensions must be finite and positive')
    # Geometry cached by size/material-independent profile; no operator overhead.
    key = ('box', tuple(size))
    if key not in CACHE:
        x, y, z = (v / 2 for v in size)
        CACHE[key] = mesh(name, [(-x,-y,-z),(x,-y,-z),(x,y,-z),(-x,y,-z),
                                (-x,-y,z),(x,-y,z),(x,y,z),(-x,y,z)],
                          [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])
    o = obj(name, CACHE[key], loc, rot=rot, mat=mat)
    if bevel:
        b = o.modifiers.new('Machined edge radius', 'BEVEL')
        b.width = min(bevel, min(size)*.25)
        b.segments = 3
        b = o.modifiers.new('Weighted corner normals', 'WEIGHTED_NORMAL')
    return o


def cyl(name, loc, radius, depth, mat='steel', axis=(0,0,1), n=24, r2=None):
    if n<3 or not all(math.isfinite(v) and v>0 for v in (radius,depth)):
        raise ValueError(f'{name}: invalid cylinder dimensions')
    if not all(math.isfinite(v) for v in axis) or Vector(axis).length==0:
        raise ValueError(f'{name}: invalid axis')
    key = ('cyl', radius, depth, n, r2)
    if key not in CACHE:
        rr = radius if r2 is None else r2
        v = [(r*math.cos(i*TAU/n), r*math.sin(i*TAU/n), z)
             for r,z in ((radius,-depth/2),(rr,depth/2)) for i in range(n)]
        f = [tuple(range(n-1,-1,-1)), tuple(range(n,n*2))]
        f += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        m = mesh(name,v,f)
        for p in list(m.polygons)[2:]:
            p.use_smooth = True
        CACHE[key] = m
    o = obj(name,CACHE[key],loc,mat=mat)
    o.rotation_mode = 'QUATERNION'
    o.rotation_quaternion = Vector((0,0,1)).rotation_difference(Vector(axis).normalized())
    return o


def beam(name, a, b, radius=.03, mat='steel', n=12):
    a,b = Vector(a),Vector(b)
    return cyl(name,(a+b)/2,radius,(b-a).length,mat,b-a,n)


def ring(name, loc, radius, width, mat='steel', axis=(0,0,1), n=64, minor=8):
    key=('ring',radius,width,n,minor)
    if key not in CACHE:
        v=[]
        for i in range(n):
            a=TAU*i/n
            for j in range(minor):
                b=TAU*j/minor
                r=radius+width*math.cos(b)
                v.append((r*math.cos(a),r*math.sin(a),width*math.sin(b)))
        f=[(i*minor+j,((i+1)%n)*minor+j,((i+1)%n)*minor+(j+1)%minor,
            i*minor+(j+1)%minor) for i in range(n) for j in range(minor)]
        m=mesh(name,v,f)
        for p in m.polygons: p.use_smooth=True
        CACHE[key]=m
    o=obj(name,CACHE[key],loc,mat=mat)
    o.rotation_mode='QUATERNION'
    o.rotation_quaternion=Vector((0,0,1)).rotation_difference(Vector(axis).normalized())
    return o


def label(name, body, loc, size=.04, mat='ink', rot=(math.pi/2,0,0), align='LEFT'):
    m=bpy.data.curves.new(name,'FONT')
    m.body=body
    m.size=size
    m.align_x=align
    m.extrude=.00015
    m.space_character=1.1
    return obj(name,m,loc,rot=rot,mat=mat)


def poly(name, verts, faces, mat='ivory', bevel=.015):
    o=obj(name,mesh(name,verts,faces),mat=mat)
    if bevel:
        b=o.modifiers.new('Edge fillet','BEVEL'); b.width=bevel; b.segments=2
        o.modifiers.new('Face weighted normals','WEIGHTED_NORMAL')
    return o


def wire(name, points, radius=.015, mat='copper'):
    m=bpy.data.curves.new(name,'CURVE');m.dimensions='3D';m.bevel_depth=radius;m.bevel_resolution=2
    s=m.splines.new('POLY');s.points.add(len(points)-1)
    for p,co in zip(s.points,points): p.co=(*co,1)
    return obj(name,m,mat=mat)


def sphere(name,loc,scale,mat='steel'):
    key='sphere'
    if key not in CACHE:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=24,ring_count=12)
        t=bpy.context.object;CACHE[key]=t.data;bpy.data.objects.remove(t,do_unlink=True)
        for p in CACHE[key].polygons:p.use_smooth=True
    return obj(name,CACHE[key],loc,scale,mat=mat)


def materials():
    palette={
        'ivory':((.43,.46,.42),.40,.40), 'paint':((.12,.19,.21),.50,.40),
        'black':((.012,.019,.023),.45,.36), 'panel':((.031,.047,.055),.6,.31),
        'steel':((.32,.38,.40),.85,.24), 'copper':((.32,.14,.062),.78,.28),
        'orange':((.70,.16,.036),.35,.38), 'rubber':((.006,.009,.011),0,.72),
        'fabric':((.068,.095,.09),0,.9), 'ink':((.63,.76,.72),.1,.45),
        'solar':((.012,.03,.085),.76,.24), 'gold':((.43,.29,.09),.75,.28),
        'screen':((.001,.014,.021),.18,.28), 'cyan':((.03,.65,.8),.1,.32),
        'amber':((.95,.40,.075),.1,.35), 'red':((.9,.02,.008),.1,.4),
        'white':((.8,.92,1),.1,.3), 'green':((.1,.75,.36),.1,.4),
    }
    for name,(color,metal,rough) in palette.items():
        m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
        nodes=m.node_tree.nodes;links=m.node_tree.links;b=nodes.get('Principled BSDF')
        b.inputs['Base Color'].default_value=(*color,1)
        b.inputs['Metallic'].default_value=metal;b.inputs['Roughness'].default_value=rough
        if name in ('cyan','amber','red','white','green','screen'):
            b.inputs['Emission Color'].default_value=(*color,1)
            b.inputs['Emission Strength'].default_value=.3 if name=='screen' else 2.2
        elif name not in ('ink',):
            tex=nodes.new('ShaderNodeTexNoise');tex.inputs['Scale'].default_value=170 if name=='fabric' else 85
            tex.inputs['Detail'].default_value=3
            bump=nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.24 if name=='fabric' else .13
            bump.inputs['Distance'].default_value=.0008 if name=='fabric' else .00018
            links.new(tex.outputs['Fac'],bump.inputs['Height']);links.new(bump.outputs['Normal'],b.inputs['Normal'])
            ramp=nodes.new('ShaderNodeMapRange');ramp.inputs['To Min'].default_value=rough-.07
            ramp.inputs['To Max'].default_value=rough+.09
            links.new(tex.outputs['Fac'],ramp.inputs['Value']);links.new(ramp.outputs['Result'],b.inputs['Roughness'])
            if name in ('ivory','paint','steel','black'):
                grime=nodes.new('ShaderNodeTexNoise');grime.inputs['Scale'].default_value=5.5
                grime.inputs['Detail'].default_value=5;grime.inputs['Roughness'].default_value=.72
                tint=nodes.new('ShaderNodeValToRGB')
                tint.color_ramp.elements[0].position=.18
                tint.color_ramp.elements[0].color=(*(v*.65 for v in color),1)
                tint.color_ramp.elements[1].position=.78
                tint.color_ramp.elements[1].color=(*color,1)
                links.new(grime.outputs['Fac'],tint.inputs[0]);links.new(tint.outputs[0],b.inputs['Base Color'])
        MAT[name]=m


def fasteners(name,x,y,z,w,h,mat='steel'):
    for dx in (-w/2+.026,w/2-.026):
        for dz in (-h/2+.026,h/2-.026):
            cyl(name+' captive screw',(x+dx,y,z+dz),.007,.005,mat,(0,-1,0),6)
            box(name+' screw slot',(x+dx,y-.003,z+dz),(.008,.002,.0014),'black',0)


def display(name,loc,w,h,mode=0):
    x,y,z=loc
    box(name+' chassis',(x,y+.05,z),(w+.075,.15,h+.075),'black',.025)
    box(name+' inset',(x,y-.031,z),(w+.022,.012,h+.02),'steel',.008)
    box(name+' glass',(x,y-.040,z),(w,.010,h),'screen',.005)
    front=y-.048
    def ln(a,b,mat='cyan',r=.0015):
        beam(name+' display trace',(x+a[0],front,z+a[1]),(x+b[0],front,z+b[1]),r,mat,6)
    def txt(s,xx,zz,size=.016,mat='cyan'):
        label(name+' '+s,s,(x+xx,front-.003,z+zz),size,mat)
    txt(['FLIGHT / ORBITAL','VECTOR / RENDEZVOUS','THERMAL / POWER'][mode],-w*.44,h*.40,.022)
    ln((-w*.44,h*.34),(w*.44,h*.34),'ink')
    if mode==0:
        for i in range(-3,4):
            yy=i*.032
            ln((-.105 if i%2==0 else -.06, yy),(.105 if i%2==0 else .06, yy),'cyan')
            if i:txt(str(abs(i)*10),.118,yy-.007,.014)
        ln((-.16,0),(-.038,0),'amber',.0025);ln((.038,0),(.16,0),'amber',.0025)
        ln((-.038,0),(0,-.024),'amber');ln((0,-.024),(.038,0),'amber')
        txt('ALT',-w*.45,.05);txt('042.7',-w*.45,.01,.023,'white')
        txt('VEL',w*.27,.05);txt('071',w*.27,.01,.023,'white')
        txt('AP  HOLD    +00.4    RCS READY',-w*.44,-h*.40,.016,'green')
    elif mode==1:
        for r in (.038,.080,.12):ring(name+' radar ring',(x,front,z),r,.0012,'cyan',(0,1,0),48,4)
        ln((-.15,0),(.15,0));ln((0,-.15),(0,.15))
        for i in range(10):
            a=i*2.399;xx=math.cos(a)*(.028+i*.010);zz=math.sin(a)*(.028+i*.010)
            box(name+' return',(x+xx,front-.002,z+zz),(.005,.001,.005),'amber',0)
        txt('ORIGIN  /  PIER A',-w*.44,-h*.36,.022,'white')
        txt('RNG  04.20 KM',-w*.44,-h*.44,.016)
    else:
        for i,lab in enumerate(('REACTOR','FUEL A','FUEL B','COOLANT','RCS')):
            zz=h*.22-i*h*.115
            txt(lab,-w*.43,zz,.016,'ink')
            for j in range(13):
                box(name+' bargraph',(x+.006+j*w*.025,front,z+zz+.005),
                    (w*.018,.002,.013),'cyan' if j<10-i else 'panel',0)
        txt('BUS 01  98%     BUS 02  97%',-w*.43,-h*.41,.016,'green')
    fasteners(name,x,y-.046,z,w+.075,h+.075)
    for side in (-1,1):
        for i in range(5):box(name+' soft key',(x+side*(w/2+.027),y-.044,z-h*.34+i*h*.17),(.013,.014,.021),'rubber',.002)


def panel(name,loc,w,h,rows=3,cols=5,yaw=0,tilt=0):
    before=set(CURRENT.objects)
    x,y,z=loc
    box(name,loc,(w,.065,h),'panel',.012)
    fasteners(name,x,y-.036,z,w,h)
    label(name+' legend',name.upper(),(x-w*.43,y-.037,z+h*.34),.022,'ink')
    for i in range(rows):
        for j in range(cols):
            xx=x-w*.35+j*w*.7/max(cols-1,1);zz=z+h*.16-i*h*.52/max(rows-1,1)
            cyl(name+' socket',(xx,y-.038,zz),.013,.008,'steel',(0,-1,0),16)
            beam(name+' toggle',(xx,y-.043,zz),(xx,y-.07,zz+.011),.004,'ink',8)
            box(name+' status',(xx,y-.039,zz+.035),(.012,.003,.004),'green' if (i+j)%3 else 'amber',0)
            label(name+' index',f'{i+1}{j+1}',(xx-.009,y-.040,zz-.028),.012,'ink')
    # Rotate the COMPLETE assembly, including legends, lever sockets and lamps.
    # Default front is -Y; +X tilt tips that normal down, hence negative tilt
    # makes the operating face look upward toward a seated pilot.
    pivot=Vector(loc)
    rotation=Matrix.Rotation(yaw,4,'Z') @ Matrix.Rotation(tilt,4,'X')
    transform=Matrix.Translation(pivot) @ rotation @ Matrix.Translation(-pivot)
    bpy.context.view_layer.update()
    for part in set(CURRENT.objects)-before:
        part.matrix_world=transform @ part.matrix_world
    face=rotation.to_3x3() @ Vector((0,-1,0))
    return face


def forward_dashboard(name='Forward avionics deck', bevel=.012):
    """Closed tapered equipment cover, seated-eye clearance preserved."""
    outline=[(-1.74,1.47),(1.74,1.47),(1.60,2.50),
             (1.45,3.64),(-1.45,3.64),(-1.60,2.50)]
    top=[(x,y,1.155+(y-1.47)*.065) for x,y in outline]
    vertices=[(x,y,z-.13) for x,y,z in top]+top
    faces=[tuple(range(5,-1,-1)),tuple(range(6,12))]
    faces += [(i,(i+1)%6,(i+1)%6+6,i+6) for i in range(6)]
    return poly(name,vertices,faces,'rubber',bevel)


def cockpit_revision4():
    assembly('00 | pressure vessel and canopy',0)
    box('Floor sandwich',(0,1,-.07),(4.0,5.8,.18),'black',.04)
    box('Rear pressure bulkhead',(0,-1.87,1.52),(3.88,.18,3.18),'paint',.055)
    box('Forward footwell closure',(0,3.72,.58),(3.70,.18,1.40),'paint',.04)
    roof=poly('Aft overhead pressure liner',
              [(-1.84,-1.81,2.97),(1.84,-1.81,2.97),(1.43,.3,3.10),
               (0,.3,3.18),(-1.43,.3,3.10)],[(0,1,2,3,4)],'paint',0)
    roof.modifiers.new('Pressure roof thickness','SOLIDIFY').thickness=.10
    beam('Roof centre load spine',(0,-1.80,2.98),(0,.3,3.18),.07,'paint')
    beam('Rear roof cross member',(-1.80,-1.76,2.97),(1.80,-1.76,2.97),.075,'paint')
    box('Forward canopy coaming',(0,3.65,1.26),(3.85,.30,.22),'paint',.04)
    for s in (-1,1):
        box('Aft upper side closure',(s*1.84,-1.27,2.12),(.14,1.14,1.58),'paint',.04)
        beam('Rear structural corner',(s*1.77,-1.72,.12),(s*1.77,-1.72,2.97),.075,'steel')
    for s in (-1,1):
        box('Pressure sill',(s*1.92,1,.67),(.22,5.4,1.44),'paint',.05)
        for y in (-1.5,-.3,.9,2.1):
            box('Side liner',(s*1.79,y,.75),(.035,1.08,1.20),'panel',.018)
        # Faceted wraparound canopy ribs: large clear field of view.
        bow=[(s*1.83,-1.55,1.30),(s*1.78,-.65,2.65),
             (s*1.43,.3,3.10),(s*.92,3.05,2.75),(s*1.45,3.65,1.30)]
        wire('Forged canopy bow',bow,.072,'paint')
        wire('Canopy seal',[(x-s*.045,y,z-.025) for x,y,z in bow],.017,'rubber')
        beam('Forward A pillar',(s*1.60,2.5,1.30),(s*.92,3.05,2.75),.055,'paint',12)
        beam('A pillar warning insert',(s*1.60,2.5,1.30),(s*1.46,2.61,1.60),.058,'orange',12)
        beam('Upper cross beam',(s*1.43,.3,3.10),(0,.3,3.18),.075,'paint')
        beam('Forward crown',(s*.92,3.05,2.75),(0,3.10,2.80),.068,'paint')
        wire('Window sill',[(s*1.83,-1.55,1.30),(s*1.84,-.6,1.30),
                           (s*1.60,2.5,1.30),(s*1.45,3.65,1.30)],.09,'black')
        for x,y in ((s*1.83,-1.55),(s*1.60,2.5),(s*1.45,3.65)):
            box('Canopy structural foot',(x,y,1.28),(.24,.26,.18),'steel',.025)
            # Load path from inset window frame into the pressure hull sill.
            beam('Canopy sill outrigger',(x,y,1.27),(s*1.91,y,1.27),.06,'paint')
            for yy in (-.08,.08):cyl('Canopy foot bolt',(x,y+yy,1.375),.017,.016,'steel',n=6)
    # No glass sheet in the pilot preview: clean aperture suitable for viewport masks.
    assembly('10 | instrument architecture',0)
    box('Main console body',(0,1.33,.70),(3.35,.65,.69),'black',.10)
    box('Glare shield',(0,1.37,1.15),(3.48,.42,.075),'rubber',.025)
    forward_dashboard()
    # An enclosed avionics bay, not a void or a decorative floating shelf.
    for x in (-1.28,1.28):
        beam('Forward deck bearer',(x,1.50,1.065),(x,3.62,1.20),.045,'paint')
    # Low-contrast removable covers follow the deck slope; no pretend HUD.
    deck_pitch=math.atan(.065)
    for x in (-.91,0,.91):
        y=2.36
        z=1.155+(y-1.47)*.065
        box('Avionics access cover',(x,y,z+.006),(.78,1.26,.018),
            'panel',.009,rot=(deck_pitch,0,0))
        for dx in (-.33,.33):
            for dy in (-.53,.53):
                cyl('Access cover captive screw',(x+dx,y+dy,z+.019+dy*.065),
                    .014,.006,'black',axis=(0,-.065,1),n=8)
    for x in (-.98,0,.98):
        y=3.34
        z=1.155+(y-1.47)*.065
        box('Windshield demister housing',(x,y,z+.009),(.75,.16,.026),
            'black',.008,rot=(deck_pitch,0,0))
        for j in range(9):
            box('Demister outlet',(x-.30+j*.075,y,z+.024),(.043,.105,.006),
                'rubber',.001,rot=(deck_pitch,0,0))
    for x in (-1.35,1.35):
        box('Instrument deck pedestal',(x,1.34,.31),(.18,.46,.55),'paint',.025)
        box('Instrument pedestal foot',(x,1.34,.075),(.32,.57,.09),'steel',.015)
        beam('Glare shield support',(x,1.37,1.01),(x,1.37,1.16),.035,'steel')
    for i,x in enumerate((-.99,0,.99)):
        display(('NAV','FLIGHT','SYSTEM')[i],(x,.96,.82),.74,.46,(1,0,2)[i])
        panel(('COMMS / DATA','FLIGHT MODES','POWER ROUTING')[i],(x,.927,.42),.78,.23,1,7)
        for j in range(4):
            box('Annunciator module',(x-.29+j*.19,.868,1.099),(.15,.025,.041),'black',.003)
            label('Annunciator legend',('RCS','FUEL','NAV','LINK')[j],
                  (x-.29+j*.19,.851,1.094),.015,'green' if j%2 else 'amber',align='CENTER')
    for s in (-1,1):
        box('Side console',(s*1.48,-.40,.46),(.75,1.92,.90),'black',.075)
        # Inboard sloping control faces: pilot does not have to lean outside
        # the seat or read the back/edge of a forward-facing switch card.
        for k,name in enumerate(('PROPULSION','RCS / TRIM') if s<0 else ('ENVIRONMENT','POWER / AUX')):
            panel(name,(s*1.37,-.88+k*.85,1.02),.76,.48,3,5,
                  yaw=-s*math.pi/2,tilt=-math.radians(55))
        beam('Console padded inboard rail',(s*1.08,-1.32,.88),(s*1.08,.51,.88),.045,'rubber')
        # Throttle and side stick with boots, guard, and thumb control.
        box('Control arm cantilever',(s*1.13,-.36,.735),(.50,.42,.18),'paint',.035)
        beam('Control arm deck stay',(s*1.12,-.36,.10),(s*1.12,-.36,.75),.055,'paint')
        box('Control stay foot',(s*1.12,-.36,.058),(.19,.20,.075),'steel',.015)
        cyl('Gimbal flange',(s*.93,-.36,.84),.09,.035,'steel')
        for k in range(5):ring('Rubber bellows',(s*.93,-.36,.88+k*.016),.063-k*.003,.008,'rubber',n=24)
        beam('Control shaft',(s*.93,-.36,.90),(s*.90,-.29,1.07),.018,'steel')
        box('Ergonomic stick grip',(s*.90,-.28,1.12),(.061,.09,.15),'rubber',.024,rot=(.22,0,0))
        cyl('Thumb hat',(s*.90,-.329,1.17),.013,.011,'orange',(0,-1,0),16)
    # No combiner placeholder: a functional HUD needs an explicit optical/UI
    # design and pilot eye-box, not an empty frame below the windshield.
    assembly('20 | overhead and service panels',1)
    box('Overhead equipment raft',(0,.23,2.77),(1.28,1.35,.13),'black',.04)
    for x in (-.48,.48):
        for y in (-.20,.62):
            beam('Overhead raft suspension',(x,y,2.80),(x,y,3.13),.03,'steel')
        beam('Overhead raft bearer',(x,-.30,3.13),(x,.66,3.13),.045,'paint')
    beam('Overhead forward cross tie',(-.65,.62,3.13),(.65,.62,3.13),.055,'paint')
    beam('Overhead aft cross tie',(-.70,-.2,3.13),(.70,-.2,3.13),.055,'paint')
    beam('Overhead forward tie to crown',(0,.3,3.18),(0,.62,3.13),.055,'paint')
    # Face panel toward pilot, upper edge never blocks forward canopy.
    panel('FLIGHT BUS / MAIN',(0,-.43,2.62),1.15,.30,2,9)
    for s in (-1,1):
        face=panel('CABIN PRESSURE' if s<0 else 'EMERGENCY',
                   (s*1.72,-.65,1.71),.42,.61,5,3,yaw=-s*math.pi/2)
        to_pilot=(Vector((0,-.78,1.74))-Vector((s*1.72,-.65,1.71))).normalized()
        assert face.dot(to_pilot)>.98, 'Side controls must face pilot eye'
        for z in (1.46,1.95):
            beam('Side switch card bracket',(s*1.74,-.65,z),(s*1.85,-.95,z),.025,'steel')
        # Follow the opaque aft wall, not a diagonal suspended across the window.
        wire('Braided wiring loom',[(s*1.755,-1.17,.88),(s*1.755,-1.17,2.72),
             (s*1.755,-1.70,2.72),(s*.55,-1.70,2.72)],.021,'rubber')
        box('Aft bus cable tray',(s*1.16,-1.76,2.72),(1.37,.10,.11),'panel',.014)
        box('Aft bus junction',(s*.55,-1.70,2.72),(.15,.12,.14),'paint',.018)
        for z in (1.1,1.5,1.9,2.3,2.68):
            box('Aft loom saddle',(s*1.765,-1.17,z),(.055,.095,.028),'steel',.005)
        box('Wiring entry gland',(s*1.755,-1.17,.92),(.10,.10,.13),'steel',.015)
        box('Side power cable tray',(s*1.80,0,1.17),(.045,1.62,.15),'panel',.013)
        for j in range(4):
            z=1.125+j*.028
            wire('Power wire',[(s*1.774,-.79,z),(s*1.774,.79,z)],.006,'copper' if j%2 else 'black')
        for y in (-.70,0,.70):
            box('Power tray clamp',(s*1.765,y,1.17),(.022,.035,.14),'steel',.006)
        beam('Cabin practical',(s*1.64,-.9,2.2),(s*1.48,-.15,2.75),.013,'white')
        beam('Cabin lamp lower clip',(s*1.64,-.9,2.2),(s*1.80,-.9,2.2),.016,'steel')
        beam('Cabin lamp upper clip',(s*1.48,-.15,2.75),(s*1.60,-.15,2.89),.016,'steel')
        for i in range(10):
            box('Console cooling slot',(s*1.48,-1.427,.27+i*.022),(.51,.007,.009),'rubber',.002)
    label('Deck serial','WF / 01     FLIGHT SYSTEMS',(0,.773,1.13),.026,'ink',align='CENTER')
    label('Master warning','MASTER CAUTION',(-.47,.762,1.12),.013,'amber',align='CENTER')
    for x in (-1.43,-1.27,1.27,1.43):
        cyl('Backup rotary',(x,.887,1.02),.022,.028,'steel',(0,-1,0),24)
        ring('Rotary index',(x,.870,1.02),.017,.0015,'ink',(0,1,0),24,4)
    assembly('30 | couch and cabin equipment',1)
    # Closed, finished walking surfaces; geometric seams survive GLB export.
    for x in (-.72,0,.72):
        for y in (-1.36,-.62,.12,.86,1.60,2.34,3.08):
            box('Removable deck tile',(x,y,.035),(.69,.71,.028),'panel',.009)
            for j in range(6):
                box('Deck non slip tread',(x,y-.26+j*.10,.053),(.59,.018,.009),'rubber',.003)
    box('Rear hatch frame',(0,-1.755,1.44),(1.08,.085,2.44),'black',.035)
    box('Rear pressure hatch',(0,-1.697,1.43),(.96,.045,2.30),'ivory',.025)
    # Rear legends face forward into the cabin, not through the pressure wall.
    label('Aft exit legend','CABIN / 01',(0,-1.66,2.34),.085,'ink',(math.pi/2,0,math.pi),align='CENTER')
    for s in (-1,1):
        box('Aft equipment locker',(s*1.16,-1.68,1.32),(.75,.16,1.93),'panel',.035)
        box('Locker inset',(s*1.16,-1.583,1.32),(.64,.035,1.76),'paint',.018)
        beam('Locker grab handle',(s*1.16,-1.54,1.15),(s*1.16,-1.54,1.49),.016,'steel')
        for z in (1.15,1.49):cyl('Locker handle fixing',(s*1.16,-1.56,z),.024,.06,'steel',(0,1,0),16)
        beam('Hatch rescue rail',(s*.58,-1.52,.70),(s*.58,-1.52,2.07),.025,'orange')
        for z in (.70,2.07):
            beam('Hatch rail standoff',(s*.58,-1.52,z),(s*.58,-1.78,z),.025,'steel')
            cyl('Hatch rail wall flange',(s*.58,-1.775,z),.052,.028,'steel',(0,1,0),24)
        beam('Recessed deck light',(s*.97,-1.40,.11),(s*.97,1.01,.11),.007,'cyan')
        for j in range(6):
            box('Rear ventilation grille',(s*1.14,-1.565,2.03+j*.032),(.48,.012,.012),'black',.003)
    box('Pilot couch plinth',(0,-.86,.16),(.64,.80,.27),'black',.06)
    for x in (-.22,.22):
        box('Seat adjustment slide',(x,-.84,.32),(.075,.72,.10),'steel',.013)
    box('Pilot seat pan',(0,-.80,.43),(.62,.70,.16),'fabric',.08)
    box('Acceleration couch back',(0,-1.16,.92),(.67,.19,.98),'fabric',.075,(-.12,0,0))
    box('Headrest',(0,-1.25,1.50),(.40,.20,.25),'rubber',.075)
    box('Harness buckle',(0,-.89,.55),(.20,.085,.14),'steel',.022)
    box('Pedal floor mounting cassette',(0,.75,.115),(.83,.52,.18),'black',.025)
    cyl('Pedal hinge shaft',(0,.75,.208),.022,.78,'steel',(1,0,0),24)
    for s in (-1,1):
        box('Shoulder bolster',(s*.37,-1.17,1.09),(.11,.25,.57),'black',.05)
        wire('Harness webbing',[(s*.18,-1.28,1.42),(s*.16,-1.01,1.0),(s*.07,-.89,.62)],.025,'orange')
        cyl('Bailout oxygen',(s*1.72,-1.25,.31),.075,.40,'orange')
        ring('Oxygen strap',(s*1.72,-1.25,.27),.078,.009,'steel',n=24)
        box('Foot pedal',(s*.25,.75,.23),(.21,.31,.035),'steel',.012,(-.2,0,0))
        for i in range(5):box('Pedal traction',(s*.25,.64+i*.038,.264),(.18,.009,.006),'rubber',0)
    # An exterior docking reference is part of the lighting set, not the export.
    assembly('STAGE | distant hangar and practical lights',9)
    for i in range(8):
        y=9+i*4
        for s in (-1,1):
            beam('Dock corridor light',(s*(3+i*.03),y,-.5),(s*(3+i*.03),y,3.7),.035,'cyan')
            box('Hangar wall bay',(s*4.1,y,1.4),(.5,3.8,6),'panel',.04)
            beam('Hangar structural brace',(s*3.80,y-1.7,-1.5),(s*3.80,y+1.7,4),.11,'steel')
        beam('Dock overhead strip',(-3,y,3.7),(3,y,3.7),.035,'white')
        box('Dock deck segment',(0,y,-1.60),(8,3.8,.18),'paint',.03)
        for s in (-1,1):box('Deck approach stripe',(s*1.9,y,-1.495),(.12,2.9,.01),'amber',0)
    box('Dock far bulkhead',(0,44,1.5),(22,.3,15),'panel',.01)
    label('Dock yard marking','ORIGIN\nBAY 04',(0,43.8,1.8),1.4,'ink',align='CENTER')
    camera('01 | pilot eye',(0,-.78,1.74),(0,2.6,1.08),18)
    camera('02 | instrument inspection',(.04,-.34,1.29),(0,1.0,.75),30,active=False)
    area('Canopy cool sky',(0,6,5),(0,0,.8),1100,5,(.40,.68,1))
    area('Instrument soft fill',(0,-1,2.5),(0,1,.6),130,2,(.55,.74,1))
    area('Port amber practical',(-1.5,-.8,1.8),(0,.1,.6),75,1,(1,.35,.12))
    area('Hangar fill',(0,18,3.5),(0,24,0),1200,5,(.5,.72,1))


def cockpit():
    build_flight_cell.build(sys.modules[__name__])


def hull_section(y,w,top,bottom):
    height=top-bottom
    if not all(math.isfinite(v) for v in (y,w,top,bottom)) or height<=0 or w<=0:
        raise ValueError('Hull section must have positive finite height and width')
    upper=min(.40,height*.30);lower=min(.25,height*.25)
    return [(-w*.67,y,top), (w*.67,y,top), (w,y,top-upper),
            (w,y,bottom+lower),(w*.68,y,bottom),(-w*.68,y,bottom),
            (-w,y,bottom+lower),(-w,y,top-upper)]


def loft(name,sections,mat='ivory',bevel=.05):
    v=[co for sec in sections for co in hull_section(*sec)]
    n=len(sections)
    f=[tuple(range(7,-1,-1)),tuple(range((n-1)*8,n*8))]
    f += [(j*8+i,j*8+(i+1)%8,(j+1)*8+(i+1)%8,(j+1)*8+i) for j in range(n-1) for i in range(8)]
    return poly(name,v,f,mat,bevel)


def wing(name,s,mat='ivory'):
    points=[(s*1.65,5.9),(s*3.15,3.4),(s*7.0,-4.9),(s*6.85,-7.0),(s*2,-6.5)]
    v=[(x,y,z) for z in (.27,.63) for x,y in points]
    f=[(4,3,2,1,0),(5,6,7,8,9)]+[(i,(i+1)%5,(i+1)%5+5,i+5) for i in range(5)]
    return poly(name,v,f,mat,.045)


def engine(name,x,y,z):
    cyl(name+' pressure jacket',(x,y,z),1.12,4.8,'black',(0,1,0),48)
    for j in range(9):ring(name+' reinforcement',(x,y-1.8+j*.48,z),1.13,.045,'steel',(0,1,0),64)['detail_tier']=1
    for i in range(16):
        a=TAU*i/16
        xx=x+1.05*math.cos(a);zz=z+1.05*math.sin(a)
        beam(name+' coolant line',(xx,y-2,zz),(xx,y+2.1,zz),.035,'copper')['detail_tier']=1
    # Open bell made of nested rings and strips; no glowing solid plug.
    for j in range(5):ring(name+' nozzle collar',(x,y-2.6-j*.17,z),1.12+j*.055,.038,'steel',(0,1,0),64)['detail_tier']=1
    for i in range(36):
        a=TAU*i/36;b=TAU*(i+.82)/36
        v=[(x+r*math.cos(t),yy,z+r*math.sin(t))
           for r,yy in ((.73,y-2.35),(1.34,y-3.32)) for t in (a,b)]
        poly(name+' nozzle petal',v,[(0,1,3,2)],'panel',0)
    ring(name+' plasma annulus',(x,y-2.45,z),.62,.045,'cyan',(0,1,0),64)
    cyl(name+' dark throat',(x,y-2.34,z),.61,.03,'black',(0,1,0),48)
    for i in range(12):
        a=TAU*i/12
        beam(name+' thrust vane',(x+.50*math.cos(a),y-2.51,z+.50*math.sin(a)),
             (x+.67*math.cos(a),y-2.49,z+.67*math.sin(a)),.018,'steel')


def ship():
    assembly('00 | continuous pressure hull and thermal belly',0)
    secs=[(-8.0,1.7,2.25,-.45),(-5.3,2.3,2.65,-.55),(-1,2.4,2.75,-.50),
          (1.85,2.273333333333,2.686666666667,-.405),
          (6.3,1.72,1.30,-.12),(9.3,.70,1.10,.10),(10,.28,.68,.25)]
    # Pressure vessel sits inside its armor. Recomputed chamfers on subdivided
    # plates must never intersect the large underlying ruled hull faces.
    substrate=[(y,w*.95,top-.08,bottom+.08) for y,w,top,bottom in secs]
    # The flight-cell interval is genuinely open, not painted glass over an
    # opaque substrate. Shared panels supply its entire pressure envelope.
    loft('Primary aft hull substrate',substrate[:4],'black',.055)
    loft('Primary nose hull substrate',substrate[4:],'black',.025)
    # Deliberate spaced armor panels follow the loft, exposing panel gaps.
    for j in range(len(secs)-1):
        a,b=secs[j],secs[j+1]
        if a[0]>=1.85 and b[0]<=6.3:continue
        for k in range(3):
            t0=(k+.035)/3;t1=(k+.965)/3
            ab=[tuple(a[q]*(1-t)+b[q]*t + (0,.025,.025,-.025)[q] for q in range(4)) for t in (t0,t1)]
            loft(f'Pressure armor {j:02}.{k:02}',ab,'ivory' if (j+k)%4 else 'paint',.035)
    for s in (-1,1):wing('Swept lifting chine',s)
    build_flight_cell.envelope(sys.modules[__name__],exterior=True)
    assembly('20 | main propulsion and thrust frame',0)
    for s in (-1,1):
        box('Thrust bridge',(s*2.70,-5.5,.92),(1.6,3.5,.70),'paint',.1)
        engine('Engine '+str(s),s*3.05,-6.0,1.04)
        for j in range(4):
            box('Engine armor petal',(s*3.05,-3.9-j*.49,2.17),(1.38,.39,.15),'ivory',.045)
        # Canted vertical stabilizer, actual swept planform.
        verts=[(s*4.10,-4.9,.6),(s*4.10,-7.2,.6),(s*4.74,-7,3.20),(s*4.65,-6.25,3.3)]
        fin=poly('Canted dorsal stabilizer',verts,[(0,1,2,3)],'paint',.02)
        m=fin.modifiers.new('Fin thickness','SOLIDIFY');m.thickness=.16
        beam('Fin leading orange',(s*4.1,-4.9,.6),(s*4.65,-6.25,3.3),.055,'orange')
    assembly('30 | inset machinery and thermal management',1)
    for s in (-1,1):
        # Recessed side machinery cassettes, pipes, louver plates, service handles.
        for j in range(5):
            y=-3.65+j*1.35
            box('Inset service cassette',(s*2.43,y,1.32),(.09,1.18,.79),'black',.025)
            for k in range(7):
                box('Cooling louver',(s*2.50,y-.46+k*.145,1.40),(.07,.07,.48),'steel',.012,rot=(0,.2*s,0))
            for z in (.98,1.82):beam('Copper distribution',(s*2.51,y-.5,z),(s*2.51,y+.5,z),.022,'copper')
            box('Service latch',(s*2.53,y-.5,1.43),(.04,.08,.18),'orange',.01)
        # Wings have layered service strips and evenly inset thermal panels.
        for j in range(7):
            x=s*(2.5+j*.45);y=-2.0-j*.50
            box('Wing upper armor',(x,y,.69),(.39,3.2,.055),'paint' if j%3 else 'ivory',.015)
            for k in range(6):
                box('Wing heat rejection slot',(x,y-1.1+k*.30,.725),(.29,.07,.013),'black',.004)
        beam('Wing tip navigation rail',(s*6.7,-5.10,.71),(s*6.7,-6.1,.71),.035,'red' if s<0 else 'green')
        # Individual wing skin panels terminate at the swept leading edge.
        for j in range(9):
            xa=2.36+j*.43;xb=xa+.39
            def leading(x):return 5.9-(x-1.65)*2.5/1.5 if x<3.15 else 3.4-(x-3.15)*8.3/3.85
            ya,yb=leading(xa)-.12,leading(xb)-.12
            skin=poly('Swept wing leading skin',[(s*xa,ya,.662),(s*xb,yb,.662),
                      (s*xb,-6.32,.662),(s*xa,-6.32,.662)],[(0,1,2,3)],'ivory' if j%4 else 'paint',0)
            skin.modifiers.new('Skin thickness','SOLIDIFY').thickness=.018
            for yy in ((ya+yb)/2-.16,-6.18):cyl('Wing flush fastener',(s*(xa+xb)/2,yy,.686),.018,.008,'steel',n=6)
        # Keep reaction-control machinery off the newly shared side glazing.
        for y in (1.0,-3.3):
            box('RCS housing',(s*2.35,y,2.15),(.50,.76,.47),'paint',.07)
            for k in range(3):
                cyl('RCS nozzle',(s*2.64,y-.21+k*.21,2.15),.069,.13,'black',(s,0,0),20)
                ring('RCS nozzle lip',(s*2.71,y-.21+k*.21,2.15),.067,.012,'steel',(s,0,0),24)
        wire('Thermal trunk',[(s*1.6,-6.1,2.65),(s*1.73,-3.2,2.86),(s*1.7,.5,2.84)],.055,'copper')
        for j in range(9):
            y=-5.5+j*.57
            box('Dorsal heat exchanger',(s*.99,y,2.91),(.83,.46,.18),'black',.023)
            for k in range(6):box('Exchanger fin',(s*.99-.33+k*.13,y,3.02),(.034,.39,.11),'steel',.008)
    # Proper human-sized hatch (1.5m clear opening), keyed alignment hardware.
    cyl('Dorsal airlock drum',(0,1.48,2.86),.92,.45,'paint',n=48)['detail_tier']=0
    ring('Universal docking gasket',(0,1.48,3.10),.77,.045,'rubber',n=64)['detail_tier']=0
    ring('Docking capture ring',(0,1.48,3.14),.86,.05,'steel',n=64)['detail_tier']=0
    cyl('Airlock hatch',(0,1.48,3.12),.72,.035,'ivory',n=48)['detail_tier']=0
    for i in range(12):
        a=TAU*i/12
        box('Dock latch',(math.cos(a)*.84,1.48+math.sin(a)*.84,3.21),(.13,.085,.10),'orange',.02,rot=(0,0,a))
    assembly('40 | stowed landing bay doors',0)
    for x,y,z in ((0,6.3,-.15),(-3.5,-3.1,.23),(3.5,-3.1,.23)):
        box('Landing bay perimeter',(x,y,z),(1.02,1.88,.065),'black',.03)
        for s in (-1,1):
            box('Closed landing bay door',(x+s*.24,y,z-.04),(.46,1.77,.035),'paint',.015)
        for yy in (-.68,.68):
            box('Captive gear door latch',(x,y+yy,z-.064),(.18,.09,.015),'steel',.006)
    # Editable deployed concept retained in master, deliberately excluded from
    # every in-flight exchange tier. Landing state/animation is not simulated yet.
    assembly('ARCHIVE | deployed gear concept (not flight export)',4)
    CURRENT.hide_render=True
    for x,y in ((0,6.3),(-3.5,-3.1),(3.5,-3.1)):
        beam('Landing piston',(x,y,.20),(x,y+.35,-1.10),.10,'steel')
        beam('Landing sleeve',(x,y,.20),(x,y+.18,-.45),.145,'paint')
        beam('Drag brace',(x,y-.75,.35),(x,y+.35,-.94),.055,'copper')
        box('Landing shoe',(x,y+.4,-1.22),(.75,1.10,.17),'black',.055)
        for k in range(5):box('Shoe traction',(x,y+.08+k*.15,-1.31),(.69,.08,.035),'steel',.004)
    assembly('41 | ventral lift propulsion',0)
    for s in (-1,1):
        for y in (1.45,-4.65):
            x=s*2.92
            cyl('Lift engine armored housing',(x,y,.36),.72,.68,'paint',n=40)
            box('Lift engine load spreader',(x,y,.38),(1.65,1.57,.26),'paint',.09)
            beam('Lift engine hull thrust tie',(s*2.05,y,.45),(x,y,.45),.13,'steel')
            ring('Lift nozzle heat shield',(x,y,.015),.61,.085,'steel',n=48)
            ring('Lift nozzle outer lip',(x,y,-.12),.50,.048,'copper',n=48)
            cyl('Lift engine recessed throat',(x,y,.055),.48,.03,'black',n=32)
            for j in range(12):
                a=TAU*j/12
                beam('Lift nozzle guide vane',(x+.27*math.cos(a),y+.27*math.sin(a),.018),
                     (x+.49*math.cos(a),y+.49*math.sin(a),-.10),.023,'panel')
            for j in range(5):
                box('Lift housing top vent',(x-.38+j*.19,y,.711),(.085,.73,.032),'black',.008)
    assembly('50 | markings and captive hardware',2)
    for j in range(len(secs)-1):
        a,b=secs[j],secs[j+1]
        if a[0]>=1.85 and b[0]<=6.3:continue
        for k in range(3):
            t=(k+.5)/3;y=a[0]*(1-t)+b[0]*t;w=a[1]*(1-t)+b[1]*t;z=a[2]*(1-t)+b[2]*t+.052
            tilt=math.atan2(b[2]-a[2],b[0]-a[0])
            for s in (-1,1):
                cyl('Pressure panel captive fastener',(s*w*.54,y,z),.025,.018,'steel',n=6)
                if y>6.5:
                    box('Reentry inspection index',(s*w*.31,y,z),(.07,.18,.015),'orange',.002,rot=(tilt,0,0))
    label('Port hull identifier','WF-01',(-2.53,-1.60,1.2),.40,'ink',(math.pi/2,0,-math.pi/2))
    label('Starboard hull identifier','WF-01',(2.53,.05,1.2),.40,'ink',(math.pi/2,0,math.pi/2))
    for s in (-1,1):
        for j in range(8):
            box('Upper wing repair patch',(s*(2.6+j*.26),-3.8,.751),(.19,.65,.022),'panel',.014)
        label('Wing maintenance stencil','NO STEP',(s*4.6,-4.8,.748),.18,'ink',(0,0,0),align='CENTER')
        label('Fuel access stencil','CRYO / 02',(s*1.02,-2.2,3.12),.12,'amber',(0,0,0),align='CENTER')
    roof_sections=secs[:3]+[(s[0],s[1],s[2],s[4]) for s in flight_cell.SPEC['hull_sections']]+secs[5:]
    def roof_surface(y):
        for a,b in zip(roof_sections,roof_sections[1:]):
            if a[0]<=y<=b[0]:
                slope=(b[2]-a[2])/(b[0]-a[0])
                skin=.003 if 1.85<y<6.3 else .029
                return a[2]+(y-a[0])*slope+skin, math.atan(slope)
        raise ValueError('Stencil outside hull roof')
    for s in (-1,1):
        z,pitch=roof_surface(-.40)
        label('Vehicle designation','WAYFINDER',(s*.72,-.40,z),.16,'black',(pitch,0,0),align='CENTER')
        z,pitch=roof_surface(3.8)
        label('Rescue stencil','RESCUE  >>',(s*.72,3.8,z),.12,'orange',(pitch,0,0),align='CENTER')
        box('Orange wing recognition band',(s*5.4,-4.9,.74),(.37,2.8,.028),'orange',.004)
        for j in range(17):
            y=-6.7+j*.64
            if 1.85<y<6.3:continue
            for x in (s*.33,s*1.38):
                z,_=roof_surface(y)
                cyl('Quarter turn armor screw',(x,y,z),.020,.018,'steel',n=6)
        for j in range(11):
            box('Heat shield tile',(s*.6,-5+j*.78,-.58),(.96,.71,.04),'panel',.012)
    z,pitch=roof_surface(7.55)
    label('Ship registry','01',(0,7.55,z),.49,'orange',(pitch,0,0),align='CENTER')
    camera('01 | forward three quarter',(22,29,18),(0,-.2,1),49)
    camera('02 | propulsion inspection',(-18,-25,12),(0,-3,1),53,active=False)
    camera('03 | airlock detail',(6,8,10),(0,1.5,2.2),60,active=False)
    studio_ship()


def sector(name,radius,a0,a1,z,width,height,mat='ivory',steps=6):
    v=[]
    for i in range(steps+1):
        a=a0+(a1-a0)*i/steps
        for r,h in ((radius-width/2,z-height/2),(radius+width/2,z-height/2),
                    (radius+width/2,z+height/2),(radius-width/2,z+height/2)):
            v.append((r*math.cos(a),r*math.sin(a),h))
    f=[(3,2,1,0),tuple(range(steps*4,steps*4+4))]
    f += [(i*4+j,i*4+(j+1)%4,(i+1)*4+(j+1)%4,(i+1)*4+j) for i in range(steps) for j in range(4)]
    return poly(name,v,f,mat,.08)


def station():
    assembly('00 | habitat structural rings',0)
    for z in (-5,5):ring('Ring structural hoop',(0,0,z),72,1.25,'black',n=192,minor=12)
    for i in range(48):
        a=TAU*i/48
        sector(f'Habitat pressure module {i:02}',72,a+.014,a+TAU/48-.014,0,10,8,
               'ivory' if i%4 else 'paint',steps=4)
        # Segmented armored canopy, view terraces and illuminated window strips.
        sector('Module crown',72,a+.025,a+TAU/48-.025,4.35,11,.35,'paint',steps=4)
        for j in range(7):
            ang=a+.025+j*.014
            for z in (-1.6,1.4):
                x=77.07*math.cos(ang);y=77.07*math.sin(ang)
                box('Habitat window',(x,y,z),(.16,.65,.38),'amber' if (i+j)%4 else 'cyan',.03,rot=(0,0,ang))
        for r in (67,77):
            beam('Exterior pressure rib',(r*math.cos(a),r*math.sin(a),-4.5),
                 (r*math.cos(a),r*math.sin(a),4.5),.18,'steel')
    assembly('10 | central nonrotating core and spokes',0)
    cyl('Core pressure column',(0,0,0),9,44,'paint',n=64)
    for z in (-20,-13,-6,6,13,20):ring('Core coupling',(0,0,z),9.15,.42,'steel',n=96)
    for z in (-10,10):
        cyl('Core command deck',(0,0,z),12.0,4,'ivory',n=48)
        ring('Command deck windows',(0,0,z),12.07,.28,'cyan',n=96)
    for i in range(8):
        a=TAU*i/8
        def p(r,t,z):return (r*math.cos(a)-t*math.sin(a),r*math.sin(a)+t*math.cos(a),z)
        beam('Pressurized spoke',p(10,0,0),p(68,0,0),1.3,'ivory',24)
        for s in (-1,1):
            beam('Spoke truss chord',p(12,s*2.1,-2.6),p(67,s*2.1,-2.6),.24,'steel')
            beam('Spoke truss chord',p(12,s*2.1,2.6),p(67,s*2.1,2.6),.24,'steel')
            for k in range(9):
                r=13+k*6
                beam('Spoke diagonal',p(r,s*2.1,-2.6),p(r+6,s*2.1,2.6),.14,'steel')
                beam('Spoke diagonal',p(r,s*2.1,2.6),p(r+6,s*2.1,-2.6),.14,'steel')
    assembly('20 | axial drydock and docking spurs',0)
    # Docking stays on axial nonrotating infrastructure, clear of habitat ring.
    cyl('Axial industrial trunk',(0,0,-42),5.4,48,'black',n=48)
    for z in (-22,-30,-38,-46,-54,-62):
        ring('Industrial service collar',(0,0,z),5.55,.35,'orange' if z==-38 else 'steel',n=64)
    for s in (-1,1):
        box('Drydock gantry',(s*13,4,-36),(3.4,70,5.5),'paint',.30)
        for j in range(13):
            y=-27+j*5
            box('Drydock armor',(s*13,y,-32.97),(3.6,4.6,.5),'ivory',.08)
            box('Drydock guidance',(s*11.15,y,-35.2),(.2,2.1,.30),'cyan',.03)
            beam('Drydock lateral brace',(s*13,y,-37),(s*4,y*.33,-45),.20,'steel')
        for y in (-24,-8,8,24):
            beam('Berthing arm',(s*13,y,-34),(s*21,y,-27),.42,'steel')
            cyl('Standard port',(s*21,y,-26),1.1,1.4,'ivory',n=32)
            ring('1.5m clear docking collar',(s*21,y,-25.26),.86,.10,'orange',n=48)
        box('Container rack',(s*20,-15,-42),(8,26,5),'black',.2)
        for i in range(3):
            for j in range(4):
                box('Freight container',(s*20+(i-1)*2.45,-25+j*6,-40.7),(2.28,5.60,2.55),
                    'orange' if (i+j)%5==0 else 'ivory',.08)
                for k in range(7):box('Container corrugation',(s*20+(i-1)*2.45,-27+j*6+k*.60,-39.35),(2.15,.06,.1),'paint',.01)
    label('Drydock registry','ORIGIN / 04',(13,-31.04,-36),.30,'ink',(math.pi/2,0,0),align='CENTER')
    assembly('30 | photovoltaic wings and radiators',0)
    cyl('Power spine',(0,0,43),3.1,42,'black',n=32)
    for s in (-1,1):
        beam('Solar spar',(0,0,48),(s*155,0,48),.8,'steel')
        for k in range(12):
            x=s*(20+k*11.0)
            for side in (-1,1):
                z=48+side*11.0
                box('Solar frame',(x,0,z),(10.2,.3,20.3),'steel',.06)
                box('PV tile bed',(x,-.21,z),(9.85,.11,19.9),'solar',.01)
                for i in range(5):
                    for j in range(10):
                        box('PV individual cell',(x-3.94+i*1.97,-.28,z-8.95+j*1.99),
                            (1.84,.018,1.86),'solar',.035)['detail_tier']=2
                for i in range(6):box('PV busbar',(x-4.93+i*1.97,-.3,z),(.025,.02,19.9),'gold',0)['detail_tier']=2
        cyl('Solar gimbal',(s*12,0,48),2.1,3.4,'orange',(1,0,0),32)
        for j in range(4):
            z=23+j*7.8
            box('Heat rejection wing',(s*31,12,z),(43,.40,5.9),'black',.05,rot=(0,0,s*.14))
            for k in range(35):box('Radiator capillary',(s*31-20.2+k*1.19,11.72,z),(.15,.13,5.5),'steel',.02)['detail_tier']=2
            beam('Radiator feed',(s*8,12,z),(s*52,12,z),.19,'copper')
    assembly('40 | propellant farm',0)
    for i in range(8):
        a=TAU*i/8
        x,y=6.4*math.cos(a),6.4*math.sin(a)
        sphere('Cryogenic tank',(x,y,62),(2.2,2.2,6.0),'ivory')
        for z in (58,65):ring('Tank retaining band',(x,y,z),1.96,.11,'steel',n=32)['detail_tier']=1
        beam('Tank plumbing',(x,y,57),(x*.5,y*.5,52),.13,'copper')['detail_tier']=1
    assembly('50 | habitat service detail',2)
    for i in range(48):
        a=TAU*(i+.5)/48
        x,y=72*math.cos(a),72*math.sin(a)
        box('Roof utility enclosure',(x,y,5.04),(3.4,3.8,.95),'black',.1,rot=(0,0,a))
        for j in range(5):
            r=70+j*.72
            beam('Roof heat pipe',(r*math.cos(a-.017),r*math.sin(a-.017),5.65),
                 (r*math.cos(a+.017),r*math.sin(a+.017),5.65),.10,'copper')
        for z in (-4.4,4.9):
            ring('Module external walkway',(0,0,z),78.1,.055,'steel',n=192,minor=4) if i==0 else None
        if i%6==0:
            box('Emergency airlock',(78*math.cos(a),78*math.sin(a),0),(3.5,3.5,4.4),'orange',.22,rot=(0,0,a))
    # Parabolic dish surface and focal-point struts, not solid cone placeholders.
    assembly('41 | communications crown',0)
    for s in (-1,1):
        center=Vector((s*7,0,74));v=[tuple(center+Vector((0,0,0)))];n=48
        for j in range(1,9):
            r=j*.45
            for i in range(n):
                a=TAU*i/n;v.append(tuple(center+Vector((r*math.cos(a),r*math.sin(a),r*r/.95/8))))
        f=[(0,1+i,1+(i+1)%n) for i in range(n)]
        f += [(1+(j-1)*n+i,1+(j-1)*n+(i+1)%n,1+j*n+(i+1)%n,1+j*n+i) for j in range(1,8) for i in range(n)]
        poly('Parabolic high gain antenna',v,f,'ivory',0)
        for i in range(3):
            a=TAU*i/3
            beam('Dish feed support',center+Vector((3.1*math.cos(a),3.1*math.sin(a),1.2)),center+Vector((0,0,2.9)),.065,'steel')
        beam('Dish support',(s*7,0,74),(0,0,69),.26,'steel')
    beam('Navigation mast',(0,0,68),(0,0,88),.20,'steel')
    ring('Beacon crown',(0,0,87),.72,.16,'red',n=24)
    assembly('STAGE | lighting',9)
    camera('01 | orbital establishing',(245,-320,225),(0,0,10),46)
    camera('02 | habitat inspection',(119,-135,83),(20,-31,0),55,active=False)
    camera('03 | working dock',(79,117,-6),(0,0,-35),48,active=False)
    sun('Sun',(-.35,.65,-1),3.5,(1,.94,.83),.07)
    sun('Planet reflected fill',(.7,-.8,.4),1.0,(.28,.48,1),.6)
    sun('Orbital soft fill',(-.5,.8,-.25),.55,(.6,.76,1),.8)
    # Original procedural orbital set; intentionally excluded from GLB assets.
    m=bpy.data.materials.new('STAGE / temperate planet');m.use_nodes=True
    nodes=m.node_tree.nodes;links=m.node_tree.links;b=nodes.get('Principled BSDF')
    b.inputs['Roughness'].default_value=.65
    tex=nodes.new('ShaderNodeTexNoise');tex.inputs['Scale'].default_value=4.8
    tex.inputs['Detail'].default_value=6;tex.inputs['Roughness'].default_value=.75
    ramp=nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position=.44;ramp.color_ramp.elements[0].color=(.009,.039,.069,1)
    ramp.color_ramp.elements[1].position=.58;ramp.color_ramp.elements[1].color=(.10,.14,.095,1)
    clouds=nodes.new('ShaderNodeTexNoise');clouds.inputs['Scale'].default_value=7.8
    clouds.inputs['Detail'].default_value=7;clouds.inputs['Roughness'].default_value=.68
    cr=nodes.new('ShaderNodeValToRGB');cr.color_ramp.elements[0].position=.50;cr.color_ramp.elements[1].position=.64
    mix=nodes.new('ShaderNodeMixRGB');mix.inputs[2].default_value=(.54,.63,.70,1)
    links.new(tex.outputs['Fac'],ramp.inputs[0]);links.new(clouds.outputs['Fac'],cr.inputs[0])
    links.new(cr.outputs[0],mix.inputs[0]);links.new(ramp.outputs[0],mix.inputs[1]);links.new(mix.outputs[0],b.inputs['Base Color'])
    bpy.ops.mesh.primitive_uv_sphere_add(segments=128,ring_count=64,radius=650000,
                                       location=(-544755,799680,-874775))
    o=bpy.context.object;o.name='STAGE / distant home planet'
    for c in list(o.users_collection):c.objects.unlink(o)
    CURRENT.objects.link(o);o['detail_tier']=9;o.data.materials.append(m)
    for p in o.data.polygons:p.use_smooth=True


def camera(name,loc,target,lens=50,active=True):
    m=bpy.data.cameras.new(name);m.lens=lens;m.clip_start=.03;m.clip_end=5000000
    o=obj(name,m,loc);o.rotation_euler=(Vector(target)-Vector(loc)).to_track_quat('-Z','Y').to_euler()
    if active:bpy.context.scene.camera=o
    return o


def area(name,loc,target,power,size,color):
    m=bpy.data.lights.new(name,'AREA');m.energy=power;m.shape='DISK';m.size=size;m.color=color
    o=obj(name,m,loc);o.rotation_euler=(Vector(target)-Vector(loc)).to_track_quat('-Z','Y').to_euler()


def sun(name,direction,power,color,angle):
    m=bpy.data.lights.new(name,'SUN');m.energy=power;m.color=color;m.angle=angle
    o=obj(name,m);o.rotation_euler=Vector(direction).to_track_quat('-Z','Y').to_euler()


def studio_ship():
    assembly('STAGE | studio cyclorama',9)
    box('Studio ground',(0,0,-1.48),(200,200,.2),'panel',0)
    area('Large warm key',(-12,11,20),(0,0,0),9000,12,(1,.86,.69))
    area('Cool rim',(12,-10,15),(0,-2,1),12000,10,(.28,.58,1))
    area('Front soft fill',(4,20,9),(0,2,1),4800,8,(.65,.78,1))


def setup(args):
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    for c in list(bpy.data.collections):bpy.data.collections.remove(c)
    bpy.context.preferences.filepaths.save_version=0
    scene=bpy.context.scene
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    scene.render.engine='CYCLES';scene.cycles.device='CPU'
    scene.cycles.samples=20 if args.draft else args.samples
    scene.cycles.use_denoising=True
    scene.cycles.max_bounces=6;scene.cycles.diffuse_bounces=3;scene.cycles.glossy_bounces=3
    scene.render.resolution_x=1200 if args.draft else (2560 if args.final else 1920)
    scene.render.resolution_y=int(scene.render.resolution_x*9/16)
    scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB'
    scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.025,.039,.065,1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.22
    scene.view_settings.view_transform='AgX'
    scene.view_settings.look='AgX - Medium High Contrast'
    scene['design_version']=5 if args.asset in ('cockpit','ship') else 3
    scene['generator']='tools/build_hero_assets.py'
    scene['forward_axis']='+Y';scene['up_axis']='+Z';scene['units']='metres'
    scene['license']='BSD-3-Clause / Apsis Drift contributors'
    materials()


def validate_and_report(stem):
    scene=bpy.context.scene;deps=bpy.context.evaluated_depsgraph_get()
    report={'asset':stem,'blender':bpy.app.version_string,'objects':0,'evaluated_triangles':0,'collections':{},'bounds_min':[1e9]*3,'bounds_max':[-1e9]*3}
    for o in scene.objects:
        if o.type not in ('MESH','CURVE','FONT') or o.get('detail_tier',9) not in (0,1,2):continue
        report['objects']+=1
        eo=o.evaluated_get(deps);m=eo.to_mesh();m.calc_loop_triangles()
        if any(not math.isfinite(v) for p in m.vertices for v in p.co):raise ValueError(f'Nonfinite geometry: {o.name}')
        if any(i>=len(m.vertices) for p in m.polygons for i in p.vertices):raise ValueError(f'Invalid mesh index: {o.name}')
        n=len(m.loop_triangles);report['evaluated_triangles']+=n
        c=o.users_collection[0].name;report['collections'][c]=report['collections'].get(c,0)+n
        for p in o.bound_box:
            q=o.matrix_world@Vector(p)
            for i in range(3):
                report['bounds_min'][i]=min(report['bounds_min'][i],q[i]);report['bounds_max'][i]=max(report['bounds_max'][i],q[i])
        eo.to_mesh_clear()
    report['dimensions_m']=[round(b-a,3) for a,b in zip(report['bounds_min'],report['bounds_max'])]
    (OUT/(stem+'-geometry.json')).write_text(json.dumps(report,indent=2)+'\n')
    print('GEOMETRY',json.dumps({k:v for k,v in report.items() if k!='collections'}),flush=True)


def proxies(asset):
    assembly('LOD3 | authored silhouette proxy',-1)
    if asset=='ship':
        loft('Proxy hull',[(-8,1.7,2.25,-.45),(-1,2.4,2.75,-.5),(3.5,2.2,2.65,-.35),
                           (4.15,2.088571,2.65,-.2966),(6.3,1.72,1.30,-.12),(10,.28,.68,.25)],'ivory',0)
        for s in (-1,1):
            wing('Proxy wing',s,'paint')
            cyl('Proxy engine',(s*3.05,-6,1.04),1.15,6,'black',(0,1,0),12)
            ring('Proxy engine glow',(s*3.05,-9.05,1.04),.72,.10,'cyan',(0,1,0),16,4)
            poly('Proxy fin',[(s*4.1,-4.9,.6),(s*4.1,-7.2,.6),(s*4.74,-7,3.2),(s*4.65,-6.25,3.3)],[(0,1,2,3)],'orange',0)
        cyl('Proxy dock',(0,1.48,2.96),.9,.42,'orange',n=12)
    elif asset=='station':
        sector('Proxy habitat',72,0,TAU,0,10,8,'ivory',48)
        cyl('Proxy core',(0,0,0),9,44,'paint',n=12)
        cyl('Proxy industrial spine',(0,0,-42),5.4,48,'black',n=12)
        cyl('Proxy upper spine',(0,0,48),3.1,53,'paint',n=12)
        for s in (-1,1):
            box('Proxy solar wing',(s*80,0,48),(141,.6,43),'solar',0)
            box('Proxy dock',(s*13,4,-36),(3.4,70,5.5),'paint',0)
        for i in range(8):
            a=TAU*i/8
            beam('Proxy spoke',(10*math.cos(a),10*math.sin(a),0),(68*math.cos(a),68*math.sin(a),0),1.35,'steel',8)
        ring('Proxy station beacons',(0,0,0),77.1,.28,'amber',n=48,minor=4)
    else:
        box('Proxy pressure floor',(0,.85,-.03),(3.30,4.40,.06),'black',0)
        box('Proxy rear bulkhead',(0,-1.33,1.15),(4.42,.06,2.30),'paint',0)
        box('Proxy dashboard',(0,.78,.75),(1.76,.34,.60),'black',0)
        for x in (-.57,0,.57):box('Proxy display',(x,.535,.835),(.465,.02,.335),'cyan',0)
        for p in flight_cell.panels():
            if p['upper']:
                poly('Proxy '+p['name'],[flight_cell.local(v) for v in p['points']],
                     [(0,1,2),(0,2,3)],'canopy' if p['glass'] else 'paint',0)
        box('Proxy equipment deck',(0,1.90,.95),(1.82,2.39,.08),'black',0)
    CURRENT.hide_render=True
    CURRENT.hide_viewport=True


def export_lods(stem):
    # Semantically remove detail first, then collapse the combined reduced mesh.
    # Never modify the master, which was saved before this export-only operation.
    # glTF cannot translate arbitrary procedural inputs. Explicitly disconnect
    # them in this export-only scene so fallback factors do not become white.
    for material in bpy.data.materials:
        if not material.use_nodes:continue
        bsdf=material.node_tree.nodes.get('Principled BSDF')
        if bsdf is None:continue
        for socket in ('Base Color','Roughness','Normal'):
            for link in list(bsdf.inputs[socket].links):material.node_tree.links.remove(link)
        bsdf.inputs['Base Color'].default_value=material.diffuse_color
    sources=list(bpy.context.scene.objects)
    deps=bpy.context.evaluated_depsgraph_get()
    metrics={}
    for level,title,budget in ((2,'hero',None),(1,'near',150000),(0,'mid',35000),(-1,'far',3000)):
        bpy.ops.object.select_all(action='DESELECT')
        if title=='hero':
            for o in sources:
                if o.type in ('MESH','CURVE','FONT') and o.get('detail_tier',9) in (0,1,2):o.select_set(True)
            count=0
            for o in bpy.context.selected_objects:
                eo=o.evaluated_get(deps);m=eo.to_mesh();m.calc_loop_triangles();count+=len(m.loop_triangles);eo.to_mesh_clear()
        else:
            assembly('EXPORT TEMP / '+title,8)
            if title=='far':
                # Hidden collections have stale world matrices until evaluated.
                # Reveal the complete proxy before copying any mesh/transform.
                for collection in bpy.data.collections:
                    if collection.name.startswith('LOD3'):
                        collection.hide_viewport=False
                bpy.context.view_layer.update()
                deps=bpy.context.evaluated_depsgraph_get()
            suppressed=[]
            if title=='mid':
                # Remove edge fillets before collapsing geometry; otherwise
                # many triangles are spent on subpixel bevels, and collapse
                # can pull neighboring armor sheets through their substrate.
                for source in sources:
                    for modifier in source.modifiers:
                        if modifier.type in ('BEVEL','WEIGHTED_NORMAL'):
                            suppressed.append((modifier,modifier.show_viewport))
                            modifier.show_viewport=False
                bpy.context.view_layer.update()
                deps=bpy.context.evaluated_depsgraph_get()
            copies=[]
            for o in sources:
                tier=o.get('detail_tier',9)
                wanted=tier==-1 if title=='far' else 0<=tier<=level
                if o.type not in ('MESH','CURVE','FONT') or not wanted:continue
                # Evaluate while proxy collection is visible for export only.
                for c in o.users_collection:
                    if c.name.startswith('LOD3'):c.hide_viewport=False
                eo=o.evaluated_get(deps)
                m=bpy.data.meshes.new_from_object(eo,depsgraph=deps)
                q=obj(o.name+' / '+title,m);q.matrix_world=o.matrix_world.copy()
                if o.material_slots:
                    m.materials.clear()
                    for slot in o.material_slots:m.materials.append(slot.material)
                q.select_set(True);copies.append(q)
            for modifier,visible in suppressed:modifier.show_viewport=visible
            bpy.context.view_layer.objects.active=copies[0]
            bpy.ops.object.join();combined=bpy.context.object
            combined.name=stem+' / '+title
            tri=combined.modifiers.new('Export triangulation','TRIANGULATE')
            bpy.ops.object.modifier_apply(modifier=tri.name)
            count=len(combined.data.polygons)
            if count>budget:
                dec=combined.modifiers.new('Budget collapse after semantic reduction','DECIMATE')
                dec.ratio=(budget*.995)/count;dec.use_collapse_triangulate=True
                bpy.ops.object.modifier_apply(modifier=dec.name)
            combined.data.calc_loop_triangles();count=len(combined.data.loop_triangles)
            combined['triangle_count']=count;combined['texture_bake_status']='Material factors only; procedural finish in Blender master'
        path=OUT/f'{stem}-{title}.glb'
        bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,
                                  export_apply=True,export_extras=True,export_cameras=False,
                                  export_lights=False,export_yup=True)
        data=path.read_bytes()
        chunk_size=struct.unpack_from('<I',data,12)[0]
        document=json.loads(data[20:20+chunk_size])
        mesh_counts=[sum(document['accessors'][p['indices']]['count']//3 for p in m['primitives'])
                     for m in document['meshes']]
        exported=sum(mesh_counts[n['mesh']] for n in document['nodes'] if 'mesh' in n)
        metrics[title]={'triangles':exported,'evaluated_before_export':count,
                        'unique_mesh_triangles':sum(mesh_counts),
                        'bytes':path.stat().st_size,'budget':budget}
        if title!='hero':
            bpy.data.objects.remove(combined,do_unlink=True)
    (OUT/f'{stem}-lods.json').write_text(json.dumps(metrics,indent=2)+'\n')
    print('LOD_METRICS',json.dumps(metrics),flush=True)


def refresh_export_metrics(stem):
    """Read exporter-normalized counts for outputs from an earlier iteration."""
    sidecar=OUT/f'{stem}-lods.json'
    metrics=json.loads(sidecar.read_text())
    for title,entry in metrics.items():
        data=(OUT/f'{stem}-{title}.glb').read_bytes()
        n=struct.unpack_from('<I',data,12)[0];document=json.loads(data[20:20+n])
        mesh_counts=[sum(document['accessors'][p['indices']]['count']//3 for p in m['primitives'])
                     for m in document['meshes']]
        actual=sum(mesh_counts[n['mesh']] for n in document['nodes'] if 'mesh' in n)
        entry.setdefault('evaluated_before_export',entry['triangles'])
        entry['triangles']=actual;entry['unique_mesh_triangles']=sum(mesh_counts);entry['bytes']=len(data)
    sidecar.write_text(json.dumps(metrics,indent=2)+'\n')


def main():
    parser=argparse.ArgumentParser();parser.add_argument('asset',choices=['cockpit','ship','station'])
    parser.add_argument('--draft',action='store_true');parser.add_argument('--final',action='store_true')
    parser.add_argument('--export',action='store_true');parser.add_argument('--view',type=int,default=1)
    parser.add_argument('--samples',type=int,default=72)
    parser.add_argument('--no-render',action='store_true',help='Rebuild masters/exports; retain existing PNG reviews')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    if not 1<=args.samples<=4096 or not 1<=args.view<=3:
        raise ValueError('Sample count or camera index out of bounds')
    OUT.mkdir(exist_ok=True);setup(args)
    {'cockpit':cockpit,'ship':ship,'station':station}[args.asset]()
    proxies(args.asset)
    stem='hero-'+args.asset
    cams=sorted((o for o in bpy.context.scene.objects if o.type=='CAMERA'),key=lambda o:o.name)
    bpy.context.scene.camera=cams[args.view-1]
    validate_and_report(stem)
    bpy.context.scene.render.filepath=str(OUT/f'{stem}-view-{args.view:02}.png')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(stem+'.blend')),compress=True)
    if not args.no_render:bpy.ops.render.render(write_still=True)
    if args.export:export_lods(stem)


if __name__=='__main__':main()
