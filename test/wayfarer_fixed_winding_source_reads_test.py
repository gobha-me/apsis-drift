"""Invented files and classes only; never original geometry or private jobs."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import types
import unittest
from fractions import Fraction as F
import hashlib
from unittest.mock import patch

DIR=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('source_read_broadphase',DIR.parent/'tools/wayfarer_fixed_winding.py')
b=importlib.util.module_from_spec(spec);sys.modules[spec.name]=b;spec.loader.exec_module(b)

class Wrapped:
    def __init__(self,stream,action):self.stream=stream;self.action=action
    def __enter__(self):return self
    def __exit__(self,*args):return self.stream.__exit__(*args)
    def fileno(self):return self.stream.fileno()
    def read(self,size):return self.action(self.stream,size)

class SourceReads(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name)/'invented.py';self.path.write_bytes(b'abcdefgh')
    def wrapped(self,action):
        original=Path.open
        def opened(path,*args,**kwargs):
            stream=original(path,*args,**kwargs)
            return Wrapped(stream,action) if path==self.path and args and args[0]=='rb' else stream
        return patch.object(Path,'open',opened)
    def test_exact_length_and_cap_plus_one_read(self):
        requests=[]
        def action(stream,size):requests.append(size);return stream.read(size)
        with self.wrapped(action):self.assertEqual(b.bounded_source(self.path,8,exact_length=8),b'abcdefgh')
        self.assertEqual(requests,[9])
    def test_preallocation_oversize_exact_mismatch_and_type(self):
        self.path.write_bytes(b'x'*131073)
        with patch.object(Path,'open',side_effect=AssertionError('must not open')):
            with self.assertRaisesRegex(ValueError,'before read'):b.bounded_source(self.path,131072)
        self.path.write_bytes(b'abc')
        with patch.object(Path,'open',side_effect=AssertionError('must not open')):
            with self.assertRaisesRegex(ValueError,'before read'):b.bounded_source(self.path,8,exact_length=8)
        for cap in (True,-1,131073,1.0):
            with self.assertRaises(ValueError):b.bounded_source(self.path,cap)
    def test_growth_before_bounded_read(self):
        requests=[]
        def action(stream,size):
            requests.append(size)
            with self.path.open('ab') as out:out.write(b'123456789')
            return stream.read(size)
        with self.wrapped(action):
            with self.assertRaisesRegex(ValueError,'after read'):b.bounded_source(self.path,8,exact_length=8)
        self.assertEqual(requests,[9])
    def test_growth_after_read_detected_by_fd(self):
        def action(stream,size):
            data=stream.read(size)
            with self.path.open('ab') as out:out.write(b'x')
            return data
        with self.wrapped(action):
            with self.assertRaisesRegex(ValueError,'during read'):b.bounded_source(self.path,8,exact_length=8)
    def test_same_size_replacement_after_read_detected_by_name(self):
        replacement=self.path.with_name('replacement.py');replacement.write_bytes(b'abcdefgh')
        def action(stream,size):
            data=stream.read(size);os.replace(replacement,self.path);return data
        with self.wrapped(action):
            with self.assertRaisesRegex(ValueError,'during read'):b.bounded_source(self.path,8,exact_length=8)
    def test_replacement_at_open_rejected_before_read(self):
        replacement=self.path.with_name('replacement.py');replacement.write_bytes(b'abcdefgh')
        original=Path.open;reads=[]
        def opened(path,*args,**kwargs):
            if path==self.path:
                os.replace(replacement,path)
                return Wrapped(original(path,*args,**kwargs),lambda stream,size:reads.append(size))
            return original(path,*args,**kwargs)
        with patch.object(Path,'open',opened):
            with self.assertRaisesRegex(ValueError,'before read'):b.bounded_source(self.path,8,exact_length=8)
        self.assertEqual(reads,[])
    def test_symlink_and_directory_rejected_before_open(self):
        link=self.path.with_name('link.py');link.symlink_to(self.path)
        for path in (link,Path(self.temp.name)):
            with patch.object(Path,'open',side_effect=AssertionError('must not open')):
                with self.assertRaisesRegex(ValueError,'Regular'):b.bounded_source(path,8)
    def test_own_maximum_and_shortened_payload(self):
        self.path.write_bytes(b'x'*131072)
        self.assertEqual(len(b.bounded_source(self.path,131072)),131072)
        self.path.write_bytes(b'abcdefgh')
        with self.wrapped(lambda stream,size:b'abc'):
            with self.assertRaises(ValueError):b.bounded_source(self.path,8,exact_length=8)

class WorkIdentity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path=DIR/'fixtures/wayfarer_fixed_winding/stowed-winding-containment-proof-preparation/winding_containment_certificate.py'
        spec=importlib.util.spec_from_file_location('source_read_original_w',path)
        cls.w=importlib.util.module_from_spec(spec);sys.modules[spec.name]=cls.w;spec.loader.exec_module(cls.w)
        vertices=((F(0),)*3,(F(2),F(0),F(0)),(F(0),F(2),F(0)),(F(0),F(0),F(2)))
        faces=((0,2,1),(0,1,3),(0,3,2),(1,2,3))
        data=cls.w.canonical(cls.w.numeric_doc(vertices,faces,'selected_static_bridge'))
        cls.material=cls.w.certify_closed_oriented_chain(data,hashlib.sha256(data).hexdigest())
    def test_direct_private_overdomain_handles_refuse_before_table(self):
        cases=((self.material.vertices+(self.material.vertices[0],)*253,self.material.faces),
               (self.material.vertices,self.material.faces+(self.material.faces[0],)*509),
               (list(self.material.vertices),self.material.faces))
        for vertices,faces in cases:
            proof={'status':'CERTIFIED_CLOSED_ORIENTED_CHAIN','coverage_complete':True,
                   'vertices':len(vertices),'faces':len(faces)}
            handle=self.w._make_handle(vertices,faces,'selected_static_bridge','0'*64,proof,'closed_oriented_chain')
            self.w.require_material(handle)
            with patch.object(b,'face_box',side_effect=AssertionError('must not allocate table')):
                with self.assertRaisesRegex(self.w.e.InvalidGeometry,'tuple mesh dimensions'):
                    b.classify_point(self.w,handle,(0,0,0))
    def test_plain_class_and_live_methods_accept(self):
        b.require_winding(self.w)
        cls=self.w.e.Work;work=cls(0);b.require_live_work(work,cls,0,initial=True)
    def test_inherited_unmetered_new_and_metaclass_refused_before_construction(self):
        original=self.w.e.Work;calls=[]
        class Unmetered(original):
            def tick(self,count=1):pass
            def number(self,x):return x
        class Forged(original):
            def __new__(cls,*args):calls.append('constructed');return Unmetered(0)
        class Meta(type):
            def __call__(cls,*args):calls.append('meta');return Unmetered(0)
        fake=Meta('Work',(object,),{'__init__':original.__init__,'tick':original.tick,'number':original.number,'__module__':self.w.e.__name__})
        try:
            for replacement in (Forged,fake):
                self.w.e.Work=replacement
                with self.assertRaisesRegex(ValueError,'plain work'):
                    b.classify_point(self.w,self.material,(3,3,3),operation_limit=0)
            self.assertEqual(calls,[])
        finally:self.w.e.Work=original
        # Isolate CPython type-slot mutation: deleting __new__ need not restore
        # the original internal tp_new calling convention on that same type.
        path=DIR/'fixtures/wayfarer_fixed_winding/stowed-winding-containment-proof-preparation/winding_containment_certificate.py'
        spec=importlib.util.spec_from_file_location('isolated_new_hook_w',path)
        isolated=importlib.util.module_from_spec(spec);sys.modules[spec.name]=isolated;spec.loader.exec_module(isolated)
        data=isolated.canonical(isolated.numeric_doc(self.material.vertices,self.material.faces,'selected_static_bridge'))
        handle=isolated.certify_closed_oriented_chain(data,hashlib.sha256(data).hexdigest())
        isolated.e.Work.__new__=staticmethod(lambda cls,*args:calls.append('new-hook'))
        with self.assertRaisesRegex(ValueError,'plain work'):
            b.classify_point(isolated,handle,(3,3,3),operation_limit=0)
        self.assertEqual(calls,[])
    def test_class_attribute_hook_and_live_instance_fields_refused(self):
        original=self.w.e.Work
        try:
            original.__getattr__=lambda self,key:0
            with self.assertRaisesRegex(ValueError,'plain work'):b.require_winding(self.w)
        finally:del original.__getattr__
        work=original(1)
        for name,value in (('operations',False),('operations',1),('limit',True)):
            old=getattr(work,name);setattr(work,name,value)
            with self.assertRaises(ValueError):b.require_live_work(work,original,1,initial=True)
            setattr(work,name,old)
        work.tick=lambda count=1:None
        with self.assertRaises(ValueError):b.require_live_work(work,original,1,initial=True)

if __name__=='__main__':unittest.main()
