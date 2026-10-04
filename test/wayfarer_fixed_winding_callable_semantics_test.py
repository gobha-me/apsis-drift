"""Invented exact callable mutations only; no actual inputs or jobs."""
from pathlib import Path
from fractions import Fraction as F
import hashlib
import importlib.util
import sys
import unittest
from unittest.mock import patch

DIR=Path(__file__).resolve().parent

def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);sys.modules[name]=module;spec.loader.exec_module(module)
    return module

b=load(DIR.parent/'tools/wayfarer_fixed_winding.py','default_guard_broadphase')
w=load(DIR/'fixtures/wayfarer_fixed_winding/stowed-winding-containment-proof-preparation/winding_containment_certificate.py','default_guard_original')

class CallableSemantics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        v=((F(0),)*3,(F(2),F(0),F(0)),(F(0),F(2),F(0)),(F(0),F(0),F(2)))
        faces=((0,2,1),(0,1,3),(0,3,2),(1,2,3))
        data=w.canonical(w.numeric_doc(v,faces,'selected_static_bridge'))
        cls.handle=w.certify_closed_oriented_chain(data,hashlib.sha256(data).hexdigest())
    def refused(self,function,attribute,value):
        old=getattr(function,attribute)
        try:
            setattr(function,attribute,value)
            with patch.object(b,'face_box',side_effect=AssertionError('no filtering before identity refusal')):
                with self.assertRaisesRegex(ValueError,'defaults|closure'):
                    b.classify_point(w,self.handle,(3,3,3),operation_limit=0)
        finally:setattr(function,attribute,old)
    def test_exact_tick_zero_budget_bypass_refused(self):
        self.refused(w.e.Work.tick,'__defaults__',(0,))
    def test_tick_bool_float_none_extra_defaults_refuse(self):
        for value in ((True,),(1.0,),None,(1,1)):
            self.refused(w.e.Work.tick,'__defaults__',value)
    def test_init_number_and_work_keyword_drift_refuse(self):
        for value in ((False,),(16_000_000.0,),(0,),None):
            self.refused(w.e.Work.__init__,'__defaults__',value)
        self.refused(w.e.Work.number,'__defaults__',(1,))
        for function in (w.e.Work.__init__,w.e.Work.tick,w.e.Work.number):
            for value in ({},{'count':0}):self.refused(function,'__kwdefaults__',value)
    def test_all_authenticated_predicate_defaults_refuse(self):
        for module,names in ((w,('require_material','numeric_doc','canonical','digest','face_values','coplanar_ray_interval','_point_classification')),
                             (w.e,('coordinate','vector','normal','sub','total','dot','cross'))):
            for name in names:
                function=getattr(module,name)
                self.refused(function,'__defaults__',(0,))
                self.refused(function,'__kwdefaults__',{})
    def test_legacy_classifier_keyword_default_typed_closed(self):
        for value in (None,{}, {'operation_limit':True},{'operation_limit':16_000_000.0},
                      {'operation_limit':0},{'operation_limit':16_000_000,'extra':0}):
            self.refused(w.classify_point,'__kwdefaults__',value)
        self.refused(w.classify_point,'__defaults__',(0,))
    def test_live_bound_method_defaults_rechecked(self):
        work=w.e.Work(0)
        old=w.e.Work.tick.__defaults__
        try:
            w.e.Work.tick.__defaults__=(0,)
            with self.assertRaisesRegex(ValueError,'defaults'):b.require_live_work(work,w.e.Work,0,initial=True)
        finally:w.e.Work.tick.__defaults__=old
    def test_closure_state_refuses_and_plain_function_accepts(self):
        def outer():
            captured=1
            def closure():return captured
            return closure
        with self.assertRaisesRegex(ValueError,'closure'):b.require_callable_semantics(outer())
        def plain():return 1
        b.require_callable_semantics(plain)
    def test_valid_zero_budget_stays_unresolved_and_work_positive(self):
        result=b.classify_point(w,self.handle,(3,3,3),operation_limit=0)
        self.assertEqual(result['status'],'UNRESOLVED_OPERATION_LIMIT')
        self.assertFalse(result['membership_complete']);self.assertEqual(result['operations'],1)
        result=b.classify_point(w,self.handle,(3,3,3))
        self.assertEqual(result['status'],'STRICT_EXTERIOR');self.assertGreater(result['operations'],0)

if __name__=='__main__':unittest.main()
