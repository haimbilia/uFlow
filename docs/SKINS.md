# User skins

Skins are folders under `SD:/wiiu/apps/uFlow/skins/<your-skin>/`.
Download a skin, extract its folder there, restart uFlow, and select its name
under General > Skin. No application rebuild is needed. The installer keeps
user changes and third-party skin folders on reinstall.

Start by copying `switch-light` or `switch-dark` to a new folder name. A skin
contains a `skin.ini` manifest and optional PNG/JPG assets. Built-in manifests
also ship inside the app as a fallback. An SD skin with the same folder name
overrides its bundled counterpart. Selection is saved by folder ID.

```ini
version=1
name=My Skin
engine=switch-dark
background=202838
ink=f0f4ff
muted=596579
accent=41e0d0
avatar_x=60
avatar_y=38
avatar_size=54
dock_x=350
dock_y=546
dock_gap=116
row_x=100
row_y=322
tile_size=256
grid_fullscreen=1
grid_columns=9
grid_rows=3
grid_top=180
grid_height=320
background_image=background.png
icon_0=notifications.png
icon_1=browser.png
icon_2=gallery.png
icon_3=controllers.png
icon_4=settings.png
icon_5=power.png
```

Colors are six-digit RGB. Coordinates use the 1280 x 720 design canvas.
Assets are relative to the skin folder; absolute paths and parent traversal
are rejected. Images for dock buttons should be transparent square PNGs.
Omitted icons use the shared vector icon renderer. Unknown fields are ignored;
unsupported manifest versions are skipped. Current engine templates are
`pixel`, `glass`, `switch-light`, and `switch-dark`. Templates provide rendering
and navigation behavior; the skin supplies its name, colors, assets, avatar
position, dock placement, thumbnail row geometry, and wall grid geometry. Skin files contain no executable scripts.

The controller icon opens the Wii U HOME controller-pairing overlay. uFlow button remapping remains under Settings > Controls. Notifications and Browser
invoke Wii U system applications. Gallery reads existing PNG/JPG screenshots
under `SD:/wiiu/screenshots`, including per-title subfolders. Power always
requires confirmation, initially focused on Cancel.

Play time and native play counts come from the current Wii U account's activity
log when a title is identified there. Unsupported launchers show that time is
not reported, rather than estimating it from time between app launches.
uFlow launch counts and personal ratings are saved separately for each account.

The remappable System Buttons action defaults to L3 and focuses the home icons from any grid row. D-pad down also reaches them below the last game row; up returns to games. Local launch counts begin when this version is installed; Wii U titles use the current account system records when available.

Wall navigation keeps the selected game centered after a short slide, moving the grid as a whole in both axes. Grid clipping protects the header and system-button row.

Controls accepts button combinations: start Replace or Add, release the activation button, hold the desired buttons together, then release all to save. Plus signs between button icons mean a combination; slashes mean alternative mappings. Existing single-button alternatives are preserved. Clear removes both kinds; Reset restores defaults. Native button glyphs use the console system font in place, with vector fallback icons.

A button used both on its own and inside a combination performs its single-button action on release. This allows either combo press order without accidentally launching or navigating on the first press.

GamePad touch browsing: swipe left/right through rows, up/down on vertical logo wheels, or in either axis on the wall. Dragging moves the artwork in the finger direction. Short flicks advance one game; longer drags advance further without release inertia. Switch buttons activate on a tap release, never at the start of a drag.

Wall and Curved Wall fill the 1280 x 720 canvas behind the skin controls by default. Grid columns set density; row pitch follows the artwork proportions. Set grid_fullscreen=0 to opt into a bounded custom viewport using grid_top, grid_height and grid_rows. Selection remains centered, and render overscan fills row seams. Fan uses a common pivot below the row with the selected 3D case raised forward from its slot. Browsing rotates the row through the centered selection.

Controls > Next Layout cycles the current cover type layouts and remembers the selection. It starts unassigned, so existing mappings are preserved; bind a button or combination with Replace/Add. Curved Wall uses the same grid navigation as Wall.

Wall navigation wraps across row seams and both library ends in all four directions, including swipes. Use System Buttons to focus the dock. Controls version 5 separates both analog sticks: default left-stick/D-pad four-way navigation, right-stick up/down front/back inspection. Exact previous defaults migrate; custom bindings remain unchanged. Settled walls reuse one cached render; loading new artwork or moving invalidates it.
