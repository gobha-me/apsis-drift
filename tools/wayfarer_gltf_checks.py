#!/usr/bin/env python3
"""Validate exported texture coordinate references before native rendering."""
import json,struct
from pathlib import Path

def texture_coordinates(value):
    if isinstance(value,dict):
        for key,item in value.items():
            if key.endswith('Texture') and isinstance(item,dict) and 'index' in item:
                yield item.get('extensions',{}).get('KHR_texture_transform',{}).get('texCoord',item.get('texCoord',0))
            else:yield from texture_coordinates(item)
    elif isinstance(value,list):
        for item in value:yield from texture_coordinates(item)

def validate(document):
    checked=0
    for mesh in document['meshes']:
        for primitive in mesh['primitives']:
            if 'material' not in primitive:continue
            for coordinate in texture_coordinates(document['materials'][primitive['material']]):
                if type(coordinate) is not int or coordinate<0 or 'TEXCOORD_'+str(coordinate) not in primitive['attributes']:
                    raise ValueError('Texture has missing/invalid UV channel: '+str(coordinate))
                checked+=1
    if checked==0:raise ValueError('No textured primitives survived export')
    return {'valid_texture_bindings':checked}

def check(path):
    with Path(path).open('rb') as f:
        magic,version,length=struct.unpack('<III',f.read(12))
        if magic!=0x46546c67 or version!=2:raise ValueError('Expected glTF 2 GLB')
        n,kind=struct.unpack('<II',f.read(8))
        if kind!=0x4e4f534a or n>length-20:raise ValueError('Invalid GLB JSON chunk')
        return validate(json.loads(f.read(n)))

if __name__=='__main__':
    import sys
    print(json.dumps(check(sys.argv[1])))
