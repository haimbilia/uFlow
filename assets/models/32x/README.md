# Prepared display mesh

Built from the user-supplied STL files with `tools/prepare-sega-models.py`. Original Desktop files remain unchanged. STL exports retain millimetres; OBJ exports are centered and normalized by cartridge height for the existing uFlow mesh pipeline.

Both detail levels are single-component, watertight, consistently wound meshes; the exported STL files were reloaded and checked. These are display assets, not replacement manufacturing shells.

The two 32X halves are boolean-unioned in their supplied assembly coordinates, rotated upright, and stripped of enclosed cavities/internal supports. Fine rear lettering is replaced with a clean label-ready panel; visible screw recesses and the connector opening remain. Detail levels: 1,800 and 900 triangles, from 188,540 source triangles.
