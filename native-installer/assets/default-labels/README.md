# Default cartridge artwork

Twelve system-specific label images: NES, SNES, Nintendo 64, Game Boy,
Game Boy Color, Game Boy Advance, Nintendo DS, Genesis, PC Engine,
WonderSwan, WonderSwan Color and MSX. These are generic system artwork,
not game labels. Real game artwork takes priority.

Created with the built-in image generation tool. Each final label is an
independent design fitted to its model's sticker proportions, not a square
tile. `sources/` keeps the high-resolution originals; `dimensions.json`
records export dimensions and generation prompts. The original square
`source-sheet.png` is a style reference only and is not installed.

NES includes a 10.85% top-edge strip, SNES a 17% inverted top-edge title and
Genesis a 7.5% wraparound strip. GBA and WonderSwan use wide compositions;
GB/GBC, DS, N64 and HuCard use their respective front-label dimensions.
MSX uses a generic landscape label. No artificial top-edge strip is added
to cartridges whose renderer does not map that surface.

The installer installs `media/<system>/labels/Default.png` in keep-existing
mode. Configured cartridge label sources discover `Default.png` automatically.
The cloud missing-media planner treats defaults as placeholders, so they do
not prevent downloading a real label later.

WonderSwan Color's unfolded sleeve is `../default-covers/wsc.png`; its layout
sidecar records the actual back/spine/front split and image byte count.
