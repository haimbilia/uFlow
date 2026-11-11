"""Bound affine face distortion at inspection scale, without runtime tessellation."""
from pathlib import Path
import math

root=Path(__file__).resolve().parents[1]
for suffix,budget,scale in [('',1100,600),('-small',450,240)]:
    vertices=[];faces=[]
    for line in (root/f'assets/models/hucard/hucard-cart{suffix}.obj').read_text().splitlines():
        if line.startswith('v '):vertices.append(tuple(map(float,line.split()[1:4])))
        elif line.startswith('f '):faces.append([int(v.split('/')[0])-1 for v in line.split()[1:]])
    assert len(faces)<=budget
    cap=[f for f in faces if max(vertices[i][2] for i in f)-min(vertices[i][2] for i in f)<1e-7]
    assert cap
    worst=0
    for angle in (-1.0,-.5,.5,1.0):
        def project(p):
            x,y,z=p;c,s=math.cos(angle),math.sin(angle)
            xx,zz=x*c+z*s,-x*s+z*c
            return (scale*xx/(1.8-zz),scale*y/(1.8-zz))
        for f in cap:
            for i,j in zip(f,f[1:]+f[:1]):
                a,b=vertices[i],vertices[j]
                pa,pb=project(a),project(b)
                pm=project(tuple((x+y)/2 for x,y in zip(a,b)))
                worst=max(worst,math.dist(pm,tuple((x+y)/2 for x,y in zip(pa,pb))))
    assert worst<1.5,(suffix,worst)
    print(f'PASS HuCard {suffix or "near"}: {len(faces)} triangles, affine midpoint error {worst:.3f}px')
