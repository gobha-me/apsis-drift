#!/usr/bin/env python3
"""Filesystem/buffer refusal tests; synthetic fixtures grant no asset license."""
import copy
import hashlib
import json
import lzma
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_operating_assets as assets
from operating_glb_fixture import craft_fixture, station_corrections, station_fixture

IDENTITY = [[1, 0, 0], [0, 1, 0], [0, 0, 1], [0, 0, 0]]
PARAMETERS = {"roof_hinge_link_clearance": {"inward_metres": .025},
              "inner_door_stroke": {"authored_fraction": .55},
              "seat_lock_withdrawal": {"withdrawal_metres": .07, "withdraw_begin": .5, "withdraw_end": .6,
                                       "turn_begin": .6, "turn_end": .95, "restore_begin": .95, "restore_end": 1},
              "seat_lock_bolt_clearance": {"pitch_circle_radius_metres": .24, "angle_radians": math.radians(7.5)}}


class OperatingPackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.package, self.output = self.root / "package", self.root / "prepared"
        self.package.mkdir()
        self.model = craft_fixture()
        self.station_model = station_fixture()
        hashed = hashlib.sha256(self.model).hexdigest()
        self.manifest = {"schema": assets.SCHEMA, "package_id": assets.PACKAGE_ID,
            "sources": assets.SOURCES, "files": [], "metadata": [],
            "model": {"output": assets.MODEL, "bytes": len(self.model), "sha256": hashed,
                      "source_sha256": assets.SOURCES["wayfarer"], "license": assets.LICENSE,
                      "chunks": ["payloads/test.xz"]}}
        self.manifest["station_model"] = {"output": assets.STATION_MODEL, "bytes": len(self.station_model),
            "sha256": hashlib.sha256(self.station_model).hexdigest(), "source_sha256": assets.SOURCES["station"],
            "license": "LicenseRef-Apsis-Station-Kit-Output", "chunks": ["payloads/station-test.xz"]}
        groups = [{"id": ident, "runtime_node": node,
                   "source_rig": next(name for name, value in assets.MOTION_RIGS.items() if value == ident),
                   "source_parent": None, "source_rest_transform": IDENTITY,
                   "runtime_rest_transform": IDENTITY, "source_objects": [ident], "contact_role": ident}
                  for ident, node in assets.RUNTIME_NODES.items()]
        poses = {ident: IDENTITY for ident in assets.RUNTIME_NODES}
        self.spec = {"schema_version": 1, "id": assets.PACKAGE_ID, "units": "metres",
            "axes": "Godot +Y up, -Z forward", "source": {"sha256": assets.SOURCES["wayfarer"]},
            "exporter": {}, "model": {"file": assets.MODEL, "sha256": hashed}, "groups": groups,
            "poses": {ident: poses for ident in assets.POSE_IDS},
            "channels": [{"id": ident, "samples": [{"progress": n / 20, "transforms": poses}
                                                    for n in range(21)]} for ident in assets.CHANNEL_IDS],
            "anchors": {"pilot_eye": [0, 1, -2], "boarding_eye": [0, 1, -1], "roof_collar": [0, 3, 5]},
            "roster": {"included": [{"source_object": ident, "source_type": "MESH", "source_parent": None,
                                      "runtime_node": node, "motion_group": ident}
                                     for ident, node in assets.RUNTIME_NODES.items()], "excluded": []},
            "gear_preview": [], "gear_preview_samples": 21, "screens": [],
            "seat_adjustment_metres": {"up": .18, "forward": .08}, "licenses": [], "limits": [],
            "derivative_corrections": {"schema_version": 1, "axes": "Blender source local", "operations": [
                {"id": ident, "reason": "Synthetic fixture", "parameters": PARAMETERS[ident],
                 "source_objects": [{"source_object": f"{ident}-{n}", "source_parent": None,
                    "original_rest_local_transform": IDENTITY, "operating_rest_local_transform": IDENTITY,
                    "local_translation_metres": [0, 0, 0]} for n in range(count)]}
                for ident, count in (("roof_hinge_link_clearance", 4), ("inner_door_stroke", 4),
                                     ("seat_lock_withdrawal", 1), ("seat_lock_bolt_clearance", 1))]}}
        self.spec["roster"]["included"] += [{"source_object": f"fixed-{n}", "source_type": "MESH",
            "source_parent": None, "runtime_node": "HopperStructure", "motion_group": None} for n in range(1733)]
        self.contact = {"schema_version": 1, "sources": assets.CONTACT_SOURCES,
            "coordinate_contracts": {}, "quantization_metres": .000001,
            "crop_bounds_metres": {"station": [[-1, -1, -1], [1, 1, 1]], "craft": [[-1, -1, -1], [1, 1, 1]]},
            "groups": [{"id": "fixture", "owner": "craft", "motion_group": "roof_port",
                        "source_objects": ["roof_port"], "vertices_micrometres": [[0, 0, 0], [1, 0, 0], [0, 1, 0]],
                        "triangles": [[0, 1, 2]]}]}
        self.closure = {"schema_version": 1, "source_sha256": assets.SOURCES["station"],
            "closure_source_sha256": assets.SOURCES["station_closure"],
            "model_sha256": "79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80",
            "coordinate_contract": "fixture", "timeline_frames": [110, 300], "attached_closed_progress": .8,
            "correction_coordinate_contract": "Synthetic fixture", "corrections": [{
                "id": name, "filename": assets.STATION_MODEL, "model_sha256": self.manifest["station_model"]["sha256"],
                "source_object": next(c['source_object'] for c in station_corrections() if c['replacement_node'] == name),
                "runtime_node": name, "runtime_parent": "fixture", "expected_extras": {},
                "rest_transform": IDENTITY, "replacement_node": name, "original_geometry_sha256": "a" * 64,
                "replacement_geometry_sha256": "b" * 64, "surface_count": 1, "kind": "fixture",
                "original_points_metres": [[0, 0, 0]] * 4 if name.endswith("PIPE") else [],
                "replacement_points_metres": [[0, 0, 0]] * 7 if name.endswith("PIPE") else [],
                "tube_radius_metres": .018 if name.endswith("PIPE") else 0,
                "bore_centre_metres": [0, 0, 0], "bore_radius_metres": 0 if name.endswith("PIPE") else .020,
                "bore_depth_interval_metres": [] if name.endswith("PIPE") else [-.1, 0]}
                for name in ("D1_CLEARANCE_PIPE", "D1_CLEARANCE_DECK", "D1_CLEARANCE_FLANGE")],
            "bindings": [{"id": f"station_d1_{n:02}", "source_object": f"control-{n}",
                "runtime_node": f"control-{n}", "runtime_parent": "fixture", "expected_extras": {},
                "rest_transform": IDENTITY, "knots": [{"progress": 0, "transform": IDENTITY},
                                                        {"progress": 1, "transform": IDENTITY}]} for n in range(17)]}
        self.contact["groups"].append({"id": "station_fixed", "owner": "station", "motion_group": None,
            "source_objects": [c["source_object"] for c in self.closure["corrections"]] + ["DK09 | equalization service cabinet.001"],
            "vertices_micrometres": [[0, 0, 0], [1, 0, 0], [0, 1, 0]], "triangles": [[0, 1, 2]]})
        self.spec["roster"]["included"][13]["source_object"] = "Swivel bearing race"
        self.closure["bindings"][0]["expected_extras"] = {"dock_leaf_index": 0}
        self.contact["groups"].append({"id": "craft_fixed", "owner": "craft", "motion_group": None,
            "source_objects": ["Swivel bearing race"], "vertices_micrometres": [[0, 0, 0], [1, 0, 0], [0, 1, 0]],
            "triangles": [[0, 1, 2]]})
        for correction in self.closure["corrections"]:
            correction["kind"] = "pipe_route" if correction["replacement_node"].endswith("PIPE") else "service_bore"
        self.qualification_mutation = None
        self.evidence_mutation = None
        self.provenance = {"sources": assets.SOURCES, "license": assets.LICENSE, "export_checks": {
            "pass": True, "source_unchanged": True, "source_sha256": assets.SOURCES["wayfarer"],
            "model_sha256": hashed}}
        aliases = {"sha256": "export_wayfarer_operating.py", "base_sha256": "wayfarer_flight_export_base.py",
                   "classification_sha256": "wayfarer_operating_spec.py", "pose_helper_sha256": "wayfarer_operating_blender.py",
                   "validation_helper_sha256": "wayfarer_gltf_checks.py", "binary_audit_sha256": "wayfarer_operating_glb_audit.py"}
        self.spec["exporter"] = {"path": "tools/export_wayfarer_operating.py", **{name: "a" * 64 for name in aliases}}
        self.spec["exporter"]["base_sha256"] = "762499e37fe13a33bf297fc58801e711590cd9d56058a6462b3467ff6b7102fe"
        self.provenance["tool_sha256"] = {filename: self.spec["exporter"][key] for key, filename in aliases.items()}
        self.provenance["export_checks"].update({"exporter": self.spec["exporter"],
            "derivative_corrections": self.spec["derivative_corrections"],
            "source_pose_proof": [{"pose": name, "maximum_source_matrix_error": 0} for name in
                                  [f"{channel}:{n}" for channel in assets.CHANNEL_IDS for n in range(21)] + list(assets.POSE_IDS)]})
        self.file("payloads/test.xz", lzma.compress(self.model))
        self.file("payloads/station-test.xz", lzma.compress(self.station_model))
        decoded = self.root / assets.MODEL
        decoded.write_bytes(self.model)
        audited = assets.validate_craft_model(decoded, self.spec)
        self.provenance["export_checks"]["glb_buffer_audit"] = audited["binary_audit"]
        self.provenance["export_checks"]["glb_bindings"] = [
            {**binding, "geometry_sha256": "a" * 64} for binding in audited["motion_bindings"]]
        self.provenance["export_checks"]["mesh_buffer_sha256"] = {
            binding["runtime_node_name"]: "a" * 64 for binding in audited["motion_bindings"]}
        self.selected = {}
        self.policy = patch.object(assets, "SELECTED_SHA256", self.selected)
        self.policy.start()
        for name in assets.LICENSES:
            self.file("licenses/" + name, b"Synthetic fixture only; no asset admission.\n")
        self.write()

    def tearDown(self):
        self.policy.stop()
        self.temp.cleanup()

    def file(self, name, data):
        path = self.package / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        self.manifest["files"] = [r for r in self.manifest["files"] if r["path"] != name] + [
            {"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}]

    def evidence(self, qualification):
        raw = {"sources": assets.CONTACT_SOURCES, "source_unchanged": True, "limits": ["Synthetic source fixture"],
            "sweeps": [{"owner": owner, "channel": channel, "samples": 41, "method": assets.SWEEP_METHODS[owner], "contacts": []}
                       for owner, channel in sorted(assets.QUALIFIED_SWEEPS)]}
        detail = {"bvh_triangle_pairs": 1, "strict_crossing_pairs": 1, "coplanar_pairs": 0,
                  "maximum_triangle_plane_crossing_depth_m": .001, "crossing_points_owner_m": [[0, 0, 0]]}
        station = {"schema_version": 1, "sources": assets.CONTACT_SOURCES, "source_unchanged": True, "pass": True,
            "unexpected_contacts": [], "standing_contacts": [], "pipe_closure_samples": 81,
            "standing_sweep_bounds_station_metres": [[-22.7367, .045, -.32], [-18.48, 1.975, .32]],
            "intentional_endpoint_pair": ["DK09 | equalization pipe.001", "DK09 | equalization service cabinet.001"],
            "endpoint_reason": "Synthetic endpoint fixture", "poses": [{"progress": n / 80,
                "frame": 110 + 190 * n / 80, "contacts": [{"object": "DK09 | equalization service cabinet.001", "detail": detail}]}
                for n in range(81)], "floor_support_samples": [{"x_metres": -22.4167 + 3.6167 * n / 120,
                    "heights_metres": [0] * 5} for n in range(121)],
            "negative_controls": [{"name": name, "detected": True, "detail": detail} for name in sorted(assets.STATION_NEGATIVES)]}
        topology = []
        for c in self.closure["corrections"]:
            boundary = 16 if c["kind"] == "pipe_route" else 0
            before = {"sha256": c["original_geometry_sha256"], "vertices": 32, "triangles": 48,
                      "boundary_edges": boundary, "nonmanifold_edges": 0, "volume_cubic_metres": 1.}
            after = {**before, "sha256": c["replacement_geometry_sha256"]}
            if c["kind"] == "service_bore":
                depth = c["bore_depth_interval_metres"]
                volume = 128 * .020 ** 2 * math.sin(2 * math.pi / 128) * .5 * (depth[1] - depth[0])
                before["bore_expected_removal_cubic_metres"] = volume
                after.update({"bore_actual_removal_cubic_metres": volume, "volume_cubic_metres": 1. - volume})
            topology.append({"id": c["id"], "before": before, "after": after})
        component = {"schema_version": 1, "source_sha256": assets.SOURCES["station"],
            "model_sha256": self.manifest["station_model"]["sha256"], "source_unchanged": True,
            "corrections": self.closure["corrections"], "topology": topology,
            "checks": {name: True for name in ("finite_buffers", "bore_manifold_capped", "identity_three_mesh_glb")}}
        support = [{"craft_back_axis_metres": 3.8 + 1.4 * n / 60, "heights_above_foot_metres": [0] * 5} for n in range(61)]
        support[1]["heights_above_foot_metres"][3] = None
        bounds = {"schema_version": 1, "sources": assets.CONTACT_SOURCES, "source_unchanged": True,
            "geometry_pass": True, "floor_support_complete": False, "pass": False,
            "shaft": {"dimensions_metres": [.6, .4, 1.95], "foot_clearance_metres": .04,
                "foot_interval_metres": [0, -4.357], "continuous_union_bounds_station_metres": [[-23.32, -4.317, -.3], [-22.92, 1.99, .3]], "contacts": []},
            "standing_transition": {"dimensions_metres": [.64, .64, 1.93], "foot_clearance_metres": .045,
                "bounds_station_metres": [[-23.44, -4.312, -.32], [-22.8, -2.382, .32]], "contacts": []},
            "inner_standing_sweep": {"bounds_craft_metres": [[-.32, -.055, 3.48], [.32, 1.875, 5.52]],
                "cases": [{"roof_progress": n, "inner_progress": 1, "contacts": []} for n in (0, 1)],
                "floor_support": support, "threshold_missing_support_interval_metres": [3.6305, 3.6644],
                "threshold_probe_heights_metres": [{"craft_back_axis_metres": 3.6 + .1 * n / 100,
                    "height_relative_to_nominal_foot_metres": -.0775 if 3.6305 < 3.6 + .1 * n / 100 < 3.6644 else 0}
                    for n in range(101)]},
            "negative_controls": [{"name": name, "detected": True} for name in sorted(assets.BOUNDS_NEGATIVES)],
            "closed_inner_contacts": [{"object": "roof_port", "triangles": 1}]}
        runtime = {"count": 17, "glb_sha256": self.closure["model_sha256"], "glb_unchanged": True, "problems": [],
            "records": [{"source_name": b["source_object"], "name": b["runtime_node"], "parent": b["runtime_parent"],
                "metadata": {"extras": b["expected_extras"]}, "transform_columns": b["rest_transform"],
                "maximum_local_transform_error": 0, "maximum_world_transform_error": 0} for b in self.closure["bindings"]]}
        crossing = {"schema_version": 1, "sources": assets.CONTACT_SOURCES, "tolerance_metres": .000001,
            "sweeps": [{"owner": row["owner"], "channel": row["channel"], "samples": 41, "pairs": []} for row in raw["sweeps"]],
            "inner_opening": {"0.0": {"central_panel_gap_m": .002, "width_reservation_m": .64},
                              "1.0": {"central_panel_gap_m": 1.025, "width_reservation_m": .64}},
            "negative_controls": [{"name": name, "result": detail if name == "crossing" else {
                **detail, "bvh_triangle_pairs": 0, "strict_crossing_pairs": 0, "maximum_triangle_plane_crossing_depth_m": 0,
                "crossing_points_owner_m": []}} for name in ("separated", "touching", "crossing")]}
        phase = {"schema_version": 1, "rotating_pin_crossings": [], "pin_bolt_crossing_samples": [],
            "corrections": self.spec["derivative_corrections"], "samples": [{"progress": n / 200,
                "pin_crossing_objects": [], "seat_rotation_radians": min(1, max(0, (n / 200 - .60) / .35)) * math.pi / 2}
                for n in range(201)], "fit_measurements": {"pin_bolt_perpendicular_interval_gap_metres": .01,
                "bolt_neighbor_aabb_separation_lower_bound_metres": .07, "withdrawn_pin_housing_radial_overlap_metres": .015,
                "mounting_bolt_count": 12, "housing_modifiers": []}}
        seat = {"source_sha256": assets.SOURCES["wayfarer"], "source_unchanged": True,
            "probes": [{"name": "seat_corrected_complete_070", "samples": [n / 200 for n in range(201)], "crossings": []}]}
        receipts = {name: {"sha256": hashlib.sha256(json.dumps(data).encode()).hexdigest(), "data": data}
            for name, data in (("raw-sweeps.json", raw), ("station-clearance-qualification.json", station),
                ("station-clearance-checks.json", component), ("bounds-proof.json", bounds),
                ("negative-controls.json", {"negative_controls": bounds["negative_controls"]}),
                ("runtime-station-proof.json", runtime), ("crossing-analysis.json", crossing),
                ("lock-qualified-phase.json", phase), ("seat-corrected-complete.json", seat))}
        qualification["runtime_station_proof_sha256"] = receipts["runtime-station-proof.json"]["sha256"]
        return {"schema_version": 1, "scope": "Synthetic boundary fixture grants no asset admission", "sources": assets.CONTACT_SOURCES,
            **{key: qualification[key] for key in ("contact_sha256", "station_closure_sha256", "station_model_sha256")},
            "qualification_sha256": "", "receipts": receipts}

    def write(self):
        values = {"wayfarer-operating-02.json": self.spec, "contact.json": self.contact, "station-closure.json": self.closure}
        self.provenance["export_checks"]["manifest_sha256"] = hashlib.sha256(json.dumps(self.spec).encode()).hexdigest()
        for name, data in values.items():
            self.file("metadata/" + name, json.dumps(data).encode())
        qualification = {"schema_version": 1, "pass": True, "sources": assets.CONTACT_SOURCES,
            "contact_sha256": assets.base.sha(self.package / "metadata/contact.json"),
            "station_closure_sha256": assets.base.sha(self.package / "metadata/station-closure.json"),
            "station_model_sha256": self.manifest["station_model"]["sha256"], "runtime_station_proof_sha256": "a" * 64,
            "checks": {name: True for name in ("contact_buffers", "source_unchanged", "station_bindings", "motion_sweeps", "negative_controls")},
            "sweeps": [{"owner": owner, "channel": channel, "samples": 41, "method": assets.SWEEP_METHODS[owner],
                "unexpected_contacts": [], "intentional_contacts": []} for owner, channel in sorted(assets.QUALIFIED_SWEEPS)],
            "exceptions": [], "negative_controls": [{"name": name, "detected": True} for name in sorted(assets.NEGATIVE_NAMES)],
            "limits": ["Synthetic test fixture;77.5mm threshold;no completed supported cabin traversal"]}
        evidence = self.evidence(qualification)
        if self.qualification_mutation:
            self.qualification_mutation(qualification)
        self.file("metadata/qualification.json", json.dumps(qualification).encode())
        evidence["qualification_sha256"] = assets.base.sha(self.package / "metadata/qualification.json")
        if self.evidence_mutation:
            self.evidence_mutation(evidence)
        self.provenance["boarding_evidence"] = evidence
        self.provenance["boarding_evidence_sha256"] = hashlib.sha256(json.dumps(evidence).encode()).hexdigest()
        self.file("metadata/provenance.json", json.dumps(self.provenance).encode())
        self.manifest["metadata"] = [{"path": "metadata/" + name, "output": name} for name in sorted(assets.METADATA)]
        self.save_manifest()

    def save_manifest(self):
        (self.package / "package.json").write_text(json.dumps(self.manifest))
        self.selected.update({assets.MODEL: self.manifest["model"]["sha256"],
                              assets.STATION_MODEL: self.manifest["station_model"]["sha256"],
                              **{name: assets.base.sha(self.package / "metadata" / name)
                                 for name in ("wayfarer-operating-02.json", "contact.json",
                                              "station-closure.json", "qualification.json")}})

    def refuses(self):
        with self.assertRaises((ValueError, OSError, EOFError, lzma.LZMAError)):
            assets.prepare(self.package, self.output)
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".apsis-operating-*")))

    def test_exact_atomic_install_and_protect_changed_output(self):
        assets.prepare(self.package, self.output)
        self.assertEqual((self.output / assets.MODEL).read_bytes(), self.model)
        receipt = assets.load_json(self.output / "prepared.json")
        self.assertEqual(set(receipt["files"]), assets.METADATA | {assets.MODEL, assets.STATION_MODEL})
        assets.prepare(self.package, self.output)
        (self.output / "contact.json").write_text("owner data")
        with self.assertRaises(ValueError):
            assets.prepare(self.package, self.output)
        self.assertEqual((self.output / "contact.json").read_text(), "owner data")

    def test_source_license_unknown_fields_and_roster_refusals(self):
        original = copy.deepcopy(self.manifest)
        for path, value in (("sources", {}), ("package_id", "other"), ("unexpected", True)):
            self.manifest = copy.deepcopy(original)
            self.manifest[path] = value
            self.save_manifest()
            self.refuses()
        self.manifest = original
        self.manifest["model"]["license"] = "UNKNOWN"
        self.save_manifest()
        self.refuses()

    def test_contact_index_dimension_and_ancestry_refusals(self):
        original = copy.deepcopy(self.contact)
        mutations = [("triangles", [[0, 1, 3]]), ("triangles", [[0, 1, True]]),
                     ("vertices_micrometres", [[0, 0]]), ("vertices_micrometres", [[0, 0, 1.5]]),
                     ("motion_group", "seat_swivel"), ("source_objects", ["roof_port", "roof_port"])]
        for field, value in mutations:
            with self.subTest(field=field, value=value):
                self.contact = copy.deepcopy(original)
                self.contact["groups"][0][field] = value
                self.write()
                self.refuses()

    def test_nonfinite_unordered_nonrigid_and_duplicate_mechanisms(self):
        original = copy.deepcopy(self.spec)
        for kind in ("nonfinite", "unordered", "nonrigid", "missing", "duplicate"):
            self.spec = copy.deepcopy(original)
            if kind == "nonfinite":
                self.spec["channels"][0]["samples"][0]["progress"] = float("nan")
            elif kind == "unordered":
                self.spec["channels"][0]["samples"][1]["progress"] = 0
            elif kind == "nonrigid":
                self.spec["groups"][0]["source_rest_transform"][0][0] = 2
            elif kind == "missing":
                self.spec["groups"].pop()
            else:
                self.spec["groups"][1] = self.spec["groups"][0]
            self.write()
            self.refuses()

    def test_decode_truncation_trailing_expansion_and_identity(self):
        packed = lzma.compress(self.model)
        for data in (packed[:-1], packed + b"trailing", lzma.compress(b"x" * 10000), lzma.compress(b"x" * len(self.model))):
            self.file("payloads/test.xz", data)
            self.save_manifest()
            self.refuses()

    def test_exact_sweep_roster_and_typed_observations(self):
        mutations = [lambda q: q.update(sweeps=[]),
                     lambda q: q["sweeps"].append(copy.deepcopy(q["sweeps"][0])),
                     lambda q: q["sweeps"][0].update(samples=True),
                     lambda q: q["sweeps"][0].update(channel="other"),
                     lambda q: q["sweeps"][0].update(method=17),
                     lambda q: q["sweeps"][0].update(method="unqualified collision algorithm"),
                     lambda q: q["sweeps"][0].update(unexpected_contacts=[{"object": "obstacle"}]),
                     lambda q: q["sweeps"][0].update(intentional_contacts=[{"object_a": "roof_port"}]),
                     lambda q: q["negative_controls"].pop(),
                     lambda q: q["negative_controls"][0].update(detected=1),
                     lambda q: q["negative_controls"][0].update(name="untested")]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                self.qualification_mutation = mutate
                self.write()
                self.refuses()

    def test_missing_stale_failed_and_contradictory_archived_receipts(self):
        def data(e, name):
            return e["receipts"][name]["data"]
        mutations = [lambda e: e["receipts"].pop("lock-qualified-phase.json"),
                     lambda e: e.update(station_model_sha256="c" * 64),
                     lambda e: e["receipts"]["runtime-station-proof.json"].update(sha256="c" * 64),
                     lambda e: data(e, "raw-sweeps.json").update(source_unchanged=1),
                     lambda e: data(e, "station-clearance-qualification.json").update({"pass": False}),
                     lambda e: data(e, "station-clearance-qualification.json")["poses"][7]["contacts"][0].update(object="new pipe obstruction"),
                     lambda e: data(e, "station-clearance-qualification.json")["floor_support_samples"][1]["heights_metres"].pop(),
                     lambda e: data(e, "station-clearance-checks.json")["topology"][1]["after"].update(boundary_edges=1),
                     lambda e: data(e, "bounds-proof.json")["shaft"].update(foot_interval_metres=[0, -2]),
                     lambda e: data(e, "bounds-proof.json").update({"floor_support_complete": True, "pass": True}),
                     lambda e: data(e, "bounds-proof.json")["inner_standing_sweep"]["threshold_probe_heights_metres"][40].update(height_relative_to_nominal_foot_metres=0),
                     lambda e: data(e, "runtime-station-proof.json")["records"][0].update(parent="other"),
                     lambda e: data(e, "runtime-station-proof.json")["records"][0]["metadata"]["extras"].update(dock_leaf_index=False),
                     lambda e: data(e, "lock-qualified-phase.json")["samples"][130].update(pin_crossing_objects=[["untyped"]]),
                     lambda e: data(e, "lock-qualified-phase.json")["samples"][130].update(pin_crossing_objects=["Swivel bearing race"]),
                     lambda e: data(e, "lock-qualified-phase.json")["samples"].pop(),
                     lambda e: data(e, "crossing-analysis.json")["negative_controls"][2]["result"].update(strict_crossing_pairs=0)]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                self.evidence_mutation = mutate
                self.write()
                self.refuses()

    def test_exception_cannot_expand_policy_or_replace_observation(self):
        def add_exception(q):
            q["exceptions"].append({"object_a": "roof_port", "object_b": "unknown",
                "channel": "roof_transfer", "progress_range": [0, 1], "reason": "pretended joint"})
        self.qualification_mutation = add_exception
        self.write()
        self.refuses()
        self.qualification_mutation = None
        # A raw observation cannot be removed from the qualification while the
        # archive pretends it is an already-reviewed intentional contact.
        def add_raw(e):
            raw = e["receipts"]["raw-sweeps.json"]["data"]["sweeps"][0]
            raw["contacts"].append({"object_a": "roof_port", "object_b": "unknown",
                "samples": 1, "first_progress": 0, "last_progress": 0,
                "triangle_pairs_max": 1, "progress_samples": [0]})
        self.evidence_mutation = add_raw
        self.write()
        self.refuses()

    def test_depth_escape_duplicate_and_nonfinite_parser_boundaries(self):
        path = self.root / "json"
        path.write_text('[' * 64 + '"[\\\"}"' + ']' * 64)
        assets.load_json(path)
        for raw in ('[' * 65 + '0' + ']' * 65, '{"x":1,"x":2}', '{"x":1e999}', '{"x":Infinity}', '}{'):
            path.write_text(raw)
            with self.assertRaises(ValueError):
                assets.load_json(path)

    def test_geometry_hash_table_types_and_node_identities(self):
        for value in ([], None, {}, {"unexpected": "a" * 64}, {"WFOpRoofPort": []}):
            with self.subTest(value=value):
                self.provenance["export_checks"]["mesh_buffer_sha256"] = value
                self.write()
                self.refuses()

    def test_unrostered_symlink_overlap_and_path_traversal(self):
        (self.package / "unexpected").write_text("unowned")
        self.refuses()
        (self.package / "unexpected").unlink()
        self.output.symlink_to(self.root / "outside", target_is_directory=True)
        with self.assertRaises(ValueError):
            assets.prepare(self.package, self.output)
        self.output.unlink()
        with self.assertRaises(ValueError):
            assets.prepare(self.package, self.package / "nested")
        self.manifest["files"][0]["path"] = "../outside"
        self.save_manifest()
        self.refuses()

    def test_concurrent_empty_destination_is_never_replaced(self):
        staged = self.root / "staged"
        staged.mkdir()
        (staged / "new.txt").write_text("new data")
        self.output.mkdir()
        inode = self.output.stat().st_ino
        with self.assertRaises(OSError):
            assets.install_new(staged, self.output)
        self.assertEqual(self.output.stat().st_ino, inode)
        self.assertTrue((staged / "new.txt").is_file())


if __name__ == "__main__":
    unittest.main()
