#!/usr/bin/env python3
"""Export only the frozen Craft09 closed-rest seat subset; BSD-3-Clause.

Blender --background --factory-startup --disable-autoexec --python THIS --
--source SELECTED_CRAFT09 --output-dir NEW_DIRECTORY [--old-package PATH].
No source save, whole-craft export, opening curve or runtime admission.
"""
import argparse
from collections import defaultdict, deque
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile

import bpy
import numpy as np

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import wayfarer_restraint_export_checks as checks
import wayfarer_gltf_checks
from wayfarer_operating_glb_audit import audit
sys.modules['check_hopper_gltf'] = wayfarer_gltf_checks
import wayfarer_flight_export_base as base
from wayfarer_operating_blender import OperatingPoseController, ancestry
from wayfarer_operating_spec import BASE_EXPORTER_SHA256, classify_motion_group

MODEL = 'wayfarer-restraints-closed-01.glb'
NODE_NAMES = [checks.RESIDUAL, *checks.NODES.values()]


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def unrelated_digest(objects):
    """Evidence that selected-only preparation left other source geometry alone."""
    result = hashlib.sha256()
    for obj in sorted(objects, key=lambda o: o.name):
        result.update(checks.canonical([obj.name, obj.type]).encode())
        result.update(np.asarray(obj.matrix_world, dtype=np.float64).tobytes())
        modifier_fields = {
            'BEVEL': ['segments', 'width', 'affect', 'limit_method'],
            'SUBSURF': ['levels', 'render_levels', 'subdivision_type'],
            'SOLIDIFY': ['thickness', 'offset', 'use_even_offset'],
        }
        modifiers = [(m.name, m.type, {
            key: getattr(m, key) for key in modifier_fields.get(m.type, [])
        }) for m in obj.modifiers]
        result.update(checks.canonical(modifiers).encode())
        if obj.type == 'MESH':
            points = np.empty(len(obj.data.vertices)*3, dtype=np.float32)
            loops = np.empty(len(obj.data.loops), dtype=np.int32)
            obj.data.vertices.foreach_get('co', points)
            obj.data.loops.foreach_get('vertex_index', loops)
            result.update(points.tobytes())
            result.update(loops.tobytes())
            result.update(checks.canonical([(p.loop_start, p.loop_total, p.material_index)
                                           for p in obj.data.polygons]).encode())
            for layer in obj.data.uv_layers:
                uv = np.empty(len(obj.data.loops)*2, dtype=np.float32)
                layer.data.foreach_get('uv', uv)
                result.update(layer.name.encode())
                result.update(uv.tobytes())
        elif obj.type in ('CURVE', 'FONT'):
            data = obj.data
            shape = {key: getattr(data, key) for key in (
                'resolution_u', 'render_resolution_u', 'bevel_depth', 'bevel_resolution',
                'extrude', 'offset', 'dimensions', 'fill_mode')}
            shape['splines'] = [{
                'type': spline.type, 'cyclic': spline.use_cyclic_u,
                'points': [(tuple(p.co), p.radius, p.tilt) for p in spline.points],
                'bezier': [(tuple(p.co), tuple(p.handle_left), tuple(p.handle_right),
                            p.handle_left_type, p.handle_right_type, p.radius, p.tilt)
                           for p in spline.bezier_points],
            } for spline in data.splines]
            if obj.type == 'FONT':
                shape['font'] = {key: getattr(data, key) for key in (
                    'body', 'size', 'space_character', 'space_word', 'space_line',
                    'shear', 'align_x', 'align_y')}
            result.update(checks.canonical(shape).encode())
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--old-package', type=Path,
                        default=TOOLS.parent / 'assets/native/wayfarer-operating-02')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    # Refuse unsafe outputs, source/reference identities and unavailable atomic
    # installation before opening the source or creating producer output.
    source, output, package, old_package, spec = checks.preflight(
        args.source, args.output_dir, args.old_package)
    checks.require(checks.sha(TOOLS / 'wayfarer_flight_export_base.py') == BASE_EXPORTER_SHA256,
                   'Qualified base exporter changed')
    old_doc, old_bin = checks.read_glb(checks.old_model(package, old_package))
    old_rows = checks.triangles(old_doc, old_bin, ['WFOpSeatLift'])
    controller = OperatingPoseController.prepare(source).freeze()
    controller.reset()
    checks.require(controller.derivative_corrections == spec['derivative_corrections'],
                   'Source correction replay differs from frozen operating02')
    originals = [obj for obj in bpy.context.scene.objects if base.keep(obj)]
    selected = [obj for obj in originals
                if classify_motion_group(ancestry(obj)) == 'seat_lift']
    expected = next(g for g in spec['groups'] if g['id'] == 'seat_lift')['source_objects']
    checks.require(len(selected) == 64 and {o.name for o in selected} == set(expected),
                   'Exact64 seat_lift source members required')
    checks.require(all(o.type in ('MESH', 'CURVE', 'FONT') for o in selected),
                   'Expected renderable seat source roster')
    unrelated = [o for o in originals if o not in selected]
    before = unrelated_digest(unrelated)
    selected_world = {o.name: np.asarray(o.matrix_world).tolist() for o in selected}
    anchors = ['Harness shoulder anchor', 'Harness shoulder anchor.001']
    source_records, source_faces, batches = [], [], defaultdict(list)
    collection = bpy.data.collections.new('Wayfarer closed restraint derivative')
    bpy.context.scene.collection.children.link(collection)
    for obj in selected:
        # Same geometry preparation as the immutable operating02 producer.
        if obj.type == 'CURVE':
            obj.data.resolution_u = min(obj.data.resolution_u, 4)
            obj.data.bevel_resolution = min(obj.data.bevel_resolution, 1)
        for modifier in obj.modifiers:
            if modifier.type == 'BEVEL': modifier.segments = min(modifier.segments, 2)
            if modifier.type == 'SUBSURF':
                modifier.levels = min(modifier.levels, 1)
                modifier.render_levels = modifier.levels
        bpy.context.view_layer.update()
        mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(bpy.context.evaluated_depsgraph_get()),
                                              preserve_all_data_layers=True,
                                              depsgraph=bpy.context.evaluated_depsgraph_get())
        checks.require(len(mesh.polygons) > 0, 'Empty selected source mesh')
        uv = np.zeros(len(mesh.loops)*2, dtype=np.float32)
        if mesh.uv_layers.active: mesh.uv_layers.active.data.foreach_get('uv', uv)
        for layer in list(mesh.uv_layers): mesh.uv_layers.remove(layer)
        mesh.uv_layers.new(name='UVMap').data.foreach_set('uv', uv)
        for material in mesh.materials:
            if material and material.use_nodes:
                for node in material.node_tree.nodes:
                    if node.type in ('NORMAL_MAP', 'UVMAP'): node.uv_map = 'UVMap'
        mesh.transform(obj.matrix_world)
        mesh.calc_loop_triangles()
        coords = np.empty(len(mesh.vertices)*3, dtype=np.float32)
        mesh.vertices.foreach_get('co', coords)
        coords = coords.reshape(-1, 3)
        checks.require(np.isfinite(coords).all() and np.isfinite(uv).all(), 'Nonfinite source coordinates/UV')
        generated = obj.get('asset_generated_surface', any(
            m and m.name.startswith('Material_0') for m in mesh.materials))
        checks.require(not (len(mesh.loop_triangles) > 5000 and generated),
                       'Seat-only source unexpectedly needs generated reduction')
        node_name = checks.NODES.get(obj.name, checks.RESIDUAL)
        source_index = len(source_records)
        source_records.append({'source_object': obj.name, 'source_parent': obj.parent.name, 'source_type': obj.type,
                               'runtime_node': node_name,
                               'source_world_rest_matrix': selected_world[obj.name],
                               'prepared_evaluated_triangles': len(mesh.loop_triangles),
                               'prepared_coordinate_sha256': hashlib.sha256(coords.tobytes()).hexdigest(),
                               'materials': [m.name if m else None for m in mesh.materials]})
        for triangle in mesh.loop_triangles:
            checks.require(all(0 <= v < len(coords) for v in triangle.vertices), 'Source triangle indices')
            material = mesh.materials[triangle.material_index]
            checks.require(material is not None, 'Missing source material')
            points = tuple((float(coords[v][0]), float(coords[v][2]), -float(coords[v][1]))
                           for v in triangle.vertices)
            source_faces.append((node_name, (material.name, checks.rotate_key(points)),
                                 source_index, triangle.index))
        copy = bpy.data.objects.new('Runtime '+obj.name, mesh)
        collection.objects.link(copy)
        batches[node_name].append(copy)
    checks.require(len(batches[checks.RESIDUAL]) == 57 and set(batches) == set(NODE_NAMES),
                   'Exact57 residual plus seven component split required')
    bpy.ops.object.select_all(action='DESELECT')
    render_nodes = []
    for name in NODE_NAMES:
        copies = batches[name]
        for obj in copies: obj.select_set(True)
        bpy.context.view_layer.objects.active = copies[0]
        if len(copies) > 1: bpy.ops.object.join()
        obj = copies[0]; obj.name = name
        obj['restraint_export_group'] = name
        obj['source_sha256'] = checks.SOURCE_SHA256
        obj['source_motion_group'] = 'seat_lift'
        checks.require(np.max(np.abs(np.asarray(obj.matrix_world)-np.eye(4))) == 0,
                       'Source rest must be baked once into flat identity mesh')
        render_nodes.append(obj)
        obj.select_set(False)
    checks.require(unrelated_digest(unrelated) == before and all(
        np.asarray(bpy.data.objects[n].matrix_world).tolist() == matrix
        for n, matrix in selected_world.items()), 'Unrelated/source/anchor transforms changed')
    staging = Path(tempfile.mkdtemp(prefix='.'+output.name+'-', dir=output.parent))
    try:
        for obj in render_nodes: obj.select_set(True)
        bpy.ops.export_scene.gltf(filepath=str(staging / MODEL), export_format='GLB',
                                 use_selection=True, export_apply=True, export_animations=False,
                                 export_cameras=False, export_lights=False,
                                 export_yup=True, export_extras=True)
        doc, payload = checks.read_glb((staging / MODEL).read_bytes())
        checks.require(len(doc['nodes']) == 8 and len(doc['meshes']) == 8 and
                       not doc.get('images') and not doc.get('textures') and
                       not doc.get('skins') and not doc.get('animations'),
                       'Seat-only flat untextured closed-rest output required')
        rows = checks.triangles(doc, payload, NODE_NAMES)
        union = checks.semantic_receipt(old_rows, rows)
        queues = defaultdict(deque)
        for node, key, owner, face in source_faces: queues[(node, key)].append((owner, face))
        mapping = []
        for row in rows:
            key = (row['node'], row['source_key'])
            checks.require(bool(queues[key]), 'Render triangle lacks exact evaluated source attribution')
            owner, face = queues[key].popleft()
            mapping.append([owner, face, NODE_NAMES.index(row['node']), row['primitive'], row['triangle']])
        checks.require(not any(queues.values()), 'Evaluated source face missing from render')
        binary = audit(staging / MODEL)
        # Inherited textured checker intentionally rejects untextured subsets.
        # Its recursive channel inspection still verifies zero texture references.
        texture_refs = [v for m in doc['materials'] for v in wayfarer_gltf_checks.texture_coordinates(m)]
        checks.require(not texture_refs, 'Unexpected texture dependency')
        dependencies = {n: checks.sha(TOOLS / n) for n in [
            'export_wayfarer_restraints.py', 'wayfarer_restraint_export_checks.py',
            'wayfarer_operating_blender.py', 'wayfarer_operating_spec.py',
            'wayfarer_flight_export_base.py', 'wayfarer_operating_glb_audit.py', 'wayfarer_gltf_checks.py']}
        write(staging / 'face-attribution.json', {
            'schema': 'apsis.restraint-render-source-faces/1', 'source_objects': source_records,
            'runtime_nodes': NODE_NAMES,
            'row_fields': ['source_object_index', 'prepared_evaluated_triangle_index',
                           'runtime_node_index', 'primitive_index', 'render_triangle_index'],
            'rows': mapping,
            'method': 'Exact source-prepared float32 position/material occurrence matching; '
                      'full render normals/UV/material/winding independently equal frozen union. '
                      'Identical occurrences assigned in stable source order; no contact admission.'})
        write(staging / 'metadata.json', {
            'schema': 'apsis.wayfarer-restraints-closed/1', 'id': 'wayfarer-restraints-closed-01',
            'units': 'metres', 'axes': 'Godot +Y up, -Z forward', 'source_sha256': checks.SOURCE_SHA256,
            'original_model_sha256': checks.MODEL_SHA256, 'replacement_node': 'WFOpSeatLift',
            'runtime_nodes': NODE_NAMES, 'source_objects': source_records,
            'retained_anchor_objects': anchors,
            'retained_anchor_source_world_rest_matrices': {n: selected_world[n] for n in anchors},
            'derivative_corrections': controller.derivative_corrections,
            'licenses': spec['licenses'], 'limits': ['Closed-rest source split only; no opening motion, fit, '
              'contact sweep, actor action, runtime package admission or displayed review.']})
        write(staging / 'checks.json', {'pass': True, 'closed_rest_union': union,
            'source_unchanged': checks.sha(source) == checks.SOURCE_SHA256,
            'unrelated_geometry_unchanged': True, 'unrelated_geometry_sha256': before,
            'selected_source_members': 64, 'residual_members': 57, 'separate_components': 7,
            'source_render_faces_mapped': len(mapping), 'glb_audit': binary,
            'texture_references': 0, 'blender_version': bpy.app.version_string})
        write(staging / 'provenance.json', {
            'schema': 'apsis.restraint-closed-provenance/1', 'source_sha256': checks.SOURCE_SHA256,
            'inherited_package_sha256': checks.PACKAGE_SHA256,
            'inherited_model_sha256': checks.MODEL_SHA256, 'tools_sha256': dependencies,
            'licenses': spec['licenses'], 'paid_jobs': 0, 'source_master_saved': False,
            'scope': 'Seat-only closed-rest split; no new geometry or selected opening operation.'})
        for entry in old_package['files']:
            if entry['path'].startswith('licenses/'):
                license_bytes = checks.verified_member(package, entry)
                destination = staging / entry['path']
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(license_bytes)
        files = [{'path': str(f.relative_to(staging)), 'bytes': f.stat().st_size,
                  'sha256': checks.sha(f)} for f in sorted(staging.rglob('*')) if f.is_file()]
        write(staging / 'package.json', {'schema': 'apsis.restraint-closed-package/1',
              'package_id': 'wayfarer-restraints-closed-01', 'files': files,
              'source_sha256': checks.SOURCE_SHA256, 'model_sha256': checks.sha(staging / MODEL)})
        checks.require(checks.sha(source) == checks.SOURCE_SHA256, 'Source changed during export')
        checks.install_no_replace(staging, output)
        print('WAYFARER_RESTRAINTS_EXPORTED', len(rows), 'triangles',
              binary['glb_bytes'], 'GLB bytes', output, flush=True)
    finally:
        if staging.exists(): shutil.rmtree(staging)


if __name__ == '__main__':
    main()
