"""Shared metre-scale cockpit/exterior contract and synthetic fit fixtures.

Pure Python: geometry/pose validation runs without Blender or a graphics context.
Ratios are authored design fixtures, not certified human-factors measurements.
"""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC_PATH = ROOT/'experiments/godot-freedom/cockpit-layout.json'
SPEC = json.loads(SPEC_PATH.read_text())


def finite_vector(v):
    return isinstance(v, (list, tuple)) and len(v) == 3 and all(
        isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x) for x in v)


def validate(spec):
    if spec.get('schema_version') != 1 or spec.get('units') != 'metres':
        raise ValueError('Unsupported flight cell contract')
    if not finite_vector(spec.get('cabin_to_ship')) or not finite_vector(spec.get('pilot_eye_blender')):
        raise ValueError('Invalid cell transform or eye')
    for group in ('grips', 'primary_switches', 'service_switches'):
        if len(spec.get(group, [])) != 2 or not all(finite_vector(v) for v in spec[group]):
            raise ValueError('Invalid control anchors')
    previous = -math.inf
    for row in spec['hull_sections']:
        if len(row) != 5 or not all(isinstance(x, (int, float)) and math.isfinite(x) for x in row):
            raise ValueError('Non-finite hull section')
        y, width, roof, sill, bottom = row
        if y <= previous or width <= 0 or not bottom < sill <= roof:
            raise ValueError('Inverted or unordered hull section')
        previous = y
    if len(spec['hull_sections']) != 4:
        raise ValueError('Flight-cell topology requires four sections')


validate(SPEC)


def local(point):
    return tuple(a-b for a,b in zip(point, SPEC['cabin_to_ship']))


def world(point):
    return tuple(a+b for a,b in zip(point, SPEC['cabin_to_ship']))


def ring(section):
    y,w,roof,sill,bottom = section
    return [(-w*.67,y,roof),(w*.67,y,roof),(w,y,sill),
            (w,y,bottom+.20),(w*.68,y,bottom),(-w*.68,y,bottom),
            (-w,y,bottom+.20),(-w,y,sill)]


def panels():
    """One definition of both sides of real glazing; no opaque hull behind it."""
    rings = [ring(s) for s in SPEC['hull_sections']]
    result = []
    for j in range(3):
        for i in range(8):
            glazing = (i == 0 and j == 2) or (i in (1,7) and j >= 1)
            name = ('Cell glazing' if glazing else 'Cell pressure skin') + f' {j}-{i}'
            # Outward normals for the hull convention used by the asset builder.
            points = [rings[j][i], rings[j][(i+1)%8], rings[j+1][(i+1)%8], rings[j+1][i]]
            result.append({'name':name, 'points':points, 'glass':glazing, 'upper':i in (0,1,7)})
    return result


def fit(stature):
    if isinstance(stature, bool) or not isinstance(stature, (float,int)) or not math.isfinite(stature):
        raise ValueError('Non-finite or nonnumeric stature')
    if not SPEC['stature_range'][0] <= stature <= SPEC['stature_range'][1]:
        raise ValueError('Stature outside tested design fixture range')
    delta = SPEC['nominal_stature'] - stature
    hip_y = -.16 + delta*.25
    hip_z = .52 + delta*.23
    eye = (0,hip_y-.035,hip_z+stature*.43)
    shoulders = [(s*stature*.105,hip_y-.06,hip_z+stature*.30) for s in (-1,1)]
    # Pedal carriage follows leg size separately from the seat carriage.
    ankle_y = hip_y + stature*.39
    ankles = [(s*.15,ankle_y,.25) for s in (-1,1)]
    hips = [(s*.105,hip_y,hip_z) for s in (-1,1)]
    knees = [joint(a,b,stature*.245,stature*.246,(0,0,1)) for a,b in zip(hips,ankles)]
    elbows = [joint(a,b,stature*.186,stature*.20,(s,0,-.3))
              for s,a,b in zip((-1,1),shoulders,SPEC['grips'])]
    return {'stature':stature,'hip':(0,hip_y,hip_z),'eye':eye,'shoulders':shoulders,
            'hips':hips,'knees':knees,'ankles':ankles,'elbows':elbows,
            'seat_delta':(0,delta*.25,delta*.23),'pedal_delta':(0,delta*.25+(stature-1.78)*.39,0),
            'arm_length':stature*(.186+.20)}


def joint(a,b,first,second,bend):
    """Two-segment fixture IK; rejects impossible poses instead of stretching."""
    vector = [y-x for x,y in zip(a,b)]
    length = math.sqrt(sum(x*x for x in vector))
    if not abs(first-second) < length < first+second:
        raise ValueError('Unreachable limb endpoint')
    direction = [x/length for x in vector]
    dot = sum(x*y for x,y in zip(direction,bend))
    normal = [x-dot*y for x,y in zip(bend,direction)]
    norm = math.sqrt(sum(x*x for x in normal))
    if norm < 1e-8:
        raise ValueError('Degenerate bend plane')
    along = (first*first-second*second+length*length)/(2*length)
    height = math.sqrt(max(0,first*first-along*along))
    return tuple(x+along*d+height*n/norm for x,d,n in zip(a,direction,normal))


def checks():
    validate(SPEC)
    assert math.dist(fit(1.78)['eye'],SPEC['pilot_eye_blender']) < 1e-9
    cases = []
    for stature in (1.60,1.78,1.96):
        pose = fit(stature)
        assert .38 < pose['hip'][2]-.09 < .49
        assert abs(pose['seat_delta'][1]) <= .046 and abs(pose['seat_delta'][2]) <= .042
        # Keep primary controls away from maximum extension; services are not
        # falsely counted as comfortably reachable from the restrained seat.
        for shoulder,grip,key,service in zip(pose['shoulders'],SPEC['grips'],SPEC['primary_switches'],SPEC['service_switches']):
            assert math.dist(shoulder,grip) < pose['arm_length']*.94
            assert math.dist(shoulder,key) < pose['arm_length']*.96
            assert math.dist(shoulder,service) > pose['arm_length']
        assert pose['eye'][2] + .22 < 2.25 # helmet crown vs interior roof
        assert pose['eye'][2] > 1.10 # front coaming sightline clearance
        cases.append(pose)
    for bad in (None,True,'1.78',float('nan'),float('inf'),0,1.59,1.97):
        try:fit(bad)
        except ValueError:pass
        else:raise AssertionError(('accepted invalid stature',bad))
    for p in panels():
        assert all(finite_vector(v) for v in p['points'])
        assert all(math.dist(world(local(v)),v)<1e-12 for v in p['points'])
    for field in ('pilot_eye_blender','cabin_to_ship'):
        bad = dict(SPEC);bad[field]=[0,math.nan,0]
        try:validate(bad)
        except ValueError:pass
        else:raise AssertionError('accepted nonfinite shared anchor')
    return cases


if __name__ == '__main__':
    print(json.dumps({'status':'passed','cases':checks(),'glazing_panels':5},indent=2))
