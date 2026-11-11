# N64 cartridge refinement

The recorded source-STL shell envelope is 115.5 x 75.96896 x 18.6 mm. The owner supplied front photographs and a dimensioned label drawing: 55 x 62.5 mm, upper corners R4, lower corners R3, crown diameter 325 mm. The label and recessed surround follow that outline. Shell contour details are photo-guided, not new caliper measurements.

Regenerate with:

```powershell
python tools/prepare-n64-model.py
python tools/embed-cartridge-model.py --system n64 assets/models/n64 app/source/n64_cartridge_data.hpp
```

The generator also emits `app/source/n64_label_shape.hpp`, used to clip the label texture to the same curved outline. The default sticker is `native-installer/assets/default-labels/n64.png` (550 x 625). Near/far meshes retain distinct budgets; the far mesh omits the small locating shoulders. No front embossed wordmark is added above the label.
