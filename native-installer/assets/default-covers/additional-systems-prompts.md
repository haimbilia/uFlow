# Additional system sleeves — 2026-11-07

Created using the imagegen skill and built-in image generation (not CLI). Original generic system packaging; no game-specific artwork. Originals remain in the generated_images folder. Final assets are gb.png, genesis.png and ps1.png beside this document. No raster post-processing was applied.

## Prompt specifications

- Game Boy: Production flat full-wrap generic GAME BOY default cover texture for a 3D game library, approximately 2.18:1; back left 45.8%, vertical spine center 8.4%, front right 45.8%. Vintage silver/ivory diagonal stripes, navy GAME BOY typography, red accent. Original gray handheld, green blank LCD, burgundy buttons. Back text GAME BOY / PORTABLE GAME SYSTEM. Silver vertical title spine. No perspective on sleeve, margins, game title, screenshots, rating, barcode or official seal.
- Genesis: Production flat full-wrap generic SEGA GENESIS default cover, approximately 1.49:1; back left 45%, spine center 10%, front right 45%. Charcoal square grid, silver/white branding, red accents. Original black Genesis and three-button controller illustration; back line-art with SEGA GENESIS / 16-BIT GAME SYSTEM; black vertical branding spine. No game title, screenshots, ratings, barcode, official seals or outer shadows.
- PlayStation: Flat full-wrap original PlayStation PS1 system sleeve approximately 2.35:1; back left 48%, spine center 4%, front right 48%. White/cool gray technical geometry, restrained four-color accents, colorful PS symbol, PlayStation typography, original gray console and controller. Back console line-art and COMPUTER ENTERTAINMENT SYSTEM. Charcoal vertical-title spine. No jewel-case perspective, margins, screenshots, game title, rating, barcode, seal or fine print.

## Mapping and packaging

Panel cuts were measured from the generated images, rather than assuming the requested percentages were exact. Each .layout stores normalized cuts and the exact PNG byte length for the released renderer. Game Boy and Genesis assets ship in Core for custom libraries; PS1 ships with the PlayStation component. All are keep-mode assets: installer does not replace user custom sleeves. The current console maps Game Boy to custom_2, PlayStation to custom_1, and Genesis to custom_3; these IDs are not hardcoded into the package.
