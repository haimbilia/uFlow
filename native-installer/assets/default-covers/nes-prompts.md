# NES default cover generation

Created with the built-in imagegen tool using the imagegen skill; AI-enhanced reconstruction, not lossless enlargement. Originals unchanged. Final image: `nes.png`. Installer compilation is deferred until the user finishes supplying covers.

## Upscale prompt

Use case: precise-object-edit.
Asset type: flat NES full-cover sleeve texture for uFlow, not a 3D mockup.
Input image: the supplied local image is the edit target.
Primary request: faithfully upscale and restore this low-resolution sleeve, targeting 2800 x 1760 pixels (4x the 700 x 440 source) with sharp clean edges and lettering. Preserve the complete original landscape composition and proportions: black back on the left, narrow central spine, front on the right. Preserve the original placement, dimensions, rotation, colors, logos, barcode, gold seals, ACTION SERIES starbursts, red Nintendo wordmarks and yellow ENTERTAINMENT SYSTEM text. Keep the large white slanted-bottom panel on the upper front completely blank white, and keep all empty black areas empty black. Preserve the thin outlined spine label at the top only, with its vertical Nintendo/ENTERTAINMENT SYSTEM lettering. Main text exactly as in source: 'Nintendo', 'ENTERTAINMENT SYSTEM', 'ACTION SERIES', 'Game Pak (NES-GP)', 'NES P BR'. Restore readable lettering without inventing new fine print. Change only resolution, pixelation, blur and edge quality. No redesign, no extra imagery, no new labels, no perspective or lighting effects, no crop, no added margins. Retain the source's precise back/spine/front geometry and approximately 1.591:1 aspect ratio.

## Final console-compositing prompt

Use case: compositing.
Asset type: high-resolution flat NES full-cover sleeve for uFlow.
Input 1 is the edit target: the cleaned-up black NES sleeve with the blank white panel. Input 2 is placement reference ONLY: a console on the front of an EverDrive sleeve. Input 3 is the actual NES console and controller to insert.
Change ONLY the blank white slanted-bottom panel on the upper right/front of Input 1: add the NES console and connected controller from Input 3, as a crisp faithful product image. Arrange it large and centered in this panel, retaining the console's three-quarter view, gray housing, black vent stripe, front controller ports, single controller with two red buttons, and connected cable. Fit the complete console and controller comfortably inside the white panel without cropping or covering any existing lettering. Keep the white panel background and its exact boundary shape.
Preserve the whole sleeve's back/spine/front proportions, positions and all existing text, seals, barcode, colors and empty back area from Input 1. No other modifications. Do NOT copy EverDrive text, Krikzz branding, stars, blue background, features, or other elements from Input 2. No additional logos, text, peripherals, controllers, borders, margins or 3D sleeve mockup. Flat landscape texture, approximately 1.591:1 aspect ratio. Target 2800x1760 high-resolution pixels if available; prioritize clean sharp artwork and preserve layout.

