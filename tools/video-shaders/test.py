"""Evaluate generated shader ALU instructions against the CPU coefficients.

This checks the actual shader source (including clamping and vertex modulation),
not a duplicate of its floating-point formula. Hardware sampling still requires
console verification.
"""
from pathlib import Path
import itertools,re
root=Path(__file__).parent
for name,mode in itertools.product(('video','nv'),range(1,5)):
    full=mode>2;bt709=mode in (2,4)
    cy=256 if full else 298
    cr=(403 if bt709 else 359) if full else (459 if bt709 else 409)
    cb=(475 if bt709 else 454) if full else (541 if bt709 else 516)
    gu=(-48 if bt709 else -88) if full else (-55 if bt709 else -100)
    gv=(-120 if bt709 else -183) if full else (-136 if bt709 else -208)
    ops=[]
    source=(root/f'{name}{mode}.psh').read_text()
    if name=='nv': assert 'SAMPLE R4, R1.xy0x, t1, s1' in source
    for line in source.splitlines():
        m=re.search(r'\d+ [xyzw]: (\w+) (.*)',line)
        if m:ops.append((m[1],m[2].split(', ')))
    assert len(ops)==17
    for y,u,v in itertools.product(range(0,256,17),repeat=3):
        r={'R0.x':.8,'R0.y':.6,'R0.z':.4,'R0.w':.3,'R1.x':y/255,'R1.y':u/255,'R1.z':v/255}
        r.update({'R4.x':u/255,'R4.y':v/255})
        for op,args in ops:
            dst,*sources=args
            values=[r[s] if s.startswith('R') else float(s.rstrip('f')) for s in sources]
            a,b=values[:2]
            r[dst]={'ADD':lambda:a+b,'MUL':lambda:a*b,'MULADD':lambda:a*b+values[2],
                    'MIN':lambda:min(a,b),'MAX':lambda:max(a,b)}[op]()
        l=cy*(y-(0 if full else 16))+128
        expected=[(l+cr*(v-128))>>8,(l+gu*(u-128)+gv*(v-128))>>8,(l+cb*(u-128))>>8]
        for ch,e,mul in zip('xyz',expected,[.8,.6,.4]):
            assert abs(r['R0.'+ch]-max(0,min(255,e))/255*mul)<1/255
        assert r['R0.w']==.3
    assert (root/f'{name}{mode}.gsh').read_bytes()[:4]==b'Gfx2'
print('PASS: eight packed/native shader matrices, limited/full range, BT601/709, saturation, colour/alpha modulation')
