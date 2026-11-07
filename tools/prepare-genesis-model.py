"""Prepare the supplied Genesis shell in millimetres, retaining its measured envelope."""
from pathlib import Path
import hashlib,json
import numpy as np
import trimesh,pymeshlab

root=Path(__file__).resolve().parents[1]
source=Path(__import__('sys').argv[1])
mesh=trimesh.load(source,force="mesh")
bounds=mesh.bounds.copy()
assert np.allclose(mesh.extents,[108.5,69.5,16.5],atol=.02)
# Two recessed rear screws, spaced 70 mm as on the standard shell.
cuts=[]
for x in (-35,35):
    cutter=trimesh.creation.cylinder(radius=2.55,height=2.2,sections=24)
    cutter.apply_translation([x,40.25,.4])
    cuts.append(cutter)
shell=trimesh.boolean.difference([mesh,*cuts],engine="manifold")
parts=[shell]
for x in (-35,35):
    screw=trimesh.creation.cylinder(radius=1.65,height=.65,sections=16)
    screw.apply_translation([x,40.25,1.25])
    parts.append(screw)
mesh=trimesh.util.concatenate(parts)
out=root/"assets/models/genesis"
report={"source_sha256":hashlib.sha256(source.read_bytes()).hexdigest(),
        "dimensions_mm":(bounds[1]-bounds[0]).tolist(),"label_mm":[76.4,58.7],
        "screw_spacing_mm":70,"levels":[]}
for suffix,budget in [("",1800),("-small",900)]:
    ms=pymeshlab.MeshSet()
    ms.add_mesh(pymeshlab.Mesh(mesh.vertices,mesh.faces))
    ms.meshing_decimation_quadric_edge_collapse(targetfacenum=budget,
        preservetopology=True,preservenormal=True,planarquadric=True,preserveboundary=True)
    m=ms.current_mesh()
    result=trimesh.Trimesh(m.vertex_matrix(),m.face_matrix())
    assert result.is_watertight and result.is_winding_consistent
    assert np.allclose(result.bounds,bounds,atol=.03)
    result.export(out/f"genesis-cart{suffix}.stl")
    result.vertices=(result.vertices-bounds.mean(0))/69.5
    result.export(out/f"genesis-cart{suffix}.obj")
    report["levels"].append({"triangles":len(result.faces),"vertices":len(result.vertices)})
(out/"geometry-report.json").write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report))
