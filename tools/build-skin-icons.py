"""Render uFlow's shared geometric dock icons at high resolution."""
from pathlib import Path
from PIL import Image, ImageDraw
import math
root=Path(__file__).resolve().parents[1]/"app/content/skins"
for skin in ("switch-light","switch-dark"):
    ink="#444b50" if skin=="switch-light" else "#eeeeee"
    colors=["#ee4a55","#eaa014","#2f86e5",ink,ink,ink]
    for index,color in enumerate(colors):
        image=Image.new("RGBA",(384,384));d=ImageDraw.Draw(image);scale=4
        def line(points,width=5):d.line([(round(x*scale),round(y*scale)) for x,y in points],fill=color,width=width*scale,joint="curve")
        def ellipse(rect,width=5):d.ellipse(tuple(int(v*scale) for v in rect),outline=color,width=width*scale)
        def rect(box,radius=4,width=5):d.rounded_rectangle(tuple(int(v*scale) for v in box),radius=radius*scale,outline=color,width=width*scale)
        if index==0:
            rect((18,22,78,65));line([(34,65),(48,78),(61,65)]);line([(27,37),(69,37)],4);line([(27,48),(50,48)],4)
        elif index==1:
            ellipse((15,15,81,81));ellipse((32,15,64,81),4);line([(17,48),(79,48)],4)
        elif index==2:
            rect((13,27,70,77),2);rect((24,17,83,67),2);line([(31,59),(46,39),(60,55),(68,44),(76,58)],4);ellipse((63,26,71,34),4)
        elif index==3:
            # Joy-Con silhouette, tilted like the Switch home menu glyph.
            angle=math.radians(38)
            def turn(x,y):return (48+x*math.cos(angle)-y*math.sin(angle),48+x*math.sin(angle)+y*math.cos(angle))
            line([turn(-16,-30),turn(16,-30),turn(16,30),turn(-16,30),turn(-16,-30)],5)
            for x,y in [(0,-15),(-5,8),(5,8),(0,3),(0,13)]:
                cx,cy=turn(x,y);r=5 if y==-15 else 2;ellipse((cx-r,cy-r,cx+r,cy+r),3)
        elif index==4:
            points=[]
            for i in range(48):
                a=math.tau*i/48;r=36 if i%6 in (1,2,3) else 27;points.append((48+math.cos(a)*r,48+math.sin(a)*r))
            d.polygon([(int(x*scale),int(y*scale)) for x,y in points],fill=color)
            d.ellipse((31*scale,31*scale,65*scale,65*scale),fill=(0,0,0,0))
        else:
            d.arc((17*scale,19*scale,79*scale,81*scale),start=310,end=230,fill=color,width=6*scale);line([(48,12),(48,48)],6)
        target=root/skin/f"icon-{index}.png";image.resize((96,96),Image.Resampling.LANCZOS).save(target)
    manifest=root/skin/"skin.ini"
    text=manifest.read_text();text="\n".join(line for line in text.splitlines() if not line.startswith("icon_"))+"\n"
    manifest.write_text(text+"".join(f"icon_{i}=icon-{i}.png\n" for i in range(6)))
