"""Blender construction using flight_cell's common physical/fit contract."""
import math
import json
import bpy
from mathutils import Vector, Matrix
import flight_cell as cell


def glazing_material(h):
    if 'canopy' in h.MAT:return
    material=bpy.data.materials.new('Optical canopy - physical opening')
    material.use_nodes=True
    material.diffuse_color=(.32,.53,.60,.085)
    bsdf=material.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value=material.diffuse_color
    bsdf.inputs['Alpha'].default_value=.085
    bsdf.inputs['Roughness'].default_value=.12
    bsdf.inputs['Metallic'].default_value=.05
    material.surface_render_method='DITHERED'
    material.use_backface_culling=False
    h.MAT['canopy']=material


def envelope(h, exterior=False):
    glazing_material(h)
    h.assembly('05 | shared flight cell envelope',0)
    edges=set()
    for panel in cell.panels():
        if not exterior and not panel['upper']:continue
        pts=panel['points'] if exterior else [cell.local(p) for p in panel['points']]
        part=h.poly(panel['name'],pts,[(0,1,2),(0,2,3)],
                    'canopy' if panel['glass'] else 'paint',0)
        part['flight_cell_revision']=5
        part['surface_kind']='real glazing' if panel['glass'] else 'pressure skin'
        if not panel['glass']:
            mod=part.modifiers.new('Pressure skin thickness','SOLIDIFY');mod.thickness=.035
        if panel['upper']:
            for a,b in zip(pts,pts[1:]+pts[:1]):
                key=tuple(sorted((tuple(a),tuple(b))))
                if key in edges:continue
                edges.add(key)
                h.beam('Cell structural frame',a,b,.037,'paint',12)
    if exterior:return
    # Inner lower liner follows the exact window sill, down to the cabin floor.
    sections=[cell.ring(s) for s in cell.SPEC['hull_sections']]
    for side,index in ((-1,7),(1,2)):
        for j in range(3):
            a,b=cell.local(sections[j][index]),cell.local(sections[j+1][index])
            pts=[a,b,(b[0],b[1],0),(a[0],a[1],0)]
            wall=h.poly('Cell inner side liner',pts,[(0,1,2,3)],'panel',0)
            wall.modifiers.new('Liner thickness','SOLIDIFY').thickness=.04
    outline=[(-2.23,-1.35),(2.23,-1.35),(2.20,-.8),(2.04,.95),
             (1.67,3.10),(-1.67,3.10),(-2.04,.95),(-2.20,-.8)]
    bottom=[(x,y,-.06) for x,y in outline];top=[(x,y,0) for x,y in outline]
    faces=[tuple(range(7,-1,-1)),tuple(range(8,16))]
    faces += [(i,(i+1)%8,(i+1)%8+8,i+8) for i in range(8)]
    h.poly('Cell floor sandwich',bottom+top,faces,'black',.005)
    front=h.poly('Cell inner forward liner',
        [(-1.67,3.08,0),(1.67,3.08,0),(1.72,3.08,.55),
         (1.1524,3.08,.95),(-1.1524,3.08,.95),(-1.72,3.08,.55)],
        [(0,1,2,3,4,5)],'panel',0)
    front.modifiers.new('Forward liner thickness','SOLIDIFY').thickness=.025
    # The aft pressure liner is a real boundary; its hatch remains noninteractive.
    h.box('Cell aft bulkhead',(0,-1.33,1.15),(4.42,.06,2.30),'paint',.015)
    h.box('Aft cabin hatch',(0,-1.287,1.02),(.80,.025,2.00),'panel',.015)
    h.label('Aft hatch legend','CABIN  /  AFT',(0,-1.27,1.78),.065,'ink',
            (math.pi/2,0,math.pi),align='CENTER')
    h.beam('Aft hatch grab',(.32,-1.25,.88),(.32,-1.25,1.18),.018,'orange')
    for s in (-1,1):
        for y in (-.95,-.30,.35):
            h.box('Floor access tile',(s*.94,y,.015),(.64,.60,.023),'panel',.008)
            for j in range(5):
                h.box('Floor tread',(s*.94,y-.23+j*.11,.030),(.55,.018,.007),'rubber',.001)


def seat(h, pose):
    hip_y,hip_z=pose['hip'][1:]
    h.assembly('30 | adjustable pilot couch',1)
    h.box('Seat fixed foundation',(0,-.20,.08),(.65,.86,.16),'black',.025)
    for x in (-.23,.23):
        h.box('Seat fixed slide',(x,-.18,.185),(.07,.88,.055),'steel',.008)
        h.box('SeatMoving carriage',(x,hip_y,.245),(.105,.52,.08),'paint',.008)
        h.beam('SeatMoving lift',(x,hip_y-.13,.25),(x,hip_y+.15,hip_z-.10),.022,'steel')
    h.box('SeatMoving cushion',(0,hip_y,hip_z-.15),(.56,.54,.12),'fabric',.045)
    h.box('SeatMoving back',(0,hip_y-.31,hip_z+.30),(.55,.16,.78),'fabric',.05,(-.10,0,0))
    h.box('SeatMoving headrest',(0,hip_y-.35,hip_z+.79),(.34,.16,.24),'rubber',.045)
    for s in (-1,1):
        h.box('SeatMoving bolster',(s*.29,hip_y-.28,hip_z+.22),(.09,.20,.50),'black',.025)
        h.box('SeatMoving arm pad',(s*.35,hip_y+.18,.66),(.105,.42,.055),'rubber',.018)
        h.beam('SeatMoving arm stay',(s*.27,hip_y,hip_z-.08),(s*.35,hip_y,.64),.024,'paint')
        h.wire('SeatMoving harness',[(s*.18,hip_y-.38,hip_z+.64),
                (s*.16,hip_y-.15,hip_z+.33),(s*.07,hip_y+.05,hip_z+.035)],.017,'orange')
    h.box('SeatMoving harness buckle',(0,hip_y+.06,hip_z+.035),(.11,.035,.07),'steel',.009)
    h.beam('Seat fixed adjustment handle',(-.32,-.13,.25),(-.32,.07,.25),.014,'orange')


def mannequin(h, stature, name):
    p=cell.fit(stature)
    h.assembly('FIT ONLY | '+name,4)
    hip=Vector(p['hip']);eye=Vector(p['eye'])
    h.box('Dummy pelvis',hip,(.29,.22,.17),'orange',.055)
    shoulder_mid=(Vector(p['shoulders'][0])+Vector(p['shoulders'][1]))/2
    h.beam('Dummy torso',hip+Vector((0,0,.07)),shoulder_mid,.16,'orange',20)
    h.beam('Dummy shoulder line',p['shoulders'][0],p['shoulders'][1],.085,'orange',20)
    h.sphere('Dummy helmet',eye+Vector((0,-.015,.02)),(.135,.145,.175),'ivory')
    h.sphere('Dummy visor',eye+Vector((0,.10,.03)),(.113,.057,.093),'screen')
    for i,s in enumerate((-1,1)):
        for name2,a,b,r in [('upper arm',p['shoulders'][i],p['elbows'][i],.065),
                          ('forearm',p['elbows'][i],cell.SPEC['grips'][i],.055),
                          ('thigh',p['hips'][i],p['knees'][i],.087),
                          ('shin',p['knees'][i],p['ankles'][i],.070)]:
            h.beam('Dummy '+name2,a,b,r,'orange',16)
        for name2,v in [('elbow',p['elbows'][i]),('knee',p['knees'][i])]:
            h.sphere('Dummy '+name2,v,(.075,.075,.075),'black')
        h.sphere('Dummy glove',cell.SPEC['grips'][i],(.060,.073,.045),'black')
        ankle=p['ankles'][i]
        h.box('Dummy boot',(ankle[0],ankle[1]+.065,.245),(.135,.25,.17),'black',.035)
    return p


def build(h):
    cell.checks()
    bpy.context.scene['control_layout_revision']=cell.SPEC['control_layout_revision']
    pose=cell.fit(1.78)
    envelope(h)
    h.assembly('10 | pilot primary instruments',0)
    h.box('Instrument housing',(0,.78,.75),(1.76,.34,.60),'black',.04)
    for x in (-.70,.70):
        h.box('Instrument floor pedestal',(x,.80,.20),(.12,.27,.40),'paint',.018)
        h.box('Instrument pedestal flange',(x,.80,.025),(.23,.34,.05),'steel',.008)
    # A lower, sloped equipment cover meets the shared forward window sill.
    # Screens stay near the pilot; this space contains remote avionics, not controls.
    v=[(-.91,.69,1.09),(.91,.69,1.09),(1.14,3.08,.945),(-1.14,3.08,.945)]
    h.poly('Forward avionics deck',[(x,y,z-.08) for x,y,z in v]+v,
           [(3,2,1,0),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'rubber',.008)
    h.box('Glare shield',(0,.67,1.08),(1.88,.16,.07),'rubber',.018)
    for x in (-.60,0,.60):
        y=1.8;z=1.09+(y-.69)*(-.145/2.39)
        h.box('Avionics service lid',(x,y,z+.006),(.51,1.30,.016),'panel',.007,
              rot=(math.atan(-.145/2.39),0,0))
        for dx in (-.21,.21):
            for dy in (-.55,.55):
                h.cyl('Deck captive screw',(x+dx,y+dy,z+.018-dy*.145/2.39),.010,.006,'black',n=6)
    for s in (-1,1):
        h.beam('Forward deck bearer',(s*.72,.80,1.01),(s*.95,3.05,.89),.025,'paint')
        h.box('Demister strip',(s*.58,2.94,.973),(.86,.13,.024),'black',.006)
        for i in range(9):h.box('Demister slot',(s*.58-.34+i*.085,2.94,.99),(.05,.08,.006),'panel',.001)
    for i,x in enumerate((-.57,0,.57)):
        # Scale the complete authored display, including traces and typography;
        # changing only the bezel dimensions made radar/text collide.
        before=set(h.CURRENT.objects)
        pivot=Vector((x,.585,.835))
        h.display(('NAV','FLIGHT','SYSTEM')[i],pivot,.74,.46,(1,0,2)[i])
        bpy.context.view_layer.update()
        transform=Matrix.Translation(pivot) @ Matrix.Diagonal((.465/.74,1,.335/.46,1)) @ Matrix.Translation(-pivot)
        for part in set(h.CURRENT.objects)-before:part.matrix_world=transform@part.matrix_world
    # Frequently used switches stay at the hand controls: small fixture can
    # reach both guarded buttons without leaning out of the harness.
    for s in (-1,1):
        # Low, upward-facing pods stay below the sightline to the bottom of
        # every main display, without changing the seated eye or display size.
        h.box('Immediate control riser',(s*.35,.23,.61),(.14,.15,.14),'paint',.012)
        h.panel('FLIGHT' if s<0 else 'JUMP',(s*.35,.25,.66),.17,.20,1,2,tilt=-1.1)
    for i,point in enumerate(cell.SPEC['primary_switches']):
        h.cyl('Primary guarded button',point,.024,.015,'amber' if i else 'cyan',axis=(0,-math.cos(1.1),math.sin(1.1)),n=20)
        h.beam('Primary button guard',(point[0]-.03,point[1],point[2]-.014),
               (point[0]-.03,point[1]-.015,point[2]+.024),.007,'steel')
    for s in (-1,1):
        h.box('Pilot side control housing',(s*.40,.06,.49),(.20,.47,.26),'black',.025)
        h.beam('Control housing floor stay',(s*.40,.06,.03),(s*.40,.06,.44),.032,'paint')
        h.cyl('Control gimbal',(s*.35,.12,.665),.06,.03,'steel',n=24)
        for j in range(4):h.ring('Control bellows',(s*.35,.12,.685+j*.012),.044,.005,'rubber',n=16)
        h.beam('Control stem',(s*.35,.12,.69),(s*.35,.12,.77),.017,'steel')
        h.box('Control grip',(s*.35,.12,.77),(.047,.070,.085),'rubber',.017)
        h.cyl('Grip thumb button',(s*.35,.083,.80),.009,.008,'orange',(0,-1,0),12)
        h.box('Secondary console',(s*.82,.04,.48),(.43,.62,.87),'black',.025)
        h.panel('NAV / ROUTE' if s<0 else 'POWER / CHECK',
                (s*.82,.05,.94),.48,.30,2,4,yaw=-s*math.pi/2,tilt=-math.radians(65))
    h.assembly('20 | standing service and utility',1)
    for s in (-1,1):
        h.box('Aft service cabinet',(s*1.63,-1.12,1.03),(.79,.29,1.92),'paint',.025)
        h.panel('SERVICE / ISOLATION',(s*1.52,-.95,1.32),.57,.63,4,3)
        h.label('Service access caution','SERVICE  /  UNSTRAP TO ACCESS',(s*1.90,-.95,1.79),.026,'amber')
        h.wire('Service cable trunk',[(s*1.88,-1.25,.1),(s*1.88,-1.25,1.95),
                                    (s*1.45,-1.25,2.18)],.018,'rubber')
        h.beam('Roof practical',(s*1.13,-1.05,2.23),(s*1.13,.65,2.23),.012,'white')
        for y in (-1.05,.65):h.beam('Roof lamp mount',(s*1.13,y,2.23),(s*1.13,y,2.31),.018,'paint')
        h.beam('Aisle guide light',(s*1.17,-1.17,.07),(s*1.17,.70,.07),.006,'cyan')
    seat(h,pose)
    h.assembly('31 | adjustable pedal carriage',1)
    pedal_y=pose['ankles'][0][1]+.065
    h.box('Pedal fixed track',(0,pedal_y,.035),(.55,.55,.07),'black',.008)
    h.box('PedalMoving cassette',(0,pedal_y,.085),(.49,.31,.06),'steel',.008)
    for s in (-1,1):
        h.box('PedalMoving footplate',(s*.15,pedal_y,.135),(.14,.25,.035),'steel',.008,(-.15,0,0))
        for j in range(5):h.box('PedalMoving tread',(s*.15,pedal_y-.08+j*.04,.16),(.12,.008,.005),'rubber',0)
    # Save all three fixture poses in the master, hidden and excluded from game
    # tiers. Separate GLBs allow native inspection without a permanent NPC.
    manifests=[]
    for stature,name in ((1.60,'small'),(1.78,'medium'),(1.96,'tall')):
        p=mannequin(h,stature,name)
        collection=h.CURRENT
        collection['fit_stature']=stature
        collection['seat_delta']=p['seat_delta'];collection['pedal_delta']=p['pedal_delta']
        bpy.ops.object.select_all(action='DESELECT')
        for part in collection.objects:part.select_set(True)
        path=h.OUT/f'cockpit-fit-{name}.glb'
        # glTF does not encode Blender procedural material inputs. Temporarily
        # expose authored factors, then restore the editable master's shader.
        links=[]
        for material in h.MAT.values():
            bsdf=material.node_tree.nodes.get('Principled BSDF')
            for key in ('Base Color','Roughness','Normal'):
                for link in list(bsdf.inputs[key].links):
                    links.append((material,link.from_socket,link.to_socket))
                    material.node_tree.links.remove(link)
        bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,
                                 export_apply=True,export_yup=True,export_cameras=False,export_lights=False)
        for material,a,b in links:material.node_tree.links.new(a,b)
        collection.hide_render=True;collection.hide_viewport=True
        manifests.append({'name':name,**p})
    (h.OUT/'cockpit-fit-report.json').write_text(json.dumps({
        'schema_version':1,'revision':5,'cabin_to_ship':cell.SPEC['cabin_to_ship'],
        'status':cell.SPEC['status'],'poses':manifests},indent=2)+'\n')
    h.assembly('STAGE | fit review lights and cameras',9)
    h.camera('01 | pilot eye',pose['eye'],(0,3,1.15),20)
    h.camera('02 | cockpit layout inspection',(3.5,-3,2.8),(0,.35,1),32,active=False)
    h.area('Canopy sky',(0,4,5),(0,0,.6),750,4,(.6,.8,1))
    h.area('Cabin fill',(0,-1,2),(0,.8,.8),90,2,(.6,.8,1))
