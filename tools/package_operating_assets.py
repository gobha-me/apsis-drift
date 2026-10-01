#!/usr/bin/env python3
"""Package qualified operating exports without changing their authoring sources.

No providers, model edits or master saves. Reuses the archived selected-design
license/lineage records from the independently preserved starter delivery.
"""
import argparse
import json
import lzma
from pathlib import Path
import shutil
import tempfile

import prepare_native_assets as base
import prepare_operating_assets as assets
from package_native_starter import archived


def build(operating, contact, destination, starter):
    operating, contact, destination = map(base.no_symlinks, (operating, contact, destination))
    starter, _, starter_hash, _ = base.verify(starter)
    base.require(not destination.exists(), "package destination already exists")
    models = {"model": base.no_symlinks(operating / assets.MODEL),
              "station_model": base.no_symlinks(contact / assets.STATION_MODEL)}
    for model in models.values():
        base.require(model.is_file(), "operating model is missing")
        base.size(model.stat().st_size, assets.MAX_MODEL)
    inputs = {"wayfarer-operating-02.json": operating / "wayfarer-operating-02.json",
              **{name: contact / name for name in ("contact.json", "station-closure.json", "qualification.json")}}
    for path in inputs.values():
        assets.load_json(base.no_symlinks(path))
    export_proof = assets.load_json(base.no_symlinks(operating / "export-checks.json"))
    evidence_path = base.no_symlinks(contact / "boarding-evidence.json")
    evidence = assets.load_json(evidence_path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".operating-package-", dir=destination.parent))
    manifest = {"schema": assets.SCHEMA, "package_id": assets.PACKAGE_ID,
                "sources": assets.SOURCES, "files": [], "model": {}, "station_model": {}, "metadata": []}

    def record(path):
        manifest["files"].append({"path": path.relative_to(staging).as_posix(),
                                  "bytes": path.stat().st_size, "sha256": base.sha(path)})

    try:
        for name in ("metadata", "licenses", "payloads"):
            (staging / name).mkdir()
        for key, model in models.items():
            chunks = []
            # The temporary compressed stream is not admitted into the roster.
            compressed = staging / "model.xz"
            with model.open("rb") as raw, lzma.open(compressed, "wb", preset=6) as packed:
                shutil.copyfileobj(raw, packed, base.BLOCK)
            with compressed.open("rb") as packed:
                while block := packed.read(base.MAX_CHUNK):
                    name = f"payloads/{key}-{len(chunks):02}.xz-part"
                    path = staging / name
                    path.write_bytes(block)
                    chunks.append(name)
                    record(path)
            compressed.unlink()
            is_craft = key == "model"
            manifest[key] = {"output": model.name, "bytes": model.stat().st_size,
                             "sha256": base.sha(model),
                             "source_sha256": assets.SOURCES["wayfarer" if is_craft else "station"],
                             "license": assets.LICENSE if is_craft else "LicenseRef-Apsis-Station-Kit-Output", "chunks": chunks}
        for name, original in inputs.items():
            path = staging / "metadata" / name
            # Preserve qualified JSON byte identities; no pretty-printing here.
            shutil.copyfile(original, path)
            record(path)
            manifest["metadata"].append({"path": "metadata/" + name, "output": name})
        for name in sorted(assets.LICENSES):
            path = staging / "licenses" / name
            shutil.copyfile(starter / "licenses" / name, path)
            record(path)
        tools = Path(__file__).resolve().parent
        tool_names = ("package_operating_assets.py", "prepare_operating_assets.py",
                      "prepare_native_assets.py", "export_wayfarer_operating.py",
                      "wayfarer_flight_export_base.py", "wayfarer_gltf_checks.py",
                      "wayfarer_operating_spec.py", "wayfarer_operating_blender.py",
                      "wayfarer_operating_glb_audit.py", "export_boarding_contact.py",
                      "qualify_boarding_contact.py", "qualify_boarding_intersections.py",
                      "validate_operating_asset_glb.py", "build_boarding_qualification.py",
                      "station_clearance_blender.py", "qualify_station_clearance.py",
                      "qualify_boarding_bounds.py", "operating_asset_identity.py",
                      "package_native_starter.py")
        provenance = {
            "schema": "apsis.wayfarer-operating-provenance/1",
            "sources": assets.SOURCES,
            "starter_package_sha256": starter_hash,
            "license": assets.LICENSE,
            "scope": "Source-bound operating derivative; no new generation or authoring-master changes; read-only mechanism inspection, not player boarding",
            "compression": "XZ preset6, ordered chunks at most32 MiB; exact decoded GLB bytes",
            "tool_sha256": {name: base.sha(tools / name) for name in tool_names},
            "export_checks": export_proof,
            "export_checks_sha256": base.sha(operating / "export-checks.json"),
            "boarding_evidence": archived(evidence),
            "boarding_evidence_sha256": base.sha(evidence_path),
            "lineage": base.load_json(starter / "metadata/authoring-lineage.json"),
        }
        path = staging / "metadata/provenance.json"
        path.write_text(json.dumps(provenance, indent=2, allow_nan=False) + "\n")
        record(path)
        manifest["metadata"].append({"path": "metadata/provenance.json", "output": "provenance.json"})
        manifest["files"].sort(key=lambda entry: entry["path"])
        manifest["metadata"].sort(key=lambda entry: entry["output"])
        (staging / "package.json").write_text(json.dumps(manifest, indent=2, allow_nan=False) + "\n")
        assets.verify(staging)
        with tempfile.TemporaryDirectory(prefix=".operating-check-", dir=destination.parent) as check:
            for key in models:
                base.unpack_model(staging, manifest[key], Path(check) / manifest[key]["output"])
            assets.validate_decoded(Path(check), staging / "metadata")
        base.require(all(base.sha(path) == base.sha(staging / "metadata" / name)
                         for name, path in inputs.items()), "input metadata changed during packaging")
        base.require(all(base.sha(model) == manifest[key]["sha256"] for key, model in models.items()),
                     "input model changed during packaging")
        base.require(not destination.exists() and not destination.is_symlink(), "destination appeared during packaging")
        assets.install_new(staging, destination)
        print(json.dumps({"package_id": assets.PACKAGE_ID, "package_sha256": base.sha(destination / "package.json"),
                          "model_sha256": manifest["model"]["sha256"], "files": len(manifest["files"])}))
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--operating-export", type=Path, required=True)
    parser.add_argument("--contact-export", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--starter-package", type=Path, required=True)
    args = parser.parse_args()
    try:
        build(args.operating_export, args.contact_export, args.destination, args.starter_package)
    except (OSError, ValueError, KeyError, EOFError, lzma.LZMAError) as error:
        parser.exit(1, "Operating packaging refused: " + str(error) + "\n")


if __name__ == "__main__":
    main()
