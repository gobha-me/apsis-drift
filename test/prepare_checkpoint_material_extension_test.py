#!/usr/bin/env python3
"""Pure package codec controls, without authoring loads or C++ admission."""
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
import prepare_checkpoint_material_extension as prep

PACKAGE = ROOT / "assets/native/wayfarer-checkpoint-material-extension-01"


class CheckpointPreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = json.loads(prep.base.pinned(PACKAGE / "completion.json",
            prep.MAX_METADATA, prep.EXTENSION_METADATA_SHA))
        cls.blob = prep.base.pinned(PACKAGE / "geometry.bin",
            prep.MAX_BINARY, prep.EXTENSION_BINARY_SHA)

    def reject_document(self, modify):
        document = copy.deepcopy(self.document)
        modify(document)
        with self.assertRaises((ValueError, KeyError, TypeError)):
            prep.emit(document, self.blob, io.StringIO())

    def test_pinned_package_emit_has_no_duplicate_old_meshes(self):
        output = io.StringIO()
        receipt = prep.emit(self.document, self.blob, output)
        source = output.getvalue()
        self.assertEqual((receipt["vertices"], receipt["triangles"], receipt["binary_bytes"]),
                         (3408, 5699, 231972))
        self.assertFalse(receipt["source_permission"])
        self.assertFalse(receipt["material_permission"])
        self.assertFalse(receipt["actor_permission"])
        self.assertEqual(source.count("static const std::array<MaterialMeshRecord,2>"), 1)
        self.assertEqual(source.count("static const std::array<RigidVector3,"), 2)
        self.assertNotIn("raw2", source)
        self.assertNotIn("MaterialMeshRecord,19", source)
        self.assertIn("650,0,-1,-1,188765,296", source)
        self.assertIn("733,0,-1,-1,341614,2898", source)
        self.assertLess(receipt["emitted_bytes"], prep.MAX_EMISSION)

    def test_pins_and_capacity_before_decode(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input"
            path.write_bytes(self.blob)
            self.assertEqual(prep.base.pinned(path, prep.MAX_BINARY, prep.EXTENSION_BINARY_SHA), self.blob)
            with self.assertRaises(ValueError):
                prep.base.pinned(path, prep.MAX_BINARY - 1, prep.EXTENSION_BINARY_SHA)
            with self.assertRaises(ValueError):
                prep.base.pinned(path, prep.MAX_BINARY, "0" * 64)
            changed = bytearray(self.blob)
            changed[0] ^= 1
            path.write_bytes(changed)
            with self.assertRaises(ValueError):
                prep.base.pinned(path, prep.MAX_BINARY, prep.EXTENSION_BINARY_SHA)

    def test_provenance_and_crop_namespaces(self):
        for key, value in (("source_sha256", "0" * 64), ("inventory_sha256", "0" * 64),
                           ("history_helper_sha256", "0" * 64), ("source_save_count", 1),
                           ("material_permission", True), ("actor_permission", True)):
            with self.subTest(key=key):
                self.reject_document(lambda d, k=key, v=value: d.__setitem__(k, v))
        self.reject_document(lambda d: d["work"].__setitem__("new_crop_face_replays", 3194))
        self.reject_document(lambda d: d["objects"][1].__setitem__("source_index", 1436))
        self.reject_document(lambda d: d["objects"][0]["identity"]["retained_original_contact_range"].__setitem__("triangle_start", 0))
        self.reject_document(lambda d: d["objects"][0]["geometry"].__setitem__("evaluated_triangle_ordinal_range", [188765, 189277]))

    def test_array_codec_malformed_and_truncated_packets(self):
        data = struct.pack("<3d", 1., 2., 3.)
        descriptor = dict(offset=0, bytes=24, rows=1, columns=3, dtype="<f8",
                          sha256=hashlib.sha256(data).hexdigest())
        self.assertEqual(list(prep.base.array(data, descriptor, "<f8")), [(1., 2., 3.)])
        for update in ({"offset": -1}, {"bytes": 25}, {"rows": 2}, {"columns": 4},
                       {"dtype": "<i8"}, {"sha256": "0" * 64}):
            with self.subTest(update=update), self.assertRaises(ValueError):
                list(prep.base.array(data, descriptor | update, "<f8"))
        with self.assertRaises(ValueError):
            list(prep.base.array(data[:-1], descriptor, "<f8"))
        self.reject_document(lambda d: d["objects"][1]["geometry"]["vertices_binary64"].__setitem__("offset", 0))
        self.reject_document(lambda d: d["objects"][0]["geometry"]["triangle_indices"].__setitem__("rows", 511))

    def test_corrupt_arrays_refuse_even_beyond_metadata_gate(self):
        for offset in (0, 256 * 24, 256 * 48):
            changed = bytearray(self.blob)
            changed[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                prep.emit(self.document, changed, io.StringIO())
        with self.assertRaises(ValueError):
            prep.emit(self.document, self.blob[:-1], io.StringIO())

    def test_numeric_and_index_boundary_guards_after_valid_array_digest(self):
        # Deliberately bypass the outer immutable package pin to exercise the
        # decoder's independent finite/quantizer/index guard, without minting
        # any runtime material capability from the altered packet.
        for key, value in (("vertices_binary64", struct.pack("<d", float("nan"))),
                           ("vertices_quantized_micrometres", struct.pack("<q", 8000001)),
                           ("triangle_indices", struct.pack("<i", 256))):
            document = copy.deepcopy(self.document)
            descriptor = document["objects"][0]["geometry"][key]
            changed = bytearray(self.blob)
            offset = descriptor["offset"]
            changed[offset:offset + len(value)] = value
            descriptor["sha256"] = hashlib.sha256(
                changed[offset:offset + descriptor["bytes"]]).hexdigest()
            with self.subTest(key=key), self.assertRaises(ValueError):
                prep.emit(document, changed, io.StringIO())

    def test_frame_topology_and_modifier_controls(self):
        self.reject_document(lambda d: d["objects"][0]["frame_base"]["polygons"][0].__setitem__(0, 32))
        self.reject_document(lambda d: d["objects"][0]["frame_base"]["vertices_local"].pop())
        self.reject_document(lambda d: d["objects"][0]["modifiers"][0].__setitem__("segments", 4))
        self.reject_document(lambda d: d["objects"][0]["modifiers"][0].__setitem__("width", .007))
        self.reject_document(lambda d: d["objects"][0]["modifiers"][1].__setitem__("show_viewport", False))
        self.reject_document(lambda d: d["objects"][1]["modifiers"].append({"type": "SOLIDIFY"}))

    def test_nonfinite_workspace_and_packet_capacity(self):
        for value in (float("nan"), float("inf"), 8.00001, True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                prep.scalar(value)
        self.assertEqual(prep.scalar(8), float(8).hex())
        with self.assertRaises(ValueError):
            prep.bounds([[1., 0., 0.], [0., 1., 1.]])
        with self.assertRaises(ValueError):
            prep.transform([[0., 0., 0.]] * 3)
        with self.assertRaises(ValueError):
            prep.canonical({"value": "x" * 4096})

    def test_short_writes_and_emission_capacity(self):
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
