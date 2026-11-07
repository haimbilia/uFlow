"""Prepare user N64/GBA/GB STL and DS glTF meshes for uFlow (offline only).
Requires numpy, trimesh, pymeshlab. Embedded game artwork is not extracted.
DS external URIs are URL-decoded; only its cartridge node is imported.
"""
from pathlib import Path
import argparse,json,hashlib,urllib.parse
import numpy as np,pymeshlab,trimesh
p=argparse.ArgumentParser();p.add_argument('system',choices=['n64','gba','gb','ds']);p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
if a.system=='ds':
 d=json.loads(a.source.read_text());buffers=[(a.source.parent/urllib.parse.unquote(b['uri'])).read_bytes() for b in d['buffers']]
 def acc(i):
  e=d['accessors'][i];v=d['bufferViews'][e['bufferView']];n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[e['type']];dtype={5126:'<f4',5123:'<u2',5125:'<u4'}[e['componentType']];item=np.dtype(dtype).itemsize
  return np.ndarray((e['count'],n),dtype=dtype,buffer=buffers[v['buffer']],offset=v.get('byteOffset',0)+e.get('byteOffset',0),strides=(v.get('byteStride',item*n),item)).copy()
 node=d['nodes'][0];vs=[];fs=[]
 for part in d['meshes'][node['mesh']]['primitives']:
  v=acc(part['attributes']['POSITION'])*node['scale']+node['translation'];f=acc(part['indices']).reshape(-1,3);fs.extend(f+len(vs));vs.extend(v)
 m=trimesh.Trimesh(vs,fs);v=m.vertices[:,[0,2,1]]*[1,-1,1];v-=v.min(axis=0);v*=35/v[:,1].max()
 m=trimesh.Trimesh(v,m.faces)
else:
 m=trimesh.load(a.source,force='mesh')
 if a.system=='n64':m.vertices=m.vertices[:,[0,2,1]]*[1,1,-1]
 m.vertices-=m.bounds[0]
assert np.isfinite(m.vertices).all()
height=m.extents[1];center=m.bounds.mean(axis=0)
report={'system':a.system,'source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'source_triangles':len(m.faces),'dimensions':m.extents.tolist(),'source_watertight':m.is_watertight,'levels':[]}
if a.system=='ds':report['credit']=d.get('asset',{}).get('copyright','');report['companion_sha256']=[hashlib.sha256(b).hexdigest() for b in buffers]
for suffix,budget in [('',1000),('-small',600)]:
 ms=pymeshlab.MeshSet();ms.add_mesh(pymeshlab.Mesh(m.vertices,m.faces))
 ms.meshing_decimation_quadric_edge_collapse(targetfacenum=budget,preservetopology=True,preservenormal=True,planarquadric=True,preserveboundary=True)
 current=ms.current_mesh();mesh=trimesh.Trimesh(current.vertex_matrix(),current.face_matrix())
 assert not m.is_watertight or mesh.is_watertight
 assert mesh.is_winding_consistent and len(mesh.faces)<=budget+100
 mesh.vertices=(mesh.vertices-center)/height
 mesh.export(a.output/(a.system+'-cart'+suffix+'.obj'))
 report['levels'].append({'triangles':len(mesh.faces),'vertices':len(mesh.vertices),'watertight':mesh.is_watertight})
(a.output/'geometry-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
