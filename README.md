# uFlow

uFlow is a cover-first Wii U game library inspired by the fast console workflow
of Aurora. One interface discovers Wii U, Wii, and GameCube games on FAT32 media;
platform-specific launch adapters stay hidden behind a single **Launch** action.

**[Downloads, setup guides and showcase videos](https://karotech.io/uflow/)**

Latest release: **2026.10.23.2 - security update**. Removes privileged IOSU
network debugging left enabled in the bundled loader. **Run the updated Setup
and restart the console; app-only updates do not replace the affected modules.**
Settings, games and saves are preserved. See the [release notes](docs/releases/2026.10.23.2.md).
Console boot/game-launch retesting of this rebuild remains pending.

uFlow requires a Wii U running Aroma. Copy `uFlow-Setup.wuhb` to
`SD:/wiiu/apps/`, launch Setup from Wii U Menu, review the selected components,
install, then fully reboot the console. Setup does not install Aroma.

## Current status

This is an active hardware-tested development project, not a finished universal
emulator. It includes:

- Box, Logo, Media and Thumbnails cover types, each with compatible layouts.
  Boxes include Classic Flow, Carousel, Flat Row, Stacked, Fan, Arc, Gallery,
  Wall, Bookshelf and Curved Wall. Logos include four directional wheels;
  walls support four-direction navigation and wraparound;
- continuous cover-spacing and zoom settings, plus adjustable inspection zoom;
- GlassFlow, Pixel Deck, Switch Light and Switch Dark skins, with external
  manifests and assets for user-created skins. See [skin format](docs/SKINS.md);
- a live case mesh with three-axis transitions, per-game artwork, optional
  projected reflections, soft shadows, media-rich title details, favorites,
  independent D-pad/stick navigation, touch swipes, multiple bindings and
  button combinations;
- an Aurora-style settings dashboard with a directory browser, multiple
  content and media paths, per-directory type/depth options, add-only discovery and explicit complete scans,
  and persistent view options;
- scanning for Loadiine-format Wii U games, Wii `.wbfs`, and GameCube `.iso`;
- listing and launching installed Wii U games from internal storage and Wii U USB;
- configurable gameplay-preview delay, and an idle demo mode that visits games
  with videos and returns to the original game when interrupted;
- current Wii U account profile, available play statistics and personal ratings;
- optional Wii U title installation through the Install Content library;
- the experimental Wii U loose-title adapter retained behind the new UI.

Wii and GameCube launch through separate WiiFlow/Nintendont backends. DS, NES,
SNES, GBA, N64, TurboGrafx-16/PC Engine and MSX have VC injection adapters;
compatibility depends on the ROM and supplied VC base. NooDS is an additional
DS backend. RetroArch offers NES, SNES, GBA and PlayStation backends. ROMs,
Nintendo VC bases and console keys are not in this repository.

GlassFlow is the default skin. Media directories are grouped by content type;
system-specific case proportions and independent front/back/spine artwork fall
back to default sleeves. MP4/AVI previews support background playback and
adjustable foreground opacity. Installed Wii U titles are discovered automatically.

The in-app updater checks on startup, offers Yes/No, shows progress and version
changes, and verifies signed releases before replacing the app. Version 1 updates
the uFlow app only, not Aroma modules or emulator backends. See
[update service](updates/README.md) for publishing and recovery tests.

## Storage layout

```text
FAT32:/
├─ wiiu/raw-games/Game Name/{code,content,meta}/
├─ wbfs/Game Name [GAMEID]/GAMEID.wbfs
├─ games/Game Name [GAMEID]/game.iso
└─ covers/GAMEID.png
```

Wii U artwork is read from `meta/iconTex.tga`; a `cover.png` or `cover.tga` in a
game folder overrides it. Favorites are stored beside the app.

Enhanced media defaults to `/wiiu/apps/uFlow/media/{covers,fullcovers,backgrounds,logos,previews,metadata}`.
Change these paths and their library scope in Settings > Media.
Files may be named with a title ID or uFlow's normalized title name. Run
`tools/prepare-media.ps1` to convert the artwork collection into an optimized
media pack without modifying its source files.
See [Content and media setup](docs/CONTENT_AND_MEDIA.md) for naming, formats,
scan depth, and metadata examples. MP4/AVI video previews play after an adjustable
idle delay on the main library screen only. Select Gameplay Videos on a media
directory to set the delay (default 1 second), foreground/background layer and
volume. Settings, Details and ordinary controller/touch activity suppress playback.
Cover inspection keeps background videos running; it suppresses foreground videos. B dismisses
a preview until the highlighted game changes (or closes an open cover inspection).

## Controls

These are the default library bindings. Settings > Controls lets you remap them;
the main-screen hints, empty Favorites prompt, and cover-inspection hints follow
the active mappings immediately. Settings and Details keep their menu navigation
controls: A selects/activates, B goes back, and Plus opens Settings from Details.
Use Replace or Add to assign a button or combination, and Clear to remove an
action's bindings. D-pad and both sticks can be assigned independently.
Next Layout is available as an action without a default binding.

| Input | Action |
|---|---|
| D-pad / left stick | Navigate the library, including four directions in wall layouts |
| Right stick up / down | Hold to inspect the front / back cover |
| L / R | Change platform tab |
| ZL / ZR | Jump ten games |
| A | Launch |
| Y | Details |
| X | Toggle favorite |
| Minus | Launch options: start the selected title once with any compatible launcher (e.g. NooDS or VC injection for DS, RetroArch or VC injection for NES/SNES/GBA, WiiFlow or USB Loader GX for Wii). Scroll to reach additional launchers. Wii VC uses Nintendo VC on USB; its broken WBFS conversion option is disabled. In the list, + makes the highlighted launcher that title's default; choosing its content type's launcher clears the per-title choice |
| Plus | Settings |
| B | Sort while browsing; back / close panels elsewhere; never exits uFlow |
| R3 | Hold the A–Z selector |
| L3 | Focus system buttons in Switch skins |
| HOME | System menu / exit uFlow |

Inside Settings, use D-pad/left stick **left/right** to move focus between the
section sidebar and main panel, and **up/down** to navigate the focused area.
**A** opens an action or enters selector edit mode (highlighted value). While editing,
**left/right** changes the value; **B** finishes editing without leaving the page,
and **up/down** finishes editing and moves to the adjacent row. Outside edit mode,
left/right moves between the sidebar and panel; **B** returns.
GamePad touch supports section/row taps, value arrows, footer actions, and vertical
swipes through directory lists. Bumpers no longer change settings sections.
**Library** lists all content directories together;
open a path to set its platform, depth, and enabled state, or remove it.
**Media** works the same way, with media type and library scope (All, Wii U,
Wii, or GameCube) inside each path. Use **Add new games** or **Complete scan**
in Library settings, or **Refresh media** in Media settings. The first run scans
automatically when no saved catalog exists. In the
directory browser, `A` opens a folder and `Y` selects the current folder.
Source lists, editors, and directory browsing share the main Settings pane;
`B` goes up a directory/back, and `X` cancels directory selection. A successful
scan is saved as a catalog beside uFlow, so later starts show the last known
library immediately. Source edits preserve the saved library until you choose
a scan. Add new games retains existing entries; Complete scan rebuilds the list.
Settings and source paths are saved beside uFlow on the FAT32 device.

**General → Audio** selects looping menu music, on/off, and volume. Put MP3, OGG Vorbis,
or WAV tracks (up to 32 MiB each) in `/wiiu/apps/uFlow/music/`, choose **Reload
Music List**, then select a track and enable music. Setup includes optional menu
music assets. Menu sound effects have a separate on/off setting and volume;
see [menu sounds](docs/MENU_SOUNDS.md).
Only the selected file is retained in memory; playback does not keep reading
the drive. Music pauses in the background and stops before game launch/exit.
If a track is missing or playback fails, the switch returns to OFF and Audio
shows the error. B returns from Audio to General in the same settings window.

Wii U raw scanning uses `code/app.xml` to distinguish base games from updates
and DLC. Updates are not separate launch entries; their metadata can fill in
missing base-game names/icons. This does not apply update or DLC game data.
Raw launch requires the experimental backend, not just the WUHB frontend.

## Build

Docker Desktop is the only host dependency:

```powershell
.\build.ps1
```

The frontend uses devkitPPC, WUT, SDL2, FFmpeg and libcurl. Use the video builder
described in [VIDEO_BUILD.md](docs/VIDEO_BUILD.md), not just the base image.
See [external dependencies](emulators/README.md) for pinned checkouts and patches.
The optional personal GlassFlow background video is not committed; without it
the skin uses its built-in background. Native installation is built using
`tools/build-native-installer.ps1` after preparing locally supplied dependencies;
follow [the release workflow](docs/native-installer-release.md). The personal
installer can embed locally supplied Nintendo VC bases. These bases and console
keys are not tracked in this source repository.

GlassFlow embeds the Oxanium typeface under the SIL Open Font License; its
license is included at `app/data/OFL.txt`.

## Architecture

- `app/` — WUHB frontend, persisted library scanner, bounded artwork cache, and dashboard.
- `tools/convert-case-model.mjs` — converts the optimized licensed glTF case
  mesh into the compact renderer data included by the frontend.
- launch adapters — isolated behind the frontend API and published once stable.

The UI and scanners are intentionally independent from launch adapters so work
on one platform cannot destabilize the whole library.

Library settings offer **Add new games** to keep existing/offline entries, and
**Complete scan** to rebuild the list from currently connected sources. Large
complete scans can take a long time. The Media tab's **Refresh media** updates
artwork/video/metadata for existing games only. Saving a source preserves the
catalog; no game files or saves are deleted by either scan mode.
