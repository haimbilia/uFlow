from pathlib import Path
import struct,zlib,json
import argparse,hashlib,io
from PIL import Image
a=argparse.ArgumentParser();a.add_argument('source',type=Path);a.add_argument('output',type=Path);args=a.parse_args()
raw=args.source.read_bytes()
assert struct.unpack_from('<I',raw,23)[0]==7400, 'Expected supplied FBX 7400'
args.output.mkdir(parents=True,exist_ok=True)
def node(pos):
 end,n,plen=struct.unpack_from('<III',raw,pos); size=raw[pos+12]; pos+=13
 if not end:return None,pos
 name=raw[pos:pos+size].decode();pos+=size;props=[]
 for _ in range(n):
  t=chr(raw[pos]);pos+=1
  if t in 'YCFDIL':
   f={'Y':'h','C':'?','F':'f','D':'d','I':'i','L':'q'}[t];v=struct.unpack_from('<'+f,raw,pos)[0];pos+=struct.calcsize(f)
  elif t in 'SR':
   length=struct.unpack_from('<I',raw,pos)[0];pos+=4;v=raw[pos:pos+length];pos+=length
   if t=='S':v=v.decode(errors='replace')
   else:v=list(v)
  else:
   count,encoded,length=struct.unpack_from('<III',raw,pos);pos+=12;buf=raw[pos:pos+length];pos+=length
   if encoded:buf=zlib.decompress(buf)
   f={'f':'f','d':'d','i':'i','l':'q','b':'?','c':'b'}[t];v=list(struct.unpack('<'+f*count,buf))
  props.append(v)
 children=[]
 while pos<end-13:
  c,pos=node(pos)
  if c:children.append(c)
 return {'name':name,'props':props,'children':children},end
pos=27;nodes=[]
while pos<len(raw)-13:
 n,pos=node(pos)
 if not n:break
 nodes.append(n)

g=next(x for t in nodes if t['name']=='Objects' for x in t['children'] if x['name']=='Geometry')
def child(n,name):return next(x for x in n['children'] if x['name']==name)
v=child(g,'Vertices')['props'][0];v=[v[i:i+3] for i in range(0,len(v),3)]
v=[[-p[2],p[1],p[0]] for p in v]
lo=[min(p[k] for p in v) for k in range(3)];hi=[max(p[k] for p in v) for k in range(3)];size=[hi[k]-lo[k] for k in range(3)]
v=[[(p[k]-(lo[k]+hi[k])/2)/size[k] for k in range(3)] for p in v]
u=child(g,'LayerElementUV');uv=child(u,'UV')['props'][0];uv=[uv[i:i+2] for i in range(0,len(uv),2)];ui=child(u,'UVIndex')['props'][0]
mi=child(child(g,'LayerElementMaterial'),'Materials')['props'][0];faces=[];face=[]
for i,index in enumerate(child(g,'PolygonVertexIndex')['props'][0]):
 face.append((~index if index<0 else index,ui[i]))
 if index<0:assert len(face)==4;faces.append(face);face=[]
assert len(v)==20 and len(faces)==13
lines=['#pragma once','#include <cstddef>','namespace Ps1Jewel {','struct Point {float x,y,z;};','struct Corner {Point p;float u,v;};','struct Face {Corner c[4];int material;};','inline constexpr Face Faces[]={']
for i,f in enumerate(faces):
 corners=[]
 for vi,ti in f:
  nums=v[vi]+[uv[ti][0],1-uv[ti][1]]
  fs=[format(x,'.9f')+'f' for x in nums]
  corners.append('{{'+','.join(fs[:3])+'},'+','.join(fs[3:])+'}')
 lines.append('    {{'+','.join(corners)+'},'+str(mi[i])+'},')
lines.append('};')
for name,file,maximum in [('Shell','5521_playstation-one-psx-prev.png',512),('Disc','CDTExt.png',256)]:
 im=Image.open(args.source.parent.parent/'textures'/file).convert('RGBA');im.thumbnail((maximum,maximum),Image.Resampling.LANCZOS)
 out=args.output/(name.lower()+'.png');im.save(out,optimize=True)
 data=out.read_bytes();lines.append('inline constexpr unsigned char '+name+'Png[]={')
 for i in range(0,len(data),24):lines.append(','.join(str(b) for b in data[i:i+24])+',')
 lines.append('};')
lines.append('}')
Path('app/source/ps1_jewel_data.hpp').write_text('\n'.join(lines)+'\n')
report={'source_sha256':hashlib.sha256(raw).hexdigest(),'vertices':len(v),'quads':len(faces),'triangles':len(faces)*2,'dimensions':size,'textures':['shell.png: 512px','disc.png: 256px']}
(args.output/'geometry-report.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
