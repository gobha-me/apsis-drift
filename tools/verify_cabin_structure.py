#!/usr/bin/env python3
"""Blender mounting-envelope and floor-ray regression checks; not a stress solver.

blender -b --factory-startup --python-exit-code 1 --python tools/verify_cabin_structure.py
"""
from pathlib import Path
import sys

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'assets/visual/hero-cockpit.blend'))
if bpy.context.scene.get('design_version',0)>=5:
    import verify_flight_cell
    verify_flight_cell.run()
    sys.exit(0)
bpy.context.view_layer.update()


def parts(prefix):
    return sorted((o for o in bpy.context.scene.objects
                   if o.name == prefix or o.name.startswith(prefix+'.')), key=lambda o:o.name)


def bounds(part):
    vertices = [part.matrix_world @ Vector(v) for v in part.bound_box]
    return [Vector(tuple(fn(v[axis] for v in vertices) for axis in range(3))) for fn in (min,max)]


def gap(a,b):
    return Vector(tuple(max(a[0][i]-b[1][i], b[0][i]-a[1][i], 0) for i in range(3))).length


errors=[]


def connected(child, support, tolerance=.015):
    children, supports = parts(child), parts(support)
    assert children and supports, (child,support,'missing geometry')
    for c in children:
        distance=min(gap(bounds(c),bounds(s)) for s in supports)
        if distance>tolerance:errors.append((c.name,support,distance))


for child,support in (
        ('Instrument deck pedestal','Floor sandwich'),
        ('Instrument deck pedestal','Main console body'),
        ('Glare shield support','Main console body'),
        ('Glare shield support','Glare shield'),
        ('Control arm cantilever','Side console'),
        ('Gimbal flange','Control arm cantilever'),
        ('Control arm deck stay','Control arm cantilever'),
        ('Control stay foot','Floor sandwich'),
        ('Seat adjustment slide','Pilot couch plinth'),
        ('Seat adjustment slide','Pilot seat pan'),
        ('Pedal floor mounting cassette','Floor sandwich'),
        ('Pedal hinge shaft','Pedal floor mounting cassette'),
        ('Foot pedal','Pedal hinge shaft'),
        ('Forward avionics deck','Glare shield'),
        ('Forward avionics deck','Forward canopy coaming'),
        ('Forward avionics deck','Window sill'),
        ('Forward deck bearer','Main console body'),
        ('Forward deck bearer','Forward canopy coaming'),
        ('Avionics access cover','Forward avionics deck'),
        ('Windshield demister housing','Forward avionics deck'),
        ('Overhead raft suspension','Overhead equipment raft'),
        ('Overhead raft suspension','Overhead raft bearer'),
        ('Overhead aft cross tie','Roof centre load spine'),
        ('Canopy structural foot','Window sill'),
        ('Canopy sill outrigger','Pressure sill'),
        ('Side switch card bracket','Aft upper side closure'),
        ('Cabin lamp lower clip','Aft upper side closure')):
    connected(child,support)

assert not errors, errors

# Demonstrate that separated mounting envelopes really fail the test predicate.
example=bounds(parts('Gimbal flange')[0])
shifted=[v+Vector((20,0,0)) for v in example]
assert gap(example,shifted)>10

deps=bpy.context.evaluated_depsgraph_get()
for x in (-.72,0,.72):
    for y in (.05,.45,2.3,3.2):
        hit, location, normal, _, part, _ = bpy.context.scene.ray_cast(
            deps,Vector((x,y,.20)),Vector((0,0,-1)),distance=.5)
        assert hit and location.z>=.015 and normal.z>.9, (x,y,'floor gap or inverted floor')
        assert part.name.startswith(('Removable deck tile','Deck non slip tread','Floor sandwich')), part.name
assert not parts('HUD equipment footing') and not parts('HUD mounting stanchion')
assert not parts('HUD lower rail'), 'Unimplemented combiner must not survive export'
for x in (-1.2,0,1.2):
    for y in (1.70,2.15,2.75,3.45):
        hit, location, normal, _, part, _ = bpy.context.scene.ray_cast(
            deps,Vector((x,y,1.65)),Vector((0,0,-1)),distance=.65)
        assert hit and 1.13<location.z<1.4 and normal.z>.9, (x,y,'dashboard gap')
        assert part.name.startswith(('Forward avionics deck','Avionics access cover',
                                    'Access cover captive screw','Windshield demister')),part.name
# A level forward view from the pilot eye must clear the dashboard.
for x in (-.12,0,.12):
    hit, *_ = bpy.context.scene.ray_cast(deps,Vector((x,-.78,1.74)),
                                        Vector((0,1,0)),distance=4.5)
    assert not hit, 'Dashboard obstructs level seated sightline'
print('Cabin: 27 mounting families, detached negative control, 12 floor rays, '
      '12 dashboard rays, 3 seated sightlines and absent HUD placeholder passed')
