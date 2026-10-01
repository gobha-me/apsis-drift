"""Bounded binary audit for the static operating derivative. BSD-3-Clause."""
import json
import math
from pathlib import Path
import struct

MAX_GLB_BYTES = 80 * 1024 * 1024
MAX_JSON_BYTES = 8 * 1024 * 1024
MAX_ELEMENTS = 4_000_000
COMPONENTS = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
              5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def require(value, message):
    if not value:
        raise ValueError(message)


def integer(value, low, high):
    return type(value) is int and low <= value <= high


def audit(path: Path):
    path = Path(path)
    size = path.stat().st_size
    require(28 <= size <= MAX_GLB_BYTES, "Produced GLB dimensions exceed bounds")
    with path.open("rb") as stream:
        magic, version, declared = struct.unpack("<III", stream.read(12))
        require((magic, version, declared) == (0x46546C67, 2, size), "Produced GLB header mismatch")
        count, kind = struct.unpack("<II", stream.read(8))
        require(kind == 0x4E4F534A and count <= MAX_JSON_BYTES and count % 4 == 0 and count <= size - 28,
                "Produced GLB JSON dimensions invalid")
        document = json.loads(stream.read(count))
        count, kind = struct.unpack("<II", stream.read(8))
        require(kind == 0x004E4942 and count % 4 == 0 and count == size - stream.tell(),
                "Produced GLB binary dimensions invalid")
        payload = stream.read(count)
    require(type(document) is dict, "Produced GLB JSON must be an object")
    buffers = document.get("buffers")
    require(type(buffers) is list and len(buffers) == 1 and type(buffers[0]) is dict and "uri" not in buffers[0],
            "Produced GLB must contain one embedded buffer")
    length = buffers[0].get("byteLength")
    require(integer(length, 1, len(payload)) and len(payload) - length <= 3,
            "Produced GLB buffer declaration invalid")
    views = document.get("bufferViews")
    accessors = document.get("accessors")
    require(type(views) is list and 1 <= len(views) <= 10000 and
            type(accessors) is list and 1 <= len(accessors) <= 10000,
            "Produced GLB buffer table dimensions invalid")
    for view in views:
        require(type(view) is dict, "Produced GLB buffer view must be an object")
        start, count = view.get("byteOffset", 0), view.get("byteLength")
        require(integer(view.get("buffer"), 0, 0) and integer(start, 0, length) and
                integer(count, 1, length) and start + count <= length,
                "Produced GLB buffer view crosses bounds")
    decoded = []
    total_elements = 0
    total_components = 0
    for accessor in accessors:
        require(type(accessor) is dict, "Produced GLB accessor must be an object")
        require("sparse" not in accessor and accessor.get("componentType") in COMPONENTS and
                accessor.get("type") in WIDTHS, "Unsupported produced GLB accessor")
        index, count = accessor.get("bufferView"), accessor.get("count")
        require(integer(index, 0, len(views) - 1) and integer(count, 1, MAX_ELEMENTS),
                "Produced GLB accessor dimensions invalid")
        view = views[index]
        token, size = COMPONENTS[accessor["componentType"]]
        width = WIDTHS[accessor["type"]]
        total_elements += count
        total_components += count * width
        require(total_elements <= 8_000_000 and total_components <= 16_000_000,
                "Produced GLB aggregate buffer dimensions exceed bounds")
        item = size * width
        offset, stride = accessor.get("byteOffset", 0), view.get("byteStride", item)
        require(integer(offset, 0, view["byteLength"]) and integer(stride, item, 252) and
                offset % size == 0 and stride % size == 0 and
                offset + (count - 1) * stride + item <= view["byteLength"],
                "Produced GLB accessor crosses buffer boundaries")
        base = view.get("byteOffset", 0) + offset
        unpacker = struct.Struct("<" + token * width)
        values = []
        for element in range(count):
            value = unpacker.unpack_from(payload, base + element * stride)
            require(all(math.isfinite(component) for component in value), "Produced GLB contains nonfinite state")
            values.append(value)
        decoded.append(values)
    primitives, triangles = 0, 0
    meshes = document.get("meshes")
    require(type(meshes) is list and 1 <= len(meshes) <= 128, "Produced GLB mesh dimensions invalid")
    for mesh in meshes:
        require(type(mesh) is dict and type(mesh.get("primitives")) is list and
                1 <= len(mesh["primitives"]) <= 1024, "Produced GLB mesh primitives invalid")
        for primitive in mesh["primitives"]:
            require(type(primitive) is dict, "Produced GLB primitive must be an object")
            attrs = primitive.get("attributes", {})
            require(type(attrs) is dict, "Produced GLB attributes must be an object")
            position, index = attrs.get("POSITION"), primitive.get("indices")
            require(primitive.get("mode", 4) == 4 and integer(position, 0, len(accessors) - 1) and
                    integer(index, 0, len(accessors) - 1), "Produced GLB primitive dimensions invalid")
            require(accessors[position]["componentType"] == 5126 and accessors[position]["type"] == "VEC3",
                    "Produced GLB position must be finite float3")
            require(accessors[index]["componentType"] in (5121, 5123, 5125) and accessors[index]["type"] == "SCALAR",
                    "Produced GLB indices must be unsigned scalar")
            require(len(decoded[index]) % 3 == 0 and
                    all(0 <= value[0] < len(decoded[position]) for value in decoded[index]),
                    "Produced GLB triangle index crosses vertex boundary")
            for attr in attrs.values():
                require(integer(attr, 0, len(accessors) - 1) and len(decoded[attr]) == len(decoded[position]),
                        "Produced GLB attribute dimensions disagree")
            primitives += 1
            triangles += len(decoded[index]) // 3
    require(1 <= primitives <= 1024 and 1 <= triangles < 1_500_000, "Produced GLB primitive budget invalid")
    return {"pass": True, "glb_bytes": path.stat().st_size, "buffer_views": len(views),
            "accessors": len(accessors), "meshes": len(meshes),
            "primitives": primitives, "triangles": triangles,
            "all_float_attributes_finite": True, "all_triangle_indices_in_bounds": True}
