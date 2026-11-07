"""Check compiled cartridge BSP visibility against ray/triangle intersections."""
from pathlib import Path
import re, sys, numpy as np
system=sys.argv[1] if len(sys.argv)>1 else "nes"
assert system in ("nes","snes","n64","gba","gb","ds")
s=(Path(__file__).resolve().parents[1]/f"app/source/{system}_cartridge_data.hpp").read_text()
def rows(name):
 body=s.split(name+"[]={",1)[1].split("};",1)[0]
 return [[float(v) for v in re.findall(r"-?\d+(?:\.\d+)?",line)] for line in body.splitlines() if line.strip()]
lod=sys.argv[2] if len(sys.argv)>2 else "Near"
assert lod in ("Near","Far")
v=np.array(rows(lod+"Vertices"));f=np.array(rows(lod+"Faces"),dtype=int);nodes=rows(lod+"Nodes");tris=v[f]
assert np.isfinite(v).all() and f.max()<len(v)
if system=="snes":assert len(f)<3000
e1=tris[:,1]-tris[:,0];e2=tris[:,2]-tris[:,0]
rng=np.random.default_rng(7);checks=0
for yaw in np.linspace(0,2*np.pi,24,endpoint=False):
 pitch=.13;eye=np.array([np.sin(yaw)*np.cos(pitch),np.sin(pitch),np.cos(yaw)*np.cos(pitch)])*2.8
 order=[]
 def visit(i):
  if i<0:return
  n=nodes[i];front=np.dot(n[:3],eye)>=n[3];a,b=int(n[4]),int(n[5])
  visit(b if front else a);order.extend(range(int(n[6]),int(n[6]+n[7])));visit(a if front else b)
 visit(0);assert sorted(order)==list(range(len(f)))
 rank=np.argsort(order)
 for target in tris.mean(axis=1)[rng.choice(len(f),60,replace=False)]:
  direction=target-eye;direction/=np.linalg.norm(direction)
  h=np.cross(np.broadcast_to(direction,e2.shape),e2);det=np.einsum('ij,ij->i',e1,h)
  valid=np.abs(det)>1e-10;inv=np.zeros_like(det);inv[valid]=1/det[valid]
  offset=eye-tris[:,0];u=inv*np.einsum('ij,ij->i',offset,h);q=np.cross(offset,e1)
  w=inv*np.dot(q,direction);t=inv*np.einsum('ij,ij->i',e2,q)
  hits=np.flatnonzero(valid & (u>1e-5) & (w>1e-5) & (u+w<1-1e-5) & (t>0))
  hits=hits[np.argsort(rank[hits])]
  assert np.all(np.diff(t[hits])<=3e-5),(yaw,t[hits])
  checks+=1
print(f"PASS: {checks} rays over 24 rotation angles, {len(f)} triangles, correct back-to-front visibility")
