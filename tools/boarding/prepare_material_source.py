#!/usr/bin/env python3
"""Prepare pinned complete-current material data; never loads an authoring master.

Only the generated translation unit holds large immutable arrays. Runtime source
admission has no JSON parser, binary-file path, or caller-authored geometry.
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

COMPLETION_SHA = "09cd65c9d0080ab792b03ab458b597db71e5cb80c38184818a6ed69a3389f63c"
BINARY_SHA = "2d6f51d7a7476d8fc0d140344f82eb8958c59e0ea3866053d99ef5cfb36840c4"
SOURCE_SHA = "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677"
MAX_METADATA = 8 * 1024 * 1024
MAX_BINARY = 24 * 1024 * 1024
MAX_EMISSION = 64 * 1024 * 1024
GROUPS = ("roof_port", "roof_starboard", "inner_port_outer", "inner_port_inner",
          "inner_starboard_outer", "inner_starboard_inner", "ladder_base",
          "ladder_upper", "seat_carriage", "seat_swivel", "seat_lift",
          "seat_entry_arm", "seat_lock")
ORIGINAL_GROUPS = ('craft_fixed',) + tuple(sorted('craft_' + g for g in GROUPS))


def original_group_index(name):
    """Original contact catalog IDs differ from OperatingPose channel indices."""
    return ORIGINAL_GROUPS.index(name) if name else -1


def require(condition, message):
    if not condition:
        raise ValueError(message)


def pinned(path, capacity, expected):
    require(path.stat().st_size <= capacity, "input byte capacity")
    data = path.read_bytes()
    require(hashlib.sha256(data).hexdigest() == expected, "input fingerprint")
    return data


def array(blob, descriptor, dtype):
    width = {"<f8": 8, "<i8": 8, "<i4": 4}[dtype]
    offset, size, count = (descriptor[k] for k in ("offset", "bytes", "rows"))
    require(descriptor["dtype"] == dtype and descriptor["columns"] == 3,
            "array codec")
    require(all(isinstance(n, int) and n >= 0 for n in (offset, size, count))
            and size == count * 3 * width and offset + size <= len(blob),
            "array buffer")
    data = memoryview(blob)[offset:offset + size]
    require(hashlib.sha256(data).hexdigest() == descriptor["sha256"],
            "array fingerprint")
    return struct.iter_unpack({"<f8": "<3d", "<i8": "<3q", "<i4": "<3i"}[dtype], data)


def scalar(value):
    require(math.isfinite(value) and abs(value) <= 1000, "numeric workspace")
    return float(value).hex()


def vec(value):
    require(len(value) == 3, "vector dimensions")
    return "RigidVector3{" + ",".join(scalar(x) for x in value) + "}"


def bounds(raw, quantized=False):
    require(len(raw) == 2, "bound dimensions")
    values = [[x * 1e-6 if quantized else x for x in row] for row in raw]
    require(all(values[0][i] <= values[1][i] for i in range(3)), "bound order")
    return "BoardingPlantedLegPointBounds{" + ",".join(vec(row) for row in values) + "}"


def transform(columns):
    require(len(columns) == 4, "transform dimensions")
    return "OperatingTransform{{" + ",".join(vec(c) for c in columns) + "}}"


def quote(value):
    return json.dumps(value or "", ensure_ascii=True)


class Emitter:
    def __init__(self, stream):
        self.stream, self.bytes = stream, 0

    def write(self, value):
        self.bytes += len(value.encode("utf-8"))
        require(self.bytes <= MAX_EMISSION, "emitted source byte capacity")
        require(self.stream.write(value) == len(value), "short source write")


def validate_constructor(c):
    kind = c["source_kind"]
    require(c["certified_material_membership"] is False, "captured authority")
    if kind == "actual_POLY_bevel_sweep":
        s, spline = c["curve_settings"], c["spline"]
        exact = dict(dimensions="3D", resolution_u=16, render_resolution_u=0,
                     bevel_resolution=3, bevel_mode="ROUND", fill_mode="FULL",
                     use_fill_caps=False, twist_mode="MINIMUM", twist_smooth=0,
                     bevel_factor_start=0, bevel_factor_end=1,
                     bevel_factor_mapping_start="RESOLUTION",
                     bevel_factor_mapping_end="RESOLUTION")
        require(all(s.get(k) == v for k, v in exact.items()) and not c["modifiers"],
                "unsupported POLY settings")
        require(spline["type"] == "POLY" and not spline["use_cyclic_u"]
                and spline["resolution_u"] == 16 and len(spline["points"]) in (5, 6),
                "unsupported POLY spline")
        require(all(p["co_homogeneous"][3] == 1 and p["radius_factor"] == 1
                    and p["tilt"] == 0 for p in spline["points"]), "POLY point settings")
        require(0 < s["bevel_depth"] < .1, "POLY radius")
    elif kind == "tapered_primitive_cube_with_current_modifiers":
        require(len(c["premodifier_vertices_local"]) == 8
                and len(c["premodifier_polygons"]) == 6, "primitive dimensions")
        require(all(len(p) == 4 and len(set(p)) == 4 and all(0 <= i < 8 for i in p)
                    for p in c["premodifier_polygons"]), "primitive face indices")
        mods = c["modifiers"]
        require(len(mods) == 2 and mods[0]["type"] == "BEVEL"
                and mods[1]["type"] == "WEIGHTED_NORMAL", "primitive modifiers")
        exact = dict(affect="EDGES", offset_type="OFFSET", limit_method="ANGLE",
                     angle_limit=0.5235987901687622, profile=.5,
                     use_clamp_overlap=True, loop_slide=True,
                     miter_outer="MITER_SHARP", miter_inner="MITER_SHARP",
                     vmesh_method="ADJ", segments=3)
        require(all(mods[0].get(k) == v for k, v in exact.items())
                and all(m["show_viewport"] and m["show_render"] for m in mods),
                "unsupported primitive settings")
    else:
        require(kind == "retained_cut_mesh_skin" and not c["modifiers"]
                and c["authored_wall_thickness_metres"] is None
                and c["added_caps_or_thickness"] is False, "unsupported shell policy")


def emit(document, blob, stream):
    require(document["schema"] == "apsis-drift-complete-material-source/1"
            and document["source_sha256"] == SOURCE_SHA
            and document["master_unchanged"] is True, "source provenance")
    inventory = document["full_original_inventory"]["objects"]
    selected = document["selected_original_geometry"]
    replacements = document["selected_stowed_source"]["replacement_objects"]
    moving = document["actual_source_posed_moving_objects"]
    constructors = document["constructor_packets"]
    require((len(inventory), len(selected), len(replacements), len(moving), len(constructors))
            == (1746, 6, 13, 165, 6), "source roster capacity")
    require(len({r["source_object"] for r in inventory}) == 1746, "duplicate source")
    for c in constructors:
        validate_constructor(c)
    records = selected + replacements
    geometry_index = {r["source_object"]: i for i, r in enumerate(selected)}
    source_index = {r["source_object"]: i for i, r in enumerate(inventory)}
    source_index.update({r["source_object"]: 1746 + i for i, r in enumerate(replacements)})
    # Original/replacement names can coincide; selected originals use their original index.
    original_index = {r["source_object"]: i for i, r in enumerate(inventory)}
    removed = {r["source_object"] for r in document["selected_stowed_source"]["original_removal_ranges"]}
    require(len(removed) == 8, "removal capacity")
    out = Emitter(stream)
    out.write('// Pinned complete-current material source; generated, immutable.\n'
              '#include "origin_boarding_initial_material_internal.hpp"\n'
              'namespace apsis_drift::detail { namespace {\n')
    vertices = triangles = 0
    for m, row in enumerate(records):
        g = row["geometry"]
        n, t = g["evaluated_vertex_count"], g["evaluated_triangle_count"]
        require(g['vertices_binary64']['rows'] == n
                and g['vertices_quantized_micrometres']['rows'] == n
                and g['triangle_indices']['rows'] == t, "packet dimensions")
        require(g["evaluated_triangle_ordinal_range"] == [0, t], "source face ordinals")
        vertices += n; triangles += t
        require(vertices <= 188535 and triangles <= 349087, "geometry capacity")
        out.write(f'static const std::array<RigidVector3,{n}> raw{m}{{{{\n')
        for p in array(blob, g["vertices_binary64"], "<f8"):
            out.write(vec(p) + ',\n')
        out.write('}};\n')
        out.write(f'static const std::array<MaterialQuantizedPoint,{n}> q{m}{{{{\n')
        for p in array(blob, g["vertices_quantized_micrometres"], "<i8"):
            require(all(abs(x) <= 1000000000 for x in p), "quantized workspace")
            out.write('MaterialQuantizedPoint{{' + ','.join(str(x) for x in p) + '}},\n')
        out.write('}};\n')
        out.write(f'static const std::array<MaterialTriangle,{t}> tri{m}{{{{\n')
        for p in array(blob, g["triangle_indices"], "<i4"):
            require(all(0 <= i < n for i in p), "face buffer index")
            out.write('MaterialTriangle{{' + ','.join(str(x) for x in p) + '}},\n')
        out.write('}};\n')
    require((vertices, triangles) == (188535, 349087), "geometry exact counts")
    out.write('static const std::array<MaterialSourceRecord,1759> sources{{\n')
    name_bytes = 0
    for index, r in enumerate(inventory + replacements):
        replacement = index >= 1746
        g = r["geometry"] if replacement else r
        raw_key = "full_bounds_corrected_world_metres"
        q_key = "full_bounds_quantized_micrometres"
        original = r.get("original_catalog_range") if replacement else r["retained_original_contact_range"]
        name = r["source_object"]
        mesh = index - 1746 + 6 if replacement else geometry_index.get(name, -1)
        motion = 10 if replacement else GROUPS.index(r["motion_group"]) if r["motion_group"] else -1
        relation = 'stowed' if replacement else 'unknown'
        if not replacement and mesh >= 0:
            kind = next(c["source_kind"] for c in constructors if c["source_object"] == name)
            relation = {'retained_cut_mesh_skin':'shell_sheet', 'actual_POLY_bevel_sweep':'service_enclosure',
                        'tapered_primitive_cube_with_current_modifiers':'support_enclosure'}[kind]
        group_name = '' if replacement else r.get('retained_contact_group') or ''
        group = original.get('group', -1) if replacement and original else original_group_index(group_name)
        oid = original.get('original_object', -1) if replacement and original else original['object'] if original else -1
        start = original.get('start', 0) if replacement and original else original['triangle_start'] if original else 0
        count = original.get('count', 0) if replacement and original else original['triangle_count'] if original else 0
        strings = [name, r.get('source_type', 'MESH'), r.get('source_parent'),
                   g['ordered_triangle_binary64_sha256'], g['ordered_triangle_micrometre_sha256']]
        name_bytes += sum(len((s or '').encode()) + 1 for s in strings)
        out.write('MaterialSourceRecord{' + ','.join(quote(s) for s in strings) + ','
                  + bounds(g[raw_key]) + ',' + bounds(g[q_key], True) + ','
                  + f'{oid},{group},{motion},{mesh},{start},{count},BoardingInitialMaterialRelation::{relation},'
                  + f'{str(not replacement and name in removed).lower()},'
                  + f'{str(not replacement and r["absent_from_original_contact_crop"]).lower()},'
                  + f'{str(replacement).lower()}' + '},\n')
    out.write('}};\nstatic const std::array<MaterialMeshRecord,19> meshes{{\n')
    for m, r in enumerate(records):
        si = original_index[r['source_object']] if m < 6 else 1746 + m - 6
        out.write(f'MaterialMeshRecord{{{si},raw{m},q{m},tri{m}}},\n')
    out.write('}};\nstatic const std::array<MaterialCurveRecord,3> curves{{\n')
    for c in constructors:
        if c['source_kind'] != 'actual_POLY_bevel_sweep':
            continue
        points = [p['co_homogeneous'][:3] for p in c['spline']['points']]
        out.write('MaterialCurveRecord{' + f'{original_index[c["source_object"]]},{geometry_index[c["source_object"]]},{len(points)},'
                  + '{{' + ','.join(vec(p) for p in points) + '}},'
                  + transform(c['corrected_world_columns']) + ',' + scalar(c['curve_settings']['bevel_depth']) + '},\n')
    out.write('}};\nstatic const std::array<MaterialSupportRecord,2> supports{{\n')
    for c in constructors:
        if c['source_kind'] != 'tapered_primitive_cube_with_current_modifiers':
            continue
        polygons = ','.join('std::array<std::uint8_t,4>{' + ','.join(map(str, p)) + '}' for p in c['premodifier_polygons'])
        out.write('MaterialSupportRecord{' + f'{original_index[c["source_object"]]},{geometry_index[c["source_object"]]},'
                  + '{{' + ','.join(vec(p) for p in c['premodifier_vertices_local']) + '}},{{' + polygons + '}},'
                  + transform(c['corrected_world_columns']) + ',' + scalar(c['modifiers'][0]['width']) + '},\n')
    out.write('}};\nstatic const std::array<MaterialMovingRecord,165> moving{{\n')
    for r in moving:
        require(r['operating_tuple'] == [1, 1, 1, 0], "moving selected pose")
        g = r['complete_posed_bounds_and_fingerprints']
        out.write('MaterialMovingRecord{' + str(original_index[r['source_object']]) + ','
                  + bounds(g['full_bounds_corrected_world_metres']) + ','
                  + bounds(g['full_bounds_quantized_micrometres'], True) + ','
                  + transform(r['source_group_delta_columns']) + '},\n')
    out.write('}};\nconstexpr std::size_t storage_bytes = ' + str(name_bytes + 256)
              + '+sizeof(sources)+sizeof(meshes)+sizeof(curves)+sizeof(supports)+sizeof(moving)'
              + ''.join(f'+sizeof(raw{i})+sizeof(q{i})+sizeof(tri{i})' for i in range(19)) + ';\n'
              'static_assert(storage_bytes <= kBoardingInitialMaterialMaximumSourceBytes);\n'
              '}\nMaterialPreparedView origin_boarding_initial_material_prepared() {\n'
              'return {sources,meshes,curves,supports,moving,storage_bytes,'
              + quote(COMPLETION_SHA) + ',' + quote(BINARY_SHA) + '};\n}\n}\n')
    return {'schema': 'apsis-drift-prepared-material-source/1', 'vertices': vertices,
            'triangles': triangles, 'emitted_bytes': out.bytes, 'string_storage_bound': name_bytes,
            'completion_sha256': COMPLETION_SHA, 'binary_sha256': BINARY_SHA,
            'source_permission': False, 'material_permission': False, 'actor_permission': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('completion', type=Path, nargs='?')
    parser.add_argument('binary', type=Path, nargs='?')
    parser.add_argument('output', type=Path, nargs='?')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    require(all((args.completion, args.binary, args.output)), 'three input/output paths required')
    document = json.loads(pinned(args.completion, MAX_METADATA, COMPLETION_SHA))
    blob = pinned(args.binary, MAX_BINARY, BINARY_SHA)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=args.output.parent,
                                     delete=False) as stream:
        temporary = Path(stream.name)
        try:
            receipt = emit(document, blob, stream)
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    temporary.replace(args.output)
    receipt['prepared_sha256'] = hashlib.sha256(args.output.read_bytes()).hexdigest()
    receipt['preparer_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    args.output.with_suffix('.json').write_text(json.dumps(receipt, indent=2) + '\n')


def self_test():
    """Pure malformed-codec controls; no source admission or numerical query."""
    checks = 0

    def refuses(action):
        nonlocal checks
        try:
            action()
        except (ValueError, KeyError, TypeError):
            checks += 1
            return
        raise AssertionError('Malformed preparation accepted')

    packed = struct.pack('<3d', 1., 2., 3.)
    descriptor = dict(offset=0, bytes=24, rows=1, columns=3, dtype='<f8',
                      sha256=hashlib.sha256(packed).hexdigest())
    assert list(array(packed, descriptor, '<f8')) == [(1., 2., 3.)]
    checks += 1
    for update in ({'offset': -1}, {'bytes': 25}, {'rows': 2}, {'columns': 4},
                   {'dtype': '<i8'}, {'sha256': '0' * 64}):
        refuses(lambda u=update: list(array(packed, descriptor | u, '<f8')))
    refuses(lambda: scalar(float('nan')))
    refuses(lambda: scalar(float('inf')))
    refuses(lambda: scalar(1001.))
    refuses(lambda: bounds([[1., 0., 0.], [0., 1., 1.]]))
    refuses(lambda: transform([[0., 0., 0.]] * 3))

    class Short(io.StringIO):
        def write(self, value):
            return len(value) - 1

    refuses(lambda: Emitter(Short()).write('packet'))
    emitter = Emitter(io.StringIO())
    emitter.bytes = MAX_EMISSION
    refuses(lambda: emitter.write('x'))
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'input'
        path.write_bytes(packed)
        refuses(lambda: pinned(path, 23, descriptor['sha256']))
        refuses(lambda: pinned(path, 24, '0' * 64))
        assert pinned(path, 24, descriptor['sha256']) == packed
        checks += 1
    refuses(lambda: validate_constructor({'source_kind': 'invented',
                                           'certified_material_membership': False}))
    print(f'{checks} pure preparation controls passed; no source/material queries')


if __name__ == '__main__':
    main()
