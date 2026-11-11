"""Bake the translucent shell and illustrative PCB into one shared RGBA atlas.

No game label is baked. Small traces/screws are texture detail, not geometry.
Pillow and a caller-supplied italic font are build-time dependencies only.
"""
from pathlib import Path
import argparse
import math
import zlib
from PIL import Image, ImageDraw, ImageFont

p = argparse.ArgumentParser()
p.add_argument('--font', type=Path, required=True)
p.add_argument('--surface-atlas', type=Path, help='Authored front/back material atlas; downsampled once at build time')
args = p.parse_args()
root = Path(__file__).resolve().parents[1]
W,H = 512,320
width,height = 66.72793579,41.66396713
atlas = Image.new('RGBA',(W*2,H))
for side in range(2):
    pixels=[]
    for j in range(H):
        y=(1-(j+.5)/H)*height
        for i in range(W):
            x=(i+.5)/W*width
            edge=min(x,width-x,y,height-y)
            grain=((i*17+j*113+(i*j)%43)%31-15)*.12
            # The PCB is opaque. Only the plastic beyond its edge blends with
            # the scene, avoiding transparent overdraw across the entire card.
            board=2.8<x<width-2.8 and 1.8<y<height-4.2
            r,g,b,a=(78+grain,106+grain,77+grain,255) if board else (163,172,166,100)
            if board and y>10:
                # Muted circuit routing behind the moulded shell.
                trace=(int(x*2.1)%9==0 and int(y*1.5)%13<9) or (int(y*2.2)%11==0 and int(x)%17<10)
                if trace:r,g,b=99,127,87
                if side and ((10<x<28 and 17<y<31) or (38<x<52 and 19<y<29)):
                    border=min(x-10,28-x,y-17,31-y) if x<28 else min(x-38,52-x,y-19,29-y)
                    r,g,b=(48,56,53) if border>.65 else (130,141,135)
                if side and ((8.5<x<10 or 28<x<29.5) and 18<y<30 or (36.5<x<38 or 52<x<53.5) and 20<y<28):
                    r,g,b=(165,174,166) if int(y*2)%2 else (77,99,76)
            # 48 contacts; copper/gold with a fine lengthwise brushed finish.
            if 4<x<width-4 and 1.9<y<8.7:
                phase=((x-4)/(width-8)*48)%1
                if .16<phase<.84:
                    glint=18*math.cos((phase-.5)*math.pi)+7*math.sin(y*.7)
                    r,g,b=207+glint,185+glint,101+glint*.6
                    if phase<.25:r,g,b=235,222,160
                    if phase>.75:r,g,b=158,140,72
                else:r,g,b=38,58,42
            if 3<x<width-3 and 8.8<y<11:
                r,g,b=177+grain,186+grain,174+grain
                phase=((x-4)/(width-8)*48)%1
                if .35<phase<.65 and 9.7<y<10.05:r,g,b=70,80,70
            # Molded layers and bevel reflections stay pale, slightly cyan.
            if edge<2.6:
                shine=math.exp(-((edge-.65)/.25)**2)*95+math.exp(-((edge-1.65)/.22)**2)*55
                dark=math.exp(-((edge-1.1)/.18)**2)*50
                r,g,b=128+shine-dark,139+shine-dark,134+shine-dark
                a=180 if edge<1.9 else 105
            if y>10.05:
                # A thin clear cover over the PCB, without real refraction.
                coat=17+12*(y/height)
                r,g,b=r*.72+coat+19,g*.72+coat+20,b*.72+coat+17
                band=math.exp(-((x/width+y/height-1.08)/.055)**2)*15
                r+=band;g+=band;b+=band
            # Upper latch windows, picked out by paired light/dark edges.
            for cx in (12.5,width-12.5):
                if abs(x-cx)<1.8 and height-4.3<y<height-2.2:
                    r,g,b,a=(180,196,190,235) if y>height-2.6 else (92,113,107,220)
            # Rear screw heads stay flat artwork; they do not add polygons.
            if side:
                for cx in (4.7,width-4.7):
                    dx,dy=x-cx,y-13.4;dist=math.hypot(dx,dy)
                    if dist<1.55:
                        shade=62+max(0,1-abs(dist-1.2)/.35)*90
                        if dist<.85:shade=142+dy*32
                        if abs(dx)<.17 and dist<.7:shade=46
                        r,g,b,a=shade,shade+8,shade+7,255
            # Rounded exterior and locating notches must remain cut out.
            if (y<.9 or y>height-.9) and (x<.9 or x>width-.9):
                cx=.9 if x<.9 else width-.9;cy=.9 if y<.9 else height-.9
                if math.hypot(x-cx,y-cy)>.9:a=0
            if side and 2.3<y<5.3 and (x<1.25 or x>width-1.25):a=0
            pixels.append(tuple(max(0,min(255,round(v))) for v in (r,g,b,a)))
    panel=Image.new('RGBA',(W,H));panel.putdata(pixels)
    # Embossed brand is deliberately subtle and sits above the separate label.
    if side==0:
        d=ImageDraw.Draw(panel);font=ImageFont.truetype(str(args.font),22)
        text='WonderSwan';box=d.textbbox((0,0),text,font=font);x=(W-(box[2]-box[0]))/2;y=10-box[1]
        d.text((x+1,y+1),text,font=font,fill=(84,94,88,210))
        d.text((x,y-1),text,font=font,fill=(222,228,218,240))
    atlas.paste(panel,(side*W,0))
if args.surface_atlas:
    # Packaging conversion only: retain the authored material, with the same
    # bounded runtime allocation and no per-frame material generation.
    atlas = Image.open(args.surface_atlas).convert('RGBA').resize((W*2,H),Image.Resampling.LANCZOS)
atlas.save(root/'assets/models/wonderswan/material.png')
data=zlib.compress(atlas.tobytes(),9)
lines=['// Generated by tools/make-wonderswan-material.py; RGBA8 zlib atlas.',
       '#pragma once','namespace WonderSwanMaterial {',
       f'inline constexpr int Width={W*2}, Height={H};',
       'inline constexpr unsigned char Compressed[]={']
lines+= [','.join(str(v) for v in data[i:i+32])+',' for i in range(0,len(data),32)]
lines+=['};','}']
(root/'app/source/wonderswan_material_data.hpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Shared RGBA atlas:',W*2*H*4,'bytes; compressed:',len(data))
