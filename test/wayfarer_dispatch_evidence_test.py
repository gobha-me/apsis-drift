"""Portable invented-record tests; no asset files or numerical methods are used."""

import copy
from fractions import Fraction
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch


SOURCE = Path(__file__).resolve().parents[1] / "tools" / "wayfarer_dispatch_evidence.py"
SPEC = importlib.util.spec_from_file_location("wayfarer_dispatch_evidence", SOURCE)
evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evidence)


def encoded(value):
    """Independent oracle for the predecessor owner's exact JSON convention."""
    def fraction(item):
        if type(item) is Fraction:
            return {"n": str(item.numerator), "d": str(item.denominator)}
        raise TypeError("unsupported oracle scalar")
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      allow_nan=False, default=fraction).encode("utf-8")


def pin(raw):
    return {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


class Fixture:
    def __init__(self, count=2, *, complete=False, payload=None):
        if payload is None:
            payload = "invented-full-certificate:" + "x" * 1024
        self.dispatch = {
            "status": "CERTIFIED_NUMERIC_COMPLETE_RETAINED_SOURCE_SUPPORT"
            if complete else "UNRESOLVED_DISPATCH_WORK_LIMIT",
            "coverage_complete": complete,
            "inspection_sha256": "1" * 64,
            "batch_approval_sha256": "2" * 64,
            "records": [], "calls_started": count, "calls_expected": 1092,
            "metadata_integrity_operations": 17,
            "pruned_faces": 27_480_638, "operations": count * 2,
            "new_embedding_factory_seed_calls": 0,
            "source_or_physical_material_or_runtime_authority": False,
            "old_material_containment_or_K_or_mutual_bridge_qualification": False,
        }
        self.bindings = {}
        self.files = {}
        self.reads = []
        for index in range(count):
            source_id = "source_2000:" + str(index)
            result = {
                "status": "CERTIFIED_NUMERIC_SOURCE_SUPPORT_EXCLUSION",
                "coverage_complete": True, "operations": 2,
                "source_id": source_id, "payload": payload,
            }
            self.dispatch["records"].append({
                "bridge_id": "shoulder_port", "source_object": "invented-original",
                "source_id": source_id, "source_face": index,
                "call_started": True, "result": result,
            })
            self.bind(index, result)

    def bind(self, index, result, *, state="COMPLETED", limit=16_000_000):
        start = {
            "index": index + 7, "stage": "source_T",
            "args": {
                "call_index": index,
                "source_id": self.dispatch["records"][index]["source_id"],
                "triangle_and_patches_sha256": "3" * 64,
                "operation_limit": limit, "incident_edge_policy_sha256": "4" * 64,
            },
            "state": "ATTEMPTED_MAY_HAVE_ENTERED", "actual_authority": False,
        }
        key = pin(encoded({k: start[k] for k in ("index", "stage", "args")}))["sha256"]
        result_raw = encoded(result)
        completion = {"stage": "source_T", "state": state,
                      "result": pin(result_raw), "actual_authority": False}
        start_raw, completion_raw = encoded(start), encoded(completion)
        self.bindings[index] = {
            "method_key": key, "start": start_raw, "completion": completion_raw,
            "result_pin": pin(result_raw),
        }
        self.files[key] = {"method-start.json": start_raw,
                           "completion.json": completion_raw, "result.bin": result_raw}

    def read(self, key, name, cap):
        # A production owner must separately enforce approved-root/FD identities.
        # This fake callback uses only immutable invented, same-fixture bytes.
        self.reads.append((key, name, cap))
        if key not in self.files or name not in self.files[key]:
            raise KeyError("outside invented same-job roster")
        raw = self.files[key][name]
        if len(raw) > cap:
            raise evidence.Refusal("callback bounded read")
        return raw

    def compact(self):
        return evidence.compact_dispatch(self.dispatch, self.bindings)


class DispatchEvidenceTests(unittest.TestCase):
    def assert_equivalent(self, fixture, compact):
        expected = pin(encoded(fixture.dispatch))
        self.assertEqual(compact["full_dispatch_pin"], expected)
        verified = evidence.verify_dispatch(compact, fixture.read)
        self.assertEqual(verified["full_dispatch_pin"], expected)
        self.assertFalse(verified["qualification"])
        self.assertEqual(verified["numerical_methods_replayed"], 0)
        expected_reads = sum(len(raw) for key in
                             (b["method_key"] for b in fixture.bindings.values())
                             for raw in fixture.files[key].values())
        self.assertEqual(verified["recorded_bytes_read"], expected_reads)
        self.assertEqual(len(fixture.reads), 3 * len(fixture.bindings))
        for key, name, cap in fixture.reads:
            self.assertIn(name, ("method-start.json", "completion.json", "result.bin"))
            self.assertEqual(cap, len(fixture.files[key][name]))
        return verified

    def test_complete_1092_calls_without_duplicating_full_payload(self):
        fixture = Fixture(1092, complete=True)
        original = encoded(fixture.dispatch)
        bindings = copy.deepcopy(fixture.bindings)
        compact = fixture.compact()
        self.assert_equivalent(fixture, compact)
        self.assertEqual(encoded(fixture.dispatch), original)
        self.assertEqual(fixture.bindings, bindings)
        self.assertLess(len(encoded(compact)), len(original))
        self.assertNotIn(b"invented-full-certificate", encoded(compact))
        self.assertTrue(all("result" not in r and "result_ref" in r
                            for r in compact["records"]))
        self.assertEqual([r["result_ref"]["method_index"] for r in compact["records"]],
                         list(range(7, 1099)))

    def test_full_aggregate_over_one_million_nodes_is_not_per_result_overflow(self):
        fixture = Fixture(1092, complete=True, payload=list(range(1000)))
        # More than 1m value nodes in the original aggregate; each result and
        # the compact reference tree have independently bounded smaller trees.
        self.assertGreater(1092 * 1000, evidence.MAX_NODES)
        compact = fixture.compact()
        self.assert_equivalent(fixture, compact)

    def test_fraction_unicode_and_mathematical_zero_canonical_equivalence(self):
        fixture = Fixture(1, payload={"point": (Fraction(0), Fraction(-2, 7),
                                               Fraction(1, 2**149)),
                                     "label": "☃😀", "raw_zero_bits": "80000000"})
        compact = fixture.compact()
        self.assert_equivalent(fixture, compact)
        raw = encoded(fixture.dispatch)
        self.assertIn(b'"d":"7","n":"-2"', raw)
        self.assertIn(b'"d":"1","n":"0"', raw)
        self.assertIn(b'"raw_zero_bits":"80000000"', raw)

    def test_zero_calls_prefix(self):
        fixture = Fixture(0)
        self.assert_equivalent(fixture, fixture.compact())
        self.assertEqual(fixture.reads, [])

    def test_refused_result_and_unknown_work_error_preserve_full_prefix(self):
        fixture = Fixture(2)
        result = fixture.dispatch["records"][1]["result"]
        result.update(status="UNRESOLVED_OPERATION_LIMIT", coverage_complete=False,
                      operations=8)
        fixture.bind(1, result, state="REFUSED", limit=7)
        row = fixture.dispatch["records"][1]
        row.update(error_type="ValueError", error="Typed integer bound",
                   work_consumed_unknown=True)
        fixture.dispatch.update(status="FIRST_SOURCE_SUPPORT_ERROR", operations=2)
        compact = fixture.compact()
        self.assert_equivalent(fixture, compact)
        self.assertEqual(compact["operations"], 2)
        self.assertTrue(compact["records"][1]["work_consumed_unknown"])
        self.assertEqual(compact["records"][1]["result_ref"]["completion_state"], "REFUSED")
        self.assertEqual(compact["records"][1]["result_ref"]["operation_limit"], 7)

    def test_exception_row_without_result_never_gets_a_reference(self):
        fixture = Fixture(2)
        del fixture.dispatch["records"][1]["result"]
        del fixture.bindings[1]
        fixture.dispatch["records"][1].update(error_type="Refusal", error="first failure",
                                             work_consumed_unknown=True)
        fixture.dispatch.update(status="FIRST_SOURCE_SUPPORT_ERROR", operations=2)
        compact = fixture.compact()
        self.assert_equivalent(fixture, compact)
        self.assertEqual(compact["records"][1], fixture.dispatch["records"][1])

    def test_missing_foreign_typed_and_duplicate_binding_selector(self):
        for kind in ("missing", "foreign", "bool", "extra_field", "duplicate_key"):
            with self.subTest(kind=kind):
                fixture = Fixture()
                if kind == "missing":
                    del fixture.bindings[1]
                elif kind == "foreign":
                    fixture.bindings[2] = copy.deepcopy(fixture.bindings[0])
                elif kind == "bool":
                    fixture.bindings[False] = fixture.bindings.pop(0)
                elif kind == "extra_field":
                    fixture.bindings[0]["alias"] = 0
                else:
                    fixture.bindings[1] = copy.deepcopy(fixture.bindings[0])
                with self.assertRaises(evidence.Refusal):
                    fixture.compact()

    def test_reordered_repeated_reference_and_rehashed_start_index_refuse(self):
        for kind in ("reorder", "repeat", "index"):
            with self.subTest(kind=kind):
                fixture = Fixture()
                if kind == "index":
                    binding = fixture.bindings[0]
                    start = json.loads(binding["start"])
                    start["index"] = 8
                    binding["start"] = encoded(start)
                    binding["method_key"] = pin(encoded({k: start[k] for k in
                                                         ("index", "stage", "args")}))["sha256"]
                    with self.assertRaises(evidence.Refusal):
                        fixture.compact()
                else:
                    compact = fixture.compact()
                    if kind == "reorder":
                        compact["records"].reverse()
                    else:
                        compact["records"][1]["result_ref"] = copy.deepcopy(
                            compact["records"][0]["result_ref"])
                    with self.assertRaises(evidence.Refusal):
                        evidence.verify_dispatch(compact, fixture.read)

    def test_rehashed_method_source_scope_pin_and_authority_drift(self):
        cases = ("source", "call_bool", "limit_bool", "policy", "unknown_arg",
                 "authority", "stage", "completion_stage", "completion_state")
        for kind in cases:
            with self.subTest(kind=kind):
                fixture = Fixture(1)
                binding = fixture.bindings[0]
                start, end = json.loads(binding["start"]), json.loads(binding["completion"])
                if kind == "source":
                    start["args"]["source_id"] = "foreign:0"
                elif kind == "call_bool":
                    start["args"]["call_index"] = False
                elif kind == "limit_bool":
                    start["args"]["operation_limit"] = True
                elif kind == "policy":
                    start["args"]["incident_edge_policy_sha256"] = "g" * 64
                elif kind == "unknown_arg":
                    start["args"]["alias"] = 0
                elif kind == "authority":
                    start["actual_authority"] = 0
                elif kind == "stage":
                    start["stage"] = "import:shoulder_port"
                elif kind == "completion_stage":
                    end["stage"] = "dispatch"
                else:
                    end["state"] = "UNFINISHED"
                binding["start"], binding["completion"] = encoded(start), encoded(end)
                binding["method_key"] = pin(encoded({k: start[k] for k in
                                                     ("index", "stage", "args")}))["sha256"]
                with self.assertRaises(evidence.Refusal):
                    fixture.compact()

    def test_mismatched_durable_result_or_completion_pin(self):
        for kind in ("result", "end"):
            with self.subTest(kind=kind):
                fixture = Fixture(1)
                if kind == "result":
                    fixture.bindings[0]["result_pin"]["sha256"] = "f" * 64
                else:
                    end = json.loads(fixture.bindings[0]["completion"])
                    end["result"]["bytes"] += 1
                    fixture.bindings[0]["completion"] = encoded(end)
                with self.assertRaises(evidence.Refusal):
                    fixture.compact()

    def test_reference_bool_aliases_fail_before_callback(self):
        for field in ("method_index", "operation_limit"):
            with self.subTest(field=field):
                fixture = Fixture(1)
                if field == "operation_limit":
                    fixture.bind(0, fixture.dispatch["records"][0]["result"], limit=0)
                compact = fixture.compact()
                compact["records"][0]["result_ref"][field] = False
                with self.assertRaises(evidence.Refusal):
                    evidence.verify_dispatch(compact, lambda *a: self.fail("reader called"))

    def test_tiny_result_completion_pin_bool_alias(self):
        fixture = Fixture(1)
        fixture.dispatch["records"][0]["result"] = 0
        fixture.bind(0, 0)
        end = json.loads(fixture.bindings[0]["completion"])
        self.assertEqual(end["result"]["bytes"], 1)
        end["result"]["bytes"] = True
        fixture.bindings[0]["completion"] = encoded(end)
        with self.assertRaises(evidence.Refusal):
            fixture.compact()

    def test_closed_dispatch_schema_typed_counts_and_authority(self):
        for field, value in (("calls_started", True), ("calls_expected", 1092.0),
                             ("operations", False), ("coverage_complete", 0),
                             ("source_or_physical_material_or_runtime_authority", 0),
                             ("pruned_faces", 40_000_001), ("status", "CERTIFIED"),
                             ("new_embedding_factory_seed_calls", 1), ("extra", 0)):
            with self.subTest(field=field):
                fixture = Fixture(1)
                fixture.dispatch[field] = value
                with self.assertRaises(evidence.Refusal):
                    fixture.compact()
        fixture = Fixture(1)
        fixture.dispatch["coverage_complete"] = True
        fixture.dispatch["status"] = "CERTIFIED_NUMERIC_COMPLETE_RETAINED_SOURCE_SUPPORT"
        with self.assertRaises(evidence.Refusal):
            fixture.compact()

    def test_nonfinite_unsupported_and_nonstr_key_values_refuse(self):
        for value in (float("nan"), float("inf"), float("-inf"), {1: "alias"},
                      {"x": object()}, 1 << 4097):
            with self.subTest(value_type=type(value).__name__):
                with self.assertRaises(evidence.Refusal):
                    evidence.canonical_pin(value)

    def test_independent_canonical_byte_boundary_and_caps(self):
        value = {"fraction": Fraction(3, 11), "label": "☃", "array": [None, True, -1]}
        raw = encoded(value)
        self.assertEqual(evidence.canonical_pin(value, len(raw)), pin(raw))
        with self.assertRaises(evidence.Refusal):
            evidence.canonical_pin(value, len(raw) - 1)
        for cap in (True, 0, -1, evidence.MAX_BYTES + 1):
            with self.subTest(cap=cap), self.assertRaises(evidence.Refusal):
                evidence.canonical_pin(value, cap)

    def test_per_record_node_depth_and_encoded_token_boundaries(self):
        with patch.object(evidence, "MAX_NODES", 4):
            self.assertEqual(evidence.canonical_pin([0, 1, 2]), pin(encoded([0, 1, 2])))
            with self.assertRaises(evidence.Refusal):
                evidence.canonical_pin([0, 1, 2, 3])
        with patch.object(evidence, "MAX_DEPTH", 4):
            self.assertEqual(evidence.canonical_pin([[[[0]]]]), pin(encoded([[[[0]]]])))
            with self.assertRaises(evidence.Refusal):
                evidence.canonical_pin([[[[[0]]]]])
        with patch.object(evidence, "MAX_TOKEN", 16):
            self.assertEqual(evidence.canonical_pin("x" * 14), pin(encoded("x" * 14)))
            with self.assertRaises(evidence.Refusal):
                evidence.canonical_pin("x" * 15)
            with self.assertRaises(evidence.Refusal):
                evidence.canonical_pin("☃" * 3)

    def test_noncanonical_duplicate_nonfinite_and_deep_method_bytes(self):
        for raw in (b'{"index":7,"index":7}', b'{"value":NaN}',
                    b'[' * 33 + b'0' + b']' * 33, b'{"x":', b'{"x": 0}'):
            with self.subTest(raw=raw[:40]):
                fixture = Fixture(1)
                fixture.bindings[0]["start"] = raw
                with self.assertRaises(evidence.Refusal):
                    fixture.compact()

    def test_referenced_bytes_corruption_truncation_wrong_type_and_rehash(self):
        for kind in ("corrupt", "truncated", "bytearray", "rehash"):
            with self.subTest(kind=kind):
                fixture = Fixture(1)
                compact = fixture.compact()
                key = fixture.bindings[0]["method_key"]
                if kind == "corrupt":
                    fixture.files[key]["result.bin"] = fixture.files[key]["result.bin"].replace(
                        b"invented-full", b"tampered-full")
                elif kind == "truncated":
                    fixture.files[key]["result.bin"] = fixture.files[key]["result.bin"][:-1]
                elif kind == "bytearray":
                    fixture.files[key]["result.bin"] = bytearray(fixture.files[key]["result.bin"])
                else:
                    changed = json.loads(fixture.files[key]["result.bin"])
                    changed["operations"] = 3
                    fixture.files[key]["result.bin"] = encoded(changed)
                    ref = compact["records"][0]["result_ref"]
                    ref["result_pin"] = pin(fixture.files[key]["result.bin"])
                    end = json.loads(fixture.files[key]["completion.json"])
                    end["result"] = ref["result_pin"]
                    fixture.files[key]["completion.json"] = encoded(end)
                    ref["completion_pin"] = pin(fixture.files[key]["completion.json"])
                with self.assertRaises(evidence.Refusal):
                    evidence.verify_dispatch(compact, fixture.read)

    def test_foreign_method_callback_and_reader_exception_are_not_hidden(self):
        fixture = Fixture(1)
        compact = fixture.compact()
        with self.assertRaisesRegex(OSError, "unavailable"):
            evidence.verify_dispatch(compact, lambda *a: (_ for _ in ()).throw(
                OSError("unavailable recorded file")))
        compact["records"][0]["result_ref"]["method_key"] = "f" * 64
        with self.assertRaises(KeyError):
            evidence.verify_dispatch(compact, fixture.read)

    def test_reference_pin_unknown_fields_full_digest_and_original_mutation(self):
        for kind in ("pin_bool", "unknown", "digest", "inline", "missing_ref"):
            with self.subTest(kind=kind):
                fixture = Fixture(1)
                compact = fixture.compact()
                ref = compact["records"][0]["result_ref"]
                if kind == "pin_bool":
                    ref["result_pin"]["bytes"] = True
                elif kind == "unknown":
                    ref["extra"] = 0
                elif kind == "digest":
                    compact["full_dispatch_pin"]["sha256"] = "f" * 64
                elif kind == "inline":
                    compact["records"][0]["result"] = fixture.dispatch["records"][0]["result"]
                else:
                    del compact["records"][0]["result_ref"]
                with self.assertRaises(evidence.Refusal):
                    evidence.verify_dispatch(compact, fixture.read)
        fixture = Fixture(1)
        compact = fixture.compact()
        old_pin = dict(compact["full_dispatch_pin"])
        fixture.dispatch["records"][0]["result"]["payload"] = "mutated original"
        self.assertEqual(compact["full_dispatch_pin"], old_pin)
        self.assert_equivalent(Fixture(1), compact)

    def test_callback_extra_bytes_and_aggregate_reads_stay_bounded(self):
        fixture = Fixture(1)
        compact = fixture.compact()
        def excessive(key, name, cap):
            return fixture.files[key][name] + b" "
        with self.assertRaises(evidence.Refusal):
            evidence.verify_dispatch(compact, excessive)
        fixture = Fixture(2, payload="x" * 3000)
        compact = fixture.compact()
        full_bytes = compact["full_dispatch_pin"]["bytes"]
        self.assertLess(len(encoded(compact)), full_bytes)
        self.assertGreater(sum(len(v) for m in fixture.files.values() for v in m.values()),
                           full_bytes)
        # Scale all byte ceilings and the canonical function's bound default
        # coherently, so the failure must occur on accumulated callback reads,
        # rather than an unrelated default cap exceeding the patched maximum.
        with patch.object(evidence, "MAX_BYTES", full_bytes), \
                patch.object(evidence, "MAX_MARKER", full_bytes), \
                patch.object(evidence, "MAX_RESULT", full_bytes), \
                patch.object(evidence.canonical_pin, "__defaults__", (full_bytes,)):
            with self.assertRaisesRegex(evidence.Refusal, "Recorded input aggregate bound"):
                evidence.verify_dispatch(compact, fixture.read)
        self.assertGreater(len(fixture.reads), 0)

    def test_aggregate_reservation_refuses_before_next_callback(self):
        fixture = Fixture(2, payload="x" * 3000)
        compact = fixture.compact()
        limit = compact["full_dispatch_pin"]["bytes"]
        reserved = 0
        allowed = []
        blocked = None
        for row in compact["records"]:
            ref = row["result_ref"]
            for name, field in (("method-start.json", "start_pin"),
                                ("completion.json", "completion_pin"),
                                ("result.bin", "result_pin")):
                item = (ref["method_key"], name, ref[field]["bytes"])
                if reserved + item[2] > limit:
                    blocked = item
                    break
                reserved += item[2]
                allowed.append(item)
            if blocked is not None:
                break
        self.assertIsNotNone(blocked)
        with patch.object(evidence, "MAX_BYTES", limit), \
                patch.object(evidence, "MAX_MARKER", limit), \
                patch.object(evidence, "MAX_RESULT", limit), \
                patch.object(evidence.canonical_pin, "__defaults__", (limit,)):
            with self.assertRaisesRegex(evidence.Refusal, "Recorded input aggregate bound"):
                evidence.verify_dispatch(compact, fixture.read)
        self.assertEqual(fixture.reads, allowed)
        self.assertNotIn(blocked, fixture.reads)

    def test_oversized_callback_return_refuses_before_hashing(self):
        fixture = Fixture(1)
        compact = fixture.compact()
        key = compact["records"][0]["result_ref"]["method_key"]
        excess = fixture.files[key]["method-start.json"] + b" "
        original_sha = hashlib.sha256
        hashed = []
        callbacks = []

        def observe_sha(raw=b"", *args, **kwargs):
            hashed.append(raw)
            return original_sha(raw, *args, **kwargs)

        def excessive(method, name, cap):
            callbacks.append((method, name, cap))
            return excess

        with patch.object(evidence.hashlib, "sha256", side_effect=observe_sha):
            with self.assertRaisesRegex(evidence.Refusal, "Bounded record bytes"):
                evidence.verify_dispatch(compact, excessive)
        self.assertEqual(callbacks, [(key, "method-start.json", len(excess) - 1)])
        self.assertNotIn(excess, hashed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
