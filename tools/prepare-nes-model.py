"""Prepare the supplied NES STL for realtime rendering (offline build tool).
Requires numpy, trimesh, pymeshlab. Original STL stays untouched.
Usage: python tools/prepare-nes-model.py SOURCE.stl OUTPUT_DIR
"""
import argparse, json, struct
from pathlib import Path
import numpy as np
import pymeshlab
import trimesh

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
original = trimesh.load(args.source, force="mesh")
report = {"source_triangles": len(original.faces), "source_bounds": original.bounds.tolist(), "levels": []}
for name, budget in [("nes-cart", 600), ("nes-cart-small", 300)]:
    meshes = pymeshlab.MeshSet()
    meshes.load_new_mesh(str(args.source))
    meshes.meshing_decimation_quadric_edge_collapse(targetfacenum=budget,
        preservetopology=True, preservenormal=True, planarquadric=True)
    current = meshes.current_mesh()
    mesh = trimesh.Trimesh(current.vertex_matrix(), current.face_matrix())
    assert mesh.is_watertight and mesh.is_winding_consistent
    assert len(mesh.faces) <= budget and np.isfinite(mesh.vertices).all()
    # Editable source-space export; STL has no specified units or UVs.
    mesh.export(args.output / (name + ".stl"))
    # Runtime coordinates: centered, height 1, X right / Y up / Z depth.
    v = mesh.vertices - original.bounds.mean(axis=0)
    v = v[:, [0, 2, 1]] / original.extents[2]
    # Axis swap reverses handedness: reverse each face to preserve outward winding.
    runtime = trimesh.Trimesh(v, mesh.faces[:, ::-1], process=False)
    runtime.export(args.output / (name + ".obj"))
    # Portable LE indexed geometry, explicitly decoded by a future loader.
    # Header: magic UFM1, uint32 vertex count, uint32 triangle count.
    # Payload: float32 xyz vertices followed by uint16 triangle indices.
    assert len(v) < 65536
    data = struct.pack("<4sII", b"UFM1", len(v), len(mesh.faces))
    data += np.asarray(v, dtype="<f4").tobytes()
    data += np.asarray(runtime.faces, dtype="<u2").tobytes()
    (args.output / (name + ".ufm")).write_bytes(data)
    report["levels"].append({"name":name,"vertices":len(v),"triangles":len(mesh.faces),
        "indexed_bytes":len(data),"watertight":mesh.is_watertight,
        "volume_change_percent":100*(mesh.volume/original.volume-1)})
(args.output / "geometry-report.json").write_text(json.dumps(report, indent=2)+"\n")
print(json.dumps(report, indent=2))
