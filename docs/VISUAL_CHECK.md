# Visual iteration

Use the real SDL renderer, not a separate HTML mockup. Keep `dist/visual-before`
as the baseline and `dist/visual-after` as the current review set.

## Desktop check

Build the image once with `docker build -f tools/visual-preview.Dockerfile -t uflow-visual-preview:1 .`.
Run from this project directory (absolute Windows paths shown):

```powershell
docker run --rm -v D:/Projects/WiiU/projects/uFlow:/project:ro -v D:/Projects/WiiU/projects/uFlow/dist/visual-after:/output -v D:/Projects/WiiU/projects/uFlow/dist/visual-fixture:/fixture:ro uflow-visual-preview:1 sh tools/visual-preview.sh
```

For supersampling checks, build the updated image and run with OpenGL/Mesa:

```powershell
docker run --rm --init -e SDL_VIDEODRIVER=x11 -e UFLOW_PREVIEW_GPU=1 -e LIBGL_ALWAYS_SOFTWARE=1 -v D:/Projects/WiiU/projects/uFlow:/project:ro -v D:/Projects/WiiU/projects/uFlow/dist/visual-aa:/output -v D:/Projects/WiiU/projects/uFlow/dist/visual-fixture:/fixture:ro uflow-visual-preview:2 xvfb-run -a sh tools/visual-preview.sh
```

The software SDL renderer cannot use the premultiplied blending required by case
supersampling, and deliberately falls back to direct rendering. The OpenGL preview
prints `Case 2x supersampling buckets: 3/3`; still test GX2 on the console.
The OpenGL run also checks four-title accelerated navigation and side-case cache
reuse, then exercises 120 frames of direction changes. Mesh counts are workload
checks, not Wii U frame-rate measurements. `sources.png` covers the in-window
source manager (path/depth alignment and text contrast). `sourceedit`, `media`,
`mediaedit`, and `mediaguide` cover the directory editors and media instructions.

The output/fixture directories must exist. Optional `cover.png`, `fullcover.png` and `background.png`
in the fixture directory provide fixed artwork. No game files are needed. Missing
artwork is deliberately included. Library/settings/directory/launch services are
stubbed on desktop; all drawing and the embedded font are the application's code.

## Console capture (.138)

Opt in by putting a 32-character lowercase hex key in
`/wiiu/apps/uFlow/visual-check.key`, then launch the new uFlow build. Only read once
at startup. Do not commit keys. Without a valid key, no socket is opened.
The development service accepts authenticated, fixed commands over UDP 32180:
`capture`, `library`, `details`, `details25`, `details50`, `details75`, `settings`,
`sources`, `sourceedit`, `media`, `mediaedit`, `mediaguide`, `directories`, `audio`, `missing`,
`live`. No arbitrary paths, shell, or launch commands.

### Idle slowdown measurements

`stats` returns a small authenticated UDP JSON reply without screenshots, storage
access, or changes to the current view. Example (use the existing development key):

```powershell
python tools/visual-check.py --host 192.168.1.116 --key <local-key-file> --screen stats
```

It reports uptime, CPU `malloc` used/free bytes, cache entry counts and render
mean/max milliseconds since the previous query (includes presentation wait).
Reading resets only timing aggregates. No continuous SD logging. CPU allocator
figures are NOT total RAM/GPU memory; constant values cannot rule out a GPU heap
leak. Compare one warmed sample, an idle sample, then a scrolling sample while
checking TV and GamePad together. Fourteen desktop previews plus a 3600-frame idle
soak validate cache counts, not console wireless delivery or Wii U memory usage.

```powershell
python tools/visual-check.py --host 192.168.1.138 --key dist/visual-check.key --screen details --output dist/visual-after/console-details.png
```

`capture` captures the current screen without changing it. Other screens enter a
fixed, read-only fixture library; B or `live` restores the real library and original
in-memory settings/sources. Preview does not save favorites/settings or launch games.
Optional console artwork: `/wiiu/apps/uFlow/visual-fixture/cover.png` and `background.png`.
Each request overwrites only `/wiiu/apps/uFlow/visual.png`; the PC tool retrieves it
after a success acknowledgement. Run requests serially. The PNG is uFlow's shared
SDL canvas, not independent physical TV/GamePad captures or capture outside uFlow.
Capturing may pause rendering briefly; never use captures to measure frame timing.
No continuous file polling, background worker, or in-game logging. The socket is
closed on launch/exit. To disable, remove the key and restart uFlow.

## Review loop

For music checks, build the current Dockerfile as `uflow-visual-preview:3` and run:

```powershell
docker run --rm -v D:/Projects/WiiU/projects/uFlow:/project:ro uflow-visual-preview:3 sh tools/test-music.sh
```

This generates test tones in a disposable container, tests the real SDL_mixer
memory-playback code with a dummy audio device, and checks configuration round-trip,
pause/resume, volume, and missing/malformed/oversized files. It does not establish
audible quality or Wii U device compatibility. Visual previews stub audio playback.

1. Render all fourteen screens before and after a change.
2. Inspect edges, contrast, clipping, missing artwork, directory controls and blur.
3. Check the 25/50/75% cover poses for rotation/occlusion. These are fixed samples,
   not a video or a test of animation timing.
4. Build the Wii U target. Upload only the existing `uFlow.wuhb`, keeping a local
   rollback copy. Never add another launcher icon.
5. Request real console frames after launch. Check exit/re-entry and transitions
   on hardware; desktop previews cannot prove GX2 correctness or performance.

First glass pass: cached antialiased nine-slice surfaces/shadows, directional rim
lighting, lighter neutral palette, stronger small text, separable backdrop blur
and a rounded background-refraction mesh. Raw launch backend is unchanged.
Cover-spin follow-up uses a timed full rotation and positive roll; inspect the
back-facing midpoint as well as the final front pose. The mesh is unchanged.

Full sleeves: `tools/prepare-fullcovers.ps1` exports local `_Source Artwork` scans
to `.media-build/fullcovers`. Install those in `/wiiu/apps/uFlow/media/fullcovers/`,
named by title ID or the same normalized title key as `covers/`. Layout is back on
left, narrow spine in center, front on right (135:14:135 proportions). Missing
sleeves retain the existing front art and plain case back; front images are never
stretched over the back. Aspect-ratio selection is heuristic: inspect exports and
replace any incorrectly matched scan. Original artwork and front exports are kept.

Spin durations: relaxed .60s, normal .45s, fast .32s. Cases use the smallest fitting
preallocated target: 640x768, 896x1024 or 1280x1536, rendering at 2x per axis then
bilinear downsampling. Use a real
`SDL_RenderClear` before each card: Wii U SDL enables alpha testing > 0, so a
transparent `SDL_RenderFillRect` is discarded, leaving old covers in the buffer.
The OpenGL desktop preview does not reproduce that default alpha-test behavior.
Allocation/blend-mode failure
falls back gracefully. Oversized cases beyond the fixed target also render directly.
Verify final slant, back text orientation, spine and opening/closing at all speeds.

Performance: Glass details reuses its frozen carousel/blur/lens backdrop through
the transition; library and settings render live. Desktop captures render twice
to exercise the reuse path. Wii U presentation waits for the queued display flip,
with V-sync reasserted on foreground return. Check actual horizontal tearing,
frame pacing and exit/resume on console; host screenshots cannot measure these.

Known hardware check issue: the first remote SDL readback PNG had repeated rows/
garbled pixels although the user approved the on-screen UI. UDP/FTP works but
pixel capture is not yet reliable on GX2. Use host previews and the existing
controller-triggered Screenshot Plugin until readback is corrected.
