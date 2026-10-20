"""Generate fixed YUV matrices for packed Y,U,V,A preview textures."""
from pathlib import Path
import subprocess
import sys
import re

root = Path(__file__).parent
header = '''; $MODE = "UniformRegister"
; $NUM_SPI_PS_INPUT_CNTL = 2
; $SPI_PS_INPUT_CNTL[0].SEMANTIC = 0
; $SPI_PS_INPUT_CNTL[0].DEFAULT_VAL = 1
; $SPI_PS_INPUT_CNTL[1].SEMANTIC = 1
; $SPI_PS_INPUT_CNTL[1].DEFAULT_VAL = 1
; $SAMPLER_VARS[0].name = "u_texture"
; $SAMPLER_VARS[0].type = "SAMPLER2D"
; $SAMPLER_VARS[0].location = 0
00 TEX: ADDR(64) CNT(1) VALID_PIX
    0 SAMPLE R1, R1.xy0x, t0, s0
01 ALU: ADDR(32) CNT(28)
'''
for mode in range(1,5):
    full = mode > 2
    bt709 = mode in (2,4)
    # Match the existing integer converter's coefficients exactly.
    cy = 256 if full else 298
    cr = (403 if bt709 else 359) if full else (459 if bt709 else 409)
    cb = (475 if bt709 else 454) if full else (541 if bt709 else 516)
    gu = (-48 if bt709 else -88) if full else (-55 if bt709 else -100)
    gv = (-120 if bt709 else -183) if full else (-136 if bt709 else -208)
    operations = [
        f'ADD R2.x, R1.x, {- (0 if full else 16)/255:.10f}f',
        f'ADD R2.y, R1.y, {-128/255:.10f}f',
        f'ADD R2.z, R1.z, {-128/255:.10f}f',
        f'MUL R2.x, R2.x, {cy/256:.10f}f',
        f'MULADD R3.x, R2.z, {cr/256:.10f}f, R2.x',
        f'MULADD R3.y, R2.y, {gu/256:.10f}f, R2.x',
        f'MULADD R3.y, R2.z, {gv/256:.10f}f, R3.y',
        f'MULADD R3.z, R2.y, {cb/256:.10f}f, R2.x',
        'MAX R3.x, R3.x, 0.0f', 'MAX R3.y, R3.y, 0.0f', 'MAX R3.z, R3.z, 0.0f',
        'MIN R3.x, R3.x, 1.0f', 'MIN R3.y, R3.y, 1.0f', 'MIN R3.z, R3.z, 1.0f',
        'MUL R0.x, R0.x, R3.x', 'MUL R0.y, R0.y, R3.y', 'MUL R0.z, R0.z, R3.z',
    ]
    # One instruction per group avoids source/read-after-write ambiguities.
    text = header.replace('CNT(28)', 'CNT(27)' if full else 'CNT(28)') + ''.join(f'    {i+1} {op.split()[1][-2]}: {op}\n' for i,op in enumerate(operations))
    text += '02 EXP_DONE: PIX0, R0\nEND_OF_PROGRAM\n'
    psh=root/f'video{mode}.psh'
    psh.write_bytes(text.encode())
    gsh=root/f'video{mode}.gsh'
    subprocess.run([sys.argv[1], 'assemble', '--vsh='+str(root/'video.vsh'), '--psh='+str(psh), str(gsh)],check=True)
    data=gsh.read_bytes()
    (root/f'video{mode}.inc').write_text('static const unsigned char video%d_gsh[] = {\n'%mode +
        ',\n'.join(','.join('0x%02x'%b for b in data[i:i+16]) for i in range(0,len(data),16))+'\n};\n')
    nv=text.replace('00 TEX: ADDR(64) CNT(1) VALID_PIX',
        '; $SAMPLER_VARS[1].name = "u_chroma"\n; $SAMPLER_VARS[1].type = "SAMPLER2D"\n; $SAMPLER_VARS[1].location = 1\n00 TEX: ADDR(64) CNT(2) VALID_PIX')
    nv=nv.replace('    0 SAMPLE R1, R1.xy0x, t0, s0',
        '    0 SAMPLE R4, R1.xy0x, t1, s1\n    1 SAMPLE R1, R1.xy0x, t0, s0')
    nv=nv.replace('R2.y, R1.y,','R2.y, R4.x,').replace('R2.z, R1.z,','R2.z, R4.y,')
    nv=re.sub(r'    (\d+) ([xyzw]):',lambda m:f'    {int(m[1])+1} {m[2]}:',nv)
    psh=root/f'nv{mode}.psh';psh.write_bytes(nv.encode());gsh=root/f'nv{mode}.gsh'
    subprocess.run([sys.argv[1], 'assemble', '--vsh='+str(root/'video.vsh'), '--psh='+str(psh), str(gsh)],check=True)
    data=gsh.read_bytes()
    (root/f'nv{mode}.inc').write_text('static const unsigned char nv%d_gsh[] = {\n'%mode +
        ',\n'.join(','.join('0x%02x'%b for b in data[i:i+16]) for i in range(0,len(data),16))+'\n};\n')
