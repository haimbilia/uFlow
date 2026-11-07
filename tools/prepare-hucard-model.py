"""Rebuild the two HuCard shell LODs using the supplied print STL's envelope."""
import math
import struct
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_bytes()
points = [tri[i:i + 3] for tri in struct.iter_unpack("<12fH", source[84:])
          for i in (3, 6, 9)]
width, height, depth = (max(axis) - min(axis) for axis in zip(*points))
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
        for ring in (rings[0][::-1], rings[-1]):
            for i in range(1, len(ring)-1):
                faces.append((ring[0], ring[i], ring[i+1]))

    rounded_plate(0, 0, width, height, 2.3, [(.45,0), (.13,.13), (0,.45),
                  (0,2.05), (.13,2.37), (.45,2.5)])
    rounded_plate(1.25, height-33, width-1.25, height-1.35, 2.1,
                  [(0,2.51), (0,depth-.28), (.28,depth)])
    lines = ["v %.9f %.9f %.9f" % v for v in vertices]
    lines.extend("f %d %d %d" % face for face in faces)
    (out / f"hucard-cart{suffix}.obj").write_text("\n".join(lines)+"\n")
