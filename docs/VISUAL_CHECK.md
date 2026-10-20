# Visual iteration

`startviewchecks` verifies controller/touch selection in General > Start View,
independent of the currently browsed library. Startup resolves the persisted
library ID after applying VC categories, with first-available fallback when
missing and legacy numeric-index support. Tests cover reordered tabs, Favorites,
wrapping and more than five libraries. Settings/native installer tests cover
round trips, default All and preservation on reinstall.

`settingsactionchecks` exercises the fixed bottom action row in both skins:
Down from the last field reaches buttons, Left/Right selects enabled actions,
Up returns to the field, and A/touch activate the same handlers. It captures
General, Library, Media, editors, browser, launchers, guide and keyboard pages.
Save (+) and Delete (X) occupy the same slots across editors; their former
scrolling rows and duplicate helper text are removed. Check the complete button
bounds, flat item highlights over one frosted panel, and no library footer
behind Settings. Console appearance/input remain a separate hardware check.

`coverzoomchecks` covers the Display slider in both skins, controller steps,
touch dragging/clamping, scrolled hit testing and 40/120/200% captures across
all four layouts with flat/3D covers. `settings` checks persistence and legacy
bounds; native installer tests check default selection and preserved zoom.


`launchanimationchecks` captures spin/zoom/fade samples from library, Details,
Showcase and flat covers in both skins. It checks cancellation, target reuse,
restored blend/color/alpha state, unchanged selection, a 32/1349 header and empty
0/0 state. Native launch dispatch is never invoked by this host-only check.
Verify final frames are black and visually inspect the turn and intermediate
opacity. Console timing and HOME cancellation still require hardware validation.

`accentchecks` renders all 17 Display accent choices in both skins and checks
the actual selected swatch, Platform following GameCube, controller/touch wrap,
and different focused glass colors. It captures representative library, Details,
scan and Showcase scenes. `settings` in `tools/test-library.sh` covers palette
persistence, legacy IDs and bounds. Console appearance remains a hardware check.

`backgroundchecks` checks selected artwork at screen resolution, transparency
endpoints/midpoint, both skins, Details entry and cached reuse, front/back cover
inspection, delayed asynchronous arrival after opening Details, and controller/
touch adjustment of the Display transparency row. `asyncartworkchecks` additionally
checks that the selected background survives detail-cache eviction. These are
host rendering/cache checks, not console timing or freeze validation.

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

With the development key enabled, startup also emits a bounded thread inventory
to the console system log. `threads` repeats that read-only snapshot and returns
the logged count as JSON. It includes a guarded observation of the mapped-heap
lock identified in the September 28 heartbeat dump; it never acquires or clears
that lock. Thread state is a transient observation, not a consistent scheduler
snapshot. Capture with `tools/capture-syslog.py` before reproducing the failure.
Authenticated `pageforward` / `pageback` perform one ordinary ten-title page,
stop at either end, and return performance stats. They do nothing in overlays
and never launch titles or save preferences. These development commands permit
repeatable cover-cache turnover tests without requiring repeated controller holds.

`videostats` returns the selected title ID/index and cumulative playback timings,
audio starvation, loop counts and safe non-reference skips without reading the
SD. `next`/`previous` move one title through the ordinary library controls and
return those same stats; they are ignored in overlays. All require the development
key and never launch games or save settings. Verify the returned title ID before
attributing a performance sample to a particular video.

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

The development key also enables `uflow-heap-watch`: a static core-0 thread polls
the observed mapped-heap lock every 100 ms and emits a single bounded OSReport
saved-context/stack capture when the same owner persists across 250 ms of samples.
It does not change the lock or inspected thread and does no filesystem work.
It stops/joins before system background, exit or launch; normal keyless installs
create no observer. This is diagnostic only: three cores stuck with interrupts
disabled can prevent it from running, and the owner's saved context may be old.
Capture OSReport with `tools/capture-syslog.py` before reproduction; look for
`heap-owner-stall` and `heap-owner-frame`. The portable detection and bounded
stack walker are covered by `tools/test-heap-owner.ps1` under ASan/UBSan.
# Artwork memory pressure regression

Run `artworkmemorychecks` in the host preview to exercise 17 distinct 1536x1024
scans (102 MiB at original decoded size). Neighbor textures must settle at a
512-pixel maximum edge, the focused cover must upgrade to its original detail,
and its texture plus the selected background must remain resident. After each
of 17 focus changes, 80 idle frames must add no source decodes. Verify the detail
cache remains within 48 MiB plus its separately bounded resident thumbnails,
and clearing releases all tracked textures. Also run asynchronous artwork,
case rendering, video, backgrounds and Details regressions. This host check
does not reproduce or establish a fix for the console's mapped-heap lock stall.


`presentationchecks` verifies cover-type selection, per-type layout memory,
spacing slider touch bounds, all 9 box / 7 logo / 4 media layouts in both skins,
flat and logo geometry, non-box inspection/launch and ten system media shapes.
Run alongside layoutchecks, coverzoomchecks, settingsactionchecks,
wrapdisplaychecks and iconpreparationchecks. Hardware visual/input checks remain
separate from these host checks.

`bookshelfchecks` covers shared-camera spine/floor alignment, DVD and mixed-size
cases in both skins, selection motion, spacing/zoom extremes, wrap endpoints and
singleton libraries. Inspect the shelf against the Aurora reference separately
from the generic presentation checks.

Bookshelf performance checks also require 40 settled frames without rebuilding
geometry, cache invalidation after artwork clearing, and fewer than 12,000
projected vertices during selection motion. Inspect spin-out and return captures
and near-camera overlap order. Remote stats expose shelf_builds, shelf_cache_hits,
shelf_faces and shelf_vertices. Host success does not establish Wii U frame rate.

Bookshelf roll/insertion regression: compare the supplied Aurora clip frame by
frame. The cover rolls in the screen plane after clearing the shelf; the return
path aligns it first, then slides along its original thickness slot.
bookshelfchecks verifies all eight corners across seven case profiles, full
visible turns at 15/30/60 fps in both directions and all speed settings, and
captures forward/backward sequences. Inspect the returning case behind its
neighbors during insertion, including the now-visible bottom face during roll.

Held-scroll response: compile/run tests/browse_motion_test.cpp against app/source.
Simulate hold/release and reversal at 10-120 fps, every animation speed, wrap and
end stops. Fast must reach top repeat speed within 0.6 s and fully settle within
1 s, including a 1347-cover synthetic backlog. A single step retains the full
roll interval. Run bookshelfchecks and existing layout/presentation checks too.
Reported timing is from host simulation; console input/frame timing is separate.

Demo mode: General adds Demo Mode, with Idle Time directly below it only when
enabled (5-600 seconds, default 30). Run demo_mode_test and settings_test, then
GPU demochecks with /fixture/demo-1.mp4 and demo-2.mp4. Check both-skin conditional
rows/footer, current-library video-only random selection, no immediate repeat,
full clip completion, empty/missing videos and cancellation by input/menu. The
first wake-up input must not also launch a game. System return resets inactivity.
Run test-video.sh for natural EOF after frame/audio drain, cancellation and normal
looping playback, plus native Core-only/Core+Wii U defaults/reinstall preservation.

Switch skins (2026-10-05): `switchchecks` renders light/dark home, square
thumbnail row/wall, General and Display settings. It checks flat square poses
and cover-type cycling. `settingsactionchecks` now runs every page in all four
skins, including touch/controller footer access. Preview tooling embeds both
GlassFlow and DejaVu Sans; the console-only account badge uses nn::act, while
host captures show the neutral Player fallback. Verify the real Mii/name on
console separately. Screenshot output: dist/visual-switch/.
