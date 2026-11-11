"""Rebuild the two HuCard shell LODs using the supplied print STL's envelope."""
import math
import struct
import sys
import argparse
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('source',nargs='?')
p.add_argument('--dimensions',nargs=3,type=float,metavar=('WIDTH','HEIGHT','DEPTH'))
args=p.parse_args()
if args.source:
    source = Path(args.source).read_bytes()
    points = [tri[i:i + 3] for tri in struct.iter_unpack("<12fH", source[84:]) for i in (3, 6, 9)]
    width, height, depth = (max(axis) - min(axis) for axis in zip(*points))
else:
    if not args.dimensions:p.error('Supply the source STL or its recorded dimensions')
    width,height,depth=args.dimensions
out = Path(__file__).resolve().parents[1] / "assets/models/hucard"
out.mkdir(parents=True, exist_ok=True)

for steps, suffix in [(6, ""), (2, "-small")]:
    vertices = []
    faces = []

    def rounded_plate(left, bottom, right, top, radius, levels):
        rings = []
        for inset, z in levels:
            ring = []
            for cx, cy, angle in [(right-radius, top-radius, 0),
                                  (left+radius, top-radius, 90),
                                  (left+radius, bottom+radius, 180),
                                  (right-radius, bottom+radius, 270)]:
                for i in range(steps+1):
                    theta = math.radians(angle + 90*i/steps)
                    x = cx + (radius-inset)*math.cos(theta)
                    y = cy + (radius-inset)*math.sin(theta)
                    vertices.append(((x-width/2)/height, (y-height/2)/height, (z-depth/2)/height))
                    ring.append(len(vertices))
            rings.append(ring)
        for lower, upper in zip(rings, rings[1:]):
            for i in range(len(lower)):
                j = (i+1) % len(lower)
                faces.extend([(lower[i], lower[j], upper[j]), (lower[i], upper[j], upper[i])])
        # SDL_RenderGeometry interpolates UVs in screen space. A single fan
        # spanning the entire card bends straight material boundaries after
        # perspective projection. Clip the planar caps into bounded cells at
        # build time; side bevels and the original silhouette stay unchanged.
        def clip(poly,axis,value,greater):
            result=[]
            for a,b in zip(poly,poly[1:]+poly[:1]):
                da=(a[axis]-value)*(1 if greater else -1)
                db=(b[axis]-value)*(1 if greater else -1)
                if da>=-1e-10:result.append(a)
                if (da<0)!=(db<0):
                    t=da/(da-db)
                    result.append(tuple(a[k]+t*(b[k]-a[k]) for k in range(3)))
            return result
        for ring in (rings[0][::-1], rings[-1]):
            poly=[vertices[i-1] for i in ring]
            size=(8 if steps==6 else 16)/height
            x0,y0=min(v[0] for v in poly),min(v[1] for v in poly)
            x1,y1=max(v[0] for v in poly),max(v[1] for v in poly)
            for ix in range(math.ceil((x1-x0)/size)):
                for iy in range(math.ceil((y1-y0)/size)):
                    cell=poly
                    for axis,value,greater in [(0,x0+ix*size,True),(0,x0+(ix+1)*size,False),
                                               (1,y0+iy*size,True),(1,y0+(iy+1)*size,False)]:
                        cell=clip(cell,axis,value,greater)
                        if len(cell)<3:break
                    if len(cell)<3:continue
                    for i in range(1,len(cell)-1):
                        a,b,c=cell[0],cell[i],cell[i+1]
                        if abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))<1e-12:continue
                        first=len(vertices)+1;vertices.extend((a,b,c));faces.append((first,first+1,first+2))

    rounded_plate(0, 0, width, height, 2.3, [(.45,0), (.13,.13), (0,.45),
                  (0,2.05), (.13,2.37), (.45,2.5)])
    rounded_plate(1.25, height-33, width-1.25, height-1.35, 2.1,
                  [(0,2.51), (0,depth-.28), (.28,depth)])
    lines = ["v %.9f %.9f %.9f" % v for v in vertices]
    lines.extend("f %d %d %d" % face for face in faces)
    (out / f"hucard-cart{suffix}.obj").write_text("\n".join(lines)+"\n")
