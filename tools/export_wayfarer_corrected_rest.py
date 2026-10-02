#!/usr/bin/env python3
"""Build the one corrected-closed diagnostic derivative; BSD-3-Clause.

Blender --background --factory-startup --disable-autoexec --python-exit-code 1
--python THIS -- --source SELECTED_CRAFT09 --output-dir NEW_DIRECTORY
[--old-package PATH]. No source save, moving restraint or runtime admission.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import bpy
import numpy as np
from mathutils import Matrix, Vector
import wayfarer_gltf_checks
sys.modules['check_hopper_gltf'] = wayfarer_gltf_checks
import wayfarer_flight_export_base as base
from wayfarer_operating_blender import OperatingPoseController, CONVERT, ancestry, columns
from wayfarer_operating_spec import BASE_EXPORTER_SHA256, classify_motion_group
import wayfarer_restraint_export_checks as original
import wayfarer_corrected_rest_geometry as geometry
import wayfarer_corrected_rest_proof as proof
import wayfarer_corrected_rest_package as package
from wayfarer_operating_glb_audit import audit
SOURCE_SHA = geometry.SOURCE_SHA256

SIGNATURE_FIELDS = [
    'included original object name/type/parent and local/world matrices',
    'ordered editable scalar/vector/ID-reference modifier properties and display flags',
    'mesh float32 vertex coordinates, ordered loops/polygons/material indices/smooth flags',
    'ordered mesh UV layers, active UV index and float32 UV coordinates',
    'curve/font dimensions, resolution/bevel/extrude/offset/fill settings',
    'curve point coordinates/weights/radius/tilt, Bezier handles/types and cyclic flags',
    'font body/layout parameters',
    'material slot names/use_nodes and normal-map/UV-map node identities/UV-map properties',
]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def _modifier_fields(modifier):
    fields = {}
    for prop in modifier.bl_rna.properties:
        if prop.is_readonly or prop.identifier == 'rna_type':
            continue
        value = getattr(modifier, prop.identifier)
        if prop.type in ('BOOLEAN', 'INT', 'FLOAT', 'STRING', 'ENUM'):
            fields[prop.identifier] = (list(value) if getattr(prop, 'is_array', False) else
                                       sorted(value) if isinstance(value, set) else value)
        elif prop.type == 'POINTER':
            fields[prop.identifier] = value.name if hasattr(value, 'name') else None
    return fields


def source_signatures(objects):
    """Purpose-specific covered-field evidence, not all Blender authoring data."""
    result = {}
    material_cache = {}
    for obj in sorted(objects, key=lambda o: o.name):
        h = hashlib.sha256()
        identity = [obj.name, obj.type, obj.parent.name if obj.parent else None,
                    columns(obj.matrix_local), columns(obj.matrix_world),
                    [(m.name, m.type, _modifier_fields(m)) for m in obj.modifiers]]
        h.update(geometry.canonical(identity).encode())
        data = obj.data
        if obj.type == 'MESH':
            points = np.empty(len(data.vertices)*3, dtype=np.float32)
            loops = np.empty(len(data.loops), dtype=np.int32)
            data.vertices.foreach_get('co', points); data.loops.foreach_get('vertex_index', loops)
            h.update(points.tobytes()); h.update(loops.tobytes())
            h.update(geometry.canonical([(p.loop_start, p.loop_total, p.material_index, p.use_smooth)
                                         for p in data.polygons]).encode())
            h.update(geometry.canonical([data.uv_layers.active_index]).encode())
            for layer in data.uv_layers:
                values = np.empty(len(data.loops)*2, dtype=np.float32)
                layer.data.foreach_get('uv', values)
                h.update(layer.name.encode()); h.update(values.tobytes())
        elif obj.type in ('CURVE', 'FONT'):
            shape = {key: getattr(data, key) for key in (
                'resolution_u', 'render_resolution_u', 'bevel_depth', 'bevel_resolution',
                'extrude', 'offset', 'dimensions', 'fill_mode')}
            shape['splines'] = [{'type': s.type, 'cyclic': s.use_cyclic_u,
                'points': [(tuple(p.co), p.radius, p.tilt) for p in s.points],
                'bezier': [(tuple(p.co), tuple(p.handle_left), tuple(p.handle_right),
                            p.handle_left_type, p.handle_right_type, p.radius, p.tilt)
                           for p in s.bezier_points]} for s in data.splines]
            if obj.type == 'FONT':
                shape['font'] = {key: getattr(data, key) for key in (
                    'body', 'size', 'space_character', 'space_word', 'space_line', 'shear', 'align_x', 'align_y')}
            h.update(geometry.canonical(shape).encode())
        materials = []
        for material in data.materials:
            if material is None:
                materials.append(None); continue
            if material.name not in material_cache:
                material_cache[material.name] = [material.name, material.use_nodes,
                    sorted((n.name, n.type, n.uv_map) for n in material.node_tree.nodes
                           if n.type in ('NORMAL_MAP', 'UVMAP')) if material.use_nodes else []]
            materials.append(material_cache[material.name])
        h.update(geometry.canonical(materials).encode())
        result[obj.name] = h.hexdigest()
    return result


def capture(source, directory, mode, spec, dependency_hashes):
    """Reload an immutable source for each independent baseline/one fixed edit."""
    package.require_dependencies(dependency_hashes)
    directory.mkdir()
    controller = OperatingPoseController.prepare(source).freeze()
    geometry.require(controller.derivative_corrections == spec['derivative_corrections'],
                     'Existing operating correction replay changed')
    if mode != 'original_rest':
        controller.apply('roof_transfer',1)
        controller.apply('inner_door',1,reset=False)
        controller.apply('seat_boarding',1,reset=False)
    originals = sorted([o for o in bpy.context.scene.objects if base.keep(o)], key=lambda o: o.name)
    objects = {o.name:o for o in originals}
    oldroster = next(g['source_objects'] for g in spec['groups'] if g['id'] == 'seat_lift')
    actual = [o.name for o in originals if classify_motion_group(ancestry(o)) == 'seat_lift']
    geometry.require(len(originals) == 1746 and len(actual) == 64 and set(actual) == set(oldroster),
                     'Selected exact source roster changed')
    seat = [objects[n] for n in oldroster]
    before = source_signatures(originals)
    restraint = ['Shoulder restraint','Shoulder restraint.001','Lap restraint','Lap restraint.001',
                 'Anti-submarining strap','Five-point buckle','Buckle release']
    raw_original = {n:[v.co.copy() for v in objects[n].data.vertices] for n in restraint}
    mani = objects['Seat service manifold']; old_mani = mani.location.copy()
    source_frames = {n:{'parent':objects[n].parent.name,'local':columns(objects[n].matrix_local),
                       'world_canonical':columns(CONVERT@objects[n].matrix_world)} for n in actual}
    created, edits, connectors, settings, material_settings = [], [], [], [], []
    collection = None
    try:
        if mode == 'corrected_closed':
            for n in restraint[:4]:
                o = objects[n]
                old = np.array([list(v.co) for v in o.data.vertices])
                roots = old[:2].copy()
                if n.startswith('Shoulder'):
                    geometry.require(all((old[k, 2] == float(np.float32(0.93)) for k in (2, 3))), 'Fixed source correction/export guard refused')
                    for k in (2, 3):
                        o.data.vertices[k].co.z = 1
                layer = {'Shoulder restraint': 0.194, 'Shoulder restraint.001': 0.206, 'Lap restraint': 0.198, 'Lap restraint.001': 0.202}[n]
                geometry.require(all((old[k, 1] == float(np.float32(0.2)) for k in (-2, -1))), 'Fixed source correction/export guard refused')
                for k in (-2, -1):
                    o.data.vertices[k].co.y = layer
                o.data.update()
                new = np.array([list(v.co) for v in o.data.vertices])
                geometry.require(np.array_equal(roots, new[:2]), 'Fixed source correction/export guard refused')

                def lengths(v):
                    return np.linalg.norm(np.diff((v[::2] + v[1::2]) / 2, axis=0), axis=1).tolist()
                edits.append({'source_object': n, 'old_vertices': old.tolist(), 'new_vertices': new.tolist(), 'original_segment_lengths_metres': lengths(old), 'corrected_segment_lengths_metres': lengths(new), 'old_total_length_metres': sum(lengths(old)), 'new_total_length_metres': sum(lengths(new))})
            mani.location.x += 0.08
            mani.location.z += 0.14
            bpy.context.view_layer.update()
            parent = objects[restraint[0]].parent
            inverse = (CONVERT @ parent.matrix_world).inverted()
            for n, anchor, side in [('Shoulder restraint', 'Harness shoulder anchor', 'port'), ('Shoulder restraint.001', 'Harness shoulder anchor.001', 'starboard')]:
                root = (raw_original[n][0] + raw_original[n][1]) / 2
                ev = objects[anchor].evaluated_get(bpy.context.evaluated_depsgraph_get())
                mesh = ev.to_mesh()
                mesh.calc_loop_triangles()
                canonical = [list(CONVERT @ objects[anchor].matrix_world @ p.co) for p in mesh.vertices]
                v = np.array([list(inverse @ Vector(p)) for p in canonical])
                hits = []
                for tri in mesh.loop_triangles:
                    q = v[list(tri.vertices)]
                    A = np.column_stack([q[1, [0, 2]] - q[0, [0, 2]], q[2, [0, 2]] - q[0, [0, 2]]])
                    if np.linalg.det(A) == 0:
                        continue
                    uv = np.linalg.solve(A, np.array([root.x, 1]) - q[0, [0, 2]])
                    if uv[0] >= 0 and uv[1] >= 0 and (sum(uv) <= 1):
                        hits.append(float(q[0, 1] + uv[0] * (q[1, 1] - q[0, 1]) + uv[1] * (q[2, 1] - q[0, 1])))
                ev.to_mesh_clear()
                geometry.require(hits, 'Fixed source correction/export guard refused')
                front = max(hits)
                geometry.require(front < float(root.y), 'Fixed source correction/export guard refused')
                verts = [(x, y, z) for z in (0.999, 1.001) for y in (front, float(root.y)) for x in (float(root.x) - 0.026, float(root.x) + 0.026)]
                faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
                data = bpy.data.meshes.new('Corrected closed shoulder connector ' + side)
                data.from_pydata(verts, [], faces)
                data.update()
                data.materials.append(objects[anchor].data.materials[0])
                obj = bpy.data.objects.new('Corrected restraint shoulder connector ' + side, data)
                bpy.context.scene.collection.objects.link(obj)
                obj.parent = parent
                created.append(obj)
                connectors.append({'source_object': obj.name, 'anchor': anchor, 'ribbon': n, 'actual_source_vertices': [list(v.co) for v in data.vertices], 'actual_bridge_length_metres': float(root.y) - front, 'borrowed_material': data.materials[0].name})
            bpy.context.view_layer.update()
        observed_manifold = {'location': list(mani.location), 'matrix_local': columns(mani.matrix_local), 'world_canonical': columns(CONVERT @ mani.matrix_world)}
        for obj in seat:
            if obj.type == 'CURVE':
                for k, limit in [('resolution_u', 4), ('bevel_resolution', 1)]:
                    settings.append((obj.data, k, getattr(obj.data, k)))
                    setattr(obj.data, k, min(getattr(obj.data, k), limit))
            for m in obj.modifiers:
                if m.type == 'BEVEL':
                    settings.append((m, 'segments', m.segments))
                    m.segments = min(m.segments, 2)
                if m.type == 'SUBSURF':
                    settings.extend([(m, 'levels', m.levels), (m, 'render_levels', m.render_levels)])
                    m.levels = min(m.levels, 1)
                    m.render_levels = m.levels
        bpy.context.view_layer.update()
        collection = bpy.data.collections.new('Corrected closed temporary export')
        bpy.context.scene.collection.children.link(collection)
        copies = {}
        source_geometry = {}
        for obj in seat + created:
            ev = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
            mesh = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=bpy.context.evaluated_depsgraph_get())
            uv = np.zeros(len(mesh.loops) * 2, dtype=np.float32)
            if mesh.uv_layers.active:
                mesh.uv_layers.active.data.foreach_get('uv', uv)
            for layer in list(mesh.uv_layers):
                mesh.uv_layers.remove(layer)
            mesh.uv_layers.new(name='UVMap').data.foreach_set('uv', uv)
            for mat in mesh.materials:
                if mat and mat.use_nodes:
                    for node in mat.node_tree.nodes:
                        if node.type in ('NORMAL_MAP', 'UVMAP'):
                            material_settings.append((node, node.uv_map))
                            node.uv_map = 'UVMap'
            mesh.transform(obj.matrix_world)
            mesh.calc_loop_triangles()
            copy = bpy.data.objects.new('Derived ' + obj.name, mesh)
            collection.objects.link(copy)
            copies[obj.name] = copy
            coords = [[float(v.co.x), float(v.co.z), float(-v.co.y)] for v in mesh.vertices]
            source_geometry[obj.name] = {'source_type': obj.type, 'source_parent': obj.parent.name if obj.parent else None, 'vertices': coords, 'triangles': [list(t.vertices) for t in mesh.loop_triangles], 'triangle_materials': [mesh.materials[t.material_index].name if mesh.materials[t.material_index] else None for t in mesh.loop_triangles], 'triangle_uv_gltf': [[[float(uv[2 * k]), float(np.float32(1) - uv[2 * k + 1])] for k in t.loops] for t in mesh.loop_triangles], 'source_world_canonical': columns(CONVERT @ obj.matrix_world), 'source_signature_before': before.get(obj.name), 'introduced_connector': obj in created}
        groups = {}
        if mode != 'corrected_closed':
            for i, n in enumerate(oldroster):
                groups['BaselineSeat%02d' % i] = [n]
        else:
            fixed = {'WFCClosedShoulderPort': 'Shoulder restraint', 'WFCClosedShoulderStarboard': 'Shoulder restraint.001', 'WFCClosedLapPort': 'Lap restraint', 'WFCClosedLapStarboard': 'Lap restraint.001', 'WFCClosedCrotch': 'Anti-submarining strap', 'WFCClosedBuckle': 'Five-point buckle', 'WFCClosedBuckleRelease': 'Buckle release', 'WFCClosedManifold': 'Seat service manifold', 'WFCClosedConnectorPort': 'Corrected restraint shoulder connector port', 'WFCClosedConnectorStarboard': 'Corrected restraint shoulder connector starboard'}
            for node, n in fixed.items():
                groups[node] = [n]
            groups['WFCClosedSeatResidual'] = [n for n in oldroster if n not in restraint + ['Seat service manifold']]
            geometry.require(len(groups) == 11 and len(groups['WFCClosedSeatResidual']) == 56, 'Fixed source correction/export guard refused')
        bpy.ops.object.select_all(action='DESELECT')
        batches = []
        for node, members in groups.items():
            for n in members:
                copies[n].select_set(True)
            bpy.context.view_layer.objects.active = copies[members[0]]
            if len(members) > 1:
                bpy.ops.object.join()
            obj = copies[members[0]]
            obj.name = node
            obj['corrected_closed_state'] = mode
            obj['source_sha256'] = SOURCE_SHA
            obj['source_member_count'] = len(members)
            geometry.require(obj.parent is None and obj.matrix_world == Matrix.Identity(4), 'Fixed source correction/export guard refused')
            batches.append(obj)
            obj.select_set(False)
        for o in batches:
            o.select_set(True)
        model = directory / 'model.glb'
        bpy.ops.export_scene.gltf(filepath=str(model), export_format='GLB', use_selection=True, export_apply=True, export_animations=False, export_cameras=False, export_lights=False, export_yup=True, export_extras=True)
    finally:
        # Every source mutation is recorded before assignment; cleanup also runs
        # when preparation, exporter, attribution or later proof refuses.
        for n, values in raw_original.items():
            for vertex, old in zip(objects[n].data.vertices, values):
                vertex.co = old
            objects[n].data.update()
        mani.location = old_mani
        for target, key, value in reversed(settings):
            setattr(target, key, value)
        for node, value in reversed(material_settings):
            node.uv_map = value
        if collection is not None:
            for obj in list(collection.objects):
                mesh = obj.data
                bpy.data.objects.remove(obj, do_unlink=True)
                if mesh.users == 0:
                    bpy.data.meshes.remove(mesh)
            bpy.data.collections.remove(collection)
        for obj in created:
            mesh = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            bpy.data.meshes.remove(mesh)
        bpy.context.view_layer.update()
        after = source_signatures(originals)
        geometry.require(before == after, 'Covered original source settings/geometry restoration failed')
        geometry.require(sha(source) == SOURCE_SHA, 'Source master byte identity changed')
        package.require_dependencies(dependency_hashes)
    relative_model = ('model.glb' if mode == 'corrected_closed' else
                      'evidence/original-rest.glb' if mode == 'original_rest' else 'evidence/original-posed.glb')
    restoration = {'objects': len(before),
                   'signature_sha256': geometry.digest(geometry.canonical(before).encode()),
                   'signature_fields': SIGNATURE_FIELDS}
    report = {'schema': 'apsis.corrected-closed-source-snapshot/1', 'mode': mode,
              'source_sha256': SOURCE_SHA, 'producer_sha256': sha(__file__),
              'blender_version': '.'.join(str(part) for part in bpy.app.version),
              'blender_version_display': bpy.app.version_string,
              'namespace': geometry.NAMESPACE,
              'operating_tuple': [0,0,0,0] if mode == 'original_rest' else [1,1,1,0],
              'model': {'file': relative_model, 'sha256': sha(model), 'bytes': model.stat().st_size},
              'groups': [{'node': node, 'source_objects': members} for node,members in groups.items()],
              'original_seat_objects': 64, 'introduced_connectors': len(connectors),
              'residual_original_objects': 56 if mode == 'corrected_closed' else None,
              'prepared_source_geometry': source_geometry,
              'source_frame_records_before_edits': source_frames,
              'declared_edits': edits, 'connector_records': connectors,
              'manifold': {'nominal_displacement_metres': [.08,0,.14] if mode == 'corrected_closed' else [0,0,0],
                           'original_location': list(old_mani),
                           'edited_fields_observed_before_restore': observed_manifold},
              'all_1746_original_signatures_restored': True,
              'original_signatures_before_and_restored': before,
              'restoration': restoration, 'source_file_unchanged': True,
              'export_preparation': 'Original operating curve/bevel/subsurf caps; active UVMap preserved; world bake before flat export; no decimation.',
              'not_admitted': True, 'limits': geometry.LIMITS}
    write(directory/'producer.json', report)
    print('CORRECTED_CLOSED_CAPTURE', mode, len(before), 'covered original signatures restored',
          len(source_geometry), 'members', sha(model), flush=True)
    return report, model.read_bytes()


def build(source, output, old_package):
    source, output, reference, reference_manifest, spec = original.preflight(source, output, old_package)
    geometry.require(sha(TOOLS/'wayfarer_flight_export_base.py') == BASE_EXPORTER_SHA256,
                     'Qualified base exporter changed')
    dependencies = package.dependency_snapshot()
    old_raw = original.old_model(reference, reference_manifest)
    old_rows = geometry.node_faces(geometry.decode_glb(old_raw), 'WFOpSeatLift')
    staging = Path(tempfile.mkdtemp(prefix='.corrected-closed-', dir=output.parent))
    try:
        work = staging/'.capture'; work.mkdir()
        rest, rest_raw = capture(source, work/'original_rest', 'original_rest', spec, dependencies)
        posed, posed_raw = capture(source, work/'original_posed', 'original_posed', spec, dependencies)
        corrected, corrected_raw = capture(source, work/'corrected_closed', 'corrected_closed', spec, dependencies)
        rest_decoded = geometry.decode_glb(rest_raw)
        rest_rows = [row for g in rest['groups'] for row in geometry.node_faces(rest_decoded, g['node'])]
        posed_decoded = geometry.decode_glb(posed_raw)
        posed_rows = {g['source_objects'][0]: geometry.node_faces(posed_decoded, g['node']) for g in posed['groups']}
        contact, attribution, verification = geometry.verify_emitted(
            corrected, corrected_raw, {'rows_by_object': posed_rows}, rest_rows, old_rows)
        static = proof.prove_static_composite(contact)
        # Archive independent baseline bytes/metadata; remove all work artifacts
        # before the closed final payload roster is verified and installed.
        evidence = staging/'evidence'; evidence.mkdir()
        (evidence/'original-rest.glb').write_bytes(rest_raw)
        (evidence/'original-posed.glb').write_bytes(posed_raw)
        write(evidence/'original-rest.json', rest)
        write(evidence/'original-posed.json', posed)
        (staging/'model.glb').write_bytes(corrected_raw)
        write(staging/'producer.json', corrected)
        write(staging/'contact.json', contact)
        write(staging/'face-attribution.json', attribution)
        write(staging/'finite-volume-proof.json', static['volume'])
        write(staging/'finite-attachment-proof.json', static['attachments'])
        write(staging/'static-composite.json', static['semantics'])
        verification['glb_audit'] = audit(staging/'model.glb')
        write(staging/'verification.json', verification)
        shutil.rmtree(work)
        package.require_dependencies(dependencies)
        return package.finalize(staging, output, source, reference, reference_manifest, dependencies)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--old-package', type=Path, default=TOOLS.parent/'assets/native/wayfarer-operating-02')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    try:
        evidence = build(args.source, args.output_dir, args.old_package)
    except (ValueError, OSError, KeyError, TypeError, IndexError, UnicodeError, RecursionError) as error:
        parser.exit(1, 'Corrected-closed export refused: '+str(error)+'\n')
    print('WAYFARER_CORRECTED_CLOSED_EXPORTED', json.dumps(evidence, sort_keys=True), flush=True)


if __name__ == '__main__':
    main()
