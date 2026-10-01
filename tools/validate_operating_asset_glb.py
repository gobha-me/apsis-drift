#!/usr/bin/env python3
"""Offline buffer and exact source-binding admission checks. BSD-3-Clause.

These checks validate produced graph structure, not motion qualification or a
license. Approved package identities are enforced separately by the preparer.
"""
import json
import math
from pathlib import Path
import struct

from wayfarer_operating_glb_audit import MAX_JSON_BYTES, audit, integer, require
from wayfarer_operating_spec import MOTION_RIGS, RUNTIME_NODES, SOURCE_SHA256

IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]
STATION_NODES = {'D1_CLEARANCE_PIPE', 'D1_CLEARANCE_DECK', 'D1_CLEARANCE_FLANGE'}
STATION_SOURCE_OBJECTS = {
    'D1_CLEARANCE_PIPE': 'DK09 | equalization pipe.001',
    'D1_CLEARANCE_DECK': 'DK09 | continuous deck with open well.001',
    'D1_CLEARANCE_FLANGE': 'DK09 | belly shaft mounting flange.001',
}
SOURCE_RIGS = {group: rig for rig, group in MOTION_RIGS.items()}


def _unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate produced GLB JSON key')
        result[key] = value
    return result


def _float(value):
    result = float(value)
    require(math.isfinite(result), 'Nonfinite produced GLB JSON number')
    return result


def _invalid(value):
    raise ValueError('Nonfinite produced GLB JSON constant: ' + value)


def _vector(value, expected):
    require(type(value) is list and len(value) == len(expected), 'Invalid node transform dimensions')
    require(all(type(component) in (int, float) and -100 <= component <= 100 and math.isfinite(component) and
                abs(component - target) <= 1e-7 for component, target in zip(value, expected)),
            'Operating mesh must have identity rest transform')


def _document(path):
    # Run the frozen binary audit first; keep malformed decoder failures in the
    # preparer's normal refusal path rather than exposing implementation errors.
    try:
        receipt = audit(path)
    except (TypeError, AttributeError, IndexError, KeyError, RecursionError, struct.error) as error:
        raise ValueError('Malformed produced GLB buffer document') from error
    with Path(path).open('rb') as stream:
        stream.seek(12)
        count, kind = struct.unpack('<II', stream.read(8))
        require(kind == 0x4E4F534A and count <= MAX_JSON_BYTES, 'Invalid produced GLB JSON header')
        raw = stream.read(count)
    try:
        document = json.loads(raw, object_pairs_hook=_unique, parse_float=_float, parse_constant=_invalid)
    except (RecursionError, UnicodeError) as error:
        raise ValueError('Invalid produced GLB JSON encoding/depth') from error
    require(type(document) is dict and type(document.get('asset')) is dict and
            document['asset'].get('version') == '2.0',
            'Unsupported produced GLB asset version')
    require(not any(document.get(key) for key in ('animations', 'skins', 'cameras')),
            'Operating GLB must not create animation, skin or camera authority')
    nodes, meshes, scenes = document.get('nodes'), document.get('meshes'), document.get('scenes')
    require(type(nodes) is list and 1 <= len(nodes) <= 128 and type(meshes) is list and
            1 <= len(meshes) <= 128 and type(scenes) is list and len(scenes) == 1 and
            integer(document.get('scene'), 0, 0), 'Invalid produced GLB graph dimensions')
    scene = scenes[0]
    require(type(scene) is dict and type(scene.get('nodes')) is list and
            len(scene['nodes']) == len(nodes) and
            all(integer(index, 0, len(nodes) - 1) for index in scene['nodes']) and
            len(set(scene['nodes'])) == len(nodes), 'All operating nodes must be unique scene roots')
    names, mesh_ids = set(), set()
    for node in nodes:
        require(type(node) is dict, 'Invalid produced GLB node')
        name = node.get('name')
        require(type(name) is str and 1 <= len(name) <= 256 and '\0' not in name and
                name.replace('.', '_') not in names, 'Ambiguous produced GLB runtime name')
        names.add(name.replace('.', '_'))
        index = node.get('mesh')
        require(integer(index, 0, len(meshes) - 1) and index not in mesh_ids,
                'Missing, duplicate or invalid operating mesh index')
        mesh_ids.add(index)
        require('children' not in node or node['children'] == [], 'Operating meshes must be flat siblings')
        require(not any(key in node for key in ('camera', 'skin', 'weights', 'extensions')),
                'Unexpected operating node authority')
        require('matrix' not in node or not any(key in node for key in ('translation', 'rotation', 'scale')),
                'Mixed node matrix and component transforms')
        for key, expected in (('matrix', IDENTITY), ('translation', [0., 0., 0.]),
                              ('rotation', [0., 0., 0., 1.]), ('scale', [1., 1., 1.])):
            if key in node:
                _vector(node[key], expected)
        require('extras' not in node or type(node['extras']) is dict, 'Invalid produced GLB extras')
    require(mesh_ids == set(range(len(meshes))), 'Unbound produced GLB mesh')
    return document, receipt


def validate_craft_model(path, spec):
    document, binary = _document(path)
    groups = spec.get('groups') if type(spec) is dict else None
    require(type(groups) is list and len(groups) == len(RUNTIME_NODES), 'Missing operating source groups')
    by_id = {}
    for group in groups:
        require(type(group) is dict and type(group.get('id')) is str and group['id'] in RUNTIME_NODES and
                group['id'] not in by_id, 'Duplicate or unknown operating source group')
        require(group.get('runtime_node') == RUNTIME_NODES[group['id']] and
                group.get('source_rig') == SOURCE_RIGS[group['id']], 'Stale operating source binding')
        by_id[group['id']] = group
    found, bindings = set(), []
    reserved = {'operating_group', 'source_rig', 'source_sha256'}
    reverse = {node: group for group, node in RUNTIME_NODES.items()}
    for index, node in enumerate(document['nodes']):
        name, extras = node['name'], node.get('extras', {})
        if name not in reverse:
            require(not reserved.intersection(extras) and not name.startswith('WFOp'),
                    'Unlisted operating identity in produced GLB')
            continue
        group = reverse[name]
        require(group not in found and extras == {'operating_group': group,
                'source_rig': SOURCE_RIGS[group], 'source_sha256': SOURCE_SHA256},
                'Missing or changed operating source identity extras')
        found.add(group)
        bindings.append({'group': group, 'source_rig': SOURCE_RIGS[group],
                         'glb_node_index': index, 'glb_node_name': name,
                         'runtime_node_name': name, 'mesh_index': node['mesh']})
    require(found == set(RUNTIME_NODES), 'Missing produced operating motion group')
    return {'pass': True, 'binary_audit': binary, 'motion_bindings': bindings}


def validate_station_model(path, closure):
    document, binary = _document(path)
    require(len(document['nodes']) == 3 and len(document['meshes']) == 3 and
            {node['name'] for node in document['nodes']} == STATION_NODES,
            'Station clearance derivative must contain exactly three replacement meshes')
    corrections = closure.get('corrections') if type(closure) is dict else None
    require(type(corrections) is list and len(corrections) == 3, 'Missing station source correction bindings')
    source_names, by_node = set(), {}
    for correction in corrections:
        require(type(correction) is dict and type(correction.get('source_object')) is str and
                correction['source_object'] and correction['source_object'] not in source_names,
                'Ambiguous station correction source identity')
        name = correction.get('replacement_node')
        require(type(name) is str and name in STATION_NODES and name not in by_node,
                'Ambiguous station replacement identity')
        require(correction['source_object'] == STATION_SOURCE_OBJECTS[name],
                'Changed station replacement source binding')
        source_names.add(correction['source_object'])
        by_node[name] = correction
    bindings = []
    for index, node in enumerate(document['nodes']):
        require(not node.get('extras'), 'Unexpected station replacement source metadata')
        bindings.append({'source_object': by_node[node['name']]['source_object'],
                         'replacement_node': node['name'], 'glb_node_index': index, 'mesh_index': node['mesh']})
    return {'pass': True, 'binary_audit': binary, 'replacement_bindings': bindings}
