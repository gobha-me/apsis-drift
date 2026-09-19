#!/usr/bin/env python3
"""BSD-3-Clause optional occupied-cabin study; never overwrites canonical assets.

blender -b --factory-startup --python tools/build_occupied_cabin.py

The canonical near GLB is a merged mesh. Its empty-seat restraint cannot be
hidden by object name at runtime. This derives a companion from the editable
master, excluding exactly two placeholder straps and their buckle. All geometry,
camera anchors and LOD/export settings otherwise use the existing source.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

import bpy

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_hero_assets as art

OUT = ROOT / "build-godot" / "pilot-study"
MASTER = ROOT / "assets" / "visual" / "hero-cockpit.blend"
REMOVALS = {
    "SeatMoving harness": "CURVE",
    "SeatMoving harness.001": "CURVE",
    "SeatMoving harness buckle": "MESH",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    # Fail closed if the master contract changes: no broad name-pattern deletion.
    master_hash = digest(MASTER)
    bpy.ops.wm.open_mainfile(filepath=str(MASTER))
    originals = set(bpy.context.scene.objects.keys())
    restraints = {obj.name for obj in bpy.context.scene.objects
                  if obj.name.startswith("SeatMoving harness")}
    if restraints != set(REMOVALS):
        raise ValueError("Unexpected canonical empty-seat restraint object set")
    removed = []
    for name, kind in REMOVALS.items():
        obj = bpy.context.scene.objects.get(name)
        if obj is None or obj.type != kind or obj.get("detail_tier") != 1:
            raise ValueError("Canonical restraint type/tier changed: "+name)
        removed.append({"name": name, "type": kind, "detail_tier": 1})
        bpy.data.objects.remove(obj, do_unlink=True)
    if originals-set(bpy.context.scene.objects.keys()) != set(REMOVALS):
        raise AssertionError("Objects beyond the placeholder restraint were removed")
    OUT.mkdir(parents=True, exist_ok=True)
    art.OUT = OUT
    # Existing exporter deliberately derives all LODs. Native occupied-preview
    # uses near; no fork of near's triangulation, budget or material conversion.
    art.export_lods("hero-cockpit-occupied")
    if digest(MASTER) != master_hash:
        raise AssertionError("Canonical master changed unexpectedly")
    files = []
    for tier in ("hero", "near", "mid", "far"):
        path = OUT / ("hero-cockpit-occupied-"+tier+".glb")
        raw = path.read_bytes()
        count = struct.unpack_from("<I", raw, 12)[0]
        document = json.loads(raw[20:20+count])
        triangles = sum(document["accessors"][primitive["indices"]]["count"]//3
                        for node in document["nodes"] if "mesh" in node
                        for primitive in document["meshes"][node["mesh"]]["primitives"])
        files.append({"file": path.name, "bytes": len(raw), "triangles": triangles,
                      "sha256": hashlib.sha256(raw).hexdigest()})
    manifest = {
        "schema_version": 1,
        "status": "Optional occupied-cabin presentation study companion",
        "license": "BSD-3-Clause (repository LICENSE.md)",
        "source": "tools/build_occupied_cabin.py",
        "source_sha256": digest(Path(__file__)),
        "source_master": "assets/visual/hero-cockpit.blend",
        "source_master_sha256": master_hash,
        "source_dependencies_sha256": {
            "tools/"+name: digest(ROOT/"tools"/name)
            for name in ("build_hero_assets.py", "build_flight_cell.py", "flight_cell.py")},
        "layout_sha256": digest(ROOT/"experiments/godot-freedom/cockpit-layout.json"),
        "excluded_source_objects": removed,
        "other_source_objects_unchanged": True,
        "export_recipe": "Unmodified build_hero_assets.export_lods; near tiers 0/1, 150000 triangle budget",
        "units": "metres",
        "origin": "Existing cabin datum; same transform as canonical hero-cockpit-near.glb",
        "purpose": "Use with a pilot carrying its own attached restraint, not as the empty-cabin default",
        "limitations": ["Static study companion; no gameplay or camera changes", "Far silhouette proxy already omitted restraint"],
        "external_assets": [],
        "artifacts": files,
    }
    (OUT/"occupied-cabin-provenance.json").write_text(json.dumps(manifest, indent=2)+"\n")
    print("OCCUPIED CABIN STUDY: "+json.dumps(files), flush=True)


if __name__ == "__main__":
    main()
