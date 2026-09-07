# uFlow

uFlow is a cover-first Wii U game library inspired by the fast console workflow
of Aurora. One interface discovers Wii U, Wii, and GameCube games on FAT32 media;
platform-specific launch adapters stay hidden behind a single **Launch** action.

## Current status

The first graphical build is usable as a library browser. It includes:

- four animated 3D layouts (Classic Flow, Carousel, Flat Row, and Stacked) with
  Wii U, Wii, GameCube, and Favorites tabs;
- two switchable skins: the original `Pixel Deck` and Wii U-inspired
  `GlassFlow`, with smooth type, translucent panels, and Aurora-style details;
- a live case mesh with per-game artwork, media-rich title details, favorites,
  analog navigation, and accelerated scrolling;
- an Aurora-style settings dashboard with persistent source, motion, navigation,
  spacing, startup-view, and accent-theme options;
- scanning for Loadiine-format Wii U games, Wii `.wbfs`, and GameCube `.iso`;
- listing and launching installed Wii U games from internal storage and Wii U USB;
- the experimental Wii U loose-title adapter retained behind the new UI.

Wii and GameCube launch adapters are not connected yet. The Wii U adapter is
experimental and is still being debugged on real hardware.

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

Enhanced media uses `/wiiu/apps/uFlow/media/{covers,backgrounds,logos,previews,metadata}`.
Files may be named with a title ID or uFlow's normalized title name. Run
`tools/prepare-media.ps1` to convert the artwork collection into an optimized
media pack without modifying its source files.

## Controls

| Input | Action |
|---|---|
| D-pad / left stick | Move through the coverflow; hold left/right to accelerate |
| L / R | Change platform tab |
| ZL / ZR | Jump ten games |
| A | Launch |
| Y | Details |
| X | Toggle favorite |
| Minus | Rescan |
| Plus | Settings |
| B | Back / exit |

Inside Settings, use `L/R` to change section, the D-pad to select and change an
option, and `B` to return. Settings are saved to
`/wiiu/apps/uFlow/uflow.cfg` on the FAT32 device.

## Build

Docker Desktop is the only host dependency:

```powershell
.\build.ps1
```

The app itself can be built from `app/` with its Dockerfile and `make`. The
frontend uses devkitPPC, WUT, SDL2, and libpng.

## Architecture

- `app/` — WUHB frontend, library scanner, cover cache, and dashboard.
- `tools/convert-case-model.mjs` — converts the optimized licensed glTF case
  mesh into the compact renderer data included by the frontend.
- launch adapters — isolated behind the frontend API and published once stable.

The UI and scanners are intentionally independent from launch adapters so work
on one platform cannot destabilize the whole library.
