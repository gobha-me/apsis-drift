# SPDX-License-Identifier: BSD-3-Clause
"""Invented coordinates only. No actual capture or project buffer reads."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import wayfarer_stowed_joint_observation as m


def fixture():
    sources = {}
    for name, (sid, nv, nf) in m.OWNER_PROFILE.items():
        sources[name] = {'vertices': [[0., 0., 0.] for _ in range(nv)],
                         'triangles': [[nv-1, nv-1, nv-1] for _ in range(nf)], 'introduced_connector': False}
    bridges = {identity: {'vertices': [[0., 0., 0.] for _ in range(nv)],
                          'faces': [[nv-1, nv-1, nv-1] for _ in range(nf)]}
               for identity, (nv, nf) in m.BRIDGE_PROFILE.items()}
    for d in m.ATTACHMENTS:
        name = next(n for n, p in m.OWNER_PROFILE.items() if p[0] == d['source_id'])
        source, bridge = sources[name], bridges[d['bridge_id']]
        for fid, tri in zip(d['cap_face_ids'], d['cap_triangles']):
            bridge['faces'][fid] = tri[:]
        capids = sorted({i for t in d['cap_triangles'] for i in t})
        if d['kind'] == 'partial_anchor':
            for i, p in zip([14,22,51,43], [[0.,0.,0.],[2.,0.,0.],[2.,2.,0.],[0.,2.,0.]]):
                source['vertices'][i] = p
            source['triangles'][104:106] = [[14,22,51],[14,51,43]]
            source['triangles'][88] = [14,43,45]
            source['triangles'][95] = [22,51,45]
            source['triangles'][0] = [14,30,31]  # genuine corner-only source face
            for i, p in zip(capids, [[0.,.25,0.],[0.,1.75,0.],[1.,1.75,0.],[2.,1.75,0.],[2.,.25,0.],[1.,.25,0.]]):
                bridge['vertices'][i] = p
        else:
            points = ([[0.,0.,0.],[2.,0.,0.],[2.,2.,.125],[0.,2.,0.]] if len(capids)==4
                      else [[0.,0.,0.],[1.,0.,0.],[2.,0.,0.],[2.,2.,.125],[1.,2.,0.],[0.,2.,0.]])
            ids = d['source_boundary_vertex_ids']
            mapping = dict(zip(capids, ids))
            for bid, p in zip(capids, points):
                bridge['vertices'][bid] = p[:]
                source['vertices'][mapping[bid]] = p[:]
            for fid, tri in zip(d['source_face_ids'], d['cap_triangles']):
                source['triangles'][fid] = [mapping[i] for i in reversed(tri)]
    # Exact root-cap0 boundary incidence fixture, without any material solver.
    source = sources['Shoulder restraint']; bridge = bridges['shoulder_port']
    source['triangles'][0] = [0,1,2]
    source['triangles'][1] = [0,3,4]  # corner-only; not edge support
    bridge['faces'][13] = [3,4,0]
    geometry = copy.deepcopy(sources)
    for i in range(56):geometry['unused_'+str(i)] = {'introduced_connector': False}
    for identity, b in bridges.items():
        geometry['Stowed restraint static connector '+identity] = {'vertices':copy.deepcopy(b['vertices']), 'triangles':copy.deepcopy(b['faces']), 'introduced_connector':True}
    signatures = {name: 'a'*64 for name in sources}
    signatures.update({'other'+str(i):'b'*64 for i in range(1738)})
    capture = {'namespace':m.NAMESPACE,'rest_shape_once':True,'operating_tuple':[0,0,0,0],
               'source_sha256':'c'*64,'model_output':{'model':{'sha256':'d'*64}},
               'all1746_covered_signatures_before':signatures,'all1746_covered_signatures_restored':copy.deepcopy(signatures),
               'prepared_source_geometry':geometry}
    return capture, bridges


def inputs(data=None, changes=None):
    c, bs = copy.deepcopy(data or fixture())
    owners = []
    for name, (sid, nv, nf) in m.OWNER_PROFILE.items():
        g = c['prepared_source_geometry'][name]; h, k = m.payload_pins(g['vertices'],g['triangles'])
        owners.append({'source_id':sid,'source_object':name,'vertices':nv,'faces':nf,'source_signature':'a'*64,'float32_bits_sha256':h,'int32_bits_sha256':k})
    rows = []
    for identity in m.IDS:
        b = bs[identity];h,k=m.payload_pins(b['vertices'],b['faces']);nv,nf=m.BRIDGE_PROFILE[identity]
        rows.append({'id':identity,'source_object':'Stowed restraint static connector '+identity,'vertices':nv,'faces':nf,'float32_bits_sha256':h,'int32_bits_sha256':k})
    buffers = {'capture':m.canonical(c),**{i:m.canonical(bs[i]) for i in m.IDS}}
    reg = {'schema':'apsis.all-attachment-perimeter-intake/1','namespace':m.NAMESPACE,
           'source_sha256':'c'*64,'model_sha256':'d'*64,'buffers':{k:m.pin(v) for k,v in buffers.items()},
           'owners':owners,'bridges':rows,'attachments':[{**d,'finite_cap_evidence_sha256':'e'*64} for d in m.ATTACHMENTS]}
    if changes:changes(reg,buffers)
    raw=m.canonical(reg)
    return raw,m.pin(raw)['sha256'],buffers


def observe(data=None,changes=None,**kwargs):return json.loads(m.observe_attachments(*inputs(data,changes),**kwargs))


class Tests(unittest.TestCase):
    def refuses(self,data,text):
        with self.assertRaisesRegex(ValueError,text):observe(data)
    def test_whole_roster_complete_and_corner_distinction(self):
        d=observe();self.assertEqual(len(d['records']),10);self.assertTrue(d['coverage_complete']);self.assertEqual(d['counts']['boundary_edge_occurrences'],46)
        root=d['records'][0];e=next(x for x in root['edges'] if x['cap_edge']==[0,3]);self.assertEqual(e['source_owner_matches'][0]['source_edge'],[0,1])
        inc=e['source_owner_matches'][0]['incidence'];self.assertIn(0,[r['face']for r in inc['edge_faces']]);self.assertIn(1,[r['face']for r in inc['endpoint_only_faces']]);self.assertNotIn(1,[r['face']for r in inc['edge_faces']]);self.assertFalse(d['physical_permission'])
    def test_independent_full_incidence_oracle(self):
        c,bs=fixture();d=observe((c,bs))
        for record in d['records']:
            desc=record['descriptor'];name=next(n for n,p in m.OWNER_PROFILE.items() if p[0]==desc['source_id']);sf=c['prepared_source_geometry'][name]['triangles'];bf=bs[desc['bridge_id']]['faces']
            for row in record['source_boundary']:
                E=set(row['vertex_ids']);self.assertEqual([x['face'] for x in row['incidence']['edge_faces']],[i for i,f in enumerate(sf) if E<=set(f)])
            for row in record['edges']:
                E=set(row['cap_edge']);self.assertEqual([x['face'] for x in row['bridge_incidence']['edge_faces']],[i for i,f in enumerate(bf) if E<=set(f)])
    def test_actual_domain_sizes_warped_source_no_rectangle_gate(self):
        d=observe();shell=[x for x in d['records']if x['descriptor']['source_id']=='source_0953'];self.assertEqual(len(shell),3);self.assertEqual(len(shell[-1]['cap_boundary']),6)
        self.assertTrue(any(h.endswith('0000003e') for x in shell for e in x['edges'] for h in e['raw_endpoint_bits']))
    def test_partial_six_boundary_internal_and_unmatched(self):
        x=observe()['records'][1];self.assertEqual(len(x['cap_boundary']),6);self.assertEqual(len(x['cap_internal_diagonals']),3);self.assertEqual(sum(e['relation']=='NOT_ON_INDEXED_OWNER_PERIMETER' for e in x['edges']),4);self.assertEqual(len(x['source_boundary']),4)
    def test_partial_original_corner_only(self):
        data=fixture();data[1]['shoulder_port']['vertices'][12]=[0.,0.,0.];data[0]['prepared_source_geometry']['Stowed restraint static connector shoulder_port']['vertices'][12]=[0.,0.,0.]
        x=observe(data)['records'][1];corner=next(z for z in x['corners'] if z['bridge_vertex']==12);self.assertEqual(len(corner['finite_owner_edge_point_contacts']),2);self.assertIn(0,[r['face'] for r in corner['original_indexed_corner_matches'][0]['source_incident_faces']]);self.assertTrue(corner['corner_contact_is_not_edge_permission'])
    def test_signed_zero_preserved_no_alias(self):
        data=fixture();data[1]['shoulder_port']['vertices'][0][2]=-0.;data[0]['prepared_source_geometry']['Shoulder restraint']['vertices'][1][2]=-0.;data[0]['prepared_source_geometry']['Stowed restraint static connector shoulder_port']['vertices'][0][2]=-0.
        root=observe(data)['records'][0];self.assertTrue(any('00000080' in e['raw_endpoint_bits'][0] for e in root['edges']))
    def test_signed_zero_whole_mapping_mismatch_refuses(self):
        data=fixture();data[0]['prepared_source_geometry']['Shoulder restraint']['vertices'][1][2]=-0.;self.refuses(data,'raw point mapping')
    def test_coincident_unused_source_not_substituted(self):
        data=fixture();g=data[0]['prepared_source_geometry']['Shoulder restraint'];g['vertices'][17]=g['vertices'][0][:];g['vertices'][18]=g['vertices'][1][:];g['triangles'][2]=[17,18,19]
        e=next(e for e in observe(data)['records'][0]['edges']if e['cap_edge']==[0,3]);self.assertNotIn(2,[r['face']for r in e['source_owner_matches'][0]['incidence']['edge_faces']])
    def test_registered_duplicate_boundary_coordinate_ambiguous_refuses(self):
        data=fixture();g=data[0]['prepared_source_geometry']['Shoulder restraint'];g['vertices'][11]=g['vertices'][1][:];self.refuses(data,'raw point mapping')
    def test_opposed_original_diagonal_preserved(self):
        data=fixture();data[0]['prepared_source_geometry']['Shoulder restraint']['triangles'][18].reverse();self.refuses(data,'Opposed internal diagonal|opposition')
    def test_cap_triangle_order_refuses(self):
        data=fixture();data[1]['shoulder_port']['faces'][0].reverse();data[0]['prepared_source_geometry']['Stowed restraint static connector shoulder_port']['triangles'][0].reverse();self.refuses(data,'Actual cap ordered')
    def test_axis_partial_domain_refuses_warp(self):
        data=fixture();data[0]['prepared_source_geometry']['Harness shoulder anchor']['vertices'][51][2]=.125;self.refuses(data,'rectangle')
    def test_partial_offfinite_owner_refuses(self):
        data=fixture();data[1]['shoulder_port']['vertices'][12][1]=-.25;data[0]['prepared_source_geometry']['Stowed restraint static connector shoulder_port']['vertices'][12][1]=-.25;self.refuses(data,'finite original rectangle')
    def test_unrelated_degenerate_source_preserved(self):
        data=fixture();data[0]['prepared_source_geometry']['Contoured seat-pan shell']['triangles'][0]=[377,377,377];self.assertTrue(observe(data)['coverage_complete'])
    def test_nonfinite_scalar_refuses(self):
        raw,h,b=inputs();b['capture']=b['capture'].replace(b'0.0',b'NaN',1);r=json.loads(raw);r['buffers']['capture']=m.pin(b['capture']);raw=m.canonical(r)
        with self.assertRaisesRegex(ValueError,'Nonfinite'):m.observe_attachments(raw,m.pin(raw)['sha256'],b)
    def test_nonf32_refuses_no_rounding(self):
        data=fixture();data[0]['prepared_source_geometry']['Shoulder restraint']['vertices'][19][0]=.1;self.refuses(data,'Exact binary32')
    def test_bool_index_and_dimension_refuse(self):
        data=fixture();data[0]['prepared_source_geometry']['Shoulder restraint']['triangles'][0][0]=True;self.refuses(data,'True integer')
        with self.assertRaisesRegex(ValueError,'Fixed owner|True integer'):observe(changes=lambda r,b:r['owners'][0].update(vertices=True))
    def test_same_name_wrong_original_or_signature_refuses(self):
        data=fixture();data[0]['prepared_source_geometry']['Shoulder restraint']['introduced_connector']=True;self.refuses(data,'roster|Original owner')
        data=fixture();data[0]['all1746_covered_signatures_restored']['Shoulder restraint']='f'*64;self.refuses(data,'signature')
    def test_model_frame_and_tuple_refuse(self):
        for key,value in [('namespace','other'),('operating_tuple',[False,0,0,0]),('source_sha256','f'*64)]:
            data=fixture();data[0][key]=value;self.refuses(data,'frame|identity')
    def test_external_pin_and_roles_predecode(self):
        raw,h,b=inputs();b.pop('crotch')
        with self.assertRaisesRegex(ValueError,'six immutable'):m.observe_attachments(raw,h,b)
        with self.assertRaisesRegex(ValueError,'Independent'):m.observe_attachments(raw,'0'*64,{})
    def test_original_capture_bridge_drift_refuses(self):
        data=fixture();data[0]['prepared_source_geometry']['Stowed restraint static connector shoulder_port']['vertices'][17][2]=1.;self.refuses(data,'raw correspondence')
    def test_tampered_descriptor_cannot_add_permission(self):
        with self.assertRaisesRegex(ValueError,'selected descriptor'):observe(changes=lambda r,b:r['attachments'][0].update(source_face_ids=[0,19]))
        with self.assertRaisesRegex(ValueError,'Closed'):observe(changes=lambda r,b:r['attachments'][0].update(whole_owner_permission=True))
    def test_large_capture_accepted_not_4096_entries(self):
        data=fixture();data[0]['passive_text']='x'*100000;self.assertTrue(observe(data)['coverage_complete'])
    def test_capture_byte_ceiling_before_decode(self):
        raw,h,b=inputs();r=json.loads(raw);r['buffers']['capture']['bytes']=m.MAX_CAPTURE+1;raw=m.canonical(r)
        with self.assertRaisesRegex(ValueError,'True integer'):m.observe_attachments(raw,m.pin(raw)['sha256'],b)
    def test_duplicate_depth_and_entry_gates(self):
        w=m.Work(m.MAX_OBSERVATION_OPERATIONS)
        for raw,text in [(b'{"x":0,"x":1}','Duplicate'),(b'['*33+b'0'+b']'*33,'depth')]:
            with self.assertRaisesRegex(ValueError,text):m.decode(raw,65536,w)
        with self.assertRaisesRegex(ValueError,'entry ceiling'):m.decode(b'[1,2,3]',65536,w,2)
    def test_work_refusal_honest_no_complete(self):
        with self.assertRaises(m.ObservationRefusal)as cm:observe(operation_limit=10)
        self.assertFalse(cm.exception.observation_context['coverage_complete'])
    def test_dense_output_refuses_without_omission(self):
        # Fixed output-bound seam, with a genuinely larger complete observation.
        old=m.MAX_OUTPUT
        try:
            m.MAX_OUTPUT=1000
            with self.assertRaises(m.ObservationRefusal)as cm:observe()
            self.assertIn('byte ceiling',str(cm.exception));self.assertFalse(cm.exception.observation_context['coverage_complete']);self.assertEqual(cm.exception.observation_context['caps_completed'],10)
        finally:m.MAX_OUTPUT=old

    def test_genuine_approved_capture_sized_invented_input(self):
        data=fixture();data[0]['passive_text']='';base=len(m.canonical(data[0]));data[0]['passive_text']='x'*(33098990-base)
        self.assertEqual(len(m.canonical(data[0])),33098990)
        self.assertTrue(observe(data)['coverage_complete'])
    def test_lexical_entry_bound_precedes_json_allocation(self):
        original=m.json.loads
        try:
            m.json.loads=lambda *a,**k:(_ for _ in ()).throw(AssertionError('must not allocate'))
            with self.assertRaisesRegex(ValueError,'before allocation'):m.decode(b'[1,2,3]',65536,m.Work(100),2)
        finally:m.json.loads=original
    def test_large_byte_refusal_precedes_json_allocation(self):
        original=m.json.loads
        try:
            m.json.loads=lambda *a,**k:(_ for _ in ()).throw(AssertionError('must not allocate'))
            with self.assertRaisesRegex(ValueError,'Bounded immutable'):m.decode(b'x'*65537,65536,m.Work(100000))
        finally:m.json.loads=original

    def test_complete_index_rosters_enable_independent_recorded_incidence(self):
        c,b=fixture();d=observe((c,b));self.assertEqual(len(d['source_index_rosters']),8);self.assertEqual(len(d['bridge_index_rosters']),5)
        for row in d['source_index_rosters']:
            self.assertEqual(row['indices'],c['prepared_source_geometry'][row['source_object']]['triangles']);self.assertNotIn('coordinates',row)
        for row in d['bridge_index_rosters']:
            self.assertEqual(row['indices'],b[row['id']]['faces']);self.assertNotIn('coordinates',row)

    def test_independent_sha_is_true_string_not_equality_spoof(self):
        class Spoof:
            def __eq__(self,other):return True
        raw,h,b=inputs()
        for bad in (True,False,None,Spoof(),'x'*64,'A'*64,h[:-1]):
            with self.assertRaisesRegex(ValueError,'SHA256 metadata'):m.observe_attachments(raw,bad,b)

    def test_oversized_registration_refuses_before_hashing(self):
        original=m.pin
        try:
            m.pin=lambda raw:(_ for _ in ()).throw(AssertionError('hash must not start'))
            for raw in (b'',b'x'*(m.MAX_REGISTRATION+1),None):
                with self.assertRaisesRegex(m.ObservationRefusal,'Registration bytes before hashing'):
                    m.observe_attachments(raw,'0'*64,{})
        finally:m.pin=original

    def test_wrong_role_length_refuses_before_hashing(self):
        raw,h,buffers=inputs();original=m.pin
        for role in m.ROLES:
            for wrong in (b'',buffers[role]+b' ',None):
                supplied=dict(buffers);supplied[role]=wrong
                try:
                    def guarded(data):
                        if data is wrong:raise AssertionError('wrong-sized role must not be hashed')
                        return original(data)
                    m.pin=guarded
                    with self.assertRaisesRegex(m.ObservationRefusal,'Role bytes before hashing'):
                        m.observe_attachments(raw,h,supplied)
                finally:m.pin=original

if __name__=='__main__':unittest.main(verbosity=2)
