# Format-correct default sleeve edits

Mode: built-in image generation, editing existing project PNGs.

## Spine correction passes

### n64

Edit this flat N64 sleeve. Keep its exact overall canvas aspect 3.088:1, colors, all artwork and logos unchanged. ONE correction: the central spine is too narrow. WIDEN the full-height central red spine to 7.77% of the total image width, centered, so it extends from 46.114% to 53.886% of the canvas. The back and front must be equal widths, each 46.114% of canvas. Reflow the existing spine title and its Nintendo/N logos inside that widened spine, shrink/reposition the front and back content slightly as necessary without distortion. Output is the entire flat sleeve, full bleed with no surrounding border or margins. Do not add elements or text.

### gba

Edit this flat GBA sleeve. Preserve overall image aspect 279:127 (2.19685:1), gray/silver/purple palette, logos and all content. ONE geometry correction: central silver spine is too WIDE. Make it exactly 8.96% of the total canvas width, centered between 45.52% and 54.48%. Back and front panels must each be exactly SQUARE and equal width: 45.52% of canvas each. Redistribute the freed width equally to left and right face panels. Scale down and rearrange the vertical spine typography so it stays inside that narrower central silver strip. Keep the main purple GAME BOY ADVANCE logo centered on the right front, without squashing or distortion. Flat full-bleed sleeve, no outer padding, no 3D mockup, no new text or decoration.

### N64 full bleed

Edit this existing flat Nintendo 64 full sleeve: REMOVE ONLY the thin white outer padding at the very top and bottom, extending each adjacent cream/red/blue artwork color to the canvas edge. The output must be full bleed from the first pixel row to the last. Preserve every other element, all logos, all text, the landscape face proportions, central wide red spine and exact spine boundaries, and same very wide 3:1 canvas. No other changes. Do not put it on a white background or add any new border.

## MSX

Use case: precise-object-edit. Input image 1 is the edit target, the existing flat MSX sleeve artwork. Preserve its exact design, ivory/blue/black colors, MSX logos, keyboard computer illustration, ROM cartridge drawing and all text. Correct only the sleeve proportions: final canvas aspect 295:190 (1.55263:1), with back and front each 135:190 and a full-height center spine 25:190. Center spine must occupy 8.475% of image width between 45.7627% and 54.2373%, so slightly widen the existing spine. Front and back equally wide. Reflow/position existing elements slightly rather than distorting them. Output flat edge-to-edge full-bleed sleeve, back left, spine center, front right. No outer padding or white border, no 3D case, no new text or objects.

The other five defaults already fit their system profiles and are retained.

## n64

Use case: precise-object-edit.
Asset type: flat full-wrap default cover texture for uFlow, NOT a 3D case or mockup.
Input image 1 is the edit target. Recompose its existing design to the new proportions, preserving the system identity, palette, logos and existing console illustration where present. Reflow elements, do not stretch or squash them. The entire output canvas is the sleeve artwork: back on the left, narrow vertical spine in the center, front on the right. No outer padding, no letterboxing, no crop marks, no labels explaining dimensions, no tabletop or perspective. All three panels touch and run from top to bottom.
Target canvas: 3088 x 1000 pixels, very wide 3.088:1. Back panel x=0..1424, spine x=1424..1664, front x=1664..3088. Each face is LANDSCAPE, 178:125 width:height; spine 30:125. Convert the reference's incorrect portrait DVD composition to an N64 landscape carton sleeve. Preserve cream background, red header and footer, blue Nintendo 64 wordmark, multicolored N logo, small Only For corner motif and Nintendo branding. Center the large N64 wordmark and N symbol attractively in the wider front; reflow the back's existing lower information block across its landscape width. Spine title vertical and wholly confined to its own band. Do not add a console picture or new promotional text.

## snes

Use case: precise-object-edit.
Asset type: flat full-wrap default cover texture for uFlow, NOT a 3D case or mockup.
Input image 1 is the edit target. Recompose its existing design to the new proportions, preserving the system identity, palette, logos and existing console illustration where present. Reflow elements, do not stretch or squash them. The entire output canvas is the sleeve artwork: back on the left, narrow vertical spine in the center, front on the right. No outer padding, no letterboxing, no crop marks, no labels explaining dimensions, no tabletop or perspective. All three panels touch and run from top to bottom.
Target canvas: 3088 x 1000 pixels, very wide 3.088:1. Back panel x=0..1424, spine x=1424..1664, front x=1664..3088. Each face is LANDSCAPE, 178:125 width:height; spine 30:125. Convert the incorrect portrait layout into a landscape SNES carton sleeve. Preserve black/charcoal, gray stripes, red SUPER NINTENDO ENTERTAINMENT SYSTEM logo, PAL VERSION, yellow/orange Only For Nintendo banner and four-color emblem. Keep the same SNES console and controller image on the front, fully visible without stretching; recompose it beneath a shallower header. Back: retain the striped design and three empty screenshot windows, rearranged horizontally to fit the landscape panel, with the existing branding footer below. Spine: vertical SUPER NINTENDO branding, confined to center band. No new objects or promotional copy.

## gba

Use case: precise-object-edit.
Asset type: flat full-wrap default cover texture for uFlow, NOT a 3D case or mockup.
Input image 1 is the edit target. Recompose its existing design to the new proportions, preserving the system identity, palette, logos and existing console illustration where present. Reflow elements, do not stretch or squash them. The entire output canvas is the sleeve artwork: back on the left, narrow vertical spine in the center, front on the right. No outer padding, no letterboxing, no crop marks, no labels explaining dimensions, no tabletop or perspective. All three panels touch and run from top to bottom.
Target canvas: 2232 x 1016 pixels, 279:127 aspect. Back panel x=0..1016, spine x=1016..1216, front x=1216..2232. Front and back must each be exactly SQUARE, spine 25:127. Recompose the reference into a square GBA carton cover; notably widen the actual central spine band to 200 pixels rather than a tiny DVD/DS spine. Preserve gray background, silver front-left GAME BOY ADVANCE strip, purple/white main GAME BOY ADVANCE logo and Nintendo and gold seal marks. Keep the large purple logo centered in the remaining front area, not stretched. Relocate small back footer labels as needed to square proportions. Keep spine's vertical GAME BOY ADVANCE marking inside its central band. Do not add a console picture or new text.

## pce

Use case: precise-object-edit.
Asset type: flat full-wrap default cover texture for uFlow, NOT a 3D case or mockup.
Input image 1 is the edit target. Recompose its existing design to the new proportions, preserving the system identity, palette, logos and existing console illustration where present. Reflow elements, do not stretch or squash them. The entire output canvas is the sleeve artwork: back on the left, narrow vertical spine in the center, front on the right. No outer padding, no letterboxing, no crop marks, no labels explaining dimensions, no tabletop or perspective. All three panels touch and run from top to bottom.
Target canvas: 2352 x 1000 pixels, 294:125 aspect. Back panel x=0..1136, spine x=1136..1216, front x=1216..2352. Front and back are slightly landscape 142:125, with a genuinely thin 10:125 spine. Convert the reference's tall DVD cover into these jewel-case proportions. Preserve black/ivory/orange design, diagonal orange stripes, TurboGrafx 16 logos, ENTERTAINMENT SUPER SYSTEM subtitle, NEC and HuCARD marks, the same TurboGrafx-16 console and controller on the front and HuCard drawing on the back. Recompose front into a shallow logo header above the console/controller image so both are fully visible and not squashed. Center the HuCard in the back beneath the logo. Thin spine with vertical logo within 80px band. Full bleed, no rendered plastic case edges or shadows around the sleeve.
