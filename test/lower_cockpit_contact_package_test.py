#!/usr/bin/env python3
"""Independent source-bound admission, real geometry and atomic refusal controls."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import lower_cockpit_contact_spec as spec
import package_lower_cockpit_contact as builder
import prepare_lower_cockpit_contact as assets
from lower_cockpit_contact_identity import ENGINE_BINDINGS, POLICY, POLICY_SHA256, RUNTIME_SHA256, SOURCE_SHA256


class LowerCockpitPackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='apsis-lower-contact-test-')
        cls.root = Path(cls.temp.name)
        cls.source = ROOT/'assets/native/lower-cockpit-contact-01'
        assets.verify(cls.source)
        cls.halo = spec.decode((cls.source/assets.RUNTIME).read_bytes(),spec.MAX_RUNTIME)
        cls.contact, cls.support = assets.dependencies()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def package(self, name):
        target = self.root/name
        shutil.copytree(self.source,target)
        return target

    def refused(self, callback):
        with self.assertRaises((ValueError,OSError,TypeError,KeyError)):
            callback()

    def rewrite(self, package, name, raw):
        (package/name).write_bytes(raw)
        manifest=json.loads((package/'package.json').read_bytes())
        for record in manifest['files']:
            if record['path']==name:
                record.update(bytes=len(raw),sha256=assets.digest(raw))
        (package/'package.json').write_bytes(spec.encode(manifest))

    def test_exact_geometry_namespaces_and_real_omitted_faces(self):
        self.assertEqual(assets.digest((self.source/assets.RUNTIME).read_bytes()),RUNTIME_SHA256)
        self.assertEqual(assets.digest((self.source/assets.POLICY_FILE).read_bytes()),POLICY_SHA256)
        self.assertEqual(spec.validate_halo(self.halo,self.contact,self.support),self.halo)
        self.assertEqual(spec.validate_policy(POLICY),POLICY)
        objects=self.halo['objects']
        step=next(o for o in objects if o['source_object']=='CRAFT | pilot transition intermediate step')
        self.assertIsNone(step['admitted_range'])
        self.assertEqual(step['halo_triangle_start'],480)
        self.assertEqual(step['evaluated_source_triangle_indices'][182:184],[182,183])
        self.assertEqual(step['halo_triangle_start']+182,662)
        tub=next(o for o in objects if o['source_object']=='CRAFT | cockpit pressure tub')
        self.assertEqual(tub['admitted_range']['triangle_start'],111783)
        self.assertEqual(tub['admitted_range']['triangle_count'],52)
        self.assertEqual(tub['halo_triangle_start'],462)
        self.assertIn(8,tub['evaluated_source_triangle_indices'])
        self.assertNotEqual(self.halo['triangles'][8],self.halo['triangles'][662])
        for triangle_index in (662,663):
            points=[self.halo['vertices_micrometres'][i] for i in self.halo['triangles'][triangle_index]]
            normal=spec.cross([points[1][i]-points[0][i] for i in range(3)],
                              [points[2][i]-points[0][i] for i in range(3)])
            self.assertGreater(normal[1],0)
            self.assertTrue(all(p[1]==-230000 for p in points))
        self.assertEqual(sum('evaluated_source_triangle_indices_in_region' in o for o in objects),6)
        self.assertTrue(any(abs(o['source_corrected_world_rows'][1][1]-0.527999997138977)<1e-6 for o in objects))
        # Retained whole-triangle extrema are not a larger query coverage certificate.
        self.assertGreater(max(v[2] for v in self.halo['vertices_micrometres']),-500000)
        self.assertEqual(POLICY['coverage']['lower_static_bounds_micrometres'][1][2],-500000)

    def test_real_prepare_byte_identity_existing_destination_and_no_source_writes(self):
        before={p.relative_to(self.source):assets.digest(p.read_bytes()) for p in self.source.rglob('*') if p.is_file()}
        output=self.root/'prepared'
        receipt=assets.prepare(self.source,output,ROOT)
        self.assertEqual(receipt['runtime_sha256'],RUNTIME_SHA256)
        self.assertEqual(receipt['policy_sha256'],POLICY_SHA256)
        for name in before:
            self.assertEqual((output/name).read_bytes(),(self.source/name).read_bytes())
        self.assertEqual(before,{p.relative_to(self.source):assets.digest(p.read_bytes()) for p in self.source.rglob('*') if p.is_file()})
        inode=output.stat().st_ino
        self.refused(lambda:assets.prepare(self.source,output,ROOT))
        self.assertEqual(inode,output.stat().st_ino)

    def test_real_producer_deterministic_and_source_roster(self):
        source=self.source/'sources/wayfarer-floor-evidence-01'
        source_tools=self.source/'sources/tools'
        output=self.root/'rebuilt'
        result=builder.build(source,output,ROOT,source_tools)
        self.assertEqual(result,assets.digest((self.source/'package.json').read_bytes()))
        for p in self.source.rglob('*'):
            if p.is_file():self.assertEqual(p.read_bytes(),(output/p.relative_to(self.source)).read_bytes())
        self.refused(lambda:builder.build(source,output,ROOT,source_tools))
        altered=self.root/'source-with-extra'
        shutil.copytree(source,altered)
        (altered/'unlisted').write_bytes(b'not source evidence')
        self.refused(lambda:builder.build(altered,self.root/'extra-source-output',ROOT,source_tools))
        self.assertFalse((self.root/'extra-source-output').exists())

    def test_supplied_checker_replays_only_disposable_copy(self):
        original=self.source/'sources/wayfarer-floor-evidence-01'
        before={p.relative_to(original):assets.digest(p.read_bytes()) for p in original.rglob('*') if p.is_file()}
        copy_path=self.root/'checker-copy';shutil.copytree(original,copy_path)
        script=self.source/'sources/tools/check_boarding_floor_evidence.py'
        result=subprocess.run([sys.executable,str(script),'--engine-root',str(ROOT),'--package',str(copy_path)],
                              capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual((copy_path/'checks.json').read_bytes(),(original/'checks.json').read_bytes())
        self.assertEqual(before,{p.relative_to(original):assets.digest(p.read_bytes()) for p in original.rglob('*') if p.is_file()})

    def test_bounded_closed_parser_and_policy_types(self):
        for raw in (b'{"x":0,"x":1}',b'{"x":NaN}',b'{"x":Infinity}',b'{"x":1e9999}',
                    b'['*17+b'0'+b']'*17,b'\xff',b'',b' '*(spec.MAX_RUNTIME+1)):
            with self.subTest(raw=raw[:20]):self.refused(lambda:spec.decode(raw,spec.MAX_RUNTIME))
        self.assertEqual(spec.decode(b'{"x":"[[[\\\"]]]"}',128),{'x':'[[["]]]'})
        changes=[lambda v:v.update(version=True),lambda v:v.update(unknown=1),
                 lambda v:v['counts'].update(vertices=4598.0),
                 lambda v:v['coverage'].update(operation='enclosing_box'),
                 lambda v:v['coverage']['lower_static_bounds_micrometres'][1].__setitem__(2,-400000),
                 lambda v:v['namespaces'].update(halo='original_contact_group_triangle'),
                 lambda v:v['ownership'].update(motion_group='seat_lift')]
        for mutate in changes:
            value=copy.deepcopy(POLICY);mutate(value)
            self.refused(lambda:spec.validate_policy(value))

    def test_halo_structure_indices_owner_geometry_and_affine_controls(self):
        changes=[lambda v:v.update(schema_version=True),lambda v:v.update(unknown=1),
            lambda v:v['vertices_micrometres'][0].__setitem__(0,True),
            lambda v:v['vertices_micrometres'][0].__setitem__(0,128000001),
            lambda v:v['vertices_micrometres'][0].__setitem__(0,.5),
            lambda v:v['triangles'][0].__setitem__(0,-1),
            lambda v:v['triangles'][0].__setitem__(0,len(v['vertices_micrometres'])),
            lambda v:v['triangles'][0].__setitem__(0,True),
            lambda v:v['triangles'].__setitem__(0,[0,0,0]),
            lambda v:v['objects'][0].update(halo_triangle_start=1),
            lambda v:v['objects'][1].update(halo_triangle_start=0),
            lambda v:v['objects'][0].update(halo_triangle_count=2**64),
            lambda v:v['objects'][0].update(halo_triangle_count=True),
            lambda v:v['objects'][0].update(owner='station'),
            lambda v:v['objects'][0].update(motion_group='seat_lift'),
            lambda v:v['objects'][1].update(source_object=v['objects'][0]['source_object']),
            lambda v:v['objects'][0].update(source_object='unknown approved-looking object'),
            lambda v:v['objects'][0]['admitted_range'].update(group='craft_seat_lift'),
            lambda v:v['objects'][0]['admitted_range'].update(triangle_start=62016),
            lambda v:v['objects'][0]['admitted_range'].update(exact_coordinate_match=1),
            lambda v:v['objects'][0].update(admitted_range=None),
            lambda v:v['objects'][0]['evaluated_source_triangle_indices'].__setitem__(0,True),
            lambda v:v['objects'][0]['evaluated_source_triangle_indices'].__setitem__(0,188),
            lambda v:v['objects'][0]['evaluated_source_triangle_indices'].__setitem__(0,1),
            lambda v:v['objects'][0]['source_corrected_world_rows'][0].__setitem__(0,float('nan')),
            lambda v:v['objects'][0]['source_corrected_world_rows'][3].__setitem__(0,.1),
            lambda v:v['objects'][0]['source_corrected_world_rows'][0].__setitem__(0,0),
            lambda v:v['objects'][0]['halo_bounds_rest_m'][0].__setitem__(0,-10),
            lambda v:v['objects'][0]['evaluated_source_triangle_indices_in_region'][0].__setitem__(1,0),
            lambda v:v['objects'][0]['evaluated_source_triangle_indices_in_region'][0].__setitem__(0,188),
            lambda v:v['query_bounds_rest_m'][1].__setitem__(2,-.4),
            lambda v:v['derivative_corrections_baked_once'].update(schema_version=True)]
        for i,mutate in enumerate(changes):
            with self.subTest(index=i):
                value=copy.deepcopy(self.halo);mutate(value)
                self.refused(lambda:spec.validate_halo(value,self.contact,self.support))
        for key in ('objects','triangles','vertices_micrometres'):
            value=copy.deepcopy(self.halo);value[key].pop()
            self.refused(lambda:spec.validate_halo(value,self.contact,self.support))

    def test_rehashed_runtime_source_policy_tools_and_licenses_cannot_self_substitute(self):
        changes=[(assets.RUNTIME,lambda v:v['vertices_micrometres'][0].__setitem__(0,v['vertices_micrometres'][0][0]+1)),
                 (assets.RUNTIME,lambda v:v['objects'][0]['source_corrected_world_rows'][0].__setitem__(3,.01)),
                 (assets.POLICY_FILE,lambda v:v['identities'].update(halo_sha256='0'*64)),
                 (assets.PROVENANCE,lambda v:v['tool_sha256'].update(lower_cockpit_contact_spec_py='0'*64)),
                 ('sources/wayfarer-floor-evidence-01/provenance.json',lambda v:v['tools_sha256'].update({'tools/check_boarding_floor_evidence.py':'0'*64})),
                 ('sources/wayfarer-floor-evidence-01/checks.json',lambda v:v.update(passed=False))]
        for i,(name,mutate) in enumerate(changes):
            package=self.package('rehash-'+str(i));value=json.loads((package/name).read_bytes());mutate(value)
            self.rewrite(package,name,spec.encode(value))
            output=self.root/('rejected-'+str(i));self.refused(lambda:assets.prepare(package,output,ROOT));self.assertFalse(output.exists())
        for i,name in enumerate(SOURCE_SHA256):
            package=self.package('source-substitution-'+str(i));path='sources/'+name
            self.rewrite(package,path,(package/path).read_bytes()+b'\n')
            self.refused(lambda:assets.verify(package,ROOT))
        self.assertFalse(list(self.root.glob('.lower-contact-prepare-*')))

    def test_dependencies_are_independently_bound(self):
        repository=self.root/'repository'
        for name in ENGINE_BINDINGS:
            target=repository/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,target)
        assets.verify(self.source,repository)
        for name in ENGINE_BINDINGS:
            with self.subTest(name=name):
                path=repository/name;before=path.read_bytes();path.write_bytes(before+b'\n')
                self.refused(lambda:assets.verify(self.source,repository));path.write_bytes(before)

    def test_closed_roster_manifest_paths_sizes_and_nonregular_inputs(self):
        mutations=[lambda v:v.update(extra=0),lambda v:v['files'][0].update(path='../escape'),
                   lambda v:v['files'][0].update(path='/absolute'),lambda v:v['files'][0].update(bytes=True),
                   lambda v:v['files'][0].update(bytes=spec.MAX_SOURCE+1),
                   lambda v:v['files'][0].update(sha256='x'*64),
                   lambda v:v['files'][1].update(path=v['files'][0]['path']),lambda v:v['files'].pop()]
        for i,mutate in enumerate(mutations):
            package=self.package('manifest-'+str(i));value=json.loads((package/'package.json').read_bytes());mutate(value)
            (package/'package.json').write_bytes(spec.encode(value));self.refused(lambda:assets.verify(package,ROOT))
        for i,action in enumerate(('file','directory','symlink','fifo')):
            package=self.package('roster-'+str(i));path=package/'unexpected'
            if action=='file':path.write_bytes(b'unrostered')
            elif action=='directory':path.mkdir()
            elif action=='symlink':path.symlink_to(self.source/assets.RUNTIME)
            else:os.mkfifo(path)
            self.refused(lambda:assets.verify(package,ROOT))
        package=self.package('runtime-fifo');(package/assets.RUNTIME).unlink();os.mkfifo(package/assets.RUNTIME)
        self.refused(lambda:assets.verify(package,ROOT))
        package=self.package('runtime-link');(package/assets.RUNTIME).unlink();(package/assets.RUNTIME).symlink_to(self.source/assets.RUNTIME)
        self.refused(lambda:assets.verify(package,ROOT))
        link=self.root/'parent-link';link.symlink_to(self.source,target_is_directory=True)
        self.refused(lambda:assets.verify(link,ROOT))

    def test_atomic_no_replace_race_preserves_competing_destination(self):
        output=self.root/'raced-output';original=assets.install_new
        def competitor(staging,target):
            target.mkdir();(target/'competitor').write_bytes(b'preserve me')
            original(staging,target)
        with patch.object(assets,'install_new',side_effect=competitor):
            self.refused(lambda:assets.prepare(self.source,output,ROOT))
        self.assertEqual((output/'competitor').read_bytes(),b'preserve me')
        self.assertEqual(list(output.iterdir()),[output/'competitor'])
        self.assertFalse(list(self.root.glob('.lower-contact-prepare-*')))

    def test_regular_read_detects_concurrent_inplace_write(self):
        path=self.root/'raced-read';path.write_bytes(b'original')
        original=os.fstat;calls=0
        def racing_fstat(fd):
            nonlocal calls
            result=original(fd);calls+=1
            if calls==1:path.write_bytes(b'modified')
            return result
        with patch.object(assets.os,'fstat',side_effect=racing_fstat):
            self.refused(lambda:assets.read_regular(path,128))

    def test_atomic_output_symlink_refusal(self):
        outside=self.root/'outside';outside.mkdir();(outside/'owner').write_bytes(b'unchanged')
        output=self.root/'raced-link';original=assets.install_new
        def symlink_competitor(staging,target):
            target.symlink_to(outside,target_is_directory=True);original(staging,target)
        with patch.object(assets,'install_new',side_effect=symlink_competitor):
            self.refused(lambda:assets.prepare(self.source,output,ROOT))
        self.assertEqual(list(outside.iterdir()),[outside/'owner'])
        self.assertEqual((outside/'owner').read_bytes(),b'unchanged')
        self.assertFalse(list(self.root.glob('.lower-contact-prepare-*')))

    def test_copied_tools_explicit_repository_optimized_positive_and_negative(self):
        tools=self.root/'copied-tools';tools.mkdir()
        for name in assets.TOOL_NAMES:shutil.copyfile(ROOT/'tools'/name,tools/name)
        script=tools/'prepare_lower_cockpit_contact.py'
        output=self.root/'copied-prepared'
        command=[sys.executable,'-O',str(script),'--package',str(self.source),'--output',str(output),'--repository',str(ROOT)]
        result=subprocess.run(command,capture_output=True,text=True,timeout=60,cwd=self.root)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((output/assets.RUNTIME).read_bytes(),(self.source/assets.RUNTIME).read_bytes())
        bad=self.package('optimized-negative');self.rewrite(bad,assets.POLICY_FILE,spec.encode({**POLICY,'version':True}))
        command[command.index(str(self.source))]=str(bad)
        command[command.index(str(output))]=str(self.root/'optimized-rejected')
        result=subprocess.run(command,capture_output=True,text=True,timeout=60,cwd=self.root)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((self.root/'optimized-rejected').exists())
        # Default repo relative to copied tools is not the project; explicit binding is required.
        command=command[:-2];command[command.index(str(bad))]=str(self.source)
        result=subprocess.run(command,capture_output=True,text=True,timeout=60,cwd=self.root)
        self.assertNotEqual(result.returncode,0)


if __name__=='__main__':
    unittest.main()
