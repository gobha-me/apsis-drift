#!/usr/bin/env python3
"""Non-destructive Wayfarer flight derivative and measured anchors. BSD-3-Clause."""
import hashlib
import argparse
import json
import math
import sys
from collections import defaultdict
from pathlib import Path
import bpy
import numpy as np
from mathutils import Matrix, Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from check_hopper_gltf import check as check_texture_bindings

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/visual/hopper-craft-09.blend'
OUT = ROOT / 'assets/visual/hopper-wayfarer-01.glb'
CONTRACT = OUT.with_suffix('.json')
CONVERT = Matrix(((1,0,0,0),(0,0,1,0),(0,-1,0,0),(0,0,0,1)))
# Runtime seat fitting after native pilot feedback. Use the authored lift/slide
# so the visible seat, armrests and controls travel with the eye.
SEAT_RAISE = .18
SEAT_FORWARD = .08


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def transform(m):
    return [list(m.col[i][:3]) for i in range(4)]


def keep(o):
    if o.type not in ('MESH','CURVE','FONT') or o.hide_render or o.hide_get() or o.hide_viewport:
        return False
    if o.name.startswith(('FIT |','REVIEW |','SENSOR |')):
        return False
    if o.name.startswith('WF03 |') and (' UI ' in o.name or 'flight attitude' in o.name or 'flight vector' in o.name):
        return False
    return o.name not in ['WF02 | '+role+' display' for role in ('NAV','FLIGHT','SYSTEMS')]


def main():
    global SOURCE, OUT, CONTRACT
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--output-dir', type=Path, default=OUT.parent)
    options = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    SOURCE = options.source.resolve()
    options.output_dir.mkdir(parents=True, exist_ok=True)
    OUT = options.output_dir / 'hopper-wayfarer-01.glb'
    CONTRACT = OUT.with_suffix('.json')
    source_hash = sha(SOURCE)
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    scene=bpy.context.scene;scene.frame_set(100);bpy.context.view_layer.update()
    originals=[o for o in scene.objects if keep(o)]
    # Capture rigid gear motion independently of the author's 600-frame film.
    gear=[o for o in originals if o.get('hopper_craft_gear01') and o.get('gear_role') in ('moving','door')]
    rest={o:o.matrix_world.copy() for o in gear}
    motion={o:[] for o in gear}
    frames=list(range(60,101,2))
    for frame in frames:
        scene.frame_set(frame);bpy.context.view_layer.update()
        for o in gear:
            delta=o.matrix_world @ rest[o].inverted()
            motion[o].append(CONVERT @ delta @ CONVERT.inverted())
    scene.frame_set(100);bpy.context.view_layer.update()
    bpy.data.objects['RIG | fore-aft carriage'].location.y += SEAT_FORWARD
    bpy.data.objects['RIG | seat height adjustment'].location.z += SEAT_RAISE
    bpy.context.view_layer.update()
    eye = CONVERT @ bpy.data.objects['FIT | eye midpoint'].matrix_world.translation
    assert (eye - Vector((0, 1.185+SEAT_RAISE, -2.41-SEAT_FORWARD))).length < .00001
    contract={'schema_version':1,'id':'wayfarer','class':'Hopper','name':'Wayfarer',
              'units':'metres','axes':'Godot +Y up, -Z forward','model':OUT.name,
              'source':str(SOURCE.relative_to(ROOT)),'source_sha256':source_hash,
              'pilot_eye':list(eye),'vertical_fov_degrees':75,
              'seat_adjustment_m':{'up':SEAT_RAISE,'forward':SEAT_FORWARD},
              'screens':[],'gear_preview':{},'flight_pose':'Gear stowed; ramp and hatches closed. Gear preview has no simulation authority.'}
    for page,role in enumerate(('NAV','FLIGHT','SYSTEMS')):
        root=bpy.data.objects['WF02 | '+role+' display assembly']
        # Quad local X/Y are screen right/up; its +Z normal faces the pilot.
        matrix=CONVERT @ root.matrix_world @ Matrix.Translation((0,0,.039))
        contract['screens'].append({'page':page,'role':role,'transform':transform(matrix),
                                   'size':[.48,.275] if role=='FLIGHT' else [.335,.25]})
    signatures={};group_for={}
    for o,poses in motion.items():
        signature=tuple(round(v,5) for m in poses for row in m for v in row)
        if signature not in signatures:
            name='HopperGear%02d'%len(signatures);signatures[signature]=name
            contract['gear_preview'][name]=[transform(m) for m in poses]
        group_for[o]=signatures[signature]
    # Lower curve/bevel tessellation in this process only. No authoring file save.
    for o in originals:
        if o.type=='CURVE':
            o.data.resolution_u=min(o.data.resolution_u,4)
            o.data.bevel_resolution=min(o.data.bevel_resolution,1)
        for mod in o.modifiers:
            if mod.type=='BEVEL':mod.segments=min(mod.segments,2)
            if mod.type=='SUBSURF':mod.levels=min(mod.levels,1);mod.render_levels=mod.levels
    bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()
    groups=defaultdict(list);source_triangles=0;reductions=[]
    export_collection=bpy.data.collections.new('Hopper runtime derivative');scene.collection.children.link(export_collection)
    for index,o in enumerate(originals):
        evaluated=o.evaluated_get(deps)
        mesh=bpy.data.meshes.new_from_object(evaluated,preserve_all_data_layers=True,depsgraph=deps)
        if not mesh.polygons:
            bpy.data.meshes.remove(mesh);continue
        # Join matches UV layers by NAME. The retained hull used "Preserved
        # Meshy UV" while imported machinery used "UVMap"; the resulting
        # material bindings exported texCoord:-1. Preserve coordinates but
        # normalize the single active map before batching (also on bare parts).
        uv_values=np.zeros(len(mesh.loops)*2,dtype=np.float32)
        if mesh.uv_layers.active:
            mesh.uv_layers.active.data.foreach_get('uv',uv_values)
        for layer in list(mesh.uv_layers):mesh.uv_layers.remove(layer)
        mesh.uv_layers.new(name='UVMap').data.foreach_set('uv',uv_values)
        for mat in mesh.materials:
            if mat and mat.use_nodes:
                for node in mat.node_tree.nodes:
                    if node.type in ('NORMAL_MAP','UVMAP'):node.uv_map='UVMap'
        mesh.transform(o.matrix_world)
        mesh.calc_loop_triangles();before=len(mesh.loop_triangles);source_triangles+=before
        copy=bpy.data.objects.new('Runtime '+o.name,mesh);export_collection.objects.link(copy)
        # Collapse dense generated surfaces only. Authored thin fairings and
        # their paint/thermal boundaries must retain their construction mesh.
        generated_surface=o.get('asset_generated_surface',
            any(mat and mat.name.startswith('Material_0') for mat in mesh.materials))
        if before>5000 and generated_surface:
            cap=55000 if o.name.startswith('ENGINE04 ') else (35000 if 'nose skin' in o.name else 85000)
            target=min(cap,max(3000,int(before*.24)))
            dec=copy.modifiers.new('Derived mesh reduction','DECIMATE');dec.ratio=target/before
            bpy.context.view_layer.objects.active=copy
            bpy.ops.object.modifier_apply(modifier=dec.name)
            reductions.append({'source':o.name,'before_triangles':before,'target_triangles':target})
        group=group_for.get(o,'HopperStructure')
        if 'fitted glass' in o.name:group='HopperGlass'
        elif o.name.startswith('ENGINE04 '):group='HopperEnginePort' if o.get('engine04_side')==-1 else 'HopperEngineStarboard'
        groups[group].append(copy)
        if index%200==0:print('EXPORT_PREP',index,len(originals),flush=True)
    # Join by service/animation role, preserving source UVs and material slots.
    batches=[]
    bpy.ops.object.select_all(action='DESELECT')
    for name,objects in groups.items():
        for o in objects:o.select_set(True)
        bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.join()
        batch=objects[0];batch.name=name;batches.append(batch);batch.select_set(False)
    images=[]
    for im in bpy.data.images:
        texture_limit=int(im.get('runtime_max_size',2048))
        assert texture_limit in (1024,2048,4096,8192), 'Unsupported authored runtime texture limit'
        if im.type=='IMAGE' and max(im.size)>texture_limit:
            old=list(im.size);factor=texture_limit/max(old);im.scale(round(old[0]*factor),round(old[1]*factor))
            images.append({'image':im.name,'source_size':old,'runtime_size':list(im.size)})
    total=0;vertices=0;bounds=[]
    for o in batches:
        o.data.calc_loop_triangles();total+=len(o.data.loop_triangles);vertices+=len(o.data.vertices)
        coords=np.empty(len(o.data.vertices)*3,dtype=np.float32);o.data.vertices.foreach_get('co',coords)
        indices=np.empty(len(o.data.loops),dtype=np.int32);o.data.loops.foreach_get('vertex_index',indices)
        assert np.isfinite(coords).all() and len(coords)>0
        assert len(indices)>0 and indices.min()>=0 and indices.max()<len(coords)//3
        bounds.append((coords.reshape(-1,3).min(0),coords.reshape(-1,3).max(0)))
        o.select_set(True)
    assert total<1500000,('Runtime derivative needs more reduction',total)
    bpy.ops.export_scene.gltf(filepath=str(OUT),export_format='GLB',use_selection=True,
                             export_apply=True,export_animations=False,export_cameras=False,
                             export_lights=False,export_yup=True,export_extras=False)
    contract['model_sha256']=sha(OUT);contract['gear_preview_samples']=len(frames)
    texture_check=check_texture_bindings(OUT)
    CONTRACT.write_text(json.dumps(contract,indent=2)+'\n')
    report={'pass':True,'source_sha256':source_hash,'model_sha256':sha(OUT),'source_objects':len(originals),
            'texture_bindings':texture_check,
            'pilot_eye_from_seat_rig_godot_m':list(eye),'seat_adjustment_m':contract['seat_adjustment_m'],
            'source_evaluated_triangles':source_triangles,'runtime_triangles':total,'vertices':vertices,
            'mesh_batches':len(batches),'gear_motion_groups':len(signatures),'texture_reductions':images,
            'bounds_blender_m':[np.min([b[0] for b in bounds],0).tolist(),np.max([b[1] for b in bounds],0).tolist()],
            'file_bytes':OUT.stat().st_size,'reductions':reductions,
            'scope':'Separate UV-preserving flight derivative. No studio, fit dummy, baked instrument telemetry or fake sensor panorama. Gear motion is an inspection adapter; no physics, pressure or runtime damage changes.'}
    OUT.with_name('hopper-wayfarer-01-export-checks.json').write_text(json.dumps(report,indent=2)+'\n')
    assert sha(SOURCE)==source_hash
    print('HOPPER_RUNTIME_EXPORTED',total,'triangles',len(batches),'batches',OUT.stat().st_size,'bytes',flush=True)


if __name__=='__main__':main()
