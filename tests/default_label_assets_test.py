"""Final export proportions match the label regions in cartridge renderers."""
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]/'native-installer/assets'
ratios={'n64':55/62.5,'gba':42/19.7,'gb':47/36,'gbc':47/36,
        'ds':29.84/25.35,'pce':48.6/42,'wonderswan':61.387936/22.92,
        'wsc':61.387936/22.92,'genesis':76.4/(58.7/.925),'snes':83/(32/.83),
        'nes':384/704,'msx':768/420}
for name,ratio in ratios.items():
    im=Image.open(root/'default-labels'/f'{name}.png')
    assert abs(im.width/im.height-ratio)<.003,(name,im.size,ratio)
    assert max(im.size)<=804
    if name=='nes':
        total=.413/(im.width/im.height)
        assert .675<total<=.810 # Activates the existing top-edge mapping.
    if name=='snes':assert 1.55<=im.width/im.height<=2.25
    print('PASS',name,im.size)
sleeve=root/'default-covers/wsc.png'
magic,size,back,front=sleeve.with_suffix('.layout').read_text().split()
assert magic=='UFLOW-SLEEVE-1' and int(size)==sleeve.stat().st_size
assert 0<float(back)<float(front)<1
print('PASS WonderSwan Color sleeve split and byte count')
