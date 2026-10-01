#!/usr/bin/env python3
"""Verify and atomically prepare the source-bound Wayfarer operating package.

Offline bytes only; preparation does not import Godot, change a save or board a
player. The existing sealed-flight starter delivery is a separate dependency.
"""
import argparse
import ctypes
from build_boarding_qualification import classify as classify_contacts
import json
import lzma
import math
from pathlib import Path
import shutil
import tempfile

import prepare_native_assets as base
from operating_asset_identity import SELECTED_SHA256
from validate_operating_asset_glb import validate_craft_model, validate_station_model
from wayfarer_operating_spec import CHANNEL_IDS, MOTION_RIGS, POSE_IDS, RUNTIME_NODES

SCHEMA = "apsis.wayfarer-operating-assets/1"
PACKAGE_ID = "wayfarer-operating-02"
MODEL = "wayfarer-operating-02.glb"
STATION_MODEL = "station-d1-clearance-01.glb"
LICENSE = "LicenseRef-Apsis-Hopper-Meshy-Output"
SOURCES = {
    "wayfarer": "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677",
    "station": "6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6",
    "station_closure": "6a12e1e6846be4de6c89ce0c65b154567a8a07818d37bf574de2a319109319b4",
}
METADATA = {"wayfarer-operating-02.json", "contact.json", "station-closure.json",
            "qualification.json", "provenance.json"}
LICENSES = {"LICENSE.md", "HOPPER_MESHY_TRIAL.md", "STATION_KIT_01.md",
            "HOPPER_GENERATED_CONCEPTS.md", "MESHY_QUALIFICATION_OUTPUT.md"}
MAX_METADATA = 64 * 1024 * 1024
MAX_MODEL = 80 * 1024 * 1024
MAX_TOTAL = 256 * 1024 * 1024
CONTACT_SOURCES = {"station_reference_sha256": SOURCES["station"],
                   "wayfarer_sha256": SOURCES["wayfarer"],
                   "station_closure_sha256": SOURCES["station_closure"]}


def install_new(staging, output):
    # Linux native delivery: the no-replace rename closes the check/rename race,
    # even when another writer creates an empty destination directory meanwhile.
    libc = ctypes.CDLL(None, use_errno=True)
    rename = getattr(libc, "renameat2", None)
    base.require(rename is not None, "atomic no-replace directory install requires Linux renameat2")
    rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    rename.restype = ctypes.c_int
    result = rename(-100, bytes(staging), -100, bytes(output), 1)
    if result != 0:
        raise OSError(ctypes.get_errno(), "atomic no-replace installation refused", str(output))


def finite(value, low=-100.0, high=100.0):
    base.require(type(value) in (int, float) and low <= value <= high and math.isfinite(value),
                 "invalid/non-finite dimension")


def vector(value):
    base.require(type(value) is list and len(value) == 3, "invalid vector dimension")
    for component in value:
        finite(component)


def transform(value):
    base.require(type(value) is list and len(value) == 4, "invalid transform dimension")
    for column in value:
        vector(column)
    basis = value[:3]
    for first in range(3):
        for second in range(3):
            dot = sum(a * b for a, b in zip(basis[first], basis[second]))
            base.require(abs(dot - (first == second)) < 0.001, "non-rigid transform")
    determinant = (basis[0][0] * (basis[1][1] * basis[2][2] - basis[1][2] * basis[2][1]) -
                   basis[1][0] * (basis[0][1] * basis[2][2] - basis[0][2] * basis[2][1]) +
                   basis[2][0] * (basis[0][1] * basis[1][2] - basis[0][2] * basis[1][1]))
    base.require(abs(determinant - 1) < 0.001, "reflected transform")


QUALIFIED_SWEEPS = {(owner, channel) for owner, channel in (
    ("craft", "roof_transfer"), ("craft", "inner_door"), ("craft", "seat_boarding"), ("station", "station_closure"))}
SWEEP_METHODS = {
    "craft": "Sampled exact source-triangle BVH surface intersection",
    "station": "Sampled authored kit09 source poses acting on reference triangles; exact pair BVH intersection"}
STATION_NEGATIVES = {
    "restore_unbored_DK09 | continuous deck with open well.001",
    "restore_unbored_DK09 | belly shaft mounting flange.001",
    "restore_original_pipe_DK09 | sliding deck hatch 1.001",
    "restore_original_pipe_DK09 | capture witness.004"}
BOUNDS_NEGATIVES = {"closed_inner_door_blocks_standing_union", "restore_inner_authored_fraction_0.60_hits_frame",
    "restore_original_hinge_link_hits_pressure_ceiling", "restore_original_mounting_bolt_hits_withdrawing_pin",
    "retain_locked_pin_hits_rotating_lift_guide"}
NEGATIVE_NAMES = STATION_NEGATIVES | BOUNDS_NEGATIVES | {"triangle_separated", "triangle_touching", "triangle_crossing"}
EVIDENCE_RECEIPTS = {"raw-sweeps.json", "crossing-analysis.json", "station-clearance-checks.json",
    "station-clearance-qualification.json", "bounds-proof.json", "negative-controls.json",
    "runtime-station-proof.json", "lock-qualified-phase.json", "seat-corrected-complete.json"}


def text(value):
    base.require(type(value) is str and 0 < len(value) <= 4096 and "\0" not in value, "invalid evidence text")


def count(value, low=0, high=4000000):
    base.require(type(value) is int and low <= value <= high, "invalid evidence integer/count")


def exact_numbers(value, expected):
    if type(expected) is list:
        base.require(type(value) is list and len(value) == len(expected), "evidence vector/interval dimension")
        for a, b in zip(value, expected):
            exact_numbers(a, b)
    else:
        finite(value, -1000, 1000)
        base.require(abs(value - expected) <= 0.000001, "evidence dimensions/interval mismatch")


def strings(value):
    base.require(type(value) is list and 1 <= len(value) <= 128, "missing evidence limits")
    for item in value:
        text(item)


def negatives(value, expected, detailed=False):
    base.require(type(value) is list and len(value) == len(expected), "missing negative controls")
    seen = set()
    for row in value:
        base.keys(row, ["name", "detected", *(["detail"] if detailed else [])])
        text(row["name"])
        base.require(row["name"] in expected and row["name"] not in seen and row["detected"] is True,
                     "duplicate/failed/unknown negative control")
        seen.add(row["name"])
        if detailed:
            detail(row["detail"])
            base.require(row["detail"]["strict_crossing_pairs"] > 0, "restored collision was not detected")


def detail(value):
    base.keys(value, ["bvh_triangle_pairs", "strict_crossing_pairs", "coplanar_pairs",
                      "maximum_triangle_plane_crossing_depth_m", "crossing_points_owner_m"])
    count(value["bvh_triangle_pairs"])
    count(value["strict_crossing_pairs"], high=value["bvh_triangle_pairs"])
    count(value["coplanar_pairs"], high=value["bvh_triangle_pairs"])
    finite(value["maximum_triangle_plane_crossing_depth_m"], 0, 100)
    points = value["crossing_points_owner_m"]
    base.require(type(points) is list and len(points) <= 256, "invalid crossing point count")
    for point in points:
        vector(point)


def sweep_map(rows):
    base.require(type(rows) is list and len(rows) == 4, "missing qualified hardware sweep roster")
    result = {}
    for row in rows:
        base.require(type(row) is dict, "invalid hardware sweep")
        text(row.get("owner"))
        text(row.get("channel"))
        identity = (row["owner"], row["channel"])
        base.require(identity in QUALIFIED_SWEEPS and identity not in result, "duplicate/unknown hardware sweep")
        count(row.get("samples"), 41, 41)
        text(row.get("method"))
        result[identity] = row
    return result


def validate_contact_row(row, sample_count, owner, source_ids, progress=False):
    base.keys(row, ["object_a", "object_b", "samples", "first_progress", "last_progress", "triangle_pairs_max",
                    *(["progress_samples"] if progress else [])])
    for field in ("object_a", "object_b"):
        text(row[field])
        base.require((owner, row[field]) in source_ids, "contact pair outside source roster")
    base.require(row["object_a"] < row["object_b"], "unordered/equal contact pair")
    count(row["samples"], 1, sample_count)
    count(row["triangle_pairs_max"], 1)
    finite(row["first_progress"], 0, 1)
    finite(row["last_progress"], row["first_progress"], 1)
    for field in ("first_progress", "last_progress"):
        base.require(abs(row[field] * (sample_count - 1) - round(row[field] * (sample_count - 1))) <= 1e-6,
                     "contact outside observed sample grid")
    if progress:
        samples = row["progress_samples"]
        base.require(type(samples) is list and len(samples) == row["samples"], "contact sample-count disagreement")
        last = -1
        for value in samples:
            finite(value, 0, 1)
            base.require(value > last and abs(value * (sample_count - 1) - round(value * (sample_count - 1))) <= 1e-6,
                         "unordered/non-grid contact observations")
            last = value
        base.require(samples[0] == row["first_progress"] and samples[-1] == row["last_progress"], "contact endpoint disagreement")


def validate_qualification(qualification, provenance, manifest, roster, source_ids, closure, spec):
    base.keys(qualification, ["schema_version", "pass", "sources", "contact_sha256", "station_closure_sha256",
        "station_model_sha256", "runtime_station_proof_sha256", "checks", "sweeps", "exceptions", "negative_controls", "limits"])
    count(qualification["schema_version"], 1, 1)
    base.require(qualification["pass"] is True and qualification["sources"] == CONTACT_SOURCES, "contact qualification failed/stale")
    for field, name in (("contact_sha256", "contact.json"), ("station_closure_sha256", "station-closure.json")):
        base.require(qualification[field] == roster["metadata/" + name]["sha256"], "qualification metadata mismatch")
    base.require(qualification["station_model_sha256"] == manifest["station_model"]["sha256"], "station component qualification mismatch")
    base.digest(qualification["runtime_station_proof_sha256"])
    base.keys(qualification["checks"], ["contact_buffers", "source_unchanged", "station_bindings", "motion_sweeps", "negative_controls"])
    base.require(all(value is True for value in qualification["checks"].values()), "qualification checks failed")
    strings(qualification["limits"])
    sweeps = sweep_map(qualification["sweeps"])
    for row in sweeps.values():
        base.keys(row, ["owner", "channel", "samples", "method", "unexpected_contacts", "intentional_contacts"])
        base.require(row["method"] == SWEEP_METHODS[row["owner"]], "unsupported sweep evidence method")
        base.require(row["unexpected_contacts"] == [] and type(row["intentional_contacts"]) is list and
                     len(row["intentional_contacts"]) <= 256, "unexplained contact/malformed sweep")
        seen = set()
        for contact in row["intentional_contacts"]:
            validate_contact_row(contact, row["samples"], row["owner"], source_ids)
            identity = (contact["object_a"], contact["object_b"])
            base.require(identity not in seen, "duplicate sweep contact")
            seen.add(identity)
    base.require(type(qualification["exceptions"]) is list and len(qualification["exceptions"]) <= 256, "invalid exception roster")
    for row in qualification["exceptions"]:
        base.keys(row, ["object_a", "object_b", "channel", "progress_range", "reason"])
        for field in ("object_a", "object_b", "channel", "reason"):
            text(row[field])
        base.require(type(row["progress_range"]) is list and len(row["progress_range"]) == 2, "invalid exception interval")
        finite(row["progress_range"][0], 0, 1)
        finite(row["progress_range"][1], row["progress_range"][0], 1)
    negatives(qualification["negative_controls"], NEGATIVE_NAMES)
    base.require(type(provenance) is dict and provenance.get("sources") == SOURCES and provenance.get("license") == LICENSE,
                 "missing provenance/license proof")
    base.digest(provenance.get("boarding_evidence_sha256"))
    evidence = provenance.get("boarding_evidence")
    base.keys(evidence, ["schema_version", "scope", "sources", "contact_sha256", "station_closure_sha256",
                         "station_model_sha256", "qualification_sha256", "receipts"])
    count(evidence["schema_version"], 1, 1)
    text(evidence["scope"])
    base.require(evidence["sources"] == CONTACT_SOURCES, "archived evidence source mismatch")
    for key in ("contact_sha256", "station_closure_sha256", "station_model_sha256"):
        base.require(evidence[key] == qualification[key], "archived evidence artifact mismatch")
    base.require(evidence["qualification_sha256"] == roster["metadata/qualification.json"]["sha256"], "archived qualification identity mismatch")
    receipts = evidence["receipts"]
    base.keys(receipts, EVIDENCE_RECEIPTS)
    for receipt in receipts.values():
        base.keys(receipt, ["sha256", "data"])
        base.digest(receipt["sha256"])
        base.require(type(receipt["data"]) is dict, "missing archived receipt data")
    def receipt(name):
        return receipts[name]["data"]
    def source_receipt(value):
        count(value.get("schema_version"), 1, 1)
        base.require(value.get("sources") == CONTACT_SOURCES and value.get("source_unchanged") is True, "stale/changed evidence source")
    raw = receipt("raw-sweeps.json")
    base.keys(raw, ["sources", "sweeps", "source_unchanged", "limits"])
    base.require(raw["sources"] == CONTACT_SOURCES and raw["source_unchanged"] is True, "changed sweep sources")
    strings(raw["limits"])
    raw_sweeps = sweep_map(raw["sweeps"])
    for identity, row in raw_sweeps.items():
        base.keys(row, ["owner", "channel", "samples", "method", "contacts"])
        base.require(row["method"] == SWEEP_METHODS[row["owner"]], "unsupported raw sweep evidence method")
        base.require(type(row["contacts"]) is list and len(row["contacts"]) <= 256, "invalid raw contacts")
        seen = set()
        for contact in row["contacts"]:
            validate_contact_row(contact, row["samples"], row["owner"], source_ids, progress=True)
            key = (contact["object_a"], contact["object_b"])
            base.require(key not in seen, "duplicate raw contact")
            seen.add(key)
    expected_sweeps, expected_exceptions = classify_contacts(raw)
    base.require(sweep_map(expected_sweeps) == sweeps and expected_exceptions == qualification["exceptions"],
                 "qualification disagrees with exact contact/phase policy")
    station = receipt("station-clearance-qualification.json")
    source_receipt(station)
    base.require(station.get("pass") is True and station.get("unexpected_contacts") == [] and station.get("standing_contacts") == [],
                 "failed station clearance evidence")
    count(station.get("pipe_closure_samples"), 81, 81)
    exact_numbers(station.get("standing_sweep_bounds_station_metres"), [[-22.7367, .045, -.32], [-18.48, 1.975, .32]])
    base.require(station.get("intentional_endpoint_pair") == ["DK09 | equalization pipe.001", "DK09 | equalization service cabinet.001"],
                 "unqualified fixed endpoint exception")
    for name in station["intentional_endpoint_pair"]:
        base.require(("station", name) in source_ids, "endpoint contact outside source roster")
    text(station.get("endpoint_reason"))
    poses = station.get("poses")
    base.require(type(poses) is list and len(poses) == 81, "missing full pipe sweep observations")
    for index, pose in enumerate(poses):
        base.keys(pose, ["progress", "frame", "contacts"])
        exact_numbers(pose["progress"], index / 80)
        exact_numbers(pose["frame"], 110 + 190 * index / 80)  # Frame values use a wider range than vector metres.
        base.require(type(pose["contacts"]) is list and len(pose["contacts"]) == 1, "unexpected/missing pipe contact")
        for contact in pose["contacts"]:
            base.keys(contact, ["object", "detail"])
            base.require(contact["object"] == station["intentional_endpoint_pair"][1], "new fixed/moving pipe obstruction")
            detail(contact["detail"])
            base.require(contact["detail"]["strict_crossing_pairs"] > 0, "missing exact service endpoint observation")
    support = station.get("floor_support_samples")
    base.require(type(support) is list and len(support) == 121, "missing supported station route proof")
    for index, row in enumerate(support):
        base.keys(row, ["x_metres", "heights_metres"])
        exact_numbers(row["x_metres"], -22.4167 + (3.6167 * index / 120))
        base.require(type(row["heights_metres"]) is list and len(row["heights_metres"]) == 5, "incomplete station support probes")
        for height in row["heights_metres"]:
            finite(height, -.03, .03)
    negatives(station.get("negative_controls"), STATION_NEGATIVES, detailed=True)
    component = receipt("station-clearance-checks.json")
    count(component.get("schema_version"), 1, 1)
    base.require(component.get("source_sha256") == SOURCES["station"] and component.get("model_sha256") == qualification["station_model_sha256"] and
                 component.get("source_unchanged") is True and component.get("corrections") == closure["corrections"], "station component source/recipe mismatch")
    base.keys(component.get("checks"), ["finite_buffers", "bore_manifold_capped", "identity_three_mesh_glb"])
    base.require(all(v is True for v in component["checks"].values()), "failed component topology/export checks")
    topology = component.get("topology")
    base.require(type(topology) is list and len(topology) == 3, "missing station topology evidence")
    by_id = {c["id"]: c for c in closure["corrections"]}
    seen = set()
    for row in topology:
        base.keys(row, ["id", "before", "after"])
        text(row["id"])
        base.require(row["id"] in by_id and row["id"] not in seen, "duplicate/unknown topology proof")
        seen.add(row["id"])
        correction = by_id[row["id"]]
        for side, sha_key in (("before", "original_geometry_sha256"), ("after", "replacement_geometry_sha256")):
            entry = row[side]
            base.require(type(entry) is dict and entry.get("sha256") == correction[sha_key], "topology geometry digest mismatch")
            count(entry.get("vertices"), 1)
            count(entry.get("triangles"), 1)
            count(entry.get("boundary_edges"))
            count(entry.get("nonmanifold_edges"), 0, 0)
            finite(entry.get("volume_cubic_metres"), 0, 100)
        if correction["kind"] == "pipe_route":
            base.require(row["before"]["boundary_edges"] == row["after"]["boundary_edges"] == 16, "changed pipe endpoint topology")
        else:
            base.require(row["before"]["boundary_edges"] == row["after"]["boundary_edges"] == 0, "uncapped/nonmanifold service bore")
            expected_volume = 128 * .020 ** 2 * math.sin(2 * math.pi / 128) * .5 * (correction["bore_depth_interval_metres"][1] - correction["bore_depth_interval_metres"][0])
            exact_numbers(row["before"].get("bore_expected_removal_cubic_metres"), expected_volume)
            actual = row["after"].get("bore_actual_removal_cubic_metres")
            finite(actual, 0, 1)
            base.require(abs(actual - expected_volume) <= 1e-7 and
                         abs(row["before"]["volume_cubic_metres"] - row["after"]["volume_cubic_metres"] - actual) <= 1e-7,
                         "service bore removed unexpected surrounding volume")
    bounds = receipt("bounds-proof.json")
    source_receipt(bounds)
    base.require(bounds.get("geometry_pass") is True, "failed full shaft/standing volume proof")
    shaft = bounds.get("shaft")
    base.keys(shaft, ["dimensions_metres", "foot_clearance_metres", "foot_interval_metres", "continuous_union_bounds_station_metres", "contacts"])
    exact_numbers(shaft["dimensions_metres"], [.60, .40, 1.95])
    exact_numbers(shaft["foot_clearance_metres"], .040)
    exact_numbers(shaft["foot_interval_metres"], [0, -4.357])
    exact_numbers(shaft["continuous_union_bounds_station_metres"], [[-23.32, -4.317, -.30], [-22.92, 1.99, .30]])
    base.require(shaft["contacts"] == [], "obstructed full descent")
    standing = bounds.get("standing_transition")
    base.keys(standing, ["dimensions_metres", "foot_clearance_metres", "bounds_station_metres", "contacts"])
    exact_numbers(standing["dimensions_metres"], [.64, .64, 1.93])
    exact_numbers(standing["foot_clearance_metres"], .045)
    exact_numbers(standing["bounds_station_metres"], [[-23.44, -4.312, -.32], [-22.80, -2.382, .32]])
    base.require(standing["contacts"] == [], "obstructed standing expansion")
    inner = bounds.get("inner_standing_sweep")
    base.keys(inner, ["bounds_craft_metres", "cases", "floor_support", "threshold_missing_support_interval_metres", "threshold_probe_heights_metres"])
    exact_numbers(inner["bounds_craft_metres"], [[-.32, -.055, 3.48], [.32, 1.875, 5.52]])
    base.require(type(inner["cases"]) is list and len(inner["cases"]) == 2, "missing inner standing reservations")
    for index, case in enumerate(inner["cases"]):
        base.keys(case, ["roof_progress", "inner_progress", "contacts"])
        exact_numbers(case["roof_progress"], index)
        exact_numbers(case["inner_progress"], 1)
        base.require(case["contacts"] == [], "obstructed inner standing reservation")
    supports = inner["floor_support"]
    base.require(type(supports) is list and len(supports) == 61, "missing inner floor observations")
    missing = False
    for index, row in enumerate(supports):
        base.keys(row, ["craft_back_axis_metres", "heights_above_foot_metres"])
        exact_numbers(row["craft_back_axis_metres"], 3.8 + 1.4 * index / 60)
        heights = row["heights_above_foot_metres"]
        base.require(type(heights) is list and len(heights) == 5, "incomplete inner support probes")
        for height in heights:
            if height is None:
                missing = True
            else:
                finite(height, -.03, .03)
    base.require(missing and bounds.get("floor_support_complete") is False and bounds.get("pass") is False,
                 "known threshold support limitation was erased/contradicted")
    exact_numbers(inner["threshold_missing_support_interval_metres"], [3.6305, 3.6644])
    heights = inner["threshold_probe_heights_metres"]
    base.require(type(heights) is list and len(heights) == 101, "missing threshold depth measurements")
    for index, row in enumerate(heights):
        base.keys(row, ["craft_back_axis_metres", "height_relative_to_nominal_foot_metres"])
        x = 3.6 + .1 * index / 100
        exact_numbers(row["craft_back_axis_metres"], x)
        exact_numbers(row["height_relative_to_nominal_foot_metres"], -.0775 if 3.6305 < x < 3.6644 else 0.)
    base.require(any("77.5mm" in line and "no completed supported cabin traversal" in line for line in qualification["limits"]),
                 "qualification omits existing threshold/body-entry limitation")
    negatives(bounds.get("negative_controls"), BOUNDS_NEGATIVES)
    closed = bounds.get("closed_inner_contacts")
    base.require(type(closed) is list and 1 <= len(closed) <= 64, "closed-door volume negative missing")
    for row in closed:
        base.keys(row, ["object", "triangles"])
        text(row["object"])
        base.require(("craft", row["object"]) in source_ids, "unknown closed-door control object")
        count(row["triangles"], 1)
    negatives(receipt("negative-controls.json").get("negative_controls"), BOUNDS_NEGATIVES)
    runtime = receipt("runtime-station-proof.json")
    base.require(receipts["runtime-station-proof.json"]["sha256"] == qualification["runtime_station_proof_sha256"] and
                 runtime.get("glb_sha256") == closure["model_sha256"] and runtime.get("glb_unchanged") is True and
                 runtime.get("problems") == [], "failed/stale runtime station proof")
    count(runtime.get("count"), 17, 17)
    records = runtime.get("records")
    base.require(type(records) is list and len(records) == 17, "missing runtime binding proof")
    binding_by_name = {b["source_object"]: b for b in closure["bindings"]}
    seen = set()
    for row in records:
        base.require(type(row) is dict, "invalid runtime binding proof")
        name = row.get("source_name")
        text(name)
        base.require(name in binding_by_name and name not in seen, "unknown/duplicate runtime source binding")
        seen.add(name)
        binding = binding_by_name[name]
        base.require(row.get("name") == binding["runtime_node"] and row.get("parent") == binding["runtime_parent"], "runtime binding name/parent drift")
        metadata = row.get("metadata")
        expected_extras = binding["expected_extras"]
        base.require(type(metadata) is dict and type(metadata.get("extras")) is dict and type(expected_extras) is dict,
                     "runtime binding identity/extras dimension")
        for key, value in expected_extras.items():
            base.require(key in {"dock_id", "dock_part", "dock_leaf_index", "role", "hatch_side"}, "unknown runtime identity extra")
            actual = metadata["extras"].get(key)
            if key in {"dock_leaf_index", "hatch_side"}:
                finite(value, -1000, 1000)
                finite(actual, -1000, 1000)
                base.require(value == round(value) and actual == round(actual), "fractional runtime identity extra")
            else:
                text(value)
                text(actual)
            base.require(actual == value, "runtime binding identity/extras drift")
        exact_numbers(row.get("transform_columns"), binding["rest_transform"])
        finite(row.get("maximum_local_transform_error"), 0, .00001)
        finite(row.get("maximum_world_transform_error"), 0, .0001)
    crossing = receipt("crossing-analysis.json")
    count(crossing.get("schema_version"), 1, 1)
    base.require(crossing.get("sources") == CONTACT_SOURCES, "crossing evidence source mismatch")
    finite(crossing.get("tolerance_metres"), .000001, .000001)
    crossing_sweeps = crossing.get("sweeps")
    base.require(type(crossing_sweeps) is list and all(type(row) is dict for row in crossing_sweeps), "invalid crossing sweep structure")
    for row in crossing_sweeps:
        base.keys(row, ["owner", "channel", "samples", "pairs"])
    crossed = sweep_map([dict(row, method="crossing") for row in crossing_sweeps])
    for identity, sweep in crossed.items():
        rows = sweep.get("pairs")
        base.require(type(rows) is list, "missing exact crossing pair observations")
        expected = {(r["object_a"], r["object_b"]): r for r in raw_sweeps[identity]["contacts"]}
        seen = set()
        for row in rows:
            base.keys(row, ["object_a", "object_b", "first_strict_crossing_progress", "last_strict_crossing_progress",
                "strict_crossing_samples", "maximum_triangle_plane_crossing_depth_m", "representative_crossing_points_owner_m", "representative_crossing_points_blender_m"])
            text(row["object_a"])
            text(row["object_b"])
            key = (row["object_a"], row["object_b"])
            base.require(key in expected and key not in seen, "unknown/duplicate crossing pair")
            seen.add(key)
            count(row["strict_crossing_samples"], 0, expected[key]["samples"])
            finite(row["maximum_triangle_plane_crossing_depth_m"], 0, 100)
            if row["strict_crossing_samples"]:
                finite(row["first_strict_crossing_progress"], expected[key]["first_progress"], expected[key]["last_progress"])
                finite(row["last_strict_crossing_progress"], row["first_strict_crossing_progress"], expected[key]["last_progress"])
            else:
                base.require(row["first_strict_crossing_progress"] is None and row["last_strict_crossing_progress"] is None, "invented crossing interval")
            for field in ("representative_crossing_points_owner_m", "representative_crossing_points_blender_m"):
                base.require(type(row[field]) is list and len(row[field]) <= 12, "invalid crossing point dimensions")
                for point in row[field]:
                    vector(point)
        base.require(seen == set(expected), "missing crossing pair observations")
    cube_controls = crossing.get("negative_controls")
    base.require(type(cube_controls) is list and len(cube_controls) == 3, "missing intersection kernel controls")
    seen = set()
    for row in cube_controls:
        base.keys(row, ["name", "result"])
        base.require(type(row["name"]) is str and row["name"] in {"separated", "touching", "crossing"} and row["name"] not in seen, "duplicate/unknown kernel control")
        seen.add(row["name"])
        detail(row["result"])
        base.require(row["result"]["strict_crossing_pairs"] > 0 if row["name"] == "crossing" else
                     row["result"]["strict_crossing_pairs"] == 0, "failed touching/crossing distinction")
        if row["name"] == "separated":
            base.require(row["result"]["bvh_triangle_pairs"] == 0, "separated surfaces falsely intersect")
    base.keys(crossing.get("inner_opening"), ["0.0", "1.0"])
    for name, opening in crossing["inner_opening"].items():
        base.keys(opening, ["central_panel_gap_m", "width_reservation_m"])
        finite(opening["central_panel_gap_m"], 0, 2)
        exact_numbers(opening["width_reservation_m"], .64)
        base.require(opening["central_panel_gap_m"] >= .64 if name == "1.0" else opening["central_panel_gap_m"] <= .01,
                     "unqualified useful door aperture")
    phase = receipt("lock-qualified-phase.json")
    count(phase.get("schema_version"), 1, 1)
    base.require(phase.get("rotating_pin_crossings") == [] and phase.get("pin_bolt_crossing_samples") == [] and
                 phase.get("corrections") == spec["derivative_corrections"], "failed/stale seat lock phase proof")
    samples = phase.get("samples")
    base.require(type(samples) is list and len(samples) == 201, "missing complete lock phase samples")
    for index, row in enumerate(samples):
        base.keys(row, ["progress", "pin_crossing_objects", "seat_rotation_radians"])
        p = index / 200
        exact_numbers(row["progress"], p)
        contacts = row["pin_crossing_objects"]
        base.require(type(contacts) is list and len(contacts) <= 16, "invalid seat phase contacts")
        for name in contacts:
            text(name)
            base.require(name in source_names_for(spec), "unknown seat phase contact object")
        base.require(len(set(contacts)) == len(contacts), "duplicate seat phase contact")
        base.require("Swivel mounting bolt" not in contacts and (not .60 < p < .95 or set(contacts) <= {"Fixed swivel housing"}), "free-turn or bolt pin obstruction")
        exact_numbers(row["seat_rotation_radians"], min(1, max(0, (p - .60) / .35)) * math.pi / 2)
    fit = phase.get("fit_measurements")
    base.require(type(fit) is dict, "missing seat lock fit measurements")
    for key in ("pin_bolt_perpendicular_interval_gap_metres", "bolt_neighbor_aabb_separation_lower_bound_metres", "withdrawn_pin_housing_radial_overlap_metres"):
        finite(fit.get(key), .001, 1)
    count(fit.get("mounting_bolt_count"), 12, 12)
    base.require(fit.get("housing_modifiers") == [], "unreviewed seat housing modifier")
    seat = receipt("seat-corrected-complete.json")
    base.require(seat.get("source_sha256") == SOURCES["wayfarer"] and seat.get("source_unchanged") is True and
                 type(seat.get("probes")) is list and len(seat["probes"]) == 1, "stale/missing complete seat sweep")
    probe = seat["probes"][0]
    base.keys(probe, ["name", "samples", "crossings"])
    base.require(probe["name"] == "seat_corrected_complete_070" and type(probe["samples"]) is list and len(probe["samples"]) == 201, "incomplete seat sequence")
    for index, p in enumerate(probe["samples"]):
        exact_numbers(p, index / 200)
    allowed = {(r["object_a"], r["object_b"]) for r in sweeps[("craft", "seat_boarding")]["intentional_contacts"]}
    base.require(type(probe["crossings"]) is list and len(probe["crossings"]) <= 256, "invalid seat crossing report")
    seen = set()
    for row in probe["crossings"]:
        base.keys(row, ["a", "b", "first", "last", "depth", "points"])
        text(row["a"])
        text(row["b"])
        key = (row["a"], row["b"])
        base.require(key in allowed and key not in seen, "unexplained/duplicate complete seat crossing")
        seen.add(key)
        finite(row["first"], 0, 1)
        finite(row["last"], row["first"], 1)
        finite(row["depth"], 0, 1)
        base.require(type(row["points"]) is list and len(row["points"]) <= 12, "invalid seat crossing points")
        for point in row["points"]:
            vector(point)


def source_names_for(spec):
    return {row["source_object"] for row in spec["roster"]["included"]}


def validate_metadata(root, manifest, roster):
    data = {name: load_json(root / "metadata" / name) for name in METADATA}
    spec, contact, closure, qualification, provenance = (data[name] for name in
        ("wayfarer-operating-02.json", "contact.json", "station-closure.json", "qualification.json", "provenance.json"))
    base.keys(spec, ["schema_version", "id", "units", "axes", "source", "exporter", "model", "groups", "channels",
                     "poses", "anchors", "roster", "gear_preview", "gear_preview_samples", "screens",
                     "seat_adjustment_metres", "licenses", "limits", "derivative_corrections"])
    base.require(type(spec) is dict and type(spec.get("schema_version")) is int and spec.get("schema_version") == 1 and
                 spec.get("id") == PACKAGE_ID and spec.get("units") == "metres" and
                 spec.get("axes") == "Godot +Y up, -Z forward", "unsupported operating manifest")
    base.require(type(spec["source"]) is dict and spec["source"].get("sha256") == SOURCES["wayfarer"] and
                 spec.get("model") == {"file": MODEL, "sha256": manifest["model"]["sha256"]}, "operating model/source mismatch")
    groups = spec.get("groups")
    base.require(type(groups) is list and len(groups) == len(RUNTIME_NODES), "missing motion groups")
    group_ids, moving_objects = set(), {}
    for group in groups:
        base.keys(group, ["id", "runtime_node", "source_rig", "source_parent", "source_rest_transform",
                          "runtime_rest_transform", "source_objects", "contact_role"])
        ident = group["id"]
        base.require(type(ident) is str and ident in RUNTIME_NODES and ident not in group_ids and
                     group["runtime_node"] == RUNTIME_NODES[ident] and
                     group["source_rig"] == next(name for name, value in MOTION_RIGS.items() if value == ident),
                     "duplicate/unknown motion group")
        group_ids.add(ident)
        transform(group["source_rest_transform"])
        transform(group["runtime_rest_transform"])
        base.require(all(abs(a - b) <= 1e-7 for column, target in zip(group["runtime_rest_transform"],
                     [[1, 0, 0], [0, 1, 0], [0, 0, 1], [0, 0, 0]]) for a, b in zip(column, target)),
                     "moving rest must be flat identity")
        objects = group["source_objects"]
        base.require(type(objects) is list and 1 <= len(objects) <= 2000, "invalid source-object count")
        for name in objects:
            base.require(type(name) is str and name and name not in moving_objects, "duplicate source object")
            moving_objects[name] = ident
    def group_poses(poses):
        base.keys(poses, group_ids)
        for pose in poses.values():
            transform(pose)
    base.keys(spec["poses"], POSE_IDS)
    for poses in spec["poses"].values():
        group_poses(poses)
    channels = spec["channels"]
    base.require(type(channels) is list and len(channels) == len(CHANNEL_IDS), "missing motion channels")
    channel_ids = set()
    for channel in channels:
        base.keys(channel, ["id", "samples"])
        base.require(type(channel["id"]) is str and channel["id"] in CHANNEL_IDS and
                     channel["id"] not in channel_ids and type(channel["samples"]) is list and
                     len(channel["samples"]) == 21, "invalid motion channel")
        channel_ids.add(channel["id"])
        for index, sample in enumerate(channel["samples"]):
            base.keys(sample, ["progress", "transforms"])
            finite(sample["progress"], 0, 1)
            base.require(abs(sample["progress"] - index / 20) < 0.0000001, "unordered motion samples")
            group_poses(sample["transforms"])
    base.keys(spec["anchors"], ["pilot_eye", "boarding_eye", "roof_collar"])
    for anchor in spec["anchors"].values():
        vector(anchor)
    base.keys(spec["roster"], ["included", "excluded"])
    included = spec["roster"]["included"]
    base.require(type(included) is list and len(included) == 1746, "incomplete selected-source roster")
    source_names = set()
    static_nodes = {"HopperStructure", "HopperGlass", "HopperEnginePort", "HopperEngineStarboard"} | {
        f"HopperGear{n:02}" for n in range(30)}
    for entry in included:
        base.keys(entry, ["source_object", "source_type", "source_parent", "runtime_node", "motion_group"])
        name = entry["source_object"]
        base.require(type(name) is str and name and name not in source_names and
                     entry["motion_group"] == moving_objects.get(name), "source roster/motion disagreement")
        motion = entry["motion_group"]
        base.require(entry["runtime_node"] == RUNTIME_NODES[motion] if motion is not None else
                     type(entry["runtime_node"]) is str and entry["runtime_node"] in static_nodes,
                     "source roster/runtime mesh disagreement")
        source_names.add(name)
    base.require(set(moving_objects) <= source_names, "motion source missing from render roster")
    corrections = spec["derivative_corrections"]
    base.keys(corrections, ["schema_version", "axes", "operations"])
    base.require(type(corrections["schema_version"]) is int and corrections["schema_version"] == 1 and
                 corrections["axes"] == "Blender source local", "unsupported derivative correction version")
    expected_operations = {"roof_hinge_link_clearance": 4, "inner_door_stroke": 4,
                           "seat_lock_withdrawal": 1, "seat_lock_bolt_clearance": 1}
    expected_parameters = {
        "roof_hinge_link_clearance": {"inward_metres": .025},
        "inner_door_stroke": {"authored_fraction": .55},
        "seat_lock_withdrawal": {"withdrawal_metres": .07, "withdraw_begin": .50, "withdraw_end": .60,
                                 "turn_begin": .60, "turn_end": .95, "restore_begin": .95, "restore_end": 1.},
        "seat_lock_bolt_clearance": {"pitch_circle_radius_metres": .24, "angle_radians": math.radians(7.5)},
    }
    operations = corrections["operations"]
    base.require(type(operations) is list and len(operations) == 4, "incomplete fit correction recipe")
    operation_ids = set()
    for operation in operations:
        base.keys(operation, ["id", "reason", "source_objects", "parameters"])
        ident = operation["id"]
        base.require(type(ident) is str and ident in expected_operations and ident not in operation_ids,
                     "unknown/duplicate correction operation")
        operation_ids.add(ident)
        records = operation["source_objects"]
        base.require(type(records) is list and len(records) == expected_operations[ident], "missing correction objects")
        names = set()
        for record in records:
            base.keys(record, ["source_object", "source_parent", "original_rest_local_transform",
                               "operating_rest_local_transform", "local_translation_metres"])
            name = record["source_object"]
            base.require(type(name) is str and name and name not in names, "duplicate correction object")
            names.add(name)
            transform(record["original_rest_local_transform"])
            transform(record["operating_rest_local_transform"])
            vector(record["local_translation_metres"])
            old, new = record["original_rest_local_transform"], record["operating_rest_local_transform"]
            base.require(all(abs(a - b) <= 1e-7 for first, second in zip(old[:3], new[:3])
                             for a, b in zip(first, second)) and
                         all(abs((b - a) - offset) <= 1e-6 for a, b, offset in
                             zip(old[3], new[3], record["local_translation_metres"])),
                         "correction transform/translation disagreement")
        base.keys(operation["parameters"], expected_parameters[ident])
        for name, parameter in operation["parameters"].items():
            finite(parameter, 0, 1)
            base.require(abs(parameter - expected_parameters[ident][name]) <= 1e-6, "unqualified correction parameter")
    base.keys(contact, ["schema_version", "sources", "coordinate_contracts", "quantization_metres", "crop_bounds_metres", "groups"])
    base.require(type(contact["schema_version"]) is int and contact["schema_version"] == 1 and contact["sources"] == CONTACT_SOURCES and
                 contact["quantization_metres"] == 0.000001, "contact source/version mismatch")
    base.keys(contact["crop_bounds_metres"], ["station", "craft"])
    for bounds in contact["crop_bounds_metres"].values():
        base.require(type(bounds) is list and len(bounds) == 2, "invalid crop bounds")
        vector(bounds[0])
        vector(bounds[1])
        base.require(all(a < b for a, b in zip(*bounds)), "invalid crop dimensions")
    geometry = contact["groups"]
    base.require(type(geometry) is list and 1 <= len(geometry) <= 2000, "invalid contact groups")
    ids, source_ids, vertices_total, triangles_total = set(), set(), 0, 0
    for group in geometry:
        base.keys(group, ["id", "owner", "motion_group", "source_objects", "vertices_micrometres", "triangles"])
        ident, owner, motion = group["id"], group["owner"], group["motion_group"]
        base.require(type(ident) is str and ident and ident not in ids and owner in ("station", "craft"), "duplicate/invalid contact group")
        ids.add(ident)
        base.require(motion is None or type(motion) is str, "invalid contact motion identity")
        if owner == "craft":
            base.require(motion is None or motion in group_ids, "unknown contact motion identity")
        else:
            base.require(motion is None or motion in {f"station_d1_{n:02}" for n in range(17)},
                         "unknown station contact motion identity")
        objects = group["source_objects"]
        base.require(type(objects) is list and 1 <= len(objects) <= 2000, "invalid contact source roster")
        for name in objects:
            base.require(type(name) is str and name and (owner, name) not in source_ids, "duplicate contact source object")
            source_ids.add((owner, name))
            if owner == "craft":
                base.require(name in source_names and moving_objects.get(name) == motion, "contact/render ancestry disagreement")
        vertices, triangles = group["vertices_micrometres"], group["triangles"]
        base.require(type(vertices) is list and 1 <= len(vertices) <= 4000000 and
                     type(triangles) is list and 1 <= len(triangles) <= 4000000, "invalid contact buffer size")
        vertices_total += len(vertices)
        triangles_total += len(triangles)
        base.require(vertices_total <= 4000000 and triangles_total <= 4000000, "contact geometry budget exceeded")
        for vertex in vertices:
            base.require(type(vertex) is list and len(vertex) == 3 and
                         all(type(v) is int and abs(v) <= 100000000 for v in vertex), "invalid contact vertex")
        for triangle in triangles:
            base.require(type(triangle) is list and len(triangle) == 3 and
                         all(type(index) is int and 0 <= index < len(vertices) for index in triangle), "contact index outside buffer")
    base.keys(closure, ["schema_version", "source_sha256", "closure_source_sha256", "model_sha256",
                       "coordinate_contract", "timeline_frames", "attached_closed_progress", "bindings",
                       "correction_coordinate_contract", "corrections"])
    base.require(type(closure["schema_version"]) is int and closure["schema_version"] == 1 and closure["source_sha256"] == SOURCES["station"] and
                 closure["closure_source_sha256"] == SOURCES["station_closure"] and
                 closure["model_sha256"] == "79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80",
                 "station closure source/model mismatch")
    finite(closure["attached_closed_progress"], 0, 1)
    base.require(closure["timeline_frames"] == [110, 300], "stale station timeline")
    bindings = closure["bindings"]
    base.require(type(bindings) is list and len(bindings) == 17, "missing station controls")
    binding_ids, node_names = set(), set()
    for binding in bindings:
        base.keys(binding, ["id", "source_object", "runtime_node", "runtime_parent", "expected_extras", "rest_transform", "knots"])
        base.require(type(binding["id"]) is str and binding["id"] not in binding_ids and
                     type(binding["runtime_node"]) is str and binding["runtime_node"] not in node_names, "ambiguous station control")
        binding_ids.add(binding["id"])
        node_names.add(binding["runtime_node"])
        transform(binding["rest_transform"])
        knots = binding["knots"]
        base.require(type(knots) is list and 2 <= len(knots) <= 64, "invalid closure knots")
        last = -1
        for knot in knots:
            base.keys(knot, ["progress", "transform"])
            finite(knot["progress"], 0, 1)
            base.require(knot["progress"] > last, "unordered closure knots")
            last = knot["progress"]
            transform(knot["transform"])
        base.require(knots[0]["progress"] == 0 and last == 1, "incomplete closure channel")
    base.require(binding_ids == {f"station_d1_{n:02}" for n in range(17)}, "stale station binding identities")
    corrections = closure["corrections"]
    base.require(type(closure["correction_coordinate_contract"]) is str and
                 closure["correction_coordinate_contract"] and type(corrections) is list and len(corrections) == 3,
                 "missing station service clearance recipe")
    replacements, corrected_sources = set(), set()
    for correction in corrections:
        base.keys(correction, ["id", "filename", "model_sha256", "source_object", "runtime_node", "runtime_parent",
                               "expected_extras", "rest_transform", "replacement_node", "original_geometry_sha256",
                               "replacement_geometry_sha256", "surface_count", "kind", "original_points_metres",
                               "replacement_points_metres", "tube_radius_metres", "bore_centre_metres",
                               "bore_radius_metres", "bore_depth_interval_metres"])
        base.require(correction["filename"] == STATION_MODEL and
                     correction["model_sha256"] == manifest["station_model"]["sha256"], "station replacement model mismatch")
        for key in ("id", "source_object", "runtime_node", "runtime_parent", "replacement_node", "kind"):
            base.require(type(correction[key]) is str and correction[key], "invalid correction identity")
        base.require(correction["source_object"] not in corrected_sources and
                     correction["replacement_node"] not in replacements and
                     type(correction["surface_count"]) is int and correction["surface_count"] == 1,
                     "duplicate/invalid replacement binding")
        corrected_sources.add(correction["source_object"])
        replacements.add(correction["replacement_node"])
        base.digest(correction["original_geometry_sha256"])
        base.digest(correction["replacement_geometry_sha256"])
        transform(correction["rest_transform"])
        vector(correction["bore_centre_metres"])
        finite(correction["tube_radius_metres"], 0, .05)
        finite(correction["bore_radius_metres"], 0, .05)
        for key in ("original_points_metres", "replacement_points_metres"):
            points = correction[key]
            base.require(type(points) is list and len(points) <= 16, "invalid service route points")
            for point in points:
                vector(point)
        depth = correction["bore_depth_interval_metres"]
        base.require(type(depth) is list and len(depth) in (0, 2), "invalid bore depth dimension")
        if depth:
            for value in depth:
                finite(value)
            base.require(depth[0] < depth[1] and abs(correction["bore_radius_metres"] - .020) < 1e-8,
                         "invalid service bore dimensions")
        else:
            old, new = correction["original_points_metres"], correction["replacement_points_metres"]
            base.require(len(old) == 4 and len(new) == 7 and old[0] == new[0] and old[-1] == new[-1] and
                         abs(correction["tube_radius_metres"] - .018) < 1e-8, "changed service endpoints/radius")
    base.require(replacements == {"D1_CLEARANCE_PIPE", "D1_CLEARANCE_DECK", "D1_CLEARANCE_FLANGE"},
                 "unexpected station replacement roster")
    validate_qualification(qualification, provenance, manifest, roster, source_ids, closure, spec)
    proof = provenance.get("export_checks", {})
    base.require(type(proof) is dict and proof.get("pass") is True and proof.get("source_unchanged") is True and
                 proof.get("source_sha256") == SOURCES["wayfarer"] and
                 proof.get("model_sha256") == manifest["model"]["sha256"] and
                 proof.get("manifest_sha256") == roster["metadata/wayfarer-operating-02.json"]["sha256"], "export qualification mismatch")
    exporter = spec["exporter"]
    aliases = {"sha256": "export_wayfarer_operating.py", "base_sha256": "wayfarer_flight_export_base.py",
               "classification_sha256": "wayfarer_operating_spec.py", "pose_helper_sha256": "wayfarer_operating_blender.py",
               "validation_helper_sha256": "wayfarer_gltf_checks.py", "binary_audit_sha256": "wayfarer_operating_glb_audit.py"}
    base.keys(exporter, ["path", *aliases])
    base.require(exporter["path"] == "tools/export_wayfarer_operating.py" and
                 exporter["base_sha256"] == "762499e37fe13a33bf297fc58801e711590cd9d56058a6462b3467ff6b7102fe" and
                 proof.get("exporter") == exporter and proof.get("derivative_corrections") == spec["derivative_corrections"],
                 "stale producer recipe qualification")
    tools = provenance.get("tool_sha256")
    base.require(type(tools) is dict, "missing tool identities")
    for name, value in tools.items():
        base.relative(name)
        base.digest(value)
    for field, name in aliases.items():
        base.digest(exporter[field])
        base.require(tools.get(name) == exporter[field], "producer/provenance tool mismatch")
    poses = proof.get("source_pose_proof")
    expected_poses = {f"{channel}:{n}" for channel in CHANNEL_IDS for n in range(21)} | set(POSE_IDS)
    base.require(type(poses) is list and len(poses) == len(expected_poses), "missing source pose proof")
    names = set()
    for pose in poses:
        base.keys(pose, ["pose", "maximum_source_matrix_error"])
        base.require(type(pose["pose"]) is str and pose["pose"] in expected_poses and pose["pose"] not in names,
                     "duplicate/unknown source pose proof")
        names.add(pose["pose"])
        finite(pose["maximum_source_matrix_error"], 0, 0.000005)
    geometry_hashes(proof.get("mesh_buffer_sha256"))


def load_json(path, limit=MAX_METADATA):
    with path.open("rb") as stream:
        raw = stream.read(limit + 1)
    base.require(len(raw) <= limit, "metadata size exceeds limit")
    # Bound nesting before invoking the recursive decoder, including unknown
    # values. Ignore braces inside strings and honor escaped backslashes/quotes.
    depth, quoted, escaped = 0, False, False
    for byte in raw:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
        elif byte in (91, 123):
            depth += 1
            base.require(depth <= 64, "metadata nesting exceeds limit")
        elif byte in (93, 125):
            depth -= 1
            base.require(depth >= 0, "unbalanced metadata")
    base.require(depth == 0 and not quoted, "unbalanced metadata")

    def invalid(value):
        raise ValueError("non-finite JSON constant: " + value)

    def finite(value):
        number = float(value)
        base.require(math.isfinite(number), "non-finite JSON number")
        return number

    return json.loads(raw.decode("utf-8"), object_pairs_hook=base.pairs_unique,
                      parse_constant=invalid, parse_float=finite)


def verify(package):
    root = base.no_symlinks(package)
    base.require(root.is_dir(), "operating package directory is missing")
    manifest_path = base.no_symlinks(root / "package.json")
    manifest = load_json(manifest_path, base.MAX_MANIFEST)
    base.keys(manifest, ["schema", "package_id", "sources", "files", "model", "station_model", "metadata"])
    base.require(manifest["schema"] == SCHEMA and manifest["package_id"] == PACKAGE_ID,
                 "unsupported operating package")
    base.require(manifest["sources"] == SOURCES, "source identity mismatch")
    files = manifest["files"]
    base.require(type(files) is list and 1 <= len(files) <= 32, "invalid hash roster count")
    roster, total = {}, 0
    for record in files:
        base.keys(record, ["path", "bytes", "sha256"])
        name = record["path"]
        relative = base.relative(name)
        base.require(name not in roster and name != "package.json", "duplicate/reserved roster path")
        limit = base.MAX_CHUNK if name.startswith("payloads/") else MAX_METADATA
        base.size(record["bytes"], limit)
        base.digest(record["sha256"])
        actual = base.no_symlinks(root / relative)
        base.require(actual.is_file() and actual.stat().st_size == record["bytes"], "missing/truncated file: " + name)
        base.require(base.sha(actual) == record["sha256"], "package hash mismatch: " + name)
        roster[name] = record
        total += record["bytes"]
    base.require(total <= MAX_TOTAL, "total package budget exceeded")
    names = set()
    for entry in root.rglob("*"):
        base.require(not entry.is_symlink() and (entry.is_dir() or entry.is_file()), "nonregular package entry")
        if entry.is_file():
            names.add(entry.relative_to(root).as_posix())
    base.require(names == set(roster) | {"package.json"}, "unrostered/missing package files")
    used = set()
    for key, output, license_id, source_hash, limit in (
            ("model", MODEL, LICENSE, SOURCES["wayfarer"], MAX_MODEL),
            ("station_model", STATION_MODEL, "LicenseRef-Apsis-Station-Kit-Output", SOURCES["station"], 8 * 1024 * 1024)):
        model = manifest[key]
        base.keys(model, ["output", "bytes", "sha256", "source_sha256", "license", "chunks"])
        base.require(model["output"] == output and model["license"] == license_id and
                     model["source_sha256"] == source_hash, "unrecognized model/license/source")
        base.size(model["bytes"], limit)
        base.digest(model["sha256"])
        chunks = model["chunks"]
        base.require(type(chunks) is list and 1 <= len(chunks) <= 8, "invalid chunk count")
        for name in chunks:
            base.relative(name)
            base.require(name.startswith("payloads/") and name in roster and name not in used,
                         "missing/reused payload chunk")
            used.add(name)
    metadata = manifest["metadata"]
    base.require(type(metadata) is list and len(metadata) == len(METADATA), "invalid metadata count")
    outputs = set()
    for record in metadata:
        base.keys(record, ["path", "output"])
        output = record["output"]
        base.require(type(output) is str and output in METADATA and output not in outputs and
                     record["path"] == "metadata/" + output and record["path"] in roster,
                     "unknown/duplicate metadata output")
        outputs.add(output)
        load_json(root / record["path"])
        used.add(record["path"])
    used.update("licenses/" + name for name in LICENSES)
    base.require(used == set(roster), "missing license or unconsumed package file")
    for output, expected in SELECTED_SHA256.items():
        base.digest(expected)
        if output == MODEL:
            actual = manifest["model"]["sha256"]
        elif output == STATION_MODEL:
            actual = manifest["station_model"]["sha256"]
        else:
            actual = roster["metadata/" + output]["sha256"]
        base.require(actual == expected, "unqualified operating identity: " + output)
    validate_metadata(root, manifest, roster)
    return root, manifest, base.sha(manifest_path), roster


def prepared_matches(output, manifest, package_hash, roster):
    base.require(load_json(base.no_symlinks(output / "prepared.json")) ==
                 prepared_receipt(manifest, package_hash, roster), "output belongs to a different package")
    for key in ("model", "station_model"):
        model = manifest[key]
        path = base.no_symlinks(output / model["output"])
        base.require(path.is_file() and path.stat().st_size == model["bytes"] and
                     base.sha(path) == model["sha256"], "prepared model changed")
    for record in manifest["metadata"]:
        path = base.no_symlinks(output / record["output"])
        base.require(path.is_file() and base.sha(path) == roster[record["path"]]["sha256"], "prepared metadata changed")
    base.require({path.name for path in output.iterdir()} == METADATA | {MODEL, STATION_MODEL, "prepared.json"},
                 "prepared output contains unowned files")


def prepared_receipt(manifest, package_hash, roster):
    return {"schema": SCHEMA, "package_sha256": package_hash, "sources": SOURCES,
            "files": {MODEL: manifest["model"]["sha256"], STATION_MODEL: manifest["station_model"]["sha256"],
                      **{record["output"]: roster[record["path"]]["sha256"]
                         for record in manifest["metadata"]}}}


def geometry_hashes(value):
    allowed = set(RUNTIME_NODES.values()) | {"HopperStructure", "HopperGlass", "HopperEnginePort", "HopperEngineStarboard"} | {
        f"HopperGear{n:02}" for n in range(30)}
    base.require(type(value) is dict and 1 <= len(value) <= len(allowed), "invalid source geometry hash table")
    for name, digest in value.items():
        base.require(type(name) is str and name in allowed, "unknown source geometry hash node")
        base.digest(digest)
    return value


def validate_decoded(output, metadata_root):
    spec = load_json(metadata_root / "wayfarer-operating-02.json")
    closure = load_json(metadata_root / "station-closure.json")
    proof = load_json(metadata_root / "provenance.json")["export_checks"]
    hashes = geometry_hashes(proof.get("mesh_buffer_sha256"))
    craft = validate_craft_model(output / MODEL, spec)
    validate_station_model(output / STATION_MODEL, closure)
    base.require(proof.get("glb_buffer_audit") == craft["binary_audit"], "decoded binary qualification mismatch")
    bindings = proof.get("glb_bindings")
    base.require(type(bindings) is list and len(bindings) == 13, "missing binary/source bindings")
    actual = {record["group"]: record for record in craft["motion_bindings"]}
    found = set()
    for binding in bindings:
        base.keys(binding, ["group", "source_rig", "glb_node_index", "glb_node_name", "runtime_node_name",
                            "mesh_index", "geometry_sha256"])
        ident = binding["group"]
        base.require(type(ident) is str and ident in actual and ident not in found and
                     {key: value for key, value in binding.items() if key != "geometry_sha256"} == actual[ident],
                     "decoded source/group binding mismatch")
        found.add(ident)
        base.digest(binding["geometry_sha256"])
        base.require(hashes.get(binding["runtime_node_name"]) == binding["geometry_sha256"],
                     "source geometry qualification mismatch")


def prepare(package, destination):
    root, manifest, package_hash, roster = verify(package)
    output = base.no_symlinks(destination)
    base.require(output != root and root not in output.parents and output not in root.parents, "output overlaps package")
    if output.exists():
        base.require(output.is_dir(), "output is not a directory")
        prepared_matches(output, manifest, package_hash, roster)
        return output
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".apsis-operating-", dir=output.parent))
    try:
        for key in ("model", "station_model"):
            base.unpack_model(root, manifest[key], staging / manifest[key]["output"])
        validate_decoded(staging, root / "metadata")
        for record in manifest["metadata"]:
            shutil.copyfile(root / record["path"], staging / record["output"])
        (staging / "prepared.json").write_text(json.dumps(prepared_receipt(manifest, package_hash, roster), indent=2) + "\n")
        prepared_matches(staging, manifest, package_hash, roster)
        base.require(verify(root)[2] == package_hash, "package changed during preparation")
        base.require(not output.exists() and not output.is_symlink(), "output appeared during preparation")
        install_new(staging, output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--verify-only", action="store_true")
    mode.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.verify_only:
            _, manifest, hashed, _ = verify(args.package)
            print(json.dumps({"package_id": manifest["package_id"], "package_sha256": hashed}))
        else:
            print(prepare(args.package, args.output))
    except (OSError, ValueError, EOFError, lzma.LZMAError) as error:
        parser.exit(1, "Operating preparation refused: " + str(error) + "\n")


if __name__ == "__main__":
    main()
