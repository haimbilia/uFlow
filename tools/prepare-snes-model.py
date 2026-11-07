"""Tessellate the user SNES STEP shell, omitting overlapping CAD variants and PCB.
Requires cadquery-ocp 8, trimesh, numpy and pymeshlab. Original stays untouched.
"""
from pathlib import Path
from OCP.STEPControl import STEPControl_Reader
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.TopExp import TopExp_Explorer
from OCP.TopAbs import TopAbs_FACE,TopAbs_REVERSED,TopAbs_SOLID
from OCP.TopoDS import TopoDS
from OCP.BRep import BRep_Tool
from OCP.TopLoc import TopLoc_Location
import trimesh,numpy as np
import pymeshlab,json,hashlib,argparse
p=argparse.ArgumentParser();p.add_argument("source",type=Path);p.add_argument("output",type=Path);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
r=STEPControl_Reader();r.ReadFile(str(args.source));r.TransferRoots();e=TopExp_Explorer(r.OneShape(),TopAbs_SOLID);solids=[]
while e.More():solids.append(e.Current());e.Next()
assert len(solids)==51, "Unexpected STEP assembly; inspect shell selection first"
s=solids[48]
BRepMesh_IncrementalMesh(s,.15,False,.25,True)
e=TopExp_Explorer(s,TopAbs_FACE);v=[];f=[]
while e.More():
 face=TopoDS.Face(e.Current());loc=TopLoc_Location();t=BRep_Tool.Triangulation_s(face,loc)
 if t:
  start=len(v)
  for i in range(1,t.NbNodes()+1):
   p=t.Node(i).Transformed(loc.Transformation());v.append([p.X(),p.Y(),p.Z()])
  for i in range(1,t.NbTriangles()+1):
   a,b,c=t.Triangle(i).Get();idx=[start+a-1,start+b-1,start+c-1];f.append(idx[::-1] if face.Orientation()==TopAbs_REVERSED else idx)
 e.Next()

m=trimesh.Trimesh(v,f)
assert m.is_watertight and m.is_winding_consistent
report={"source_sha256":hashlib.sha256(args.source.read_bytes()).hexdigest(),"selected_solid":48,"source_triangles":len(m.faces),"dimensions_mm":m.extents.tolist(),"levels":[]}
for name,budget in [("snes-cart",1000),("snes-cart-small",600)]:
 ms=pymeshlab.MeshSet();ms.add_mesh(pymeshlab.Mesh(m.vertices,m.faces))
 ms.meshing_decimation_quadric_edge_collapse(targetfacenum=budget,preservetopology=True,preservenormal=True,planarquadric=True)
 mesh=ms.current_mesh();v=mesh.vertex_matrix();f=mesh.face_matrix()
 reduced=trimesh.Trimesh(v,f)
 assert reduced.is_watertight and reduced.is_winding_consistent
 # Standard cartridge axes X right, Y up, Z front; dimensions relative to 85 mm height.
 v=(v-np.array([67.5,42.4925,10]))/84.985
 runtime=trimesh.Trimesh(v,f,process=False);runtime.export(args.output/(name+".obj"))
 report["levels"].append({"name":name,"triangles":len(f),"vertices":len(v),"watertight":True,"volume_change_percent":100*(reduced.volume/m.volume-1)})
(args.output/"geometry-report.json").write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))
