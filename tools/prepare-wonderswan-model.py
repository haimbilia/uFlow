"""Build bounded display LODs from the assembled user-supplied shell envelope.

The print meshes use X for width, Y for depth and -Z for vertical. Preserve
their assembled size and front-shell cutoff; omit invisible print supports.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct

p = argparse.ArgumentParser()
p.add_argument('top', type=Path)
p.add_argument('bottom', type=Path)
args = p.parse_args()
out = Path(__file__).resolve().parents[1] / 'assets/models/wonderswan'
out.mkdir(parents=True, exist_ok=True)
parts = []
for path in (args.top, args.bottom):
    data = path.read_bytes()
    count = struct.unpack_from('<I', data, 80)[0]
    assert len(data) == 84 + count * 50
    points = [t[i:i+3] for t in struct.iter_unpack('<12fH', data[84:]) for i in (3, 6, 9)]
    parts.append(points)
points = parts[0] + parts[1]
lo = [min(v[i] for v in points) for i in range(3)]
hi = [max(v[i] for v in points) for i in range(3)]
width, height, depth = hi[0]-lo[0], hi[2]-lo[2], hi[1]-lo[1]
cutoff = hi[2] - max(v[2] for v in parts[0])

def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])

def triangulate(poly):
    left = list(range(len(poly)))
    result = []
    while len(left) > 3:
        for j, b in enumerate(left):
            a, c = left[j-1], left[(j+1) % len(left)]
            if cross(poly[a], poly[b], poly[c]) <= 1e-8:
                continue
            if any(all(cross(poly[u], poly[v], poly[k]) >= -1e-8
                       for u, v in ((a,b),(b,c),(c,a)))
                   for k in left if k not in (a,b,c)):
                continue
            result.append((a,b,c)); left.pop(j); break
        else:
            raise ValueError('Invalid outline')
    return result + [tuple(left)]

for steps, suffix in ((5, ''), (2, '-small')):
    vertices, faces = [], []
    def solid(poly, levels):
        rings = []
        bottom, top = min(y for x,y in poly), max(y for x,y in poly)
        for inset, z in levels:
            ring = []
            for x,y in poly:
                xx = width/2 + (x-width/2)*(width-2*inset)/width
                yy = (bottom+top)/2 + (y-(bottom+top)/2)*(top-bottom-2*inset)/(top-bottom)
                ring.append(len(vertices))
                vertices.append(((xx-width/2)/height, (yy-height/2)/height, (z-depth/2)/height))
            rings.append(ring)
        for lower, upper in zip(rings, rings[1:]):
            for i in range(len(poly)):
                j = (i+1) % len(poly)
                faces.extend(((lower[i],lower[j],upper[j]),(lower[i],upper[j],upper[i])))
        for a,b,c in triangulate(poly):
            faces.extend(((rings[0][c],rings[0][b],rings[0][a]),(rings[-1][a],rings[-1][b],rings[-1][c])))

    def rounded(bottom, top, radius):
        poly = []
        for x,y,start in ((width-radius,top-radius,0),(radius,top-radius,90),
                          (radius,bottom+radius,180),(width-radius,bottom+radius,270)):
            for i in range(steps+1):
                a = math.radians(start+i*90/steps)
                poly.append((x+radius*math.cos(a),y+radius*math.sin(a)))
        return poly

    # Lower shell preserves the two connector-side locating notches.
    outline = rounded(0,height,.9)
    # Insert side notches between the upper and lower corner arcs.
    outline[2*(steps+1):2*(steps+1)] = [(0,5.3),(1.25,5.3),(1.25,2.3),(0,2.3)]
    outline += [(width,2.3),(width-1.25,2.3),(width-1.25,5.3),(width,5.3)]
    # The connector carrier sits inside the upper shell, not flush with its
    # outer edge. Keep one narrow bevel per lip instead of stacked ledges.
    outline=[(1.05+x*(width-2.1)/width,y) for x,y in outline]
    solid(outline,[(.16,0),(0,.35),(0,2.3),(.18,2.55)])
    solid(rounded(cutoff,height,1.1),[(.22,2.56),(0,2.9),(0,depth-.65),(.45,depth)])
    (out/f'wonderswan-cart{suffix}.obj').write_text(
        '\n'.join(['v %.9f %.9f %.9f'%v for v in vertices]+
                  ['f %d %d %d'%tuple(i+1 for i in f) for f in faces])+'\n',encoding='utf-8')

(out/'geometry-report.json').write_text(json.dumps({
    'width_mm':width,'height_mm':height,'depth_mm':depth,'front_cutoff_mm':cutoff,
    'source_triangles':len(points)//3,
    'sources':{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in (args.top,args.bottom)},
    'method':'Display LODs retain assembled STL envelope; print interior supports omitted. Internals are baked material detail.'
},indent=2)+'\n',encoding='utf-8')
print(width,height,depth,cutoff)
