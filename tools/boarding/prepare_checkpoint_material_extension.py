#!/usr/bin/env python3
"""Decode the pinned extension01 package into one immutable C++ provider.

No Blender, authoring master, runtime JSON, old geometry duplication or material
permission is involved. The two package pins are installed only after the
separately frozen read-only capture completes.
"""
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

# Exact bytes from the single separately registered read-only capture.
EXTENSION_METADATA_SHA = "bbd9c53400d3d82f6bd90631178847467c9db15e297fbb42f627b6b419a9d069"
EXTENSION_BINARY_SHA = "1bccd3451558ad0b6938ad74426746519983b59cf5d6222dd42843f849b45bb9"
SOURCE_SHA = base.SOURCE_SHA
INVENTORY_SHA = "a78800c6f5223323855aae1c37e1b40f7f83af76c709696b4eb0f08f3a9f55d8"
HISTORY_SHA = "1ff8c7de423eeddf100f5a36e1f494225263c342e745361075dbef88c7215db1"
SCHEMA = "apsis-drift-checkpoint-material-extension/1"
MAX_METADATA = 65536
MAX_BINARY = 231972
MAX_SOURCE = 262144
MAX_EMISSION = 4 * 1024 * 1024
SELECTED = (
    (1436, "WF02 | CABIN emergency pressure frame", 256, 512, 650, 188765, 296,
     "0047c631d9b53d9f7d6d0955f99a546b9d2562f128748eecc12be818280ba9aa",
     "789f5d12d72f9b9185a40e33bdcdb888df0a15f89083de3b1ddcb86738a75507"),
    (1574, "WF02 | retained nose joint backing", 3152, 5187, 733, 341614, 2898,
     "983a916affe2927c3fc9f7b96e6d2102cb26bdcac80d7c27568fc57714801c5f",
     "9731912e8929e9476f7a0aef3ae7badbd6c193489ac5493606fb542f6756d7ef"),
)
require = base.require


def canonical(packet):
    result = json.dumps(packet, sort_keys=True, separators=(",", ":"),
                        ensure_ascii=True, allow_nan=False)
    require(len(result.encode()) <= 4096, "bounded constructor/property packet")
    return result


def scalar(value):
    require(isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value) and abs(value) <= 8, "finite eight-metre workspace")
    return float(value).hex()


def vec(value):
    require(len(value) == 3, "vector dimensions")
    return "RigidVector3{" + ",".join(scalar(x) for x in value) + "}"


def bounds(raw, quantized=False):
    require(len(raw) == 2 and all(len(p) == 3 for p in raw), "bound dimensions")
    values = [[x * 1e-6 if quantized else x for x in p] for p in raw]
    require(all(values[0][i] <= values[1][i] for i in range(3)), "bound order")
    return "BoardingPlantedLegPointBounds{" + ",".join(vec(p) for p in values) + "}"


def transform(columns):
    require(len(columns) == 4, "affine frame dimensions")
    return "OperatingTransform{{" + ",".join(vec(c) for c in columns) + "}}"


class Emitter(base.Emitter):
    def write(self, value):
        require(self.bytes + len(value.encode()) <= MAX_EMISSION, "extension emission capacity")
        super().write(value)


def validate_frame(frame):
    vertices, polygons = frame["frame_base"]["vertices_local"], frame["frame_base"]["polygons"]
    require(len(vertices) == 32 and len(polygons) == 32, "frame base dimensions")
    for p in vertices:
        vec(p)
    require(all(len(p) == 4 and len(set(p)) == 4
                and all(type(i) is int and 0 <= i < 32 for i in p) for p in polygons),
            "frame base quad index buffer")
    mods = frame["modifiers"]
    require(len(mods) == 2 and mods[0]["type"] == "BEVEL"
            and mods[1]["type"] == "WEIGHTED_NORMAL", "frame modifier identity")
    # This is the actual registered capture convention, not inferred defaults.
    expected = dict(width=0.006000000052154064, segments=3,
                    affect="EDGES", offset_type="OFFSET", limit_method="ANGLE",
                    angle_limit=0.5235987901687622, profile=.5,
                    use_clamp_overlap=True, loop_slide=True,
                    miter_outer="MITER_SHARP", miter_inner="MITER_SHARP",
                    vmesh_method="ADJ")
    require(all(mods[0].get(k) == v for k, v in expected.items())
            and all(m["show_viewport"] is True and m["show_render"] is True for m in mods),
            "registered frame bevel settings")
    canonical(mods)
    canonical(frame["properties"])
    transform(frame["corrected_world_columns"])


def emit(document, blob, stream):
    require(document["schema"] == SCHEMA and document["source_sha256"] == SOURCE_SHA
            and document["inventory_sha256"] == INVENTORY_SHA
            and document["base_completion_sha256"] == base.COMPLETION_SHA
            and document["history_helper_sha256"] == HISTORY_SHA
            and document["blender_version"] == "5.2.2 LTS"
            and document["master_unchanged"] is True and document["source_save_count"] == 0
            and document["material_permission"] is False and document["actor_permission"] is False,
            "extension source provenance")
    packet = document["binary_payload"]
    require(len(blob) == MAX_BINARY and packet["bytes"] == MAX_BINARY
            and packet["vertices"] == 3408 and packet["triangles"] == 5699
            and packet["sha256"] == EXTENSION_BINARY_SHA, "exact additive geometry")
    require(document["work"] == {"full_mesh_summaries": 2, "ordered_triangle_hash_streams": 4,
        "inherited_verified_crop_faces": [296, 2898], "new_crop_face_replays": 0},
        "inherited crop attribution versus new work")
    rows = document["objects"]
    require(len(rows) == 2, "two-source extension only")
    validate_frame(rows[0])
    require(not rows[1]["modifiers"], "current nose modifier state")
    out = Emitter(stream)
    out.write('// Pinned checkpoint material extension01; generated, immutable.\n'
              '#include "origin_boarding_checkpoint_material_extension_prepared.hpp"\n'
              'namespace apsis_drift::detail { namespace {\n')
    offset = 0
    string_bytes = 0
    for m, (row, selected) in enumerate(zip(rows, SELECTED)):
        index, name, n, t, oid, start, count, raw_sha, q_sha = selected
        identity, g = row["identity"], row["geometry"]
        require(row["source_index"] == index and identity["source_object"] == name
                and identity["source_type"] == "MESH" and identity["source_parent"] is None
                and identity["motion_group"] is None and identity["retained_contact_group"] == "craft_fixed"
                and identity["retained_original_contact_range"] == {"object": oid,
                    "group": "craft_fixed", "triangle_start": start, "triangle_count": count}
                and identity["retained_quantized_triangles_verified"] == count
                and identity["absent_from_original_contact_crop"] is False,
                "original source and inherited crop identity")
        require(g["evaluated_vertex_count"] == n and g["evaluated_triangle_count"] == t
                and g["evaluated_triangle_ordinal_range"] == [0, t]
                and g["ordered_triangle_binary64_sha256"] == raw_sha
                and g["ordered_triangle_micrometre_sha256"] == q_sha
                and all(g[k] == identity[k] for k in (
                    "evaluated_vertex_count", "evaluated_triangle_count", "evaluated_triangle_ordinal_range",
                    "ordered_triangle_binary64_sha256", "ordered_triangle_micrometre_sha256",
                    "full_bounds_corrected_world_metres", "full_bounds_quantized_micrometres"))
                and row["corrected_world_columns"] == identity["source_local_blender_to_corrected_world_columns"],
                "selected full geometry fingerprint identity")
        # Exact contiguous arrays, never aliases, padding, or a second old mesh.
        for key, dtype, expected_rows in (("vertices_binary64", "<f8", n),
                                         ("vertices_quantized_micrometres", "<i8", n),
                                         ("triangle_indices", "<i4", t)):
            d = g[key]
            require(d["offset"] == offset and d["rows"] == expected_rows, "contiguous packet layout")
            list_count = sum(1 for _ in base.array(blob, d, dtype))
            require(list_count == expected_rows, "packet row count")
            offset += d["bytes"]
        out.write(f'static const std::array<RigidVector3,{n}> raw{m}{{{{\n')
        for p in base.array(blob, g["vertices_binary64"], "<f8"):
            out.write(vec(p) + ',\n')
        out.write('}};\n')
        out.write(f'static const std::array<MaterialQuantizedPoint,{n}> q{m}{{{{\n')
        for raw, p in zip(base.array(blob, g["vertices_binary64"], "<f8"),
                          base.array(blob, g["vertices_quantized_micrometres"], "<i8")):
            require(all(abs(x) <= 8000000 for x in p) and tuple(round(x * 1000000) for x in raw) == p,
                    "registered nearest-micrometre quantization")
            out.write('MaterialQuantizedPoint{{' + ','.join(map(str, p)) + '}},\n')
        out.write('}};\n')
        out.write(f'static const std::array<MaterialTriangle,{t}> tri{m}{{{{\n')
        for p in base.array(blob, g["triangle_indices"], "<i4"):
            require(all(0 <= i < n for i in p), "full evaluated face index buffer")
            out.write('MaterialTriangle{{' + ','.join(map(str, p)) + '}},\n')
        out.write('}};\n')
    require(offset == MAX_BINARY, "no trailing/duplicate geometry")
    out.write('static const std::array<CheckpointMaterialExtensionSourceRecord,2> sources{{\n')
    for m, (row, selected) in enumerate(zip(rows, SELECTED)):
        index, name, _, _, oid, start, count, raw_sha, q_sha = selected
        g = row["geometry"]
        strings = [name, "MESH", "", raw_sha, q_sha]
        string_bytes += sum(len(s.encode()) + 1 for s in strings)
        out.write('CheckpointMaterialExtensionSourceRecord{' + str(index) + ',MaterialSourceRecord{'
            + ','.join(base.quote(s) for s in strings) + ','
            + bounds(g["full_bounds_corrected_world_metres"]) + ','
            + bounds(g["full_bounds_quantized_micrometres"], True) + ','
            + f'{oid},0,-1,-1,{start},{count},BoardingInitialMaterialRelation::unknown,false,false,false' + '}},\n')
    out.write('}};\nstatic const std::array<MaterialMeshRecord,2> meshes{{\n')
    for m, selected in enumerate(SELECTED):
        out.write(f'MaterialMeshRecord{{{selected[0]},raw{m},q{m},tri{m}}},\n')
    out.write('}};\n')
    frame = rows[0]
    mods = frame["modifiers"]
    strings = [canonical(mods), canonical(frame["properties"]), canonical(rows[1]["properties"])]
    string_bytes += sum(len(s.encode()) + 1 for s in strings)
    # All pins/retained handles are immutable source storage, not free literals.
    pins = [EXTENSION_METADATA_SHA, EXTENSION_BINARY_SHA, SOURCE_SHA, INVENTORY_SHA, HISTORY_SHA]
    string_bytes += sum(len(p) + 1 for p in pins)
    polygons = ','.join('std::array<std::uint8_t,4>{' + ','.join(map(str, p)) + '}'
                        for p in frame["frame_base"]["polygons"])
    out.write('static const CheckpointMaterialExtensionFrameRecord frame{1436,0,{{'
        + ','.join(vec(p) for p in frame["frame_base"]["vertices_local"]) + '}},{{' + polygons + '}},'
        + transform(frame["corrected_world_columns"]) + ','
        + ','.join(scalar(mods[0][k]) for k in ("width", "profile", "angle_limit"))
        + ',3,true,true,true,true,' + base.quote(strings[0]) + ',' + base.quote(strings[1]) + '};\n')
    out.write('static const std::string_view nose_properties = ' + base.quote(strings[2]) + ';\n'
        + 'constexpr std::size_t storage_bytes = ' + str(string_bytes)
        + '+sizeof(sources)+sizeof(meshes)+sizeof(frame)+sizeof(nose_properties)'
        + '+sizeof(CheckpointMaterialExtensionPreparedView)'
        + ''.join(f'+sizeof(raw{i})+sizeof(q{i})+sizeof(tri{i})' for i in range(2)) + ';\n'
        + 'static_assert(storage_bytes <= 262144);\n}\n'
        + 'CheckpointMaterialExtensionPreparedView origin_boarding_checkpoint_material_extension_prepared() {\n'
        + 'return {sources,meshes,&frame,nose_properties,storage_bytes,'
        + ','.join(base.quote(p) for p in pins) + '};\n}\n}\n')
    return {"schema": "apsis-drift-prepared-checkpoint-material-extension/1",
        "vertices": 3408, "triangles": 5699, "binary_bytes": MAX_BINARY,
        "emitted_bytes": out.bytes, "string_storage_bytes": string_bytes,
        "metadata_sha256": EXTENSION_METADATA_SHA, "binary_sha256": EXTENSION_BINARY_SHA,
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
    require(len(EXTENSION_METADATA_SHA) == 64 and len(EXTENSION_BINARY_SHA) == 64,
            "extension capture pins not installed")
    require(all((args.completion, args.binary, args.output)), "three input/output paths required")
    document = json.loads(base.pinned(args.completion, MAX_METADATA, EXTENSION_METADATA_SHA))
    blob = base.pinned(args.binary, MAX_BINARY, EXTENSION_BINARY_SHA)
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
    """Finite malformed-codec controls only: no master/source/geometry query."""
    checks = 0

    def refuses(action):
        nonlocal checks
        try:
            action()
        except (ValueError, KeyError, TypeError, OverflowError):
            checks += 1
            return
        raise AssertionError("Malformed preparation accepted")

    packed = struct.pack("<3d", 1., 2., 3.)
    descriptor = dict(offset=0, bytes=24, rows=1, columns=3, dtype="<f8",
                      sha256=hashlib.sha256(packed).hexdigest())
    assert list(base.array(packed, descriptor, "<f8")) == [(1., 2., 3.)]
    checks += 1
    for update in ({"offset": -1}, {"bytes": 25}, {"rows": 2}, {"columns": 4},
                   {"dtype": "<i8"}, {"sha256": "0" * 64}):
        refuses(lambda u=update: list(base.array(packed, descriptor | u, "<f8")))
    for bad in (float("nan"), float("inf"), 8.00001, True):
        refuses(lambda b=bad: scalar(b))
    refuses(lambda: bounds([[1., 0., 0.], [0., 1., 1.]]))
    refuses(lambda: transform([[0., 0., 0.]] * 3))
    refuses(lambda: canonical({"x": "x" * 4096}))
    refuses(lambda: validate_frame({"frame_base": {"vertices_local": [], "polygons": []}}))

    class Short(io.StringIO):
        def write(self, value):
            return len(value) - 1

    refuses(lambda: Emitter(Short()).write("packet"))
    emitter = Emitter(io.StringIO())
    emitter.bytes = MAX_EMISSION
    refuses(lambda: emitter.write("x"))
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "input"
        path.write_bytes(packed)
        refuses(lambda: base.pinned(path, 23, descriptor["sha256"]))
        refuses(lambda: base.pinned(path, 24, "0" * 64))
        assert base.pinned(path, 24, descriptor["sha256"]) == packed
        checks += 1
    print(f"{checks} pure preparation controls passed; no source/material queries")


if __name__ == "__main__":
    main()
