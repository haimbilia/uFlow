"""Prepare the supplied Sega print meshes as display assets, leaving originals intact."""
from pathlib import Path
import argparse,json,hashlib,trimesh,pymeshlab,numpy as np
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
paths=[a.source/'SEGA-32X_CartridgeShell-Bottom.stl',a.source/'SEGA-32X_CartridgeShell-Top.stl',a.source/'Genesis_Cartridge.stl']
bottom,top,genesis=[trimesh.load(f,force='mesh') for f in paths]
m=trimesh.boolean.union([bottom,top],engine='manifold')
m.apply_transform([[-1,0,0,0],[0,0,-1,0],[0,1,0,0],[0,0,0,1]])
core=trimesh.creation.box([108,67,12.5]);core.apply_translation([0,-.5,8.25]);m=trimesh.boolean.union([m,core],engine='manifold')
# Drop enclosed negative-volume cavity surfaces, retaining the exterior component.
m=max(m.split(),key=lambda part:part.volume)
# Tiny moulded rear text is unsuitable for the display LOD. A clean panel can
# receive the common rear label; preserve the visible screw recess locations.
panel=trimesh.creation.box([96,61,2]);panel.apply_translation([0,.5,1]);m=trimesh.boolean.union([m,panel],engine='manifold')
cut=[]
for x in [40.1229,-41.3771]:
 c=trimesh.creation.cylinder(radius=3.64,height=2.5,sections=24);c.apply_translation([x,-.425,-.25]);cut.append(c)
m=trimesh.boolean.difference([m,*cut],engine='manifold');m=max(m.split(),key=lambda part:part.volume)
reports=[]
for system,source,budgets,input_count in [('32x',m,[1800,900],len(bottom.faces)+len(top.faces)),('genesis',genesis,[1000,600],len(genesis.faces))]:
 dest=a.output/system;dest.mkdir(parents=True,exist_ok=True);report={'system':system,'source_triangles':input_count,'dimensions_mm':source.extents.tolist(),'levels':[]}
 for suffix,budget in zip(['','-small'],budgets):
  ms=pymeshlab.MeshSet();ms.add_mesh(pymeshlab.Mesh(source.vertices,source.faces));ms.meshing_remove_duplicate_faces();ms.meshing_remove_null_faces()
  ms.meshing_decimation_quadric_edge_collapse(targetfacenum=budget,preservetopology=True,preservenormal=True,planarquadric=True,preserveboundary=True)
  q=ms.current_mesh();r=trimesh.Trimesh(q.vertex_matrix(),q.face_matrix(),process=False)
  assert r.is_watertight and r.is_winding_consistent and np.isfinite(r.vertices).all()
  assert len(r.faces)<=budget+100
  # Retain a millimetre STL for general reuse; OBJ is normalized for uFlow.
  r.export(dest/(system+'-cart'+suffix+'.stl'));r.vertices=(r.vertices-source.bounds.mean(0))/source.extents[1];r.export(dest/(system+'-cart'+suffix+'.obj'))
  report['levels'].append({'triangles':len(r.faces),'vertices':len(r.vertices),'watertight':True,'winding_consistent':True})
 report['source_sha256']={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in (paths[:2] if system=='32x' else paths[2:])}
 (dest/'geometry-report.json').write_text(json.dumps(report,indent=2)+'\n');reports.append(report)
print(json.dumps(reports,indent=2))
