# Content and media directories

Open Settings with **+**. Left/right on the **D-pad or left stick** moves focus
between the sidebar and main panel. Up/down navigates the focused area; holding
repeats. **A** opens a path/action or enters selector edit mode. While editing,
**left/right** changes the highlighted value; **B** ends editing in place, and
**up/down** ends editing and moves to the adjacent row. A also ends editing.
Returning to the sidebar and back preserves your draft unless you change sections.
Touch a section or row to select/open it; tap value arrows to change options.
Vertical swipes scroll lists; the footer buttons are touchable too. In the folder
browser, tap a folder to enter it, **Use Folder** to select it, or **Parent/Cancel**.
Every editor and folder browser stays inside the settings window.
**B only goes back**; exit uFlow with **HOME → Wii U Menu**. Minus does nothing.

From the cover flow, **hold D-pad/stick Up** to zoom the selected front cover;
release it to return automatically. **Hold D-pad/stick Down** to rotate the 3D
case to its back and zoom it; release it to return. **B** still cancels the
preview immediately. Both actions can be reassigned under **Settings → Controls**.
Manual scans are available only inside the Library/Media settings.

- **Library > Add new games** (also **Y** in the Library directory list) scans
  enabled content paths and adds discoveries without removing existing games.
  Repeated scans skip already cataloged entries. Existing artwork, metadata and
  favorites are kept, including games on disconnected drives. Separate copies
  at different paths remain separate launchable entries.
- **Library > Complete scan** replaces the catalog with the games currently
  found. Missing or disconnected games are removed from the list, not from disk.
  Large libraries may take a long time; the notice appears beside the action and
  during scanning. Use this to apply removals or rebuild existing game metadata.
- **Media > Refresh media** updates artwork, video and metadata associations for
  the existing catalog only. It does not discover or remove games.

Saving content paths/types preserves the catalog until you choose a scan.
Startup adds newly installed titles without removing disconnected installed
entries. Cancelled or failed scans keep the previous saved catalog.

## Game details

**General → Auto Sort Installed VC**, directly below **Show Installed Wii U**,
groups recognized installed Wii U VC/injected titles under their original
console. For raw titles, enable **Auto Sort VC by System** in each Wii U content
directory's Library settings. Both options default to Off and are independent.
Changes apply to the cached list immediately; turning the option off restores
the source category. The title keeps its Wii U launch method, media and favorites.
Unknown/custom packages without recognizable metadata remain in their source
library. Existing caches gain system metadata on load without a full scan.

Under **Library → Manage Content Types → SNES** (or another system), enable
**Media by Content Type**, then **Save Type**. Sorted VC titles use that type's
box dimensions and matching covers, sleeves, backgrounds, logos and videos.
Matching uses title ID or game name in that type's configured media directories.
Missing covers use the type's default sleeve or a placeholder, not a stretched
Wii U DVD cover. Existing backgrounds/logos/videos remain available when there
is no type-specific replacement. Returning to the library refreshes artwork in
a worker without rescanning game directories. Turning the option off restores
source artwork; launch settings, saves and title identities are unchanged.
Each type defaults to Off, including on fresh installation.

Press **Y** (or the remapped Details control) to open or close details. The first
star tab shows the synopsis; **A** toggles the favorite. The other icon tabs are
Play, Game launcher, Source, Media and Information. Up/down changes tabs; touching
a tab only selects it. Right enters selectable launcher/media controls, and Left
or B returns to the tabs. Read-only tabs retain focus; left/right pages long text.

**Game launcher** contains only launcher controls: select the title's launcher
(or Use Default), Inject/Re-inject for supported ROMs, and Launcher Settings.
The synopsis is confined to the first tab.

On **Media**, use Previous/Next to browse video and artwork. **Play/Stop** previews
the assigned video inside the page. Leaving video, closing details or opening
settings stops it. The library and details players do not decode concurrently.
PixelDeck uses its dark panel/controls; Glass Flow keeps its glass styling.

## Menu music

Put your MP3, OGG Vorbis, or WAV files directly inside
`/wiiu/apps/uFlow/music/`. In **General → Audio**, choose **Reload Music List**, select a
**Track**, turn **Menu Music** on, and set **Volume** (5% steps). Press A to edit,
then left/right to change; touch arrows also work. Settings persist across restarts.
The selected track loops. Native Setup includes GameCube Menu, Wii Menu, Wii Shop
Channel, and Wii U Menu tracks. Music is off by default; existing audio settings
and replacement music files are preserved on reinstall.
The switch stays OFF when no track is available or playback fails; a persistent
message in Audio explains the error. B returns to General without closing Settings.
Files must be nonempty and at most 32 MiB; the selected compressed file stays in
memory, with no continuous storage reads. Switching tracks may briefly pause the
UI while reading the new file. Playback pauses when backgrounded and stops before
launching a game or exiting. Codec/device compatibility still needs console testing.

## Library

The Library section lists all configured directories together. Choose **Add
Directory**, then set:

- **Directory:** A opens the browser. A enters a folder; Y selects the folder
  currently shown at the top; B goes to its parent; X cancels the browser.
- **Content type:** Wii U, Wii, or GameCube. Use a separate path for each type.
- **Scan depth:** 0 checks the selected directory itself; 1 includes its immediate
  subfolders; higher values include more levels, up to 8.
- **Enabled:** disabled paths stay configured but are not scanned.
- **Launcher (Wii/GameCube):** WiiFlow or USB Loader GX, independently per
  directory. Existing paths default to WiiFlow. Save and scan to apply changes.
- **Save Path:** save changes, then choose **Scan Now** in the directory list.
- **Remove Path:** removes this scan configuration, not the games or their saves.

Example: select `fs:/vol/external01/wiiu/raw-games`, type **Wii U**, depth **1**
when each game sits directly inside that folder. Use depth 2 if games are grouped
into another layer of folders. Raw Wii U games need `code/*.rpx` and `content/`;
normally they also have `code/app.xml`, `code/cos.xml`, and `meta/`.
Encrypted WUP packages (`title.tmd`, `.app`, `.h3`) are not raw games.
Wii discovery currently supports `.wbfs`; GameCube supports `.iso` and skips
`disc2.iso` as a separate title. Wii WBFS launches from SD or FAT32 USB with the
updated WiiFlow/USB Loader GX adapters. GameCube ISO currently launches from SD
and also requires Nintendont. WBFS scanning reads the disc ID and region, uses a
tagged parent folder for the display name, and matches configured media by ID.
Add new games repairs older unidentified Wii entries without removing the library.
RVZ must be converted to ISO, not renamed. See [launch setup](WII-LAUNCH.md).

Installed Wii U titles are discovered separately; **General → Show Installed Wii U**
controls their visibility. Installed titles don't need a raw-directory entry.
Existing `content_sources.cfg` paths and their options remain supported.

## Start on boot

**General → Start uFlow on Boot** enables startup when Aroma's patched boot module
would normally open Wii U Menu. Default is off. Hold **+** on the GamePad during
boot to bypass and choose Wii U Menu. Missing uFlow or launch failure falls back
to Menu. This requires the supplied `99_autoboot.rpx`; it does not install Aroma
coldboot or change NAND. Quick Start's explicit game choices are preserved.
Disable the toggle, or remove only `wiiu/apps/uFlow/start-on-boot.cfg`, to turn it off.
Exiting uFlow does not relaunch it; returning from vWii is a new Wii U boot and may.

## Media

Media works the same way: **Add Directory → Directory → Media Type → Library →
Scan Depth → Enabled → Save Path**, then **Scan Now**. The Library field selects
**All Libraries**, **Wii U**, **Wii**, or **GameCube**, including installed and raw
entries of that platform. It is a platform filter, not a Windows drive or a second
game-directory selector.

Use one media type per directory entry:

| Media type | Files | Purpose |
|---|---|---|
| Front covers | PNG, TGA, JPG/JPEG | Flat front artwork; replaces the built-in icon fallback |
| Full sleeves | PNG, TGA, JPG/JPEG | Wide image containing **back · spine · front**, left to right |
| Backgrounds | PNG, TGA, JPG/JPEG | Selected game's background |
| Logos | PNG, TGA, JPG/JPEG | Title logo; PNG/TGA can preserve transparency |
| Gameplay videos | MP4, AVI (MOV also indexed) | Full-screen idle preview, in front of or behind the library |
| Metadata | INI | Synopsis, developer, genre, year, players and other details |

Name each file by the title ID (preferred) or game name, not `cover.png` or
`background.png`. Names and extensions are matched without case sensitivity;
spaces/punctuation normalize to underscores and a trailing `(USA)`/`(EUR)`-style
parenthesized suffix is removed for name matching.

For the game **140** (title ID `00050000101fa800`):

```text
art/covers/00050000101fa800.png
art/fullcovers/00050000101fa800.png
art/backgrounds/140.png
art/logos/140.png
art/metadata/140.ini
```

Register each of these directories with its media type. Set Library to **Wii U**
and depth 0 for these flat folders. If you put assets in subfolders, increase depth;
the filename still needs the title ID or game name. Wii/GameCube can use the disc
ID extracted from their game filename/folder, or the displayed game name.

For duplicates: a title-ID filename takes precedence over a name filename;
within that match, a platform-specific directory wins over All Libraries; then
the first configured directory wins. Keep one matching asset per type to avoid
ambiguity. Unsupported file extensions, such as WebP, are ignored.

Keep images reasonably small for console memory: for example, 512×720 front art,
1024×685 full sleeves, and 1280×720 backgrounds. Full sleeves must be roughly
1.3–1.7 times wider than tall; the UV layout assumes 135 mm back, 14 mm spine,
135 mm front. Images larger than 2048 pixels on either side, or more than four
million pixels total, are not uploaded at full size. Large PNG/JPEG scans are
downscaled during decoding; source images are limited to 8192 pixels per side
and 16 million pixels. PNG uses a row buffer (including interlaced images), and
JPEG uses reduced-size decoding. Texture memory keeps the existing limits.
TGA retains the 2048-pixel/four-million-pixel limit.

### Optional default files

Each saved media directory has a **Default File** row. Select it to browse for a
file of that media type, or choose **None (No Default)** to disable the fallback.
For example, copy your GameCube `FullCover/Default.png` to the SD, edit the
GameCube **Full Sleeves** directory, and select that SD file as its default.
The console cannot access a Windows `D:` path. New image media directories
automatically select `Default.png` in the chosen folder (case-insensitive), if
present. You can select another file or explicitly choose None; the automatic
selection never overrides that choice. Existing saved directories are unchanged.
Use **Save Path**, then **Scan Now**.

Defaults apply only when no matching file exists for that media type. Existing
native icons/backgrounds are retained. A content-type-specific default wins over
an All Libraries default; ties use saved directory order. A real game-specific
file, including one from All Libraries, always wins over a default. Disabled
sources and missing default files are ignored. Default full sleeves still need
the back/spine/front layout and the same image size limits as regular sleeves.

The picker lists folders and supported files only; A/touch opens a folder or
selects a file, B goes to the parent, and X cancels without changing the selection.
Selecting None clears only the setting, never the file. Image-type changes retain
an image default; changing to an incompatible type clears the draft selection.
Video defaults use that directory's delay/layer/volume settings. Metadata defaults
apply only when no matching INI exists and fill only otherwise-empty fields.
Fallback lookup happens during scanning and is cached, not repeated every frame.

Example `140.ini` (plain UTF-8, one key/value per line):

```ini
publisher=Your publisher
developer=Your developer
description=Write the game's synopsis here on one line.
genre=Platformer
region=USA
year=2016
players=1
rating=4.0
```

The filename identifies the game; `title=` only fills an otherwise empty name.
Media → **Display Options** retains box-art, background, logo and preview toggles.
Media → **Media Setup Guide** shows a short version of these instructions on-console.

### Video previews

On a Media directory, change **Media Type** to **Gameplay Videos**. Three extra
rows appear: **Video Start Delay** (0–10 seconds in 0.25-second steps; default 1),
**Video Layer** (Foreground or Background), and **Video Volume** (0 mutes). Choose
**Background** to reveal **Foreground Opacity** (0–100% in 5% steps, default
100%). This fades the covers, navigation, title and footer together over the
unchanged video; it does not change the video's opacity or the Settings/Details
screens. At 0%, controls still work—open Settings to restore visibility. The value
is retained when switching layers but only applies during background playback.
Use up/down or touch swipes to reach the volume/save/remove rows. Save Path; run Scan
Now after adding a video directory or changing which games it matches. Changing
only delay, layer or volume applies when returning to the library.

Automatic previews start only while the main library is idle. Ordinary controller/touch activity,
game selection changes, library transitions, Settings and Details stop/suppress
the preview. Returning from Settings or Details requires a fresh idle delay.
Cover inspection is an exception for Background mode: the video keeps playing
behind the inspected cover. Inspection controls do not reset its timer, and B
closes inspection without dismissing that background video. Foreground videos
stop during inspection and require a fresh idle delay after closing it.
Foreground playback hides the library, with B to return; background playback
keeps covers and controls over the video. B dismisses playback until the selection
changes. Game launching and the configured library shortcuts still work. Menu
music is paused during video playback and resumed afterwards. Clips loop.

Supported codecs in MP4/AVI are H.264, MPEG-4 Part 2 (DivX/Xvid), and MJPEG;
audio supports AAC, MP3 and PCM. A container extension does not guarantee that
every possible codec inside it is supported. H.264 uses the Wii U decoder and
requires Baseline/Main/High (8-bit 4:2:0), with a declared level of 4.2 or lower;
other codecs use software decoding. Input is bounded to 1920×1088, displayed at
up to 1280×720/30 fps with aspect ratio preserved. For demanding sources, prefer
720p/30 encodes. Actual on-console performance depends on the video and storage.
The player streams through bounded buffers; it never loads a whole movie or
replaces/transcodes the original file. Missing, damaged and unsupported videos
show an error without repeatedly reopening while the same title stays selected.
Unsupported H.264 levels are rejected before opening the hardware decoder.
Cancelling a video hides it and detaches audio immediately; the main UI does not
wait for decoder cleanup. A new preview waits until that worker has exited.

`media_sources.cfg` retains its original five fields. Video rows optionally append
three tab-separated fields: delay milliseconds, foreground (1)/background (0),
and volume (0–100). Old rows default to `1000`, `1`, `70`. For example:

```text
previews\tid:wii\t1\t0\tfs:/vol/external01/wiiu/apps/uFlow/media/Wii/Videos\t1000\t1\t70
```

An optional ninth field stores the default file (absolute path or relative to the
source directory). Rows with a default include the three video-option fields as
placeholders even for image/metadata types. Missing/empty ninth fields mean no
default; old five- and eight-field files remain supported.
Video rows may also append a tenth field for foreground opacity (0–100); older
rows default to 100. The ninth/default-file field remains in place, including an
empty field when no default is selected.

The example uses `\t` to denote actual tabs. See [VIDEO_BUILD.md](VIDEO_BUILD.md)
for decoder sources, build instructions and verification commands.

## Existing files and storage

Without a saved `media_sources.cfg`, the existing `wiiu/apps/uFlow/media/` folders
(`covers`, `fullcovers`, `back`, `spine`, `backgrounds`, `logos`, `previews`, `metadata`) remain the
defaults. No artwork is moved. Saving an empty media path list intentionally
disables those defaults; built-in/local game icons may still be shown as fallbacks.

Choose a path the console can actually browse. A Windows path such as `D:\WiiU`
is not accessible to uFlow. `fs:/vol/external01` is the currently mounted SD/FAT32
source in this setup; other devices are usable only if mounted and exposed to the
application. Missing/disconnected paths stay saved and are skipped during scans.
Changing scan paths does not extend the raw launch backend's storage support.

Changes take effect after **Scan Now** or reopening uFlow. Scans rebuild game and
media associations together; they are currently synchronous, so large directory
trees can pause the UI. Media indexing happens during scans, not while scrolling.

## Front, back and spine artwork

Fresh setup creates these folders under `wiiu/apps/uFlow/media/{system}/`:

- `fullcovers`: a complete sleeve, laid out **back | spine | front**.
- `front`: individual front covers (older `covers` directories still work).
- `back`: individual back covers.
- `spine`: individual upright spine images.

Use the same title ID or game name in each folder, for example `front/Game.png`
and `back/Game.png`. Choose the corresponding media type in Media settings.
PNG, JPEG and TGA are supported. Existing registered full-sleeve folders also
discover sibling `front`, `back` and `spine` directories when those exist.

A game's full sleeve wins. Without one, each face is independent: a real front
plus a real back uses just the default spine; a front alone uses the default back
and spine. Missing faces use `Default.png` for that face, then the corresponding
section of the default full sleeve. A failed texture decode also permits the
default sleeve fallback. Extracted native icons continue to act as front art.
No extra composite image is written per game. Rescan after changing artwork.

Bundled default sleeves have `Default.png.layout` files describing their authored
cut positions, independently of the displayed box dimensions. If replacing a
default sleeve, remove its layout or update `UFLOW-SLEEVE-1 <image-byte-length>
<back-end-u> <front-start-u>` (coordinates between 0 and 1). A byte-length mismatch
or invalid cuts are ignored. User images are never resized or overwritten.

### Default box shapes

These are display presets for common packaging, not universal print templates;
regional boxes and MSX publishers vary. Custom width/height/thickness remain
editable in the content-type settings.

| System | Width × height × spine (mm) |
| --- | --- |
| Wii U / Wii / GameCube | 135 × 190 × 14 |
| Nintendo DS | 135 × 122 × 15 |
| NES | 130 × 178 × 25 (reference-derived proportions) |
| SNES | 178 × 125 × 25; sideways retail sleeve strip |
| Nintendo 64 | 178 × 125 × 30 |
| GBA | 127 × 127 × 25 |
| TurboGrafx-16 / PC Engine | 142 × 125 × 10 |
| MSX | 135 × 190 × 25 |

NES proportions come from the user's 3366 × 2100 Les Schtroumpfs sleeve:
approximately 1538 px back, 290 px spine, 1538 px front, normalized to height 178.
This image establishes ratios, not verified physical millimetres. The existing
NES default artwork already matches these ratios closely and is retained.

The PC Engine preset follows a [standard jewel case](https://www.cdrom2go.com/jewel-case-dimensions).
[MSX packaging varies by publisher](https://www.msx.org/forum/msx-talk/general-discussion/what-are-the-dimensions-of-the-boxes-for-msx-games),
so its preset is a tall cartridge case. Images with a substantially different
aspect ratio are fitted with margins instead of stretched. Old installer-written
135 × 190 × 14 defaults migrate once on load; non-default custom values are kept.


## Cover presentation

Display starts with **Cover Type**: Box, Logo or Media. Each type remembers its
own layout. Boxes offer Classic Flow, Carousel, Flat Row, Stacked, Fan, Arc,
Gallery, Wall and Bookshelf. Logos offer a flat row, Left/Right/Top/Bottom wheels,
an arc and a wall. Media offers a row, arc, fan and wall. Flat Row uses front
artwork without case perspective; the selected cover is larger.

Cover Spacing is a 50-200% slider (100% normal), controlled with Left/Right while
editing or by touch/drag. Existing Compact/Balanced/Wide values migrate to
75/100/125%; Cover Zoom remains independent at 40-200%.

Logo mode uses transparent logo PNGs from configured Logos directories, with a
title fallback when missing. Media uses system-shaped discs/cartridges, with the
logo on the label. Optional transparent game-specific artwork goes into a
**Discs / Cartridges** media source (`media` in media_sources.cfg). Fresh Setup
creates `media/<system>/media/`; existing installs discover this sibling of a
configured fullcovers folder when present. Run Scan Now after adding artwork.
Match files by game ID or name. A `Default.png` can override the generic media
shape and receives the title logo; a game-specific image is displayed intact.
Existing configs and media remain untouched on reinstall.
