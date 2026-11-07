# Genesis display model

The standard Western shell uses the supplied Genesis_Cartridge.stl envelope of
108.5 × 69.5 × 16.5 mm. These are STL measurements, assuming millimetres, rather
than a physical caliper measurement of a retail cartridge. The label recess
measures 76.4 × 58.7 mm. Photos guide the added rear screw wells, relief lettering,
textured black polymer, and connector finish. The two screw centres are 70 mm apart.
The relief lettering is an approximation rendered at library-view scale.

Run tools/prepare-genesis-model.py with the source STL to rebuild the 1,800 and
900 triangle assets, then tools/embed-cartridge-model.py with --system genesis.
STL exports retain millimetres. OBJ geometry is centred and normalized by height.
The app uses a static BSP generated offline, cached materials and two detail levels.
The runtime material and label wrap are in app/source/cartridge_models.hpp.
