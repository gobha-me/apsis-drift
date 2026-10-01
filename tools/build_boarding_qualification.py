#!/usr/bin/env python3
"""Fail closed on unexplained exact contacts; assemble bounded evidence. BSD-3-Clause."""
import argparse
import hashlib
import json
import math
from pathlib import Path


def pair(a, b):
    return tuple(sorted((a, b)))


def contact_policy():
    """Enumerated source joints; never classify unknown names by substrings."""
    craft, station = {}, {}

    def add(table, a, b, reason, intervals=((0., 1.),)):
        identity = pair(a, b)
        if identity in table:
            raise ValueError('Duplicate exact contact policy')
        table[identity] = {'reason': reason, 'intervals': intervals}

    for side in (-1, 1):
        for height in (-.2, .2):
            add(craft, f'AFT01 | hatch hinge fixed foot {side} {height}', f'AFT01 | hatch hinge link {side} {height}',
                'Exact authored hinge foot/link engagement at the closed stop; on roof_transfer the link leaves this contact after progress0.')
            add(craft, f'AFT01 | hatch hinge link {side} {height}', f'AFT01 | hatch hinge pin {side}',
                'Exact hinge link rotates on the authored pin; this bearing pair remains engaged.')
    for height in (.18, 1.36):
        add(craft, f'AFT01 | ladder pivot arm {height}', f'AFT01 | ladder wall bracket {height}',
            'Exact authored ladder pivot arm and wall-bracket bearing joint, not a pressure-wall exception.')
    for a, b in (('Fixed swivel housing', 'Lift outer guide'), ('Lift outer guide', 'Swivel bearing race'),
                 ('Swivel bearing race', 'Swivel detent boss'), ('Swivel bearing race', 'Swivel detent boss.001')):
        add(craft, a, b, 'Exact source seat bearing/guide/detent installation interface; plain source solids represent the engaged joint without modeled bearing recesses.')
    add(craft, 'Fixed swivel housing', 'Swivel lock pin',
        'Exact source pin actuator/housing guided engagement through withdrawal/restoration; this pair is distinct from the obstructing bolt and rotating surfaces.')
    for b in ('Rotating swivel plate', 'Swivel bearing race'):
        add(craft, 'Swivel lock pin', b,
            'Exact positive-lock engagement only while the seat is aligned: locked withdrawal progress0..0.60 or aligned restoration0.95..1. The freely rotating interval must have no crossing.',
            ((0., .60), (.95, 1.)))
    add(craft, 'Swivel detent boss', 'Swivel lock pin',
        'Exact forward detent/pin engagement during aligned lock withdrawal only; no free-turn exception.', ((0., .60),))
    for index in range(4):
        for side, suffix in ((-1, '.002'), (1, '.003')):
            bolt = f'DK09 DOOR | KIT05 | door 04 lock bolt {index} {side}.001'
            for component in ('guide rail', 'jamb'):
                add(station, f'DK09 DOOR | KIT03 | door 04 {component}{suffix}', bolt,
                    'Exact authored isolation-door bolt traverses its own fixed guide/jamb locking interface; no leaf/frame or pipe exception.')
            actuator_index = 8 + 2 * index + (side == 1)
            add(station, f'DK09 DOOR | KIT05 | door 04 lock actuator.{actuator_index:03}', bolt,
                'Exact bolt remains within its dedicated actuator until authored locking extension205..215.', ((0., 105 / 190),))
    for leaf, gasket in ((2, 3), (3, 4), (4, 5)):
        add(station, f'DK09 DOOR | KIT04 | door 04 leaf {leaf}.001', f'DK09 DOOR | KIT05 | door 04 leaf joint gasket.{gasket:03}',
            'Exact adjacent isolation-leaf joint gasket receives the lowering leaf and stays compressed after closure; no broad leaf overlap exception.',
            ((((160 + (leaf - 2) * 20) - 110) / 190, 1.),))
    for side in (-1, 1):
        add(station, 'DK09 | deck hatch retracting gasket.001', f'DK09 | sliding deck hatch {side}.001',
            'Exact retracting deck gasket raises only after the deck lids close at frame260 and compresses against the two closed lid surfaces.', ((150 / 190, 1.),))
    for suffix in (4, 5, 6, 7):
        add(station, f'DK09 | magnetic retention cartridge.{suffix:03}', 'DK09 | transfer shaft.001',
            'Exact authored magnetic retention cartridge mounting interface on the shaft outer wall through the bounded40mm release stroke.')
    for a, b in (('DK09 | safety gate crossbar.002', 'KIT10 | gate hinge knuckle'),
                 ('DK09 | safety gate crossbar.003', 'KIT10 | gate hinge knuckle.001'),
                 ('DK09 | safety gate upright.002', 'KIT10 | gate hinge knuckle'),
                 ('DK09 | safety gate upright.002', 'KIT10 | gate hinge knuckle.001')):
        add(station, a, b, 'Exact safety-gate hinge edge/knuckle pivot joint through authored gate rotation; no guardrail/body waiver.')
    if len(craft) != 18 or len(station) != 37:
        raise ValueError('Exact contact policy roster changed')
    return {'craft': craft, 'station': station}


def classify(raw):
    policies = contact_policy()
    sweeps, exceptions = [], []
    for source in raw['sweeps']:
        unexpected, intentional = [], []
        for contact in source['contacts']:
            identity = pair(contact['object_a'], contact['object_b'])
            rule = policies[source['owner']].get(identity)
            progress = contact.get('progress_samples', [])
            valid = len(progress) == contact['samples'] and all(type(v) in (float, int) and math.isfinite(v) and 0 <= v <= 1 for v in progress)
            intervals = rule['intervals'] if rule else ()
            if source['owner'] == 'craft' and source['channel'] != 'seat_boarding' and rule:
                # Other independent channels freeze the seat at its aligned
                # locked rest. Seat progression ranges don't apply to their p.
                intervals = ((0., 1.),)
            if source['owner'] == 'craft' and source['channel'] == 'roof_transfer' and identity[0].startswith('AFT01 | hatch hinge fixed foot '):
                intervals = ((0., 0.),)
            valid &= bool(rule) and all(any(low - 1e-9 <= p <= high + 1e-9 for low, high in intervals) for p in progress)
            output = {key: contact[key] for key in ('object_a', 'object_b', 'samples', 'first_progress', 'last_progress', 'triangle_pairs_max')}
            (intentional if valid else unexpected).append(output)
            if valid:
                for low, high in intervals:
                    if any(low - 1e-9 <= p <= high + 1e-9 for p in progress):
                        exceptions.append({'object_a': identity[0], 'object_b': identity[1], 'channel': source['channel'],
                                           'progress_range': [low, high], 'reason': rule['reason']})
        sweeps.append({'owner': source['owner'], 'channel': source['channel'], 'samples': source['samples'], 'method': source['method'],
                       'unexpected_contacts': unexpected, 'intentional_contacts': intentional})
    return sweeps, exceptions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runtime-proof', type=Path, required=True)
    parser.add_argument('--negative-proof', type=Path, required=True)
    args = parser.parse_args()
    def read(name):
        return json.loads((args.output / name).read_text())
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    raw = read('raw-sweeps.json')
    station = read('station-clearance-qualification.json')
    component = read('station-clearance-checks.json')
    crossing = read('crossing-analysis.json')
    negatives = json.loads(args.negative_proof.read_text())
    contact = read('contact.json')
    closure = read('station-closure.json')
    bounds = read('bounds-proof.json')
    proof = json.loads(args.runtime_proof.read_text())
    sweeps, exceptions = classify(raw)
    negative_controls = [{'name': row['name'], 'detected': row['detected']} for row in station['negative_controls']]
    negative_controls += negatives['negative_controls']
    negative_controls += [{'name': 'triangle_' + row['name'], 'detected':
                           row['result']['strict_crossing_pairs'] == 0 if row['name'] != 'crossing' else row['result']['strict_crossing_pairs'] > 0}
                          for row in crossing['negative_controls']]
    checks = {'contact_buffers': contact['schema_version'] == 1 and len(contact['groups']) == 32,
              'source_unchanged': raw['source_unchanged'] and station['source_unchanged'] and component['source_unchanged'],
              'station_bindings': len(closure['bindings']) == 17 and len(closure['corrections']) == 3 and proof['count'] == 17 and not proof['problems'] and proof['glb_unchanged'],
              'motion_sweeps': station['pass'] and bounds['geometry_pass'] and all(not sweep['unexpected_contacts'] for sweep in sweeps),
              'negative_controls': bool(negative_controls) and all(row['detected'] for row in negative_controls)}
    source_objects = set()
    for group in contact['groups']:
        vertices, faces = group['vertices_micrometres'], group['triangles']
        checks['contact_buffers'] &= bool(vertices) and bool(faces) and len(vertices) <= 2000000 and len(faces) <= 4000000
        checks['contact_buffers'] &= all(type(v) is list and len(v) == 3 and all(type(n) is int and abs(n) <= 10000000000 for n in v) for v in vertices)
        checks['contact_buffers'] &= all(type(face) is list and len(face) == 3 and len(set(face)) == 3 and all(type(n) is int and 0 <= n < len(vertices) for n in face) for face in faces)
        source_objects.update(group['source_objects'])
    if any(row['object_a'] not in source_objects or row['object_b'] not in source_objects for row in exceptions):
        raise ValueError('Exception pair outside extracted contact roster')
    component_digest = digest(args.output / 'station-d1-clearance-01.glb')
    if component_digest != component['model_sha256'] or any(row['model_sha256'] != component_digest for row in closure['corrections']):
        raise ValueError('Qualification component identity disagreement')
    if contact['sources'] != raw['sources'] or station['sources'] != raw['sources'] or crossing['sources'] != raw['sources']:
        raise ValueError('Qualification source identities disagree')
    report = {'schema_version': 1, 'pass': all(checks.values()), 'sources': raw['sources'],
              'contact_sha256': digest(args.output / 'contact.json'), 'station_closure_sha256': digest(args.output / 'station-closure.json'),
              'station_model_sha256': component_digest,
              'runtime_station_proof_sha256': digest(args.runtime_proof), 'checks': checks,
              'sweeps': sweeps, 'exceptions': exceptions, 'negative_controls': negative_controls,
              'limits': ['Operating hardware only: no player body entry, articulated ladder reach, seat reach, flight boarding or pressure/load certification.',
                         '41 samples per full hardware channel and81pipe closure samples detect exact triangle crossings; sampled rotational motion does not prove continuous separation.',
                         'Exact source hinge/bearing/bolt/guide/gasket/retention/positive-lock engagements are enumerated; unknown contact pairs or forbidden free-turn pin contact fail qualification.',
                         'The affected existing supported station corridor has full standing swept-AABB separation and121×5realtriangle floor probes. No floor is added under the D1 shaft.',
                         'The corrected4.357mshaft pure-descent reservation is clear for a0.60×0.40×1.95mcompact proxy; this geometric result does not admit climbing or body entry.',
                         'The existing inner cabin threshold lies77.5mm below the nominal floor over craft back-axis3.6305..3.6644m and fails complete30mm-reach five-probe support. Its failed support evidence is retained; no completed supported cabin traversal is claimed.',
                         '40mm deck/flange bores around the18mm pipe leave2mm nominal radial clearance; no sealing collar is modeled. Source pressure/service ratings are not certified.',
                         'Surface intersection diagnostics do not establish absence of solid containment; full standing-volume SAT separately catches contained contact faces.']}
    (args.output / 'qualification.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    evidence_inputs = {name: args.output / name for name in ('raw-sweeps.json', 'crossing-analysis.json', 'station-clearance-checks.json',
                      'station-clearance-qualification.json', 'bounds-proof.json', 'negative-controls.json')}
    evidence_inputs['runtime-station-proof.json'] = args.runtime_proof
    for name in ('lock-qualified-phase.json', 'seat-corrected-complete.json'):
        path = args.output.parent / name
        if path.exists():
            evidence_inputs[name] = path
    evidence = {'schema_version': 1, 'scope': 'Source-bound operating hardware and conservative bounded geometric evidence; no actor boarding admission',
                'sources': raw['sources'], 'contact_sha256': report['contact_sha256'],
                'station_closure_sha256': report['station_closure_sha256'], 'station_model_sha256': component_digest,
                'qualification_sha256': digest(args.output / 'qualification.json'),
                'receipts': {name: {'sha256': digest(path), 'data': json.loads(path.read_text())} for name, path in evidence_inputs.items()}}
    (args.output / 'boarding-evidence.json').write_text(json.dumps(evidence, indent=2, allow_nan=False) + '\n')
    print('BOARDING_QUALIFICATION', report['pass'], checks)


if __name__ == '__main__':
    main()
