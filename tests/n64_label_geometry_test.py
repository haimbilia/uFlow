from pathlib import Path
import re,math
root=Path(__file__).resolve().parents[1]
s=(root/'app/source/n64_label_shape.hpp').read_text()
points=[tuple(map(float,p)) for p in re.findall(r'\{([-\d.]+)f,([-\d.]+)f\}',s)]
assert len(points)>30
xs,ys=zip(*points)
assert abs(min(xs))<1e-5 and abs(max(xs)-55)<1e-5
assert abs(min(ys))<1e-5 and abs(max(ys)-62.5)<1e-5
# Crown points lie on the specified 325 mm diameter circle.
crown=[p for p in points if 8<p[0]<47 and p[1]>60]
assert len(crown)>5
for x,y in crown:assert abs(math.hypot(x-27.5,y-(62.5-162.5))-162.5)<1e-5
for name,limit in [('n64-cart.obj',1200),('n64-cart-small.obj',850)]:
 text=(root/'assets/models/n64'/name).read_text()
 assert sum(line.startswith('f ') for line in text.splitlines())<=limit
 verts=[list(map(float,l.split()[1:])) for l in text.splitlines() if l.startswith('v ')]
 assert all(math.isfinite(v) for p in verts for v in p)
 # Sticker corners must all exist in the recessed well, for both LODs.
 for x,y in points:
  expected=((30.25+x-115.5/2)/75.96896,(5.4+y-75.96896/2)/75.96896,(18.6-.62-18.6/2)/75.96896)
  assert any(math.dist(v,expected)<1e-7 for v in verts),(name,x,y)

 assert max(p[2] for p in verts)-min(p[2] for p in verts)<=18.6/75.96896+.00001
print('PASS N64: 55 x 62.5 mm outline, 325 mm crown, finite shell and bounded near/far source meshes')
