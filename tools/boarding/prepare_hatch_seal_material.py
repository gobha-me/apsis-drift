#!/usr/bin/env python3
"""Prepare immutable SealSweep01 source; never loads Blender or issues material."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import tempfile

import prepare_material_source as base

# Exact bytes from the separately frozen single read-only source capture.
METADATA_SHA = "03f0fe49aa849cbfe20dc063cf5699ac714eac5db461a8b020c30cfe57ab5d88"
BINARY_SHA = "732771d076b0480d8c71ceb6585169488c51556f75be2c4a4aa45ac3641ff235"
SOURCE_SHA = base.SOURCE_SHA
INVENTORY_SHA = "a78800c6f5223323855aae1c37e1b40f7f83af76c709696b4eb0f08f3a9f55d8"
LIFEBOAT_SHA = "1ff8c7de423eeddf100f5a36e1f494225263c342e745361075dbef88c7215db1"
FINISH_SHA = "8c9fe3208c3360c5d96d2fd38847674c5875289e171ea9ca232031b33d78f38f"
RAW_SHA = "1c46cd423627cbdf9f94bb2cbc74f23befe9f8e5ac903d9be195b79d8c346e40"
GRID_SHA = "21c6d9c5dd54af597e8e04a65d9fda80c56c3fa97c45f9ad315c86de9bfbfce1"
NAME = "WF02 | CABIN hatch perimeter seal"
MAX_METADATA = 65536
BINARY_BYTES = 6240
MAX_EMISSION = 1024 * 1024
require = base.require


def canonical(value):
    text = json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False)
    require(len(text.encode()) <= 4096, "canonical constructor packet capacity")
    return text


def scalar(value):
    require(isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value) and abs(value) <= 8, "finite eight-metre workspace")
    return float(value).hex()


def vec(value):
    require(len(value) == 3, "vector dimensions")
    return "RigidVector3{" + ",".join(scalar(x) for x in value) + "}"


def bounds(value, grid=False):
    require(len(value) == 2 and all(len(p) == 3 for p in value), "bound dimensions")
    decoded = [[x * 1e-6 if grid else x for x in p] for p in value]
    require(all(decoded[0][i] <= decoded[1][i] for i in range(3)), "bound order")
    return "BoardingPlantedLegPointBounds{" + ",".join(vec(p) for p in decoded) + "}"


def within_bounds(point, envelope):
    require(len(point) == 3 and len(envelope) == 2
            and all(len(p) == 3 for p in envelope), "coordinate/bound dimensions")
    require(all(envelope[0][i] <= point[i] <= envelope[1][i] for i in range(3)),
            "complete geometry/metadata bound consistency")


def transform(value):
    require(value == [[1, 0, 0], [0, 0, -1], [0, 1, 0], [0, 0, 0]],
            "registered signed-permutation source affine")
    return "OperatingTransform{{" + ",".join(vec(p) for p in value) + "}}"


class Emitter(base.Emitter):
    def write(self, text):
        require(self.bytes + len(text.encode()) <= MAX_EMISSION, "seal emitted-source capacity")
        super().write(text)


def constructor(row):
    settings, spline = row["curve_settings"], row["spline"]
    require(settings["dimensions"] == "3D" and settings["bevel_mode"] == "ROUND"
            and settings["fill_mode"] == "FULL"
            and settings["bevel_object_absent"] is True
            and settings["taper_object_absent"] is True
            and not row["modifiers"], "registered dependency-free ROUND/FULL sweep")
    require(spline["type"] == "POLY" and spline["use_cyclic_u"] is False
            and len(spline["points"]) == 9, "registered noncyclic nine-point POLY")
    points = []
    for p in spline["points"]:
        require(len(p["co_homogeneous"]) == 4 and p["co_homogeneous"][3] == 1
                and p["radius_factor"] == 1 and p["tilt"] == 0,
                "unit homogeneous/radius and zero-tilt source points")
        points.append(p["co_homogeneous"][:3])
        vec(points[-1])
    require(points[0] == points[8] and all(a != b for a, b in zip(points, points[1:])),
            "coincident terminal point and nonzero actual segments")
    scalar(settings["bevel_depth"])
    require(settings["bevel_depth"] > 0, "positive actual bevel depth")
    transform(row["corrected_world_columns"])
    for packet in (settings, spline, row["modifiers"], row["properties"]):
        canonical(packet)
    return points


def emit(document, blob, stream):
    require(document["schema"] == "apsis-drift-hatch-seal-material/1"
            and document["source_sha256"] == SOURCE_SHA
            and document["inventory_sha256"] == INVENTORY_SHA
            and document["base_completion_sha256"] == base.COMPLETION_SHA
            and document["history_lifeboat_sha256"] == LIFEBOAT_SHA
            and document["history_finish_sha256"] == FINISH_SHA
            and document["blender_version"] == "5.2.2 LTS"
            and document["master_unchanged"] is True and document["source_save_count"] == 0
            and document["material_permission"] is False and document["actor_permission"] is False,
            "seal package provenance")
    require(len(blob) == BINARY_BYTES and document["binary_payload"]["bytes"] == BINARY_BYTES
            and document["binary_payload"]["vertices"] == 90
            and document["binary_payload"]["triangles"] == 160
            and document["binary_payload"]["sha256"] == BINARY_SHA,
            "exact one-object binary dimensions")
    require(document["work"] == {"full_mesh_summaries": 1, "ordered_triangle_hash_streams": 2,
        "inherited_verified_crop_faces": 140, "new_crop_face_replays": 0},
        "new work versus inherited crop attribution")
    row = document["object"]
    r, g = row["identity"], row["geometry"]
    require(row["source_index"] == 1441 and r["source_object"] == NAME
            and r["source_type"] == "CURVE" and r["source_parent"] is None
            and r["motion_group"] is None and r["retained_contact_group"] == "craft_fixed"
            and r["retained_original_contact_range"] == {"object": 655,
                "group": "craft_fixed", "triangle_start": 189501, "triangle_count": 140}
            and r["retained_quantized_triangles_verified"] == 140
            and r["absent_from_original_contact_crop"] is False,
            "original seal/current crop identity")
    require(g["evaluated_vertex_count"] == 90 and g["evaluated_triangle_count"] == 160
            and g["evaluated_triangle_ordinal_range"] == [0, 160]
            and g["ordered_triangle_binary64_sha256"] == RAW_SHA
            and g["ordered_triangle_micrometre_sha256"] == GRID_SHA
            and all(g[k] == r[k] for k in (
                "evaluated_vertex_count", "evaluated_triangle_count", "evaluated_triangle_ordinal_range",
                "ordered_triangle_binary64_sha256", "ordered_triangle_micrometre_sha256",
                "full_bounds_corrected_world_metres", "full_bounds_quantized_micrometres"))
            and row["corrected_world_columns"] == r["source_local_blender_to_corrected_world_columns"],
            "complete seal geometry fingerprint/affine identity")
    points = constructor(row)
    offset = 0
    for key, dtype, rows in (("vertices_binary64", "<f8", 90),
                             ("vertices_quantized_micrometres", "<i8", 90),
                             ("triangle_indices", "<i4", 160)):
        d = g[key]
        require(d["offset"] == offset and d["rows"] == rows, "contiguous fixed one-mesh packet")
        require(sum(1 for _ in base.array(blob, d, dtype)) == rows, "actual packet row count")
        offset += d["bytes"]
    require(offset == BINARY_BYTES, "no duplicate/trailing geometry")
    out = Emitter(stream)
    out.write('// Pinned hatch-seal material01; generated, immutable.\n'
              '#include "origin_boarding_hatch_seal_material_prepared.hpp"\n'
              'namespace apsis_drift::detail { namespace {\n'
              'static const std::array<RigidVector3,90> raw{{\n')
    for p in base.array(blob, g["vertices_binary64"], "<f8"):
        within_bounds(p, g["full_bounds_corrected_world_metres"])
        out.write(vec(p) + ',\n')
    out.write('}};\nstatic const std::array<MaterialQuantizedPoint,90> q{{\n')
    for p, grid in zip(base.array(blob, g["vertices_binary64"], "<f8"),
                       base.array(blob, g["vertices_quantized_micrometres"], "<i8")):
        require(all(abs(x) <= 8000000 for x in grid)
                and tuple(round(x * 1e6) for x in p) == grid, "nearest-grid mapping/workspace")
        within_bounds(grid, g["full_bounds_quantized_micrometres"])
        out.write('MaterialQuantizedPoint{{' + ','.join(map(str, grid)) + '}},\n')
    out.write('}};\nstatic const std::array<MaterialTriangle,160> triangles{{\n')
    for indices in base.array(blob, g["triangle_indices"], "<i4"):
        require(all(0 <= i < 90 for i in indices), "full seal face index bounds")
        out.write('MaterialTriangle{{' + ','.join(map(str, indices)) + '}},\n')
    out.write('}};\n')
    identity_strings = [NAME, "CURVE", "", RAW_SHA, GRID_SHA]
    out.write('static const MaterialSourceRecord source{' + ','.join(base.quote(s) for s in identity_strings)
        + ',' + bounds(g["full_bounds_corrected_world_metres"]) + ','
        + bounds(g["full_bounds_quantized_micrometres"], True)
        + ',655,0,-1,-1,189501,140,BoardingInitialMaterialRelation::unknown,false,false,false};\n'
        + 'static const MaterialMeshRecord mesh{1441,raw,q,triangles};\n')
    packets = [canonical(row[k]) for k in ("curve_settings", "spline", "modifiers", "properties")]
    out.write('static const HatchSealSweepConstructorRecord constructor{{{'
        + ','.join(vec(p) for p in points) + '}},' + transform(row["corrected_world_columns"])
        + ',' + scalar(row["curve_settings"]["bevel_depth"]) + ','
        + ','.join(base.quote(p) for p in packets) + '};\n')
    pins = [METADATA_SHA, BINARY_SHA, SOURCE_SHA, INVENTORY_SHA, LIFEBOAT_SHA, FINISH_SHA]
    string_bytes = sum(len(s.encode()) + 1 for s in identity_strings + packets + pins)
    out.write('constexpr std::size_t storage_bytes = ' + str(string_bytes)
        + '+sizeof(raw)+sizeof(q)+sizeof(triangles)+sizeof(source)+sizeof(mesh)+sizeof(constructor)'
        + '+sizeof(HatchSealMaterialPreparedView);\nstatic_assert(storage_bytes <= 65536);\n}\n'
        + 'HatchSealMaterialPreparedView origin_boarding_hatch_seal_material_prepared() {\n'
        + 'return {&source,&mesh,&constructor,storage_bytes,' + ','.join(base.quote(p) for p in pins)
        + '};\n}\n}\n')
    return {"schema": "apsis-drift-prepared-hatch-seal-material/1",
        "vertices": 90, "triangles": 160, "binary_bytes": BINARY_BYTES,
        "emitted_bytes": out.bytes, "string_storage_bytes": string_bytes,
        "metadata_sha256": METADATA_SHA, "binary_sha256": BINARY_SHA,
        "source_permission": False, "material_permission": False, "actor_permission": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("completion", type=Path, nargs="?")
    parser.add_argument("binary", type=Path, nargs="?")
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    require(len(METADATA_SHA) == 64 and len(BINARY_SHA) == 64, "frozen seal capture pins not installed")
    require(all((args.completion, args.binary, args.output)), "three input/output paths required")
    document = json.loads(base.pinned(args.completion, MAX_METADATA, METADATA_SHA))
    blob = base.pinned(args.binary, BINARY_BYTES, BINARY_SHA)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=args.output.parent,
                                     delete=False) as stream:
        temporary = Path(stream.name)
        try:
            receipt = emit(document, blob, stream)
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    temporary.replace(args.output)
    receipt["prepared_sha256"] = hashlib.sha256(args.output.read_bytes()).hexdigest()
    receipt["preparer_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    args.output.with_suffix(".json").write_text(json.dumps(receipt, indent=2) + "\n")


def self_test():
    """Pure malformed representation controls; no source or inclusion query."""
    checks = 0

    def refuses(action):
        nonlocal checks
        try:
            action()
        except (ValueError, TypeError, KeyError, OverflowError):
            checks += 1
            return
        raise AssertionError("Malformed seal codec accepted")

    data = struct.pack("<3d", 1., 2., 3.)
    desc = dict(offset=0, bytes=24, rows=1, columns=3, dtype="<f8",
                sha256=hashlib.sha256(data).hexdigest())
    assert list(base.array(data, desc, "<f8")) == [(1., 2., 3.)]
    checks += 1
    for bad in ({"offset": -1}, {"bytes": 25}, {"rows": 2}, {"columns": 4},
                {"dtype": "<i8"}, {"sha256": "0" * 64}):
        refuses(lambda b=bad: list(base.array(data, desc | b, "<f8")))
    refuses(lambda: list(base.array(data[:-1], desc, "<f8")))
    for value in (float("nan"), float("inf"), 8.00001, True):
        refuses(lambda v=value: scalar(v))
    refuses(lambda: bounds([[1., 0., 0.], [0., 1., 1.]]))
    refuses(lambda: transform([[0., 0., 0.]] * 4))
    refuses(lambda: canonical({"x": "x" * 4096}))

    class Short(io.StringIO):
        def write(self, text):
            return len(text) - 1

    refuses(lambda: Emitter(Short()).write("packet"))
    emitter = Emitter(io.StringIO())
    emitter.bytes = MAX_EMISSION
    refuses(lambda: emitter.write("x"))
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "input"
        path.write_bytes(data)
        refuses(lambda: base.pinned(path, 23, desc["sha256"]))
        refuses(lambda: base.pinned(path, 24, "0" * 64))
        assert base.pinned(path, 24, desc["sha256"]) == data
        checks += 1
    print(f"{checks} pure seal codec controls passed; no source/material queries")


if __name__ == "__main__":
    main()
