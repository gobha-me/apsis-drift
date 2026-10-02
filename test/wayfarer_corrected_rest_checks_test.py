"""Independent portable integrity/refusal fixtures for corrected q0 export."""
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
from fractions import Fraction
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import wayfarer_corrected_rest_checks as checks
import wayfarer_corrected_rest_proof as proof


def binary_glb(document, payload):
    document = copy.deepcopy(document)
    text = json.dumps(document, separators=(',', ':'), allow_nan=False).encode()
    text += b' ' * (-len(text) % 4)
    payload += b'\0' * (-len(payload) % 4)
    size = 28 + len(text) + len(payload)
    return (struct.pack('<III', 0x46546c67, 2, size) +
            struct.pack('<II', len(text), 0x4e4f534a) + text +
            struct.pack('<II', len(payload), 0x004e4942) + payload)


def fixture(positions=None, indices=None, normals=None, uvs=None):
    positions = positions or [[0., 0., 0.], [1., 0., 0.], [0., 1., 0.]]
    indices = indices or [0, 1, 2]
    normals = normals or [[0., 0., 1.] for _ in positions]
    uvs = uvs or [[0., 0.], [1., 0.], [0., 1.]]
    chunks = [struct.pack('<' + 'f' * (len(positions)*3), *[x for p in positions for x in p]),
              struct.pack('<' + 'f' * (len(normals)*3), *[x for p in normals for x in p]),
              struct.pack('<' + 'f' * (len(uvs)*2), *[x for p in uvs for x in p]),
              struct.pack('<' + 'H' * len(indices), *indices)]
    views = []; offset = 0
    for chunk in chunks:
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(chunk)})
        offset += len(chunk)
    payload = b''.join(chunks)
    document = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
                'nodes': [{'name': 'Fixture', 'mesh': 0}],
                'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2},
                                           'indices': 3, 'material': 0, 'mode': 4}]}],
                'materials': [{'name': 'Metal', 'pbrMetallicRoughness': {'metallicFactor': .5}}],
                'buffers': [{'byteLength': len(payload)}], 'bufferViews': views,
                'accessors': [{'bufferView': 0, 'componentType': 5126, 'type': 'VEC3', 'count': len(positions)},
                              {'bufferView': 1, 'componentType': 5126, 'type': 'VEC3', 'count': len(normals)},
                              {'bufferView': 2, 'componentType': 5126, 'type': 'VEC2', 'count': len(uvs)},
                              {'bufferView': 3, 'componentType': 5123, 'type': 'SCALAR', 'count': len(indices)}]}
    source = {'vertices': copy.deepcopy(positions), 'triangles': [indices[i:i+3] for i in range(0,len(indices),3)],
              'triangle_materials': ['Metal']*(len(indices)//3),
              'triangle_uv_gltf': [[uvs[v] for v in indices[i:i+3]] for i in range(0,len(indices),3)],
              'introduced_connector': False}
    report = {'groups': [{'node': 'Fixture', 'source_objects': ['Keep']}],
              'prepared_source_geometry': {'Keep': source}}
    return document, payload, report


def decoded(document=None, payload=None):
    if document is None:
        document, payload, _ = fixture()
    return checks.decode_glb(binary_glb(document, payload), ['Fixture'], True)[1]['Fixture']


class DecodeBoundaryTests(unittest.TestCase):
    def test_valid_flat_attributes_and_source_occurrence(self):
        doc, data, report = fixture()
        rows = checks.decode_glb(binary_glb(doc,data), ['Fixture'], True)[1]
        objects, attribution = checks.validate_attribution(report, rows, report['groups'])
        self.assertEqual(set(objects), {'Keep'})
        self.assertEqual(attribution[0]['faces'][0]['prepared_source_vertex_ids'], [0,1,2])
        self.assertEqual(objects['Keep'][0]['corners'][1]['POSITION'], [1.,0.,0.])

    def test_cyclic_winding_survives_but_reversal_refuses(self):
        doc, data, report = fixture(indices=[1,2,0])
        source = report['prepared_source_geometry']['Keep']
        source['triangles'] = [[0,1,2]]
        source['triangle_uv_gltf'] = [[[0.,0.],[1.,0.],[0.,1.]]]
        rows = checks.decode_glb(binary_glb(doc,data), ['Fixture'], True)[1]
        checks.validate_attribution(report, rows, report['groups'])
        doc, data, _ = fixture(indices=[0,2,1])
        rows = checks.decode_glb(binary_glb(doc,data), ['Fixture'], True)[1]
        with self.assertRaises(ValueError): checks.validate_attribution(report,rows,report['groups'])

    def test_source_corner_uv_signed_zero_requires_exact_bits(self):
        doc, data, report = fixture()
        rows = checks.decode_glb(binary_glb(doc, data), ['Fixture'], True)[1]
        report['prepared_source_geometry']['Keep']['triangle_uv_gltf'][0][0][0] = -0.0
        self.assertEqual(rows['Fixture'][0]['corners'][0]['TEXCOORD_0'][0], -0.0)
        with self.assertRaises(ValueError): checks.validate_attribution(report, rows, report['groups'])

    def test_preserved_degenerate_occurrences_are_not_dropped(self):
        doc,data,report=fixture(positions=[[0.,0.,0.],[1.,0.,0.],[2.,0.,0.]],indices=[0,1,2,0,1,2])
        rows=checks.decode_glb(binary_glb(doc,data),['Fixture'],True)[1]
        objects,attribution=checks.validate_attribution(report,rows,report['groups'])
        self.assertEqual([f['prepared_source_face'] for f in attribution[0]['faces']],[0,1])
        self.assertEqual(len(objects['Keep']),2)
        with self.assertRaises(ValueError):
            checks.validate_attribution(report,{'Fixture':rows['Fixture'][:1]},report['groups'])

    def test_missing_extra_duplicate_face_occurrences_refuse(self):
        doc,data,report=fixture();rows=checks.decode_glb(binary_glb(doc,data),['Fixture'],True)[1]
        for bad in [[],rows['Fixture']*2]:
            with self.subTest(count=len(bad)),self.assertRaises(ValueError):
                checks.validate_attribution(report,{'Fixture':bad},report['groups'])

    def test_duplicate_nested_nonroot_and_extra_nodes_refuse(self):
        original,data,_=fixture()
        variants=[]
        d=copy.deepcopy(original);d['nodes'].append(copy.deepcopy(d['nodes'][0]));d['scenes'][0]['nodes'].append(1);variants.append(d)
        d=copy.deepcopy(original);d['nodes'].append({'name':'Parent','children':[0]});d['scenes'][0]['nodes']=[1];variants.append(d)
        d=copy.deepcopy(original);d['scenes'][0]['nodes']=[];variants.append(d)
        d=copy.deepcopy(original);d['nodes'].append({'name':'Extra','mesh':0});d['scenes'][0]['nodes'].append(1);variants.append(d)
        for d in variants:
            with self.subTest(nodes=d['nodes']),self.assertRaises(ValueError): decoded(d,data)

    def test_nonfinite_attribute_and_bad_index_refuse(self):
        doc,data,_=fixture()
        for value in [math.nan,math.inf,-math.inf]:
            bad=bytearray(data);struct.pack_into('<f',bad,0,value)
            with self.subTest(value=value),self.assertRaises(ValueError): decoded(doc,bytes(bad))
        bad=bytearray(data);struct.pack_into('<H',bad,doc['bufferViews'][3]['byteOffset'],3)
        with self.assertRaises(ValueError): decoded(doc,bytes(bad))

    def test_masked_negative_view_offset_and_numeric_types_refuse(self):
        original,data,_=fixture();variants=[]
        d=copy.deepcopy(original);d['bufferViews'][0]['byteOffset']=-4;d['accessors'][0]['byteOffset']=4;variants.append(d)
        d=copy.deepcopy(original);d['accessors'][0]['count']=True;variants.append(d)
        d=copy.deepcopy(original);d['accessors'][0]['componentType']=5126.;variants.append(d)
        d=copy.deepcopy(original);d['meshes'][0]['primitives'][0]['mode']=4.;variants.append(d)
        d=copy.deepcopy(original);d['buffers'][0]['byteLength']=-1;variants.append(d)
        d=copy.deepcopy(original);d['bufferViews'][0]['byteLength']=len(data)+4;variants.append(d)
        for d in variants:
            with self.subTest(doc=d),self.assertRaises(ValueError): decoded(d,data)

    def test_truncated_chunk_and_morph_payload_refuse(self):
        doc,data,_=fixture();glb=binary_glb(doc,data)
        for bad in [glb[:12],glb[:-1],glb[:24],glb+b'XXXX']:
            with self.subTest(size=len(bad)),self.assertRaises(ValueError): checks.decode_glb(bad,['Fixture'],True)
        doc['meshes'][0]['primitives'][0]['targets']=[{'POSITION':0}]
        with self.assertRaises(ValueError): decoded(doc,data)

    def test_source_geometry_numeric_and_invisible_vertices_refuse(self):
        _,_,report=fixture();original=report['prepared_source_geometry']
        for change in ['nan','bool_index','float_index','wrong_width','unused_vertex','float32_loss']:
            g=copy.deepcopy(original)
            if change=='nan':g['Keep']['vertices'][0][0]=math.nan
            if change=='bool_index':g['Keep']['triangles'][0][0]=True
            if change=='float_index':g['Keep']['triangles'][0][0]=0.
            if change=='wrong_width':g['Keep']['vertices'][0]=[0.,0.]
            if change=='unused_vertex':g['Keep']['vertices'].append([2.,2.,2.])
            if change=='float32_loss':g['Keep']['vertices'][0][0]=.123456789
            with self.subTest(change=change),self.assertRaises(ValueError):checks.validate_geometry(g)


class PreservationTests(unittest.TestCase):
    def test_same_tuple_is_separate_from_rest_union(self):
        rest=decoded();doc,data,_=fixture(positions=[[2.,0.,0.],[3.,0.,0.],[2.,1.,0.]])
        posed=decoded(doc,data)
        receipt=checks.compare_baselines(rest,rest,{'Keep':posed},{'Keep':posed})
        self.assertEqual(receipt['untouched_objects'],['Keep'])
        with self.assertRaises(ValueError):checks.compare_baselines(rest,rest,{'Keep':rest},{'Keep':posed})
        with self.assertRaises(ValueError):checks.compare_baselines(rest,posed,{'Keep':posed},{'Keep':posed})

    def test_changed_ribbon_geometry_does_not_authorize_unrelated_change(self):
        old=decoded();new=copy.deepcopy(old);new[0]['corners'][0]['POSITION'][0]=.25
        checks.compare_baselines(old,old,{'Lap restraint':old},{'Lap restraint':new})
        with self.assertRaises(ValueError):checks.compare_baselines(old,old,{'Keep':old},{'Keep':new})

    def test_normals_uv_material_and_topology_are_full_bit_attributes(self):
        old=decoded()
        for change in ['normal','uv','material','extra','missing']:
            new=copy.deepcopy(old)
            if change=='normal':new[0]['corners'][0]['NORMAL'][0]=.25
            if change=='uv':new[0]['corners'][0]['TEXCOORD_0'][0]=.25
            if change=='material':new[0]['material_sha256']='0'*64
            if change=='extra':new*=2
            if change=='missing':new=[]
            with self.subTest(change=change),self.assertRaises(ValueError):
                checks.compare_baselines(old,old,{'Keep':old},{'Keep':new})

    def test_signed_zero_is_not_erased_by_float_equality(self):
        old=decoded();new=copy.deepcopy(old);new[0]['corners'][0]['NORMAL'][0]=-0.
        self.assertEqual(old[0]['corners'],new[0]['corners'])
        self.assertNotEqual(checks.semantic_key(old[0]),checks.semantic_key(new[0]))
        with self.assertRaises(ValueError):checks.compare_baselines(old,old,{'Keep':old},{'Keep':new})

    def test_source_contact_tamper_and_boolean_indices_refuse(self):
        doc,data,report=fixture();objects,_=checks.validate_attribution(report,checks.decode_glb(binary_glb(doc,data),['Fixture'],True)[1],report['groups'])
        report['model']={'sha256':'a'*64}
        contact={'model_sha256':'a'*64,'source_sha256':checks.SOURCE_SHA256,'coordinate_namespace':checks.NAMESPACE,
                 'fixed_operating_tuple':[1,1,1,0],'actor_load_motion_or_runtime_admission':False,
                 'objects':[{'source_object':'Keep','node':'Fixture','vertices_metres':report['prepared_source_geometry']['Keep']['vertices'],
                             'triangles':[[0,1,2]],'triangle_materials':['Metal'],'introduced_connector':False}]}
        checks.validate_contact(contact,report,objects)
        for change in ['coordinate','index','bool_index','tuple','actor','missing','duplicate']:
            bad=copy.deepcopy(contact)
            if change=='coordinate':bad['objects'][0]['vertices_metres'][0][0]=.25
            if change=='index':bad['objects'][0]['triangles'][0]=[0,2,1]
            if change=='bool_index':bad['objects'][0]['triangles'][0][1]=True
            if change=='tuple':bad['fixed_operating_tuple']=[True,1,1,0]
            if change=='actor':bad['actor_load_motion_or_runtime_admission']=True
            if change=='missing':bad['objects']=[]
            if change=='duplicate':bad['objects']*=2
            with self.subTest(change=change),self.assertRaises(ValueError):checks.validate_contact(bad,report,objects)


class PackageBoundaryTests(unittest.TestCase):
    def test_duplicate_nonfinite_deep_and_bad_json_refuse(self):
        for value in [b'{"x":1,"x":2}',b'{"x":NaN}',b'{"x":1e999}',b'['*34+b'0'+b']'*34,b'\xff',b'{}extra']:
            with self.subTest(value=value),self.assertRaises(ValueError):checks.closed_json(value)

    def test_member_identity_path_symlink_and_dimension_refuse(self):
        with tempfile.TemporaryDirectory() as raw:
            root=Path(raw);(root/'payload').write_bytes(b'proof')
            entry={'bytes':5,'sha256':hashlib.sha256(b'proof').hexdigest()}
            self.assertEqual(checks.read_member(root,'payload',entry),b'proof')
            for path in ['../payload','/payload']:
                with self.subTest(path=path),self.assertRaises(ValueError):checks.read_member(root,path,entry)
            for bad in [{'bytes':True,'sha256':entry['sha256']},{'bytes':4,'sha256':entry['sha256']},{'bytes':5,'sha256':'0'*64}]:
                with self.subTest(entry=bad),self.assertRaises(ValueError):checks.read_member(root,'payload',bad)
            (root/'link').symlink_to(root/'payload')
            with self.assertRaises(ValueError):checks.read_member(root,'link',entry)

    def test_missing_license_tampered_payload_and_unlisted_file_refuse(self):
        with tempfile.TemporaryDirectory() as raw:
            root=Path(raw);(root/'licenses').mkdir();(root/'model.glb').write_bytes(b'payload');(root/'licenses'/'LICENSE.md').write_bytes(b'license');(root/'package.json').write_text('{}')
            entries=[{'path':name,'bytes':len((root/name).read_bytes()),'sha256':hashlib.sha256((root/name).read_bytes()).hexdigest()} for name in ['model.glb','licenses/LICENSE.md']]
            manifest={'files':entries};checks.validate_files(root,manifest,[e['path'] for e in entries])
            (root/'model.glb').write_bytes(b'payloae')
            with self.assertRaises(ValueError):checks.validate_files(root,manifest,[e['path'] for e in entries])
            (root/'model.glb').write_bytes(b'payload');(root/'licenses'/'LICENSE.md').unlink()
            with self.assertRaises(ValueError):checks.validate_files(root,manifest,[e['path'] for e in entries])
            (root/'licenses'/'LICENSE.md').write_bytes(b'license');(root/'hidden').write_bytes(b'new')
            with self.assertRaises(ValueError):checks.validate_files(root,manifest,[e['path'] for e in entries])

    def test_duplicate_source_and_group_inventory_refuse(self):
        doc,data,report=fixture();rows=checks.decode_glb(binary_glb(doc,data),['Fixture'],True)[1]
        bad=copy.deepcopy(report);bad['groups'][0]['source_objects']=['Keep','Keep']
        with self.assertRaises(ValueError):checks.validate_attribution(bad,rows,bad['groups'])
        with self.assertRaises(ValueError):checks.validate_attribution(report,rows,[{'node':'Wrong','source_objects':['Keep']}])

    def test_wrong_source_namespace_and_pose_are_not_snapshot_identity(self):
        doc,data,report=fixture();model=binary_glb(doc,data)
        report.update(mode='corrected_closed',source_sha256=checks.SOURCE_SHA256,namespace=checks.NAMESPACE,
                      operating_tuple=[1,1,1,0],not_admitted=True,source_file_unchanged=True,
                      all_1746_original_signatures_restored=True,model={'bytes':len(model),'sha256':hashlib.sha256(model).hexdigest()})
        for change in ['source','namespace','pose','bool_pose','payload']:
            bad=copy.deepcopy(report)
            if change=='source':bad['source_sha256']='0'*64
            if change=='namespace':bad['namespace']='runtime rest'
            if change=='pose':bad['operating_tuple']=[0,0,0,0]
            if change=='bool_pose':bad['operating_tuple']=[True,1,1,0]
            if change=='payload':bad['model']['sha256']='0'*64
            with self.subTest(change=change),self.assertRaises(ValueError):checks.validate_snapshot(bad,model,bad['groups'],'corrected_closed')


def box_mesh(xlow=0., xhigh=1., ylow=0., yhigh=1., zlow=0., zhigh=1.):
    vertices = [[xlow,ylow,zlow],[xhigh,ylow,zlow],[xhigh,yhigh,zlow],[xlow,yhigh,zlow],
                [xlow,ylow,zhigh],[xhigh,ylow,zhigh],[xhigh,yhigh,zhigh],[xlow,yhigh,zhigh]]
    triangles = [[0,2,1],[0,3,2],[4,5,6],[4,6,7],[0,4,7],[0,7,3],
                 [1,2,6],[1,6,5],[0,1,5],[0,5,4],[3,7,6],[3,6,2]]
    return {'vertices':vertices,'triangles':triangles}


class ProductionBoundaryTests(unittest.TestCase):
    def test_wrong_source_closed_manifest_and_scope_refuse_before_reference(self):
        valid = {'schema': 'apsis.wayfarer-corrected-closed-assets/1',
                 'id': 'wayfarer-corrected-closed-01', 'source_sha256': checks.SOURCE_SHA256,
                 'model': {}, 'files': [], 'limits': checks.LIMITS}
        for change in ['source', 'schema', 'extra', 'scope']:
            bad = copy.deepcopy(valid)
            if change == 'source': bad['source_sha256'] = '0' * 64
            if change == 'schema': bad['schema'] = 'different/1'
            if change == 'extra': bad['runtime_admission'] = True
            if change == 'scope': bad['limits'] = []
            with self.subTest(change=change), tempfile.TemporaryDirectory() as tmp:
                Path(tmp, 'package.json').write_text(json.dumps(bad))
                with self.assertRaises(ValueError): checks.validate_derivative(tmp, Path(tmp, 'absent'))

    def test_missing_or_symlink_package_and_wrong_reference_refuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError): checks.validate_derivative(tmp)
            outside = Path(tmp, 'outside'); outside.write_text('{}')
            Path(tmp, 'package.json').symlink_to(outside)
            with self.assertRaises(ValueError): checks.validate_derivative(tmp)
            with self.assertRaises(ValueError): checks.old_reference(tmp)

    def test_provenance_version_must_match_all_fixed_snapshots(self):
        provenance = {key: None for key in ('schema', 'id', 'source', 'old_package',
                      'producer', 'helpers', 'baselines', 'registered_corrections',
                      'source_restoration', 'licenses', 'namespace', 'limits')}
        provenance.update(schema='apsis.corrected-closed-provenance/1',
                          id='wayfarer-corrected-closed-01', namespace=checks.NAMESPACE,
                          limits=checks.LIMITS,
                          source={'file': 'assets/visual/hopper-craft-09.blend',
                                  'sha256': checks.SOURCE_SHA256},
                          old_package={'id': 'wayfarer-operating-02',
                              'package_sha256': checks.PACKAGE_SHA256,
                              'model_sha256': checks.legacy.MODEL_SHA256,
                              'metadata_sha256': checks.METADATA_SHA256},
                          producer={'file': 'tools/export_wayfarer_corrected_rest.py',
                                    'sha256': '0' * 64, 'blender_version': '5.2.2'})
        reports = {mode: {'blender_version': '5.2.2'} for mode in
                   ['original_rest', 'original_posed', 'corrected_closed']}
        for change in ['display_label', 'different_producer', 'different_baseline', 'all_new_version']:
            bad, snapshots = copy.deepcopy(provenance), copy.deepcopy(reports)
            if change == 'display_label': bad['producer']['blender_version'] = '5.2.2 LTS'
            if change == 'different_producer': bad['producer']['blender_version'] = '5.3.0'
            if change == 'different_baseline': snapshots['original_rest']['blender_version'] = '5.3.0'
            if change == 'all_new_version':
                bad['producer']['blender_version'] = '5.3.0'
                for snapshot in snapshots.values(): snapshot['blender_version'] = '5.3.0'
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, 'Qualified Blender version'):
                checks.validate_provenance(bad, snapshots, {}, {}, Path('/absent-fixture'))

    def test_field_specific_json_bound_is_explicit(self):
        payload = b'{"evidence":"' + b'x' * 128 + b'"}'
        with self.assertRaises(ValueError): checks.closed_json(payload, maximum=128)
        self.assertEqual(checks.closed_json(payload, maximum=256)['evidence'], 'x' * 128)


class FiniteProofTests(unittest.TestCase):
    def test_exact_finite_patch_area_on_actual_coordinates(self):
        first=box_mesh();second=box_mesh(1.,2.,.25,.75,.25,.75)
        result=proof.finite_patch(first,[6,7],second,[4,5])
        area=result['total_exact_area_square_metres']
        self.assertEqual(Fraction(int(area['numerator']),int(area['denominator'])),Fraction(1,4))
        for part in result['contact_parts']:
            for p in part['exact_common_polygon_metres']:
                self.assertEqual(Fraction(p[0]),1)
                self.assertTrue(Fraction(1,4)<=Fraction(p[1])<=Fraction(3,4))
                self.assertTrue(Fraction(1,4)<=Fraction(p[2])<=Fraction(3,4))

    def test_plane_ulp_gap_same_winding_and_boundary_only_refuse(self):
        first=box_mesh()
        gap=box_mesh(math.nextafter(1.,2.),2.)
        winding=box_mesh(1.,2.)
        for index in [4,5]:winding['triangles'][index].reverse()
        boundary=box_mesh(1.,2.,1.,2.)
        for second in [gap,winding,boundary]:
            with self.subTest(second=second),self.assertRaises(ValueError):proof.finite_patch(first,[6,7],second,[4,5])
        with self.assertRaises(ValueError):proof.finite_patch(first,[6,6],box_mesh(1.,2.),[4,5])

    def test_distinct_ids_cannot_double_count_same_finite_area(self):
        first = box_mesh()
        first['triangles'].extend([first['triangles'][6][:], first['triangles'][7][:]])
        second = box_mesh(1., 2., .25, .75, .25, .75)
        with self.assertRaises(ValueError): proof.finite_patch(first, [6, 7, 12, 13], second, [4, 5])
        first = box_mesh()
        second['triangles'].append(second['triangles'][4][:])
        with self.assertRaises(ValueError): proof.finite_patch(first, [6, 7], second, [4, 5, 12])

    def test_proof_bad_coordinates_indices_and_dimensions_refuse(self):
        first=box_mesh()
        for change in ['nan','boolean_index','negative_index','short_mesh']:
            bad=copy.deepcopy(first)
            if change=='nan':bad['vertices'][0][0]=math.nan
            if change=='boolean_index':bad['triangles'][0][0]=True
            if change=='negative_index':bad['triangles'][0][0]=-1
            if change=='short_mesh':bad['vertices']=bad['vertices'][:3]
            with self.subTest(change=change),self.assertRaises(ValueError):proof.finite_patch(bad,[6,7],box_mesh(1.,2.),[4,5])

    def test_exact_overlap_enclosure_is_not_aabb_only(self):
        # Independently known integer cubes intersect in [1,2]^3.
        def cell(low,high):
            points=box_mesh(low,high,low,high,low,high)['vertices']
            points=[tuple(int(x) for x in p) for p in points]
            return {'points':points,'planes':proof.hull_planes(points),'bounds':[(low,high)]*3}
        actual,_=proof.intersection_vertices(cell(0,2),cell(1,3))
        self.assertEqual(set(actual),{(Fraction(x),Fraction(y),Fraction(z)) for x in [1,2] for y in [1,2] for z in [1,2]})
        kernel=cell(0,4)['planes']
        self.assertTrue(all(proof.dot(p[:3],v)<p[3] for p in kernel for v in actual))
        shifted=[tuple(x+4 for x in v) for v in actual]
        self.assertTrue(any(proof.dot(p[:3],v)>p[3] for p in kernel for v in shifted))
        clear,_=proof.intersection_vertices(cell(0,1),cell(2,3))
        self.assertEqual(clear,[])

    def test_open_or_reversed_volume_cannot_claim_closed_material(self):
        mesh=box_mesh();points=[tuple(int(x) for x in p) for p in mesh['vertices']]
        positive=proof.topology(points,mesh['triangles'])
        self.assertTrue(positive['edge_incidence_two'] and positive['oriented_edge_cancellation'] and positive['positive_volume'])
        opened=proof.topology(points,mesh['triangles'][:-1]);self.assertFalse(opened['edge_incidence_two'])
        reversed_mesh=[list(reversed(t)) for t in mesh['triangles']]
        self.assertFalse(proof.topology(points,reversed_mesh)['positive_volume'])


if __name__ == '__main__':
    unittest.main()
