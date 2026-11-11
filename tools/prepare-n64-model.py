"""Photo-guided N64 shell; preserve the recorded STL envelope, build both LODs."""
from pathlib import Path
import math,json
W,H,D=115.5,75.96896,18.6
root=Path(__file__).resolve().parents[1]
out=root/'assets/models/n64'
def roof(x):return H-10.5*((x-W/2)/(W/2))**2
def outline(left,right,bottom,top,radius,steps,arch=False):
    pts=[]
    for cx,cy,angle in [(right-radius,top-radius,0),(left+radius,top-radius,90),(left+radius,bottom+radius,180),(right-radius,bottom+radius,270)]:
        for i in range(steps+1):
            a=math.radians(angle+90*i/steps);x=cx+radius*math.cos(a);y=cy+radius*math.sin(a)
            if arch:y-=(H-roof(x))*max(0,(y-bottom)/(top-bottom))**8
            pts.append((x,y))
    # Add top/bottom samples: the broad top is a curve, never one straight chord.
    result=[]
    for i,p in enumerate(pts):
        q=pts[(i+1)%len(pts)];result.append(p)
        if abs(p[0]-q[0])>(right-left)*.6:
            for j in range(1,steps*2+1):
                t=j/(steps*2+1);x=p[0]+t*(q[0]-p[0]);y=p[1]+t*(q[1]-p[1])
                if arch and p[1]>top-15 and q[1]>top-15:y=roof(x)
                result.append((x,y))
    return result
def radial(poly,count,cx,cy,reference=None):
    result=[]
    for i in range(count):
        a=math.atan2(reference[i][1]-cy,reference[i][0]-cx) if reference else 2*math.pi*i/count;dx,dy=math.cos(a),math.sin(a);hits=[]
        for p,q in zip(poly,poly[1:]+poly[:1]):
            ex,ey=q[0]-p[0],q[1]-p[1];den=dx*ey-dy*ex
            if abs(den)<1e-9:continue
            px,py=p[0]-cx,p[1]-cy;t=(px*ey-py*ex)/den;u=(px*dy-py*dx)/den
            if t>=0 and -.00001<=u<=1.00001:hits.append(t)
        assert hits
        t=min(hits);result.append((cx+t*dx,cy+t*dy))
    return result
def label_outline(steps=8):
    # 55 x 62.5, upper corners R4, lower R3, crown diameter 325 mm.
    width,height,R,r=55.,62.5,162.5,4.
    mid=width/2;bigY=height-R;cx=width-r
    cy=bigY+math.sqrt((R-r)**2-(cx-mid)**2)
    tangent=math.atan2(cy-bigY,cx-mid)
    pts=[]
    def arc(x,y,r,a,b,n):
        for i in range(n+1):
            t=a+(b-a)*i/n;pts.append((x+r*math.cos(t),y+r*math.sin(t)))
    arc(cx,cy,r,0,tangent,steps)
    arc(mid,bigY,R,tangent,math.pi-tangent,steps*2)
    arc(r,cy,r,math.pi-tangent,math.pi,steps)
    arc(3,3,3,math.pi,math.pi*1.5,steps)
    arc(width-3,3,3,math.pi*1.5,math.pi*2,steps)
    clean=[]
    for p in pts:
        if not clean or math.dist(p,clean[-1])>1e-7:clean.append(p)
    return clean
shape=label_outline()
header='#pragma once\n#include <array>\n#include <SDL2/SDL.h>\nnamespace N64Label {\ninline constexpr std::array<SDL_FPoint,%d> Outline{{\n'%len(shape)
header+=''.join('{%.7ff,%.7ff},\n'%p for p in shape)+'}};\n}\n'
(root/'app/source/n64_label_shape.hpp').write_text(header)
reports=[]
for suffix,steps in [('',4),('-small',2)]:
    v=[];f=[]
    def ring(poly,z):
        ids=[]
        for x,y in poly:ids.append(len(v));v.append(((x-W/2)/H,(y-H/2)/H,(z-D/2)/H))
        return ids
    def join(a,b):
        assert len(a)==len(b)
        for i in range(len(a)):
            j=(i+1)%len(a)
            # Leave the bottom open; its rim and recessed socket are separate.
            if all(abs(v[k][1]+.5)<.001 for k in (a[i],a[j],b[i],b[j])):continue
            f.extend([(a[i],a[j],b[j]),(a[i],b[j],b[i])])
    def cap(ids,reverse=False):
        p=tuple(sum(v[k][i] for k in ids)/len(ids) for i in range(3));c=len(v);v.append(p)
        for i in range(len(ids)):
            tri=(c,ids[i],ids[(i+1)%len(ids)]);f.append(tri[::-1] if reverse else tri)
    outer=outline(0,W,0,H,2.1,steps,True)
    inner=[(W/2+(x-W/2)*.985,H/2+(y-H/2)*.985) for x,y in outer]
    seam=[(W/2+(x-W/2)*.997,H/2+(y-H/2)*.997) for x,y in outer]
    rings=[ring(inner,0),ring(outer,1.5),ring(outer,8.9),ring(seam,9.15),ring(seam,9.4),ring(outer,9.65),ring(outer,D-2),ring(inner,D-1.25)]
    for a,b in zip(rings,rings[1:]):join(a,b)
    cap(rings[0],True);cap(rings[-1])
    # Central face with an actual recessed label well, separated from shoulders.
    # Use the exact same boundary as the rendered sticker at BOTH LODs.
    # Radial resampling of the well cut across the rounded sticker corners.
    exact=[(30.25+x,5.4+y) for x,y in shape]
    n=len(exact)
    center=radial(outline(22.5,93,0.7,H-.15,1.1,steps,True),n,W/2,36.65,exact)
    label=[(W/2+(x-W/2)*1.024,36.65+(y-36.65)*1.021) for x,y in exact]
    a=ring(center,D-.05);b=ring(label,D-.05);join(a,b)
    c=ring(exact,D-.62);join(b,c);cap(c)
    bottom=ring(center,D-1.20);join(bottom,a)
    # Broad side shoulders and narrow longitudinal grooves.
    for left,right in [(1.0,21.8),(93.7,W-1.0)]:
        poly=outline(left,right,1.0,H-.6,1.2,steps,True)
        r0=ring(poly,D-1.20)
        inset=[((left+right)/2+(x-(left+right)/2)*.96,y) for x,y in poly]
        r1=[]
        for x,y in inset:
            z=D-.30-(1.8 if not suffix else 0)*((x-W/2)/(W/2))**2
            r1.append(len(v));v.append(((x-W/2)/H,(y-H/2)/H,(z-D/2)/H))
        join(r0,r1);cap(r1)
    # Lower side locating shoulders visible in both owner photographs.
    for mirror in ([] if suffix else [False,True]):
        poly=[(1.8,1),(7.0,1),(7.0,16),(6.7,17.4),(5.8,18.3),(1.8,20.5)]
        if mirror:poly=[(W-x,y) for x,y in reversed(poly)]
        a=[];b=[]
        for x,y in poly:
            z=D-.30-(1.8 if not suffix else 0)*((x-W/2)/(W/2))**2
            a.append(len(v));v.append(((x-W/2)/H,(y-H/2)/H,(z-D/2)/H))
            b.append(len(v));v.append(((x-W/2)/H,(y-H/2)/H,(z+.20-D/2)/H))
        join(a,b);cap(b)
    # Bottom rim around a real recessed connector opening (not a black decal).
    def quad(points):
        ids=[]
        for x,y,z in points:ids.append(len(v));v.append(((x-W/2)/H,(y-H/2)/H,(z-D/2)/H))
        f.extend([(ids[0],ids[1],ids[2]),(ids[0],ids[2],ids[3])])
    for x0,x1,z0,z1 in [(0,22.5,1.5,D-2),(93,W,1.5,D-2),(22.5,93,1.5,4),(22.5,93,14.5,D-2)]:
        quad([(x0,0,z0),(x1,0,z0),(x1,0,z1),(x0,0,z1)])
    # The cavity walls and PCB are shaded in the renderer, behind the shell.
    # Subtract the two rear locating notches from the actual shell surfaces.
    for left in (6.,W-11.):
        bounds=[((left-W/2)/H,(left+5-W/2)/H),(-.5,(5.5-H/2)/H),(-D/2/H,(5.5-D/2)/H)]
        clipped=[]
        for tri in f:
            pending=[v[k] for k in tri]
            if any(max(p[axis] for p in pending)<=bounds[axis][0] or min(p[axis] for p in pending)>=bounds[axis][1] for axis in range(3)):
                clipped.append(tri);continue
            outside=[]
            for axis in range(3):
                for side in range(2):
                    if not pending:break
                    boundary=bounds[axis][side];sign=1 if side==0 else -1
                    ins=[];outs=[]
                    for a,b in zip(pending,pending[1:]+pending[:1]):
                        da=(a[axis]-boundary)*sign;db=(b[axis]-boundary)*sign
                        (ins if da>=0 else outs).append(a)
                        if (da>=0)!=(db>=0):
                            t=da/(da-db);q=tuple(a[i]+t*(b[i]-a[i]) for i in range(3));ins.append(q);outs.append(q)
                    if len(outs)>=3:outside.append(outs)
                    pending=ins
            for poly in outside:
                for i in range(1,len(poly)-1):
                    pts=[poly[0],poly[i],poly[i+1]]
                    a,b,c=pts;u=[b[j]-a[j] for j in range(3)];w=[c[j]-a[j] for j in range(3)]
                    cross=[u[1]*w[2]-u[2]*w[1],u[2]*w[0]-u[0]*w[2],u[0]*w[1]-u[1]*w[0]]
                    if sum(x*x for x in cross)<1e-18:continue
                    ids=[]
                    for point in pts:ids.append(len(v));v.append(point)
                    clipped.append(tuple(ids))
        f=clipped
        quad([(left,0,0),(left,5.5,0),(left,5.5,5.5),(left,0,5.5)])
        quad([(left+5,0,5.5),(left+5,5.5,5.5),(left+5,5.5,0),(left+5,0,0)])
        quad([(left,5.5,0),(left+5,5.5,0),(left+5,5.5,5.5),(left,5.5,5.5)])
        quad([(left,0,5.5),(left,5.5,5.5),(left+5,5.5,5.5),(left+5,0,5.5)])
    lines=['v %.9f %.9f %.9f'%p for p in v]+['f %d %d %d'%tuple(k+1 for k in tri) for tri in f]
    (out/f'n64-cart{suffix}.obj').write_text('\n'.join(lines)+'\n')
    reports.append({'lod':suffix or 'near','vertices':len(v),'triangles':len(f)})
(out/'photo-refinement.json').write_text(json.dumps({'envelope_mm':[W,H,D],'label_mm':{'width':55,'height':62.5,'top_radius':4,'bottom_radius':3,'crown_diameter':325},'basis':'Existing STL shell envelope; owner label dimension drawing and front photographs.','levels':reports},indent=2))
print(reports)
