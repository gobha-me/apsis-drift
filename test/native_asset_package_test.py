#!/usr/bin/env python3
"""Real filesystem refusal/install tests for bounded native asset preparation."""
import hashlib
import importlib.util
import json
import lzma
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("native_assets", Path(__file__).resolve().parents[1] / "tools/prepare_native_assets.py")
assets = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(assets)


class PackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.package = self.root / "package"
        self.output = self.root / "prepared"
        self.package.mkdir()
        self.manifest = {"schema": assets.SCHEMA, "package_id": "freedom-starter-01", "files": [], "models": [], "metadata": []}
        self.data = {}
        for ident, (output, license_id) in assets.MODELS.items():
            # Container header fixtures test byte safety, not valid mesh import.
            model = struct.pack("<4sII", b"glTF", 2, 24) + ident.encode().ljust(12, b" ")
            self.data[output] = model
            hashed = hashlib.sha256(model).hexdigest()
            compressed = lzma.compress(model)
            chunks = []
            for n, begin in enumerate(range(0, len(compressed), 7)):
                name = f"payloads/{ident}-{n:02}.xz"
                self.file(name, compressed[begin:begin + 7])
                chunks.append(name)
            receipt = f"metadata/{ident}-receipt.json"
            self.file(receipt, json.dumps({"pass": True, "model_sha256": hashed}).encode())
            self.manifest["models"].append({"id": ident, "output": output, "bytes": len(model), "sha256": hashed,
                "source_sha256": "a" * 64, "license": license_id, "chunks": chunks, "receipt": receipt})
        for name in ["LICENSE.md", "HOPPER_MESHY_TRIAL.md", "STATION_KIT_01.md"]:
            self.file("licenses/" + name, b"Synthetic test-only license record; no asset admission.\n")
        self.file("metadata/descriptor.json", b'{"fixture": true}\n')
        self.manifest["metadata"] = [{"path": "metadata/descriptor.json", "output": "descriptor.json"}]
        self.write()

    def tearDown(self):
        self.temp.cleanup()

    def file(self, name, data):
        path = self.package / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        record = {"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        self.manifest["files"] = [r for r in self.manifest["files"] if r["path"] != name] + [record]

    def write(self):
        (self.package / "package.json").write_text(json.dumps(self.manifest))

    def refuses(self):
        with self.assertRaises((ValueError, OSError, EOFError, lzma.LZMAError)):
            assets.prepare(self.package, self.output)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".apsis-assets-*")))

    def test_exact_install_idempotence_and_existing_output_protection(self):
        self.assertEqual(assets.prepare(self.package, self.output), self.output)
        for name, data in self.data.items():
            self.assertEqual((self.output / name).read_bytes(), data)
        first = (self.output / "prepared.json").read_bytes()
        assets.prepare(self.package, self.output)
        self.assertEqual((self.output / "prepared.json").read_bytes(), first)
        (self.output / "keep.txt").write_text("owner data")
        with self.assertRaises(ValueError):
            assets.prepare(self.package, self.output)
        self.assertEqual((self.output / "keep.txt").read_text(), "owner data")

    def test_path_traversal_absolute_separator_and_duplicate_roster(self):
        original = self.manifest["files"][0]["path"]
        for name in ["../outside", "/outside", "a/../outside", "a//b", "./a", "a\\b", "x\0y"]:
            with self.subTest(name=name):
                self.manifest["files"][0]["path"] = name
                self.write()
                self.refuses()
        self.manifest["files"][0]["path"] = original
        self.manifest["files"].append(self.manifest["files"][0])
        self.write()
        self.refuses()

    def test_symlink_payload_root_destination_and_parent(self):
        linked = self.package / self.manifest["files"][0]["path"]
        data = linked.read_bytes()
        linked.unlink()
        other = self.root / "other"
        other.write_bytes(data)
        linked.symlink_to(other)
        self.refuses()
        linked.unlink()
        linked.write_bytes(data)
        self.output.symlink_to(self.root / "outside", target_is_directory=True)
        with self.assertRaises(ValueError):
            assets.prepare(self.package, self.output)
        self.output.unlink()
        parent = self.root / "redirect"
        parent.symlink_to(self.root, target_is_directory=True)
        with self.assertRaises(ValueError):
            assets.prepare(self.package, parent / "outside")
        alias = self.root / "alias"
        alias.symlink_to(self.package, target_is_directory=True)
        with self.assertRaises(ValueError):
            assets.verify(alias)
        self.assertFalse((self.root / "outside").exists())

    def test_missing_unrostered_corrupt_and_duplicate_json(self):
        path = self.package / self.manifest["files"][0]["path"]
        data = path.read_bytes()
        path.write_bytes(b"corrupt")
        self.refuses()
        path.unlink()
        self.refuses()
        path.write_bytes(data)
        extra = self.package / "unowned.txt"
        extra.write_text("unrostered")
        self.refuses()
        extra.unlink()
        raw = (self.package / "package.json").read_text()
        (self.package / "package.json").write_text(raw[:-1] + ',"schema":"duplicate"}')
        self.refuses()

    def test_unknown_license_receipt_failure_and_byte_boundaries(self):
        model = self.manifest["models"][0]
        license_id = model["license"]
        model["license"] = "UNKNOWN"
        self.write()
        self.refuses()
        model["license"] = license_id
        for count in [0, -1, True, assets.MAX_MODEL + 1]:
            model["bytes"] = count
            self.write()
            self.refuses()
        model["bytes"] = len(self.data[model["output"]])
        self.file(model["receipt"], json.dumps({"pass": False, "model_sha256": model["sha256"]}).encode())
        self.write()
        self.refuses()

    def test_truncated_trailing_expansion_and_decoded_identity_are_atomic(self):
        model = self.manifest["models"][0]
        name = model["chunks"][0]
        payload = lzma.compress(self.data[model["output"]])
        old = set(model["chunks"])
        for path in old:
            (self.package / path).unlink()
        self.manifest["files"] = [r for r in self.manifest["files"] if r["path"] not in old]
        model["chunks"] = [name]
        for data in [payload[:-1], payload + b"trailing", lzma.compress(b"x" * 10000), lzma.compress(b"x" * model["bytes"])]:
            self.file(name, data)
            self.write()
            self.refuses()

    def test_invalid_glb_header_cannot_publish(self):
        model = self.manifest["models"][0]
        data = b"x" * model["bytes"]
        model["sha256"] = hashlib.sha256(data).hexdigest()
        for path in model["chunks"]:
            (self.package / path).unlink()
        old = set(model["chunks"])
        self.manifest["files"] = [r for r in self.manifest["files"] if r["path"] not in old]
        name = next(iter(old))
        model["chunks"] = [name]
        self.file(name, lzma.compress(data))
        self.file(model["receipt"], json.dumps({"pass": True, "model_sha256": model["sha256"]}).encode())
        self.write()
        self.refuses()

    def test_nonfinite_and_duplicate_runtime_metadata_refuse(self):
        for data in [b'{"dimension": NaN}', b'{"dimension": Infinity}', b'{"dimension": 1e999}', b'{"dimension": 1, "dimension": 2}']:
            self.file("metadata/descriptor.json", data)
            self.write()
            self.refuses()


if __name__ == "__main__":
    unittest.main()
