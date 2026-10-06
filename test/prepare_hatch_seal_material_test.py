#!/usr/bin/env python3
"""Pinned seal package codec controls; no material inclusion or source load."""
import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/boarding"))
import prepare_hatch_seal_material as prep

PACKAGE = ROOT / "assets/native/wayfarer-hatch-seal-material-01"


class SealPreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = json.loads(prep.base.pinned(
            PACKAGE / "completion.json", prep.MAX_METADATA, prep.METADATA_SHA))
        cls.blob = prep.base.pinned(PACKAGE / "geometry.bin", prep.BINARY_BYTES, prep.BINARY_SHA)

    def reject_document(self, change):
        document = copy.deepcopy(self.document)
        change(document)
        with self.assertRaises((ValueError, KeyError, TypeError)):
            prep.emit(document, self.blob, io.StringIO())

    def test_pinned_package_single_owned_mesh_and_no_authority(self):
        output = io.StringIO()
        receipt = prep.emit(self.document, self.blob, output)
        self.assertEqual((receipt["vertices"], receipt["triangles"], receipt["binary_bytes"]),
                         (90, 160, 6240))
        for field in ("source_permission", "material_permission", "actor_permission"):
            self.assertFalse(receipt[field])
        emitted = output.getvalue()
        for declaration in ("std::array<RigidVector3,90>",
                            "std::array<MaterialQuantizedPoint,90>",
                            "std::array<MaterialTriangle,160>",
                            "static const MaterialMeshRecord mesh{1441,raw,q,triangles}"):
            self.assertEqual(emitted.count(declaration), 1)
        self.assertIn("655,0,-1,-1,189501,140", emitted)
        self.assertNotIn("MaterialMeshRecord,19", emitted)
        self.assertLess(receipt["emitted_bytes"], prep.MAX_EMISSION)

    def test_pins_and_capacity_precede_decode(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "packet"
            for content, cap, pin in ((self.blob, prep.BINARY_BYTES, prep.BINARY_SHA),
                                     ((PACKAGE / "completion.json").read_bytes(),
                                      prep.MAX_METADATA, prep.METADATA_SHA)):
                path.write_bytes(content)
                self.assertEqual(prep.base.pinned(path, cap, pin), content)
                with self.assertRaises(ValueError):
                    prep.base.pinned(path, len(content) - 1, pin)
                with self.assertRaises(ValueError):
                    prep.base.pinned(path, cap, "0" * 64)
                changed = bytearray(content)
                changed[0] ^= 1
                path.write_bytes(changed)
                with self.assertRaises(ValueError):
                    prep.base.pinned(path, cap, pin)

    def test_provenance_work_and_source_namespaces(self):
        for field, value in (("schema", "wrong"), ("source_sha256", "0" * 64),
                             ("inventory_sha256", "0" * 64),
                             ("base_completion_sha256", "0" * 64),
                             ("history_lifeboat_sha256", "0" * 64),
                             ("history_finish_sha256", "0" * 64),
                             ("blender_version", "other"), ("source_save_count", 1),
                             ("master_unchanged", False), ("material_permission", True),
                             ("actor_permission", True)):
            with self.subTest(field=field):
                self.reject_document(lambda d, f=field, v=value: d.__setitem__(f, v))
        self.reject_document(lambda d: d["work"].__setitem__("new_crop_face_replays", 140))
        self.reject_document(lambda d: d["object"].__setitem__("source_index", 1440))
        self.reject_document(lambda d: d["object"]["identity"].__setitem__("source_type", "MESH"))
        self.reject_document(lambda d: d["object"]["identity"]["retained_original_contact_range"].__setitem__("triangle_start", 0))
        self.reject_document(lambda d: d["object"]["geometry"].__setitem__("evaluated_triangle_ordinal_range", [189501, 189661]))

    def test_curve_points_radius_settings_and_packets(self):
        for field, value in (("dimensions", "2D"), ("bevel_mode", "PROFILE"),
                             ("fill_mode", "HALF"), ("bevel_object_absent", False),
                             ("taper_object_absent", False), ("bevel_depth", 0),
                             ("bevel_depth", float("nan"))):
            with self.subTest(field=field, value=value):
                self.reject_document(lambda d, f=field, v=value:
                    d["object"]["curve_settings"].__setitem__(f, v))
        self.reject_document(lambda d: d["object"]["modifiers"].append({"type": "BEVEL"}))
        self.reject_document(lambda d: d["object"]["spline"].__setitem__("type", "BEZIER"))
        self.reject_document(lambda d: d["object"]["spline"].__setitem__("use_cyclic_u", True))
        self.reject_document(lambda d: d["object"]["spline"]["points"].pop())
        for field, value in (("radius_factor", .5), ("tilt", .1)):
            self.reject_document(lambda d, f=field, v=value:
                d["object"]["spline"]["points"][0].__setitem__(f, v))
        self.reject_document(lambda d: d["object"]["spline"]["points"][0]["co_homogeneous"].__setitem__(3, 2))
        self.reject_document(lambda d: d["object"]["spline"]["points"][8]["co_homogeneous"].__setitem__(0, .123))
        self.reject_document(lambda d: d["object"]["spline"]["points"].__setitem__(1,
            copy.deepcopy(d["object"]["spline"]["points"][0])))
        self.reject_document(lambda d: d["object"]["properties"].__setitem__("overflow", "x" * 4096))
        self.reject_document(lambda d: d["object"]["corrected_world_columns"][0].__setitem__(0, 2))

    def test_full_bounds_consistency_independent_of_identity_equality(self):
        for field in ("full_bounds_corrected_world_metres", "full_bounds_quantized_micrometres"):
            def shrink(document):
                row = document["object"]
                value = copy.deepcopy(row["geometry"][field])
                value[1][0] = value[0][0]
                row["geometry"][field] = value
                row["identity"][field] = copy.deepcopy(value)
            with self.subTest(field=field):
                self.reject_document(shrink)

    def test_slice_descriptors_and_payload_corruption(self):
        for key in ("vertices_binary64", "vertices_quantized_micrometres", "triangle_indices"):
            for field, value in (("offset", -1), ("rows", 1), ("columns", 4),
                                 ("sha256", "0" * 64), ("dtype", "<u8")):
                with self.subTest(key=key, field=field):
                    self.reject_document(lambda d, k=key, f=field, v=value:
                        d["object"]["geometry"][k].__setitem__(f, v))
            offset = self.document["object"]["geometry"][key]["offset"]
            blob = bytearray(self.blob)
            blob[offset] ^= 1
            with self.assertRaises(ValueError):
                prep.emit(self.document, blob, io.StringIO())
        with self.assertRaises(ValueError):
            prep.emit(self.document, self.blob[:-1], io.StringIO())
        with self.assertRaises(ValueError):
            prep.emit(self.document, self.blob + b"x", io.StringIO())

    def test_independent_numeric_mapping_and_index_guards(self):
        # Repaired slice digests intentionally bypass outer pins solely to test
        # codec guards; these altered packets never receive material authority.
        for key, value in (("vertices_binary64", struct.pack("<d", float("nan"))),
                           ("vertices_quantized_micrometres", struct.pack("<q", 8000001)),
                           ("vertices_quantized_micrometres", struct.pack("<q", 0)),
                           ("triangle_indices", struct.pack("<i", 90)),
                           ("triangle_indices", struct.pack("<i", -1))):
            document = copy.deepcopy(self.document)
            descriptor = document["object"]["geometry"][key]
            blob = bytearray(self.blob)
            start = descriptor["offset"]
            blob[start:start + len(value)] = value
            descriptor["sha256"] = hashlib.sha256(blob[start:start + descriptor["bytes"]]).hexdigest()
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                prep.emit(document, blob, io.StringIO())

    def test_nonfinite_workspace_short_write_and_capacity(self):
        for value in (float("nan"), float("inf"), 8.00001, True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                prep.scalar(value)
        class Short(io.StringIO):
            def write(self, value):
                return len(value) - 1
        with self.assertRaises(ValueError):
            prep.Emitter(Short()).write("packet")
        emitter = prep.Emitter(io.StringIO())
        emitter.bytes = prep.MAX_EMISSION
        with self.assertRaises(ValueError):
            emitter.write("x")


if __name__ == "__main__":
    unittest.main()
