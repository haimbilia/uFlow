# WonderSwan clear cartridge

The owner supplied `source-top.stl` and `source-bottom.stl`. The assembled STL
coordinate envelope is 66.727936 x 41.663967 x 6.300000 mm. This preserves the
supplied model scale, not an independent caliper measurement of a retail shell.

`tools/prepare-wonderswan-model.py` reconstructs display-only shell LODs from
that envelope and the front shell's connector cutoff. It retains the connector
notches and bevels while omitting invisible print supports. These display meshes
are not replacements for the printable source files.

Regenerate from repository root:

```
python tools/prepare-wonderswan-model.py assets/models/wonderswan/source-top.stl assets/models/wonderswan/source-bottom.stl
python tools/embed-cartridge-model.py assets/models/wonderswan app/source/wonderswan_cartridge_data.hpp --system wonderswan
python tools/make-wonderswan-material.py --font <italic-font.ttf> --surface-atlas assets/models/wonderswan/material-source-v3.png
```

Rendering uses a 502-triangle near shell / 288-triangle far shell and continuous
surface quads. A single 1024x320 RGBA atlas (1.25 MiB)
is decompressed/uploaded once per renderer; every game shares it. PCB traces,
chips and rear fasteners are illustrative baked detail, not per-game PCB scans.
Opaque board/label surfaces prevent full-card transparent overdraw. Only clear
plastic margins blend with the background. There is no refraction, screen read
or runtime sort. The lower carrier is inset with a single front-shell bevel;
the exposed connector board has a recessed face and visible lower edge.

Game labels remain separate textures with rounded corners. The model uses the
existing shared preview/details/launch transform and media card/spine caches.
It is selectable as `wonderswan`; new type defaults recognize ws/wsc and .ws/.wsc.
This model change does not install an emulator or change any existing launcher.

Host test: compile tests/wonderswan_model_test.cpp with SDL2 and zlib, then pass
an output BMP path and optionally a label BMP. Host software-render timing is
not Wii U performance verification. The low-poly mesh and baked optical
material intentionally approximate clear plastic rather than simulate optics.

The revised material-source-v3.png was generated with the built-in imagegen tool
using the owner's Pocket Fighter photo as reference. It supplies clean molded
plastic, long gold fingers, a thin silver strip and illustrative PCB detail.
It is not a scan of retail internals. One-time texture upload adds mild contrast,
feathered shell opacity, and graduated contact shading: dark roots, bright middle
and softer dark tips. This adds no per-frame material processing. The blank
yellow label is a placeholder; game labels remain independent textures.
The revised source is a preview candidate, not yet a console-validated build.
Generation specification is recorded in material-prompt-v3.txt.
