"""Blender shared-envelope and synthetic seated-fit regressions, revision 5."""
import math
import sys
from pathlib import Path
import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import flight_cell as cell


def run():
    poses=cell.checks()
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/'assets/visual/hero-cockpit.blend'))
    bpy.context.view_layer.update()
    scene=bpy.context.scene
    assert scene['design_version']==5
    deps=bpy.context.evaluated_depsgraph_get()
    glass={o.name:[cell.world(o.matrix_world@v.co) for v in o.data.vertices]
           for o in scene.objects if o.name.startswith('Cell glazing')}
    assert len(glass)==5
    assert not any(o.name.startswith('HUD ') for o in scene.objects)
    for x in (-.5,0,.5):
        hit,_,_,_,part,_=scene.ray_cast(deps,Vector((x,2.8,.30)),Vector((0,1,0)),distance=.5)
        assert hit and part.name.startswith('Cell inner forward liner'), 'Open forward footwell'
    for pose in poses:
        # Sample each active display from each adjusted eye. The complete
        # scene participates: nearby switch pods must not hide lower pixels.
        for center in (-.57,0,.57):
            for ix in range(17):
                for iz in range(11):
                    target=Vector((center+(ix/16-.5)*.465*.94,.545,
                                   .835+(iz/10-.5)*.335*.94))
                    origin=Vector(pose['eye']); delta=target-origin
                    hit,point,_,_,part,_=scene.ray_cast(deps,origin,delta.normalized(),distance=delta.length+.02)
                    assert hit and point.y>.50, ('display occluded',pose['stature'],center,ix,iz,part.name if hit else None)
        # Eye-box samples test level and slightly depressed forward vision.
        for angle in (-.20,0,.20):
            direction=Vector((math.sin(angle),math.cos(angle),-.03)).normalized()
            hit,_,_,_,part,_=scene.ray_cast(deps,Vector(pose['eye']),direction,distance=10)
            assert hit and part.name.startswith('Cell glazing'), ('obstructed eye',pose['stature'],part.name if hit else None)
        hit,point,_,_,part,_=scene.ray_cast(deps,Vector(pose['eye']),Vector((0,0,1)),distance=3)
        assert hit and point.z-pose['eye'][2]>.30, ('helmet roof clearance',pose['stature'])
        # Approximate swept shin/thigh clearance against hard equipment AABBs;
        # mannequin radii are conservative. Seat contact itself is intentional.
        for name in ('Instrument housing','Pilot side control housing','Pilot side control housing.001',
                     'Immediate control riser','Immediate control riser.001'):
            o=scene.objects[name]
            corners=[o.matrix_world@Vector(p) for p in o.bound_box]
            lo=[min(p[k] for p in corners) for k in range(3)]
            hi=[max(p[k] for p in corners) for k in range(3)]
            for joints,radius in ((('hips','knees'),.087),(('knees','ankles'),.070)):
                for a,b in zip(pose[joints[0]],pose[joints[1]]):
                    for i in range(21):
                        p=Vector(a).lerp(Vector(b),i/20)
                        distance=math.sqrt(sum(max(lo[k]-p[k],p[k]-hi[k],0)**2 for k in range(3)))
                        assert distance>radius, ('leg/equipment collision',pose['stature'],name,i,distance)
    for x in (-1.30,1.30):
        for y in (-.95,-.3,.35):
            hit,point,normal,_,part,_=scene.ray_cast(deps,Vector((x,y,.10)),Vector((0,0,-1)),distance=.2)
            assert hit and -.01<=point.z<=.05 and normal.z>.9, ('floor gap',x,y)
    # Forward deck cover: samples avoid intentional pane/frame intersections.
    for x in (-.6,0,.6):
        for y in (1.1,1.8,2.7):
            hit,point,normal,_,part,_=scene.ray_cast(deps,Vector((x,y,1.20)),Vector((0,0,-1)),distance=.4)
            assert hit and .9<point.z<1.15 and normal.z>.9, ('deck gap',x,y)
    # Exact same vertices in the exterior, using only the documented translation.
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/'assets/visual/hero-ship.blend'))
    bpy.context.view_layer.update()
    scene=bpy.context.scene;deps=bpy.context.evaluated_depsgraph_get()
    for name,points in glass.items():
        other=scene.objects[name]
        assert len(other.data.vertices)==len(points)
        assert max(math.dist(a,other.matrix_world@b.co) for a,b in zip(points,other.data.vertices))<1e-5, name
    # This catches the old opaque hull underneath decorative black glazing.
    for pose in poses:
        for angle in (-.20,0,.20):
            hit,_,_,_,part,_=scene.ray_cast(deps,Vector(cell.world(pose['eye'])),
                Vector((math.sin(angle),math.cos(angle),-.03)).normalized(),distance=10)
            assert hit and part.name.startswith('Cell glazing'), ('exterior blocks window',part.name if hit else None)
    # Negative control: an old independent interior placement would fail matching.
    assert math.dist(cell.world((0,0,0)),(0,0,0))>3
    print('Flight cell v5: five exact shared glazing panels; 3 size fixtures; 18 interior/exterior '
          'sightline rays; 1683 active-display visibility rays; helmet, sampled leg/equipment, floor/deck checks; negative control passed')


if __name__=='__main__':run()
