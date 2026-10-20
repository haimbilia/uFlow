# Native installer release workflow

Release 2026.10.21.1 (sequence 2026102101): app includes additive scans and Wii
USB identification/dispatch; Setup includes the tested WiiFlow probe/retry and
USB Loader GX adapters. Publish installer plus app to Karotech and GitHub, and
advance signed stable app-only OTA. Backend updates require Setup. See
docs/releases/2026.10.21.1.md for observed rear/front USB results and limits.
User explicitly requested public publication; prior supplied-installer approval
remains in force. Freeze completed artifacts before uploading.


USB mount diagnostics/retry (2026-10-21): rebuild only the WiiFlow bridge after
running prepare-wiiflow-backend.py, which copies usb_launch_probe.hpp. The
existing Wii component mapping ships its boot.dol; sync payload and rebuild
Setup, then test Core+Wii fresh install/reinstall. Deploy only the new WiiFlow
DOL to the console and keep Setup local. No app, cIOS, port, settings, game or
save changes. Host tests cover one retry after mount failure, partition remapping,
missing/invalid/wrong-ID/ambiguous files, and no USB probe for SD launches.
Hardware result (2026-10-21): user confirmed Wii Sports launched from USB with
the changed build. Whether the remount retry ran has not been checked in the log.
Validation passed: Wii/GC/GX/probe host tests, WiiFlow and Setup builds, payload
sync and actual Core+Wii fresh install/reinstall. WiiFlow DOL deployed to .116
with staged SHA256 verification; Setup remains local. Evidence:
artifacts/usb-probe-deployment.json.

Wii FAT32 USB launch and WBFS identification (2026-10-21): rebuild the app,
WiiFlow exact-path bridge and USB Loader GX adapter. Both backends advertise
UFLOW_WII_USB_STORAGE_V1; the app refuses old SD-only backends for USB requests.
Existing sync-native-payload.py mappings ship all three binaries. Rebuild Setup
and test Core+Wii fresh install/reinstall. Keep Setup local; the console needs
app plus apps/wiiflow/boot.dol and apps/usbloader_gx/boot.dol. No settings,
catalog, ROMs, saves, channel or cIOS changes. Add new games repairs unidentified
Wii entries. Evidence: artifacts/usb-wii* and artifacts/usb-gx*.
Console USB launch and simultaneous WFS/FAT32 behavior remain unverified until
Wii Sports is retested; GameCube USB is outside this change.
Validation: Wii launch/GX dispatch, WBFS scanner/additive repair and library tests,
21 payload tests, Core+Wii actual fresh install/reinstall, app/backends/Setup builds
passed. App and both DOLs deployed to .116 with staged SHA256 verification;
configuration/catalog hashes unchanged. Installer kept local.


Additive library scans (2026-10-20): Core provides Add new games and explicitly
labeled Complete scan, with replacement/large-library notices. Media scans are
labeled Refresh media. Source saves preserve the catalog; startup keeps missing
installed titles. Rebuild app, sync Core payload, rebuild Setup, and run scan
worker/offline catalog regressions plus actual Core fresh install/reinstall.
Deploy app only; keep Setup local. Evidence: artifacts/additive-scan*.
Validation: additive worker ASan/UBSan and installed discovery passed; 21
payload-sync tests and actual Core fresh install/reinstall passed. App deployed
to .116 with SHA256 verification; configuration and catalog hashes unchanged.
Console scan behavior remains a hardware retest.


Region audit 2026.10.20.2: rebuild app and RPXLoadingModule, sync payload,
rebuild Setup and test fresh/reinstall paths. OTA is app-only; Setup and a
restart are required for the HOME-menu module fix. See REGION_COMPATIBILITY.md.


Independent preview audio reading (2026-10-20): the compressed video queue
could block the shared demux reader, starving audio during sustained native
decoder waits. Give audio its own local-file demux cursor and worker, retaining
the existing two-second PCM ring and 512 KiB/96-packet video queue. Discard
non-audio streams on the audio cursor; drain audio at EOF and join both readers
before seek, close, or codec teardown. No conversion or replacement media.
The app ships through the existing Core payload mapping; no settings or
dependency changes. Evidence: artifacts/video-independent-audio.
Console playback confirmation is required before calling this a hardware fix.
Console test: user reports mostly smooth audio after independent reading;
initial loading still interrupts. Follow-up gates startup on a half-second
PCM reserve or audio EOF; the video timeout cannot override audio readiness.
An injected 800 ms initial audio-read delay exercises the previous underrun.
Final deployment and remaining hardware status are in the evidence directory.

DK Tropical Freeze USB path fix (2026-10-06): live capture reports errno 22
opening /vol/content/. followed by FSA_STATUS_MEDIA_ERROR and 1503030.
Enable FatFs dot-name handling (FF_FS_RPATH=1), retaining read-only storage.
Both app and resident loader compile the shared filesystem and must be rebuilt.
Loader identifies raw-port18-dot-paths. Existing sync-native-payload.py app
and RPXLoadingModule mappings ship both artifacts; no additional dependency.
Settings, game content, and saves remain unchanged. Loader needs console restart.
Validation: exact CONTENT/. FAT32 reproduction failed before the fix and
passed afterward; dot, parent, file-read and missing-parent cases pass. USB
RAW suite, app/loader/Setup builds and Core/Core+WiiU install/reinstall pass.
App and loader deployed to .110 with completed transfers and matching sizes.
Installer remains local. Pending console restart and DK launch confirmation.
Evidence: artifacts/dk-tropical-startup.



Held A-Z fix (2026-10-06): active held alphabet selector retains ownership
of its activation binding across the modal input guard, including combos.
Release still applies/exits; other modal actions remain isolated. Core app
carries the fix on fresh installation without settings/dependency changes.
Validation: single-button/combo A-Z hold/navigation/release checks, held zoom
and Settings/Sort regressions passed. Native app/Setup and actual Core plus
Core+WiiU install/reinstall passed. App deployed to 192.168.1.110 with
completed FTP transfer and matching published size; settings/library untouched.
Installer local only. Hardware interaction remains unverified. Evidence:
dist/alphabet-hold-* and artifacts/alphabet-hold-deployment/deployment.json.



Held inspection fix (2026-10-06): menu transition guard excludes held cover
inspection so right-stick zoom stays active until release. Settings/Sort
transition isolation remains. Core app carries this on fresh installation;
no settings or dependency changes. FTP address now 192.168.1.110.
Validation: held front/back zoom across 60 input frames and Settings/Sort
regressions passed in host UI checks. Native app/Setup builds and actual
Core/Core+WiiU installation/reinstall passed. App deployed to .110 with
completed transfer and matching published size; settings/catalog untouched.
Installer local only. Hardware interaction remains unverified. Evidence:
dist/zoom-hold-* and artifacts/zoom-hold-deployment/deployment.json.



Menu input/icons (2026-10-05): discard pending chord singles across overlay
transitions so Settings Back cannot open Sort on release. Sort opens only
from browsing and its assigned action closes it. Draw all 28 binding bits,
including right stick; use solid direction triangles beside D-pad/stick icons.
PixelDeck small helper text uses readable 17px smooth text. Core app carries
these fixes on fresh installation without changing custom settings.
Validation: native app build and UI/menu-transition checks passed; previews
confirm right-stick glyphs and larger helper text. Actual Core/Core+WiiU
fresh install/reinstall passed. FTP app transfer completed and published
size matched; settings/catalog untouched. Installer local only. Logs:
dist/menu-icons-*. Console interaction remains unverified.



Wall navigation/right stick (2026-10-05): infinite wall coordinates permit
left/right row seams and four-direction wrapping, including swipes. Controls
version 5 adds four right-stick virtual buttons, labels/icons, capture/combos
and GamePad/Pro input routing. Defaults use left stick + D-pad navigation and
right stick up/down inspection; migrate exact old defaults only. Core app
carries defaults on fresh installation; preserve custom controls on reinstall.
Cache the settled wall until movement/artwork revision and replace per-line
chrome shading with gradient geometry to reduce per-frame rendering work.

Validation: settings/chord migration and GamePad/Pro direction capture tests,
UI/navigation/wall-cache checks, native app build, 21 payload checks, and
actual Core/Core+WiiU fresh installation/reinstall passed. App deployed with
completed FTP transfer and matching published size; no large readback, per
user preference. Settings/catalog were not written. Hardware audio smoothness
remains unverified. Evidence: dist/wall-controls-* logs and
artifacts/wall-controls-deployment/deployment.json. Installer remains local.



Fullscreen walls and WiiFlow fan correction (2026-10-05): default wall viewport
is now the entire canvas, with row/column overscan across grid seams. Skin
controls draw above it; custom skins may opt into grid_fullscreen=0 for bounded
views. Fan cases now roll toward a common pivot below the row, with a larger
selected 3D case raised from its slot. The supplied video clarifies a centered
radial fan; browsing moves cases along the arc. Existing Core app
payload carries this; no patched dependency or installer config change needed.


Validation: native app/Setup builds, GPU previews and navigation/settings
checks, skin/swipe tests, 21 payload checks, and actual Core/Core+WiiU fresh
install/reinstall passed. Evidence: artifacts/full-wall-deployment and
dist/full-wall-* logs. App deployed after clean exit with staged hash verified. User requested
stopping the remaining readback and minimizing future console checks.

Wall/fan correction and Curved Wall (2026-10-05): use a shared camera for
3D cases on the table, with front faces away from the viewer at the far edge.
Curved Wall is appended as Box layout 9, preserving all existing layout IDs.
Both walls keep centered selection and four-direction navigation/swipes. Fan
uses restrained rolls and skin-safe geometry. Default wall density is 9 x 3;
custom skin grid fields still override it; Curved Wall fits more columns to
keep upright cases close together. Next Layout is appended as control action
18, unassigned by default; all prior binding IDs remain intact. Existing Core app payload carries
these changes; no new dependency or installer setting is required.


Validation: host GPU previews/settings controls, swipe/settings unit checks,
21 payload checks, native app/Setup builds, and actual Core/Core+WiiU fresh
install/reinstall passed. Evidence: artifacts/swipe-deployment and
dist/wall-fan-* logs. Console interaction remains unverified.

Deployed app staged/final full hashes verified. Console restarted and loaded
the saved library; uflow.cfg then changed (new layout combo assigned). The
other settings and saved catalog hashes are unchanged. Installer stayed local.

Touch browsing (2026-10-05): GamePad swipes navigate horizontal game rows,
vertical logo wheels and both axes of the wall. Lock the gesture axis, support
short flicks and continuous bounded steps, and stop on release without inertia.
Switch dock actions activate on taps released without dragging. Missing/invalid
samples, overlays, controller input, demo wake and background transitions cancel
the gesture. Existing settings gestures remain unchanged. No new defaults or
dependencies; the Core app mapping carries the fix on fresh installation.


Control combinations and icons (2026-10-05): controls_version=4 adds combo_N
lists alongside legacy bind_N alternative masks. Capture waits for activation
release, records the largest simultaneously held chord, and commits on release.
Clear/reset covers both lists; completed chords take precedence over constituent
single buttons. Use native system-font button glyphs with vector fallbacks,
plus signs for chords and slashes for alternatives. No external font or patched
dependency is shipped; Core app payload carries this and defaults remain intact.
Buttons shared between a single action and a chord defer the single action
until release; a completed chord cancels those pending singles. This protects
launch/navigation for either button order. Host capture, routing, persistence,
settings UI and 21 payload tests passed. Deployment/installer evidence is under
artifacts/combo-deployment and dist/combo-*; hardware behavior remains unverified.
Native app/local installer builds and actual Core/Core+WiiU install/reinstall
passed. Console staged/final app hashes match; all nine settings files and
the saved catalog are unchanged. Setup was kept local only.


Controller icon correction (2026-10-05): use SYSSwitchToSyncControllerOnHBM
to open the native HOME controller-pairing overlay, with foreground recovery
on launch failure. uFlow remapping remains in Settings > Controls. Existing
Core app payload ships this fix; no installer defaults or dependencies change.
Fan now uses the supersampled 3D case renderer with a shallow yaw/pitch,
retaining its curved arrangement and outward roll. Flat Row stays flat.
Wall follows the selection in both axes with a short camera slide and keeps
the selected game centered at rest. Box+Wall renders actual 3D cases; other
cover types retain their own artwork renderer. Clip the wall to its skin area.
Binding capture runs before normal UI input, consumes the assignment gesture,
resets repeat state and waits for release/neutral before navigation resumes.
The Aurora table reference adds a dense tilted grid under an enlarged centered
selection. Switch Details, Sort and Showcase now use the active skin palette
and clean panels. Grid density defaults are 9 columns / 4 box rows; custom
skin geometry overrides remain supported. No SD manifest overwrite is needed.


Home navigation, skins and layouts (2026-10-05): square thumbnail artwork in
Details/Showcase, stable selectable wall grids, independent D-pad/stick bindings
with replace/add/clear, adjustable inspection zoom, Switch system icons and
status indicators, per-account statistics/ratings, radial logo-wheel tilt and
a front-facing curved fan. User skins are data-defined folders; see SKINS.md.
Core includes four editable manifests and twelve icon assets in keep mode.
Reinstall must preserve edited manifests, third-party skins, account statistics,
settings and saved catalogs. Native defaults include inspection_zoom=4 (2.0x).
No new patched binary dependency is required. App/host UI, settings and skin
statistics checks passed, as did all 21 payload tests. Native installer and
console publication results are recorded in artifacts/home-upgrade-deployment
and dist/home-upgrade-* logs; host tests do not establish hardware behavior.
Local Setup build and actual Core/Core+WiiU fresh install/reinstall passed.
Console app staged/final readbacks match; all nine settings files and the saved
catalog remain byte-for-byte unchanged. Installer stays local only.


Media-library settings preservation (2026-10-05): saving media sources no
longer invalidates the saved game catalog. Playback-only options apply without
scanning. Matching changes persist a pending marker and refresh artwork/video
for existing entries after Settings closes; Media > Scan Now uses this same
media-only path. The worker never discovers, sorts or removes games. Source
appearance overrides remain transient; refreshed source artwork is committed
only when a saved catalog exists, preserving pending game-source invalidation.
Cancellation or save failure retains the previous cache and pending retry.
Media configuration is published through the existing atomic SavedFile helper.
No defaults or new dependencies are added. The existing Core app payload ships
the fix on fresh installs; Setup must preserve cache/library-v1.bin and the
media-refresh.pending marker on reinstall, for Core and Core+WiiU selections.
Validation evidence: artifacts/media-library-deployment and dist/media-library-*
logs. Host regression/build and deployment results are recorded separately;
console behavior remains to be confirmed by editing a media source after launch.
Host catalog/media regressions, all-four-skin settings actions, native app and
local installer builds, 20 payload checks, and actual Core/Core+WiiU install
and reinstall passed. The broad video UI test still fails its sharp-cover
upgrade assertion on both the preceding build and this build; recorded as a
pre-existing host-test failure, not a media-catalog regression.
Console deployment: staged/final app readbacks verified; all nine settings
files and the existing saved game catalog are byte-for-byte unchanged.
The installer remains local only. Hardware settings-edit retest is pending.


Switch skins, thumbnails and account badge (2026-10-05): add skin IDs 2/3
for Switch Light/Dark with flat backgrounds, clean sans-serif text, square
selection outlines and a round action rail using the actual uFlow bindings.
Display's first row adds Thumbnails (type 3), with independently remembered
2D row/wall layouts. Front artwork is center-cropped, never stretched or given
box depth; existing Box/Logo/Media IDs survive. Selecting a Switch skin selects
the thumbnail cover type. General retains the existing two skins.
The active Wii U user's Mii/name are read through nn::act once at startup,
kept in memory only, and shown top-left in Switch skins and top-right in the
existing skins. A simple avatar is used if no Mii image is available.
DejaVu Sans and its license are bundled in the app; preview tooling includes
both fonts and keys text caches by font family. Native Setup includes the
thumbnail layout default, preserves saved skin/type/layout choices, and uses
the existing app payload mapping for the embedded font and profile support.
Validation: native app/local installer builds passed, Switch visual/geometry
checks and all-four-skin settings navigation passed, settings round trips and
20 payload checks passed, and actual Core/Core+WiiU install/reinstall passed.
Console staged/final app hashes match; all nine settings files are unchanged.
Host visuals use the neutral avatar; real account portrait validation remains
a console check.


Demo return selection (2026-10-05): capture the selected game's absolute path
when an idle demo begins. On input or menu/ineligible exit, stop demo playback
and restore that game by identity in the current visible list, resetting cover
motion and preview focus. Clear the saved identity after exit so each session
captures its own starting game. If the game is no longer visible, retain the
current valid selection. No persisted settings or installer defaults change.
Host demo checks cover a non-video origin, multiple random selections, changed
list order, a second session with a different origin, and menu cancellation.
Demo, settings-action and control-hint checks and the native app build passed.
The native installer bundles the updated app through the existing Core mapping.
Validation: local installer build, 20 payload tests and actual Core/Core+WiiU
install/reinstall tests passed. Console app staged/final readbacks match; all
nine settings files are unchanged. Controller validation remains pending.


Raw USB game startup cache (2026-10-05): Super Mario 3D World's controller
splash persisted while the live trace continued opening new preload resources
successfully for several minutes. Small absolute-path opens took roughly
0.5-1.5 seconds; the raw loader lacked the browser's FAT sector cache. Port17
uses the same bounded 2 MiB read-only cache in the resident game module,
serialized by the existing mutex, reset on raw close/remount, and invalidated
on read errors. Save routing and game data remain unchanged. The existing
sync-native-payload.py RPXLoadingModule mapping includes this dependency in
Core, including Core+WiiU selections; no additional payload entry is needed.
A production-FatFs startup fixture repeats 600 late-directory title opens,
missing-file checks and bulk reads: identical file bytes, raw calls reduced
from 2,703,002 to 1,156. These are host I/O counts, not console launch timings.
Build the production loader without RAW_TRACE, rebuild the local installer,
and test actual Core/Core+WiiU install/reinstall preservation. All cache/USB regressions, 20 payload checks, actual Core/Core+WiiU
install/reinstall checks, and local installer build passed. The user confirms
the uncached title eventually loaded after approximately 10 minutes. Console
validation of the cache is pending restart and launch. Production port17
was deployed with staged/final hash verification; all nine settings files are
unchanged. The console rebooted before publication and still reports port16,
so one further restart is required to activate the new loader. Subsequent
boot logs confirm port17 active. The user relaunched Super Mario 3D World and
confirms faster arrival at the title screen. Exact launch duration and
prolonged gameplay were not measured.


Demo mode (2026-10-03): General adds Demo Mode (default Off) and a conditional
Idle Time row immediately below it (5-600 seconds, step 5, default 30). Persist
both settings; native Setup creates the defaults and preserves existing choices.
After inactivity, select random video-equipped games in the current visible
library, avoid immediate repeats when possible, wait for scrolling to settle,
then play each preview once. Advance only on natural EOF after final frame/audio
drain. Demo playback works independently of the normal preview toggle and uses
the existing source's playback presentation/volume. Skip broken video paths;
empty candidate sets stay idle. Input exits the demo and consumes the wake-up
press. Menus, scanning, installation and system return reset inactivity.

Validation: state/settings round trips, bounds, rollover and random selection;
GPU demochecks in both skins with full clip completion, conditional rows/footer,
input/menu exit and stale/empty video libraries passed. Settings actions/hints,
bookshelf/presentation regressions and the video player suite passed, including
natural completion, normal loops, audio drain and cancellation. Final production
app/local Setup builds, all 20 payload checks and actual Core-only/Core+Wii U
fresh-install/reinstall checks passed, including demo defaults/preservation.
Console demo playback/input retest remains pending.

Deployed to verified .251 after clean exit, staged and final full readbacks.
App SHA256: 688aba5d2f66be38bb16da286ef0edda32ba59206fbbc96f0402d6ae4af75da1.
Local-only installer SHA256:
0ccb8fbf687ab410cbe04f17074c7a017bb35328ca27b2092563eaa560c81a84.
All nine settings files unchanged. Evidence: artifacts/demo-mode-deployment,
dist/demo-mode-*.log and dist/visual-demo-mode.


Held-scroll response (2026-10-03): shorten speed-aware repeat acceleration
(Fast top tier at 450 ms). Latch rapid bookshelf catch-up when repeated inputs
or pages produce a backlog, keep it through the last cover and clear it at exact
rest. Preserve the full roll duration for individual steps. Reset motion state
when rebuilding the library or leaving Bookshelf. No new persisted settings.

Validation: browse_motion_test simulates 10-120 fps, every speed, hold/release,
reversal, wrap/end stops and a 1347-cover Fast backlog. Worst normal-held release
was 0.50 s across settings; Fast was 0.316667 s, reaching top speed in 0.508333 s.
The extreme backlog settled within one second on Fast. These are host timing
results; console input timing remains to be retested. Production app/local Setup,
GPU bookshelf/presentation/layout checks, all 20 payload checks and actual
Core-only/Core+Wii U fresh-install/reinstall checks passed.

Deployed to verified .251 after clean exit with staged and final full readbacks.
Console reopened during the first attempt; final publish followed a confirmed
exit. All nine settings files unchanged. Installer remains local only.
App SHA256: c982b920d8b9d29a1a6b4f4b6329085e398682539fe50ec5de25f4dcee0efbb6.
Installer SHA256:
a3051d7b07fddd23bffa28f008fd0be992938aeeea26212c79b45a2492ea2696.
Evidence: artifacts/browse-response-deployment, dist/browse-response-*.log,
dist/visual-browse-response and tests/browse_motion_test.cpp.


Bookshelf Aurora roll and slot return (2026-10-03): inspect the user's supplied
55-63 second clip frame by frame. Replace the yaw revolution with a full roll
in the screen plane. Extract along the original thickness slot before rotating;
reverse that path so return aligns first, then slides between the neighboring
cases. Restore shelf depth ordering during insertion, instead of drawing every
moving case in front. Close the case's bottom surface for upside-down rotation.
Use timed selection motion in both directions, with existing speed settings.

Validation: production app/local native Setup builds; GPU bookshelf, presentation
and layout checks passed. Corner checks cover seven profiles and the original
slot during insertion plus whole-case clearance during roll. Timing checks cover
both directions/all speed settings at 15/30/60 fps. Reviewed sequential forward
and backward preview captures. All 20 payload tests and actual Core-only/Core+
Wii U fresh-install/reinstall checks passed. Console motion retest remains pending.
Deployed to verified .251 after clean exit, staged and final full readbacks.
App SHA256: d766a10d925ac3bd13e73c80a39e181212a409f14d6a893cef20407df00204ec.
Local-only installer SHA256:
af9ee29714e85a6a3c7fda6e4b570ceb6ad0f5a7a668d48e42d9d2a9a3e4aabe.
All nine settings files unchanged. Evidence: artifacts/bookshelf-roll-deployment,
dist/bookshelf-roll-*.log, dist/visual-bookshelf-roll and dist/aurora-reference.


Bookshelf slower spin (2026-10-03): lengthen bookshelf selection/spin-out and
return timing fourfold at every existing animation-speed setting. Other layouts
and menu timing remain unchanged; preserve the settled shelf cache.
Validation: production app and local native Setup builds, GPU bookshelf,
presentation and layout checks, all 20 payload tests and actual Core-only and
Core+Wii U fresh-install/reinstall checks passed. The preview launcher initially
stalled starting Xvfb; running the checks on its ready display passed.
Deployed to verified .251 after clean exit with staged/final full readbacks.
App SHA256: 7a2b8be72d487d230317b300a511d5f3ae1ea0071a5e6fb2c5b9c3eadff7d5b4.
Local-only installer SHA256:
24af41e5634633f41e697c943a408d158f2fa5d58f65bf08daecd46550f210e8.
All nine settings files unchanged. Console animation retest remains pending.
Evidence: artifacts/bookshelf-slow-deployment, dist/bookshelf-slow-*.log and
dist/visual-bookshelf-slow.


Bookshelf speed, depth order and spin-out (2026-10-03): cache the resting shelf
in a single transparent texture; invalidate it on artwork and scene changes.
Bound scrolling geometry and prioritize nearby artwork. Snap settled selection
to its exact position. Sort shelf cases relative to the actual camera so nearby
cases overlap correctly. Pull the selected box clear before spinning it forward,
and reverse the motion on return. Preserve the selected install badge.
Expose shelf build/cache-hit/face/vertex counts in performance diagnostics.

Validation: production app and local native Setup builds passed. GPU bookshelf
checks cover both skins, mixed cases, spin-out/return, 40 idle frames without
geometry rebuild, artwork invalidation, moving geometry budget, zoom/spacing,
wrap and singleton cases. Presentation and layout checks passed. Projection tests
passed 18,150 poses plus subdivision limits. All 20 payload checks and actual
Core-only/Core+Wii U fresh-install/reinstall checks passed.
App deployed to verified .251 after clean exit, staged and final full readbacks:
372bf4468c9d775db20df5c5f7f8802e1ba0b54d5f9e4884630a9522bff5dad1.
All nine settings files unchanged. Native installer retained locally, SHA256
39590c951e56402389c9cb54406437e56e5aa1dbdb805c0b6618aec0259d160e.
Evidence: artifacts/bookshelf-fast-deployment, dist/bookshelf-fast-*.log and
dist/visual-bookshelf-fast. Wii U frame rate and appearance retest remain pending.


Bookshelf perspective correction (2026-10-03): replace independent carousel
billboards with cases packed by physical thickness in a single shared camera.
Align spines and bases on common planes, show the top surfaces in a diagonal
receding shelf, and pull the focused cover forward to the left. Interpolate
selection in the same scene; project floor reflections through the shared camera.
Retain the other box/logo/media layouts and existing saved preferences.

Validation: app/native Setup builds, GPU previews in both skins with DVD and
mixed-size cases, movement, zoom/spacing extremes, wrap boundaries and singleton
passed. A geometry check verifies adjacent spine boundaries coincide. All-type
presentation and existing layout regressions passed; all 20 payload tests and
actual Core-only/Core+Wii U fresh-install/reinstall checks passed.
App deployed to verified .251 after clean exit with staged/final full readbacks:
a5cc489cce4f5afc9f996834977f64c9738e5698289c9a4aeac814cefec11784.
All nine settings files unchanged. Native installer retained locally, SHA256
e0e2e22e28c8e53f9625b27a53f8193951ad2260142315aac642a5b1fe52e672.
Evidence: artifacts/bookshelf-correction-deployment, dist/bookshelf-*.log and
dist/visual-bookshelf. Hardware appearance/input retest remains pending.


Cover presentation and layouts (2026-10-03): Display starts with Box / Logo /
Media and retains an independent layout choice for each. Add Fan, Arc, Gallery,
Wall and Bookshelf box layouts; Logo Row, four HyperSpin-style wheel orientations,
Logo Arc and Logo Wall; Media Row/Arc/Fan/Wall. Flat Row draws front artwork only,
with a larger focus, no perspective/side geometry or offset shadow, and optional
floor reflections. Support sideways sleeve fronts. Cover Spacing is a 50-200%
touch/controller slider; legacy presets migrate to 75/100/125%.

Media uses system-shaped discs/cartridges with the game logo on the label, or
matched transparent media artwork. Add media source kind/folders, Default.png
discovery, cache v9 and v7/v8 migration. Logo/media browsing prioritizes its own
artwork and stays flat during inspection and launch. Fresh native Setup defaults
to Box and 100% spacing; reinstall preserves existing choices and data.

Validation: production app and native Setup builds passed. Both-skin presentation
checks cover all 20 layouts, type/layout memory, spacing drag, non-box inspection
and launch and ten media shapes. Layout, zoom, settings actions, wrap/display,
hints, paging, launch options and preparation checks passed. Settings, library,
media, content-type and v7/v8 cache migration tests passed; all 20 payload checks
and final actual Core-only/Core+Wii U fresh/reinstall tests passed.
App deployed to verified .251 after clean exit, staged and final full readbacks:
7630e7e13817a7727b7cc75d08dc1820b8e08174539a3d71c089cab9ff18c575.
All nine settings files unchanged. Setup rebuilt/tested locally only, SHA256
7fb03d3f39af463e5e84e7dd7225fc2d44c2d0f0e7a22f3dbd43d40e5f4458b1.
Evidence: artifacts/cover-presentation-deployment, dist/presentation-*.log and
dist/visual-presentation. Console appearance/input retest remains pending.


Start View correction (2026-10-03): General now displays the selected startup
library instead of the currently browsed library. Controller editing and touch
arrows select a persisted start_library ID; retain start_tab for legacy configs
without its obsolete five-tab limit. Startup applies VC categories before
resolving the ID, preserving the selection across rail reordering and falling
back to the first available tab when absent. New Setup configs default to All;
reinstall preserves the user's chosen ID and existing settings.
Validation: app and native Setup builds passed; settings round trips, legacy
indices, both-skin controller/touch/startup/reordering/wrap checks and existing
settings actions/hints/layout/paging regressions passed. All 20 payload checks
and actual Core-only/Core+Wii U fresh/reinstall tests passed.
App deployed to verified .251 after confirmed clean exit, with full staged and
final readbacks: 3853cef856d1bc62c34edde96edc5384cc965ae87b9c7699a4761f79c3ee635d.
All nine settings files unchanged. Setup rebuilt locally only, SHA256
dffa49e205273fcd0e161f52cac836a69c631f19c8cf82f54b862da647be1515.
Evidence: artifacts/start-view-deployment, dist/start-view-*.log and
 dist/visual-start-view. Hardware startup/UI retest remains pending.


Settings window/actions correction (2026-10-03): use a single frosted window
background and flat section/field highlights, with a reserved 574-614 footer.
The shared action model supplies rendering, touch dispatch, keyboard shortcuts
and controller focus. Down reaches the footer after the last field; Left/Right
select enabled buttons, Up returns to content, A activates. Save (+) and Delete
(X) keep fixed editor slots; disabled actions are skipped. Remove scrolling
Save/Delete rows, duplicate helper text and the underlying library footer.
Browser shows seven rows so its last entry cannot overlap the action bar.

Validation: production app and local native Setup builds passed; both-skin
settingsactionchecks and focused content-media, launcher, browser, media-list,
cover-zoom regressions passed, as did hints/settings controls, layouts and paging.
Corrected a stale host-test thickness assertion to match its 25 mm fixture.
All 20 payload checks and actual Core-only/Core+Wii U fresh/reinstall tests passed.
Broader videochecks passed its settings section but failed the sharp-artwork
512-pixel assertion after playback (line 2954 at this revision), with a synthetic
video fixture. Do not report the full video regression as passing. No video or
artwork implementation changes were made in this correction.

App deployed to verified .251 after checking clean exit; full staged and final
readbacks match 29db1763090de5d382c512aadca8f8f78ba3b3a9712aec0d7efb2a6c98218878.
All nine settings files unchanged. Native installer rebuilt locally, not uploaded,
per user instruction; SHA256
3fd81930ba6fd64be5e8c0c63cb6beea4ca486c63db533b0fc3200614e6018c9.
Evidence: artifacts/settings-actions-deployment, dist/settings-actions-*.log and
dist/visual-settings-actions-verified. Console appearance/input retest pending.


Inspection zoom and header correction (2026-10-03): scale front/back inspection
above the current library cover size so D-pad up/down cannot shrink a 200% cover.
Draw the header after the carousel and inspection in both skins; launch scene
capture uses the same foreground header order. Host zoom/inspection captures,
layout, paging, hints and launch animation checks passed. App/native Setup built;
20 payload checks and actual Core-only/Core+Wii U fresh/reinstall checks passed.
Deployed app only to verified .251 after confirmed clean exit; staged and final
full readbacks match a1d3984032ca43a1f360d98a9635a14bbfd69f6fb1095ce113e85b22163a8d53.
All nine settings files unchanged. Setup kept local, SHA256
fd8e50f85da6f8e8427a485efa271bf339f05b14f2ff778439dd30ab877648cd.
Evidence: artifacts/cover-zoom-inspect-deployment and dist/cover-zoom-inspect-*.
Hardware appearance retest pending.


Full-cover zoom correction (2026-10-03): remove the fixed shelf clip that cut
off enlarged covers and reflections. Extend the slider to 40-200%, keeping
120% at its exact midpoint/default. Preserve all saved zoom values. At the
highest zoom gently lower the shared cover center to retain top-screen margin;
overlay transitions use the same origin. App build, settings and both-skin
40/120/200% visual checks passed, including full-cover top/bottom inspection;
all 20 payload checks and actual Core-only/Core+Wii U install/reinstall passed.
Deployed after clean exit to 192.168.1.251; full remote readback SHA256:
bed9dbd374b7593627d10320f0a1f459b9708f2432c817346f30d368b2ecf767.
All nine settings files remain unchanged. Native installer updated locally
only (SHA256 2d3f1e96b25ad998bcbed5760ce4351e13af7cddb2eadf2a9225c27799c251e7).
Hardware appearance retest remains pending.


Cover zoom range correction (2026-10-03): extend to 50-190% in 5% steps,
with 120% at the exact slider midpoint and reference mark. New/missing/invalid
settings and native Setup default to 120%; existing saved percentages are
preserved. Share range constants between persistence, rendering, input and
slider position. Close-up zoom crops the shelf to keep tabs and the title
readable; normal zoom retains the existing reflection area. Extend endpoint,
midpoint touch, clip restoration and round-trip checks.
Validation: settings persistence, both-skin zoom/drag/layout/preparation and
existing preview regressions passed; all 20 payload checks and actual Core-only/
Core+Wii U install/reinstall passed. App and native Setup rebuilt. Deployed app
only to verified FTP .251 after clean shutdown; staged and final full SHA-256
matched eed4cf54dec4887016389dd7ef3c8236069b264208d5684ba7b3a0638d6e4679.
All nine settings files unchanged. Installer kept local; hardware retest pending.


Cover zoom slider (2026-10-03): Display now has COVER ZOOM immediately below
COVER SPACING. Controller changes use 5% steps; touch supports direct position
and dragging, bounded to 50-120% with a 100% reference mark. Persist cover_zoom;
missing or invalid legacy values default to 100%, and loaded values are bounded.
Scale all carousel layouts and flat/3D covers; detail, Showcase and inspection
transitions begin at the zoomed cover geometry. Native Setup explicitly defaults
to 100% and preserves existing user configuration on reinstall. Installer stays
local per user instruction. Production app build, settings persistence tests,
both-skin zoom/drag/layout and preparation checks, existing display regressions,
all 20 payload checks and actual Core-only/Core+Wii U install/reinstall passed.
Reinstall preserves cover_zoom=75 and user settings, saves and media. Local Setup
build passed. App deployed to verified FTP .251 after clean shutdown; full
staged and final SHA-256 readbacks match 828806bf5192e739f262eb4c1929bd119c7eb96785afd69bda23c3c9d3588901.
All nine settings files are unchanged; native Setup remains local. Hardware
zoom/drag and logo appearance retest is pending.


Cover mip speed build deployed and verified to .110:
app 453f4eaf76af5864f29c980f5415b29bcb05d8f171838d50acfd33b7c97f1459;
local Setup 1d734248c845624f0b195bf66e57a4890a65b7540fa7299d88d56cb688270b49.
Full staged SHA256 and published size/tail pass; nine settings unchanged.
Host artwork/preload/memory/fade tests, actual Core-only/Core+Wii U fresh/reinstall,
and 20 payload tests passed. Hardware speed/appearance retest remains pending.


Cover-loading follow-up (2026-10-02): warm preload reads only the requested
thumbnail mip from the existing sharp cache. It no longer reads/CRC-checks the
entire 1024px payload before that thumbnail. Full mip CRC checks still occur
when that mip is requested; missing caches still prepare sharp artwork. Host
fixture payload drops from 590464 to 9680 bytes without another source decode.
Distant native cases request 512px mips; focus and six nearby cases keep 1024px,
and detail inspection retains 2048px. A cold distant request builds the 1024px
cache before extracting its 512px mip, preventing cache quality downgrade.
Ready sharper textures take precedence; ready 512px textures bridge upgrades
before the tiny resident fallback. Warm the entry windows of adjacent libraries
when the current queue has fewer than eight requests, within the existing
96 MiB LRU and 48 MiB resident limits. No cache deletion or settings migration.
Validation/deployment evidence: artifacts/cover-mip-speed/. Hardware timing and
fast-scroll appearance remain pending until the new app is deployed and tested.


Hardware progress for tiled build: 1097/1097 preparation completed; coverflow
and video responsive, then artwork queue drained; no GX2 hangs in this run.
Normal screenshot shortcut and visible fast-scroll quality still pending.
Diagnostic SDL ReadPixels image is scrambled due to upstream byte-pitch bug,
so it is not evidence of on-screen corruption or a successful visual check.


Tiled BC3 follow-up deployed and verified to .110 (2026-10-02 21:38):
app 55b9c4cf6604f31c4a50d49e7ccffc79b4c17976f6bf8bf2e8775fedea8d9fc7;
local Setup 9ba3c35c85f87aac6bcdcce2eba410f49fe4096557324b6e249dac9bd805e3c2.
Full staged app SHA256, published size/tail and all nine unchanged settings pass.
Core-only/Core+Wii U actual bundle fresh/reinstall plus 20 payload tests pass.
MemoryMapping mutex2 remains installed and was confirmed loaded after reboot.
Prepared disk covers retained. Awaiting post-deploy preparation, browsing and
screenshot hardware results; do not infer success from host tests.


Hardware follow-up (2026-10-02): preparation advanced, but the user reported
stuck at 99%; live capture recorded repeated GX2 hangs at uptime 14:20 onward.
This is a graphics failure, distinct from the earlier mapped-heap wait cycle.
The first compressed renderer used LINEAR_ALIGNED BC3 textures. R600 compressed
textures require tiling (Mesa r600_choose_tiling excludes compressed formats
from linear sampling). Corrected the backend to TILED_1D_THIN1 with CPU
microtile uploads, preserving disk caches and SDL ownership/fences.
The 128-bit element ordering matches decaf-emu/addrlib's R600 address calculation
for odd dimensions and padded pitch; unique writes and boundary sentinels pass
ASan/UBSan. References and validation logs: artifacts/sharp-native/.
DXT5 is now last in the supported format list, preserving RGBA as SDL's default.
The app link depends on the actual SDL archive so renderer rebuilds relink it.
Updated dependency pins and installer require actual bundle tests before deploy.
Hardware stability and visual quality remain unverified.


Deployment verified to .110 for native sharp covers + heap mutex2:
app 532be3b34e6f99ea41d34482b2b3741cce68cb8f4aa63c6394dbd6aadb156beb;
MemoryMapping 10f4183b237cbc5211794df9f909866db6ce7fb0832f00d6727350cb4917f73d.
Full staged checksums, final module readback and app size/tail passed; nine settings
unchanged. Setup stays local: ea56d83dd42974cc9507195164b888395be3b70a7998cf7855150fad6ac2bca7.
Actual Core-only/Core+Wii U fresh install/reinstall and all 20 payload tests passed.
Awaiting full-reboot hardware confirmation and browsing/screenshot test; not yet
claiming the freeze resolved. Live capture: artifacts/sharp-native/hardware-retest.log.

Sharp native covers and mapped-heap mutex candidate (2026-10-02):

The Core app now links the pinned SDL Wii U backend with a static BC3 texture
format through SDL_CreateTexture/SDL_UpdateTexture. Allocation, sampling and
retirement use SDL's normal texture ownership and GPU fences. Compressed covers
have no color-buffer allocation, GPU surface-copy operation, context switch or
native mip upload. CPU row copies validate dimensions/pitches/image bounds.
The legacy app-side private-ABI importer remains unlinked. Reproduce with
 tools/build-cover-cache.ps1 (uflow-builder:covers); ordinary video-only app
linking is intentionally insufficient for the native texture-size helper.
New patch/header/Dockerfile inputs are pinned in tools/sync-native-payload.py.

Worker results retain BC3 blocks instead of expanding them to RGBA. Browse
textures use the 1024px cache; details retain their 2048px path. Account the
actual GX2 allocation against the existing 96 MiB LRU. The native path looks
40 titles ahead. PRELOAD COVERS prepares sharp disk caches in addition to
resident fallbacks. Existing full BC3 caches are reusable; new .native1024 and
.native2048 files are derived from originals and may be rebuilt safely. First
preparation has a conversion cost; subsequent cached visits skip decoding and
compression. Explicit skip/OFF can still expose a cold fallback. No settings or
media are removed. The stats reply reports compressed_covers for hardware checks.

MemoryMapping v0.2.7-uflow-heap-mutex2 replaces the mapped root heaps' spinlock
with the recursive gate at heap CREATION. It never clears a live lock or changes
live heap flags. Fourteen create/destroy/allocation/free/query/visit/check entry
points plus mapped heap lookup share the same gate; normal application heaps
bypass it. Child-heap callers retain their requested flags. This supersedes the
failed gate1 candidate which kept the observed stalled spinlock underneath the
mutex. Source is pinned at the same upstream revision and reproduced with
 tools/prepare-mapped-heap-gate.py --apply. Full reboot required. The recorded
wait cycle is understood; why the prior spinlock retained its owner is not
proven. Hardware browsing/screenshot stability must still be verified.

Host validation: compressed cache reuse, thumbnail preparation from sharp mips,
independent detail cache and damaged-cache recovery; BC3 bounds/sentinels under
ASan/UBSan; 1120000 extracted hook calls across 14 APIs and the recursive gate
stress test. Existing artwork/memory/preload/layout checks passed. Release
artifacts and final installer/deployment results: artifacts/sharp-native/.

Screenshot post-restart verification: saved uflow.log; MemoryMapping and
ContentRedirection hashes match the pinned payload. Expanded the installed
screenshot.wps for read-only symbol analysis (SHA b15afedc0cd05cd62d5644492c167756989adecc1b67f39fa58efbc81e09bef0).
Owner stack resolves to setvbuf -> __swhatbuf_r -> __wut_fsa_fstat. It waits for
ContentRedirection, whose worker spins in mapped-heap free. Why ownership remains
recorded is still unresolved; no screenshot or module replacement deployed.
See artifacts/screenshot-freeze/analysis.json.

Screenshot hardware retest failed (2026-10-02): artifacts/screenshot-freeze/syslog.log
confirms MemoryMappingModule v0.2.7-uflow-heap-gate1 loaded. Screenshot capture
begins at uptime 00:21:51; at 00:23:29 the mapped-heap lock owner is recorded as
ScreenshotPlugin IO Thread (0x804ebf98), waiting, spinLockCount zero. Core2 then
reports a heartbeat timeout spinning in MEMFreeToExpHeap, reached through
MemoryMapping_free and ContentRedirection processShimBufferForFSA. FTP's server
had stopped before capture. This establishes that the gate did not prevent the
stall; it does not establish why the heap retained this owner or prove the
screenshot plugin alone is defective. No emergency unlocking or further binary
change made. Saved app log and exact installed plugin hashes await restart.

Fast-scroll deployment verified to FTP .110: app SHA-256
1e9d00446fd2b63052a18373f111f8ecaea3cdc63843b244712bd70228f6e086,
full staged readback plus published size/tail verified; all nine settings files
unchanged. No module or media changes. Setup SHA-256
9971ff67b0d6fd815bc3c69b4556580a59bb2e0e149ef23183ecec8c520e7df2
remains local. Hardware retest pending.

Fast-scroll cover follow-up (2026-10-02): decoded browse covers are uploaded
before lossless display-cache compression/writing. A bounded 64-path queue
persists visited browse art only after visible/preload work is drained and the
selection has been idle for 1.2 seconds. Video playback suppresses this idle
preparation. An already-running write is not cancellable. Cover requests precede
background requests; the sharp window looks up to 12 titles ahead and four
behind while retaining the current 96 MiB LRU and 48 MiB fallback limits.
The existing public SDL upload path and mapped-heap guard remain unchanged.
Tiny fallbacks can still appear when scrolling beyond prepared artwork; do not
claim parity with WiiFlow or hardware timing from host checks. The supplied
videoplayback.mp4 is the visual reference for rapid scrolling with art present.

Validation: host asynchronous artwork/working-set checks, idle display-write
scheduler and cache-reuse regression, decoder/layout/preload checks passed.
App and Setup builds passed. Actual Core-only/Core+Wii U fresh install and
reinstall checks passed, preserving settings/media/saves. No component/default
changes. Artifacts: artifacts/scroll-lookahead/. Setup stays local. Hardware
fast-scroll performance and long-run stability remain to be tested separately.

Deployment verified to FTP .110: full staged SHA-256 of app and memory module,
final app tail and full memory-module readback, all nine settings unchanged.
App: 7d443808e8ea6c951743f035b231546c7a31af00ebf5b80e754e612dc868cf5c
Memory module: 2d10cf4e3ae81498e0b67063e0b4aae2388d2390ca17ea40a796f9c3ecb45588
Setup remains local. Full console restart and hardware retest still required.

Mapped-heap serialization and sharp browsing cache (2026-10-02): the recorded
heap stall remains unproven as a root cause; this is a targeted concurrency
mitigation requiring a full reboot and hardware retest. MemoryMappingModule
v0.2.7-uflow-heap-gate1 serializes 12 expanded-heap entry points with one recursive
OSMutex for mapped heaps, including direct callers outside MemoryMapping's
exports. Existing coreinit spinlocks and normal application heaps are unchanged.
The custom startup/end heap check takes the same gate. Never force-release a
stalled lock or disable its original protection. Reproduce from pinned source
b849fecf421f9f7ce3056d8d08581ef0bb0a4d91 using tools/prepare-mapped-heap-gate.py
--apply, the source Dockerfile, and make. The resulting WMS hash is pinned in
tools/sync-native-payload.py and ships in Core as a replacement. No settings or
plugin-enable choices are modified. This runtime module loads on a full reboot.

Covers: ordinary browsing uses a single 1024px-edge working texture for both
selected and side cases, prefetching eight titles in each direction. The 2048px
original remains available for details/inspection. Startup preparation also
queues the visible sharp row. A lossless zlib RGBA display cache eliminates
repeat source decoding/BC3 expansion, and the non-cancellable idle full-mip
compressor no longer blocks newly requested covers. Existing BC3 cache files
remain usable. Tiny resident fallbacks remain for skipped/cold loading; do not
claim that any uncached title at arbitrary scroll speed can never show one.
No private GPU importer is linked. Artwork remains within the 96 MiB LRU budget
plus the bounded 48 MiB resident fallback pool.

Validation: concurrent gate test (100000 operations), actual extracted allocator
hook tests (960000 mapped/unmapped calls across 12 APIs), lossless cache and
corruption checks passed under ASan/UBSan. Host cover checks passed 33 focus
changes, no repeat source decoding on cached revisits, full-detail preservation,
preload semantics, and memory bounds. All 20 payload tests passed. Native app,
module and Setup builds passed. Actual Core-only/Core+Wii U installation and
reinstallation tests passed with settings/media/saves preserved and the pinned
memory module included. Artifacts: artifacts/heap-gate/. Deployment and hardware
status are recorded separately; host tests do not prove the console freeze fixed.


Hardware retest failed again (2026-10-02): app 83f82eb8f04fb44587fbf7d2386e93276ce508ae3c63977d0d5b15ae2e131cf3
still froze while browsing. artifacts/content-io-heap-stall/captured-freeze.log
records the mapped-heap lock at 0x80000020 owned by ContentRedirection IO Thread 0
(0x804d2f38), with video reading and artwork file-open allocation spinning on
cores 1/2. The owner snapshot is waiting in fsIOthreadCallback's OSReceiveMessage
(state 4, recorded spinLockCount 0). This does not establish why ownership was
retained, nor distinguish lock corruption from another synchronization failure.
FTPiiU's server stopped before this launch; the startup snapshot has no FTP
worker. Disabling FTP is therefore not a sufficient fix. No GPU-reset signature
appears in this run. Do not describe the cover-budget or preload fixes as fixing
this memory-lock fault. After restart, retrieved uflow.log and verified installed
ContentRedirection and PluginBackend hashes match the pinned payload. Retrieved
MemoryMapping hash: fd44e5849ddb79e7446c5454741d49050a47856cba5fe71771d7a26df9e24e6f.
No replacement binary or settings change was deployed for this investigation.

Honor enabled cover preloading (2026-10-02): remove the 1.5-second automatic
preparation bypass. PRELOAD COVERS ON waits for the resident cover-thumbnail
queue to finish; B still explicitly skips the screen while loading continues.
OFF still avoids that preload queue. Full-resolution disk-cache preparation
remains idle work; do not claim all original-resolution art fits in RAM.
Library switches retain resident thumbnails. If preparation was explicitly
skipped, visible thumbnail requests now precede full-resolution jobs. Selection
readiness recognizes an existing browse texture while its original is pending,
so the face renderer does not blank that cover. A visible cache miss decodes
source artwork directly; full mip compression is reserved for idle preparation.
JPEG/PNG reduction retains at least the requested edge before the final resize.
The public SDL upload safety change remains. Core ships the updated app with
no new settings or component defaults. Preserve settings, media, saves and
existing caches. The app and native installer builds passed. Host tests cover
the enabled wait beyond the removed deadline, explicit skip, OFF, retained
resident texture across library switches, selection without a resident thumbnail,
cache misses without full-cache writes, decoder quality/bounds, 33 focus changes,
2048px RGBA memory pressure, settings/preparation UI and layout/control checks.
Actual Core-only and Core+Wii U install/reinstall preserved settings/media/saves.
App and packaged payload hashes match. Deployed to FTP .110 after the user closed the running app and authenticated
preflight checks confirmed no app response. Full staged SHA-256 and published
size/tail verified; all nine settings hashes preserved. App SHA-256:
83f82eb8f04fb44587fbf7d2386e93276ce508ae3c63977d0d5b15ae2e131cf3.
Native Setup remains local. Hardware retest pending; artifacts/preload-setting/.

GPU hang after cover loading (2026-10-02): console retest of the preceding
build with FTPiiU disabled produced repeated GX2WaitTimeStamp timeouts and
GPU resets as covers appeared. Trace: artifacts/cover-cache-speed/post-deploy-freeze.log.
The private BC3/GX2 upload is now unlinked and unused. Worker-side cache reads
expand the selected mip to an ordinary RGBA SDL surface; uploads use the same
public SDL path as other artwork. Disk caches, the startup wait budget, and idle
preparation remain. Limit protected original-resolution neighbors to one each
side (three originals including selection), retaining six nearby browse mips
within the 96 MiB texture budget. Host checks passed: asynchronous cache loading,
cache reuse/corruption recovery, 33 focus changes, worst-case 2048px RGBA scans,
both-skin layouts/controls, all 19 payload checks, and real Core-only/Core+Wii U
install/reinstall with settings/media/saves preserved. The rebuilt ELF was
checked for absence of UFlow_CreateCoverTexture. Hardware retest is pending;
release records are in artifacts/cover-gpu-hang/. App and native Setup builds
passed. App deployed to FTP .110 with full staged SHA-256 verification, final
size/tail readback, and all nine settings hashes preserved. App SHA-256:
df4e20f5ee7cdab75213561ce03df081f71003f33338db1068c907792399ff53.
Native Setup remains local; console retest pending.
The bridge source remains an unused experimental implementation; no installer
component enables it. Core installs the rebuilt app. Preserve settings, source
media, existing caches and saves. Build with the ordinary video builder; the
app no longer links libuflow_covertex or references its native entry point.

Responsive cover preparation and GPU context follow-up (2026-10-02): thumbnail
misses decode at thumbnail size and use separate source-keyed cache entries.
Existing full-cover mip caches are reused unchanged. Cover preparation blocks
for at most 1.5 seconds; the separately enabled startup intro still plays.
Unfinished thumbnails continue behind visible requests. Full-library conversion
runs when visible artwork/thumbnails are settled, selection has been idle for
750 ms, and no menu or video preview is active. Encoding avoids a redundant
full-image conversion and reserves the mip buffer without reducing quality.
Native cover upload now flushes SDL commands and restores SDL's GX2 context
after surface copies. Hardware verification of the distorted Settings and
browsing freeze is pending; this is not a claim to have fixed every freeze.
The subsequent captured allocator stall names an FTPiiU owner; its timing
relative to the disabled-FTP report is not confirmed.

Core includes the updated app and statically linked bridge; sync-native-payload.py
pins the changed bridge. No new component, setting or default is required.
App and native Setup builds passed, as did sanitizer cache tests, thumbnail
reuse/invalidation, asynchronous loading, 33 focus changes within the 96 MiB
texture budget, layouts/controls, 19 payload tests, and actual Core-only and
Core+Wii U install/reinstall with settings/media/saves preserved. App/payload
hashes match. Deployed app to FTP .110 after restart/SD identity checks and an
authenticated no-response app probe. Full staged SHA-256, published size/tail,
and all nine settings hashes verified; existing cache/media were not changed.
App SHA-256: a4cd9e184af1d88b701f54c2de004af532e759035df13fc7fc59cfa1ed930e1b.
Native Setup remains local. Console retest pending; records in
artifacts/cover-cache-speed/.

Compressed cover cache (2026-10-02): Core now prepares versioned BC3 cover
files with full mip chains under cache/cover-textures-v1. Cache hits read the
required levels without source-image decoding; source size/mtime invalidates
replaced artwork, and CRC checks regenerate damaged entries. First preparation
creates the cache automatically through the existing enabled-by-default cover
preload setting. User media and settings are unchanged. Disk write failures
still display the prepared cover; unsupported GPU import falls back to decoded
cached art. The 96 MiB texture budget remains enforced; three originals on each
side are protected, and requests prioritize the current scrolling direction.

The statically linked GX2 bridge uses the exact devkitPro SDL 2.32.10-2 private
headers. tools/cover-cache.Dockerfile pins those sources; sync-native-payload.py
pins the bridge, build recipe and stb_dxt encoder. Build the app with build.ps1
or tools/build-cover-cache.ps1, then tools/build-native-installer.ps1. No extra
module or component selection is required; Core installs this app. Validation: ASan/UBSan cache codec, all 19 payload tests, asynchronous loading,
33 focus changes within the 96 MiB budget, cache reuse/corruption recovery,
both prefetch directions for 1/2/7/33-title libraries with wrap on/off, retail
front/back visual checks, and layout/control regressions passed. App and native
Setup builds passed; actual Core-only and Core+Wii U install/reinstall preserved
settings, music, media and saves. Console browsing performance and native
compressed upload remain hardware-unverified. Deployed the app to FTP .110
while uFlow was closed, after SD identity checks. Complete staged SHA-256
matched before publication; final name/size/64 KiB tail verified and all nine
settings files unchanged. The duplicate full final download was stopped because
of slow FTP throughput. App/payload SHA-256:
5538390d561caedcaced5eb1dfe7e3ba4d806e22a77798ee331cb9b3f9430cdf.
Rebuilt native Setup remains local; it was not uploaded or run on this SD.


Preparation logo and marker follow-up (2026-10-02): pin the dot to the actual
progress endpoint with subpixel coordinates and soft edges, including when
background motion is off. Remove logo bobbing. Reduce the packaged logo once
with alpha-weighted area filtering to its display size, then cache a subtle
cyan alpha-only outline glow. The logo artwork itself is unchanged. Host
checks cover progress endpoints, a transparent-edge filtering fixture, and
identical logo/ring pixels across elapsed time and motion settings in both
skins. Rebuild the app and local native Setup and test actual install/reinstall.
Per user instruction, deploy only the app; keep the installer update local.
The previous partial installer upload was removed and nine settings hashes
were unchanged. Follow-up build, preview and hardware verification pending.

Preparation progress circle (2026-10-02): replace integer-snapped line segments
with a cached subpixel ring mesh and one-pixel alpha fringes on both edges.
The progress endpoint interpolates continuously; the radius, accent colors,
motion setting and loading behavior remain unchanged. The host preparation
preview covers empty, 36%, 73% and complete rings in both skins and four accents.
Ship the rebuilt app through the existing Core/replace payload entry; no new
dependency or installer setting is required. Validation: both-skin preparation
previews and settings checks, app/Setup builds, all 18 payload checks and
actual Core-only/Core+Wii U installation and unchanged reinstall passed.
Settings, saves and user media preservation checks passed; app/payload SHA-256
matches. Console deployment and hardware visual retest are pending.

N64 sleeve orientation (2026-10-02): full-sleeve scans now use sideways
front/back mappings on the landscape N64 case; separate front/back/spine
images stay upright. A new bundled generic N64 sleeve matches the retail
strip format. Its front follows retail authoring and its back reverses it;
reflection uses the same front mapping. Legacy wide landscape defaults remain
unturned, recognized only for the N64 generic fallback, never user face art.
Nine retail/default/separate-face previews were visually checked in both skins.
Cover-face, case-sleeve and content-type-media regressions passed; all 18 native
payload checks passed and the production app built. App and new N64 default deployed to FTP .251 after SD identity verification
and user confirmation that uFlow was closed. Staged/final readback hashes match;
all nine saved settings files are unchanged. Native Setup rebuilt successfully;
actual Core+N64 payload installation/reinstall (mask 129, 473 files) passed with
settings, saves and user media preserved. Hardware visual retest is pending.


SNES face orientation correction (2026-10-01): retail full-sleeve front and
back panels require opposite quarter turns. The bundled generic sleeve uses
the inverse authoring direction for each face. CoverFaces::SidewaysUV now
selects direction per face/source; reflection shares the front mapping.
Standalone front/back artwork is unchanged. Nine zoom previews cover retail
front/back, generic front/back, front-only, and both skins; visual review and
case/media regressions passed. App and native Setup rebuilt, all 18 payload
checks passed, and actual Core+SNES installation/reinstall (mask 33) passed.
The corrected app was deployed to FTP .251 after SD identity and clean exit
checks; staged and published SHA-256 matched and all nine saved configuration
files were unchanged. Updated native Setup is local; the console Setup still
contains the preceding build. Hardware orientation retest remains pending.

SNES carton sleeve layout (2026-10-01): the SNES case profile now matches the
3366x2100 `Full Sleeve` scans and landscape `Front Cover` art. uFlow keeps the
landscape box upright, rotates only full-sleeve front/back artwork, leaves
standalone face images upright, and migrates only the former 178x125x30 preset
to 178x125x25. The bundled SNES default sleeve uses the same sideways layout.
Host rendering with the supplied Contra III sleeve/front shows upright artwork
and cases. The native app and Setup build passed, all 18 payload tests passed,
and the actual 993-file payload passed clean install and unchanged reinstall
while preserving user settings, media, music and saves. Deployed app and SNES
default art to FTP .251 with matching full readback hashes. Setup resumed in
32 MiB segments after FTP interruption; every byte passed range SHA-256
readback before publication under its final name. Staging file is gone, and
library/source configurations retain their preflight hashes. Hardware visual
review remains pending.

Content-type media (2026-10-01): append an optional default-off field to
libraries.cfg, exposed in Manage Content Types after case dimensions. Sorted VC
titles may use target-type media and physical box dimensions while source
library/platform/title/save/launcher identity remains unchanged. Target lookup
uses only that type's configured media directories. Missing covers use the type
sleeve/placeholder; source backdrop/logo/video remain fallback options. Original
media is retained in immutable snapshots and serialized to the normal cache;
turning sorting or the appearance option off restores it. A source-directory
rescan is unnecessary: the existing cancellable scan worker can now do an
artwork-only refresh, publishing on success and preserving selection. Refresh
runs after leaving settings and on startup when required; renderer never indexes
media directories. Native Setup emits default-off fields and keeps user config.
Validation: content_type_media, media/library/VC/launcher regressions, ASan/UBSan
scan worker, both-skin editor previews, app/Setup builds, native payload tests and
actual Core/optional install/reinstall all passed. Final worker regression also
verifies that artwork refresh never recreates an invalidated game catalog.
App and native Setup rebuilt; deployed app to FTP .251 after SD identity and
clean shutdown checks. Staged/final read-back hashes match; eight settings/text
files are unchanged. Console appearance/playback retest remains pending.

VC category settings (2026-10-01): add auto_sort_installed_vc (default off)
immediately below Show Installed Wii U, and a separate eighth content-source
field (default off for legacy rows), exposed only on raw Wii U directories.
VC category is transient and separate from source library/platform, launch
routing, favorite identity and media. Settings apply immediately and reverse
without a scan; a category-only source edit preserves cache validity. Native
Setup explicitly defaults both switches off and preserves existing settings.
Cache v8 stores detected original console; v7 loads/migrates metadata without
walking source directories or dropping offline entries. Unknown packages stay
in their source category. Product codes are checked against bundled VC bases;
PC Engine/GBA are disambiguated by family/content, and Wii firmware plus disc
product IDs distinguish GameCube injections. Reference injector implementation:
https://github.com/piratesephiroth/TeconmoonWiiVCInjector (Form1.cs).
Installed discovery accepts MCP Wii-game titles, including Wii VC injections
in the demo title group, only at their exact internal/USB installed path.
Tests: vc_sort, installed_discovery, library, settings, launcher_overrides;
vcsortchecks/controller/touch in both skins; native payload/defaults and actual
Core/optional installation/reinstall all passed. App and native Setup rebuilt.
Deployed app to FTP .251 after verifying SD identity and clean shutdown; staged
and final read-back hashes matched. All eight settings/text files unchanged.
This build also deploys the preceding launcher-tab correction. Hardware
verification remains pending.

Launcher-tab correction (2026-10-01): launcher selection (including Use Default),
supported ROM injection/reinjection, and Launcher Settings replace the leftover
Synopsis action and its focus-dependent rendering. Both-skin controller/touch
checks, screenshots, 18 payload tests and actual Core/optional install/reinstall
passed. Native app rebuilt and synchronized into the Setup payload. Console
deployment and hardware verification pending; uFlow was running at preflight.

Details layout and embedded media player (2026-10-01): open the synopsis first,
use a shared code-drawn icon rail, and keep PixelDeck content and controls dark.
Right enters only actual launcher/media controls; read-only pages retain tab
focus and use left/right for paging. Touching a tab selects it without launching
or opening settings. Media has video plus seven artwork types, previous/next
and play/stop controls. A separate UI player uses the existing bounded playback
backend, starts only after the library/background decoder has released, and
stops on tab exit, details close, settings, launch and system-background exit.
The library decoder also waits for media-player cleanup before restarting.
Validation: both-skin navigation, screenshots and real synthetic-video playback
passed, including restart, no overlapping library decoder and HOME cleanup.
App and native Setup builds, 18 payload checks and actual Core/optional component
install/reinstall passed. Deployed through FTP .251 after verifying SD identity
and clean shutdown. Staged and installed app read-back hashes match the tested
build; all eight saved configuration/text files are unchanged. Hardware UI and
embedded playback retest remains pending.

Installed-title launch and restored menu music (2026-10-01): installer details
show Launch Installed Game alongside installation controls only when the normal
catalog has a verified installed base title on the selected storage. Controller
and touch use a one-shot request carrying that installed entry through the
normal launch/animation/history/return path; package folders cannot be launched.
The action remains locked during installation and also works with a base game
installed before a failed update/DLC. Both skins use their native button styles.
Four user-provided SD tracks are preserved under native-installer/assets/music:
GameCube Menu, Wii Menu, Wii Shop Channel, Wii U Menu. Payload synchronization
pins every track, includes them as Core/keep, enforces the player size limit,
and preserves existing tracks and audio preferences. Full audio decoding,
controller/touch checks and real fresh/reinstall tests cover this release.
The E: 128 GB FAT32 source card reported unrelated directory I/O errors; music
reads matched twice, but no writes or repair were performed on that card.
Hardware installation follow-up: user confirms NSMBU installed after the module
restart and continues to the game after dismissing the account popup (104-1930).
Validation: app and native Setup builds passed, both-skin launch-button touch/
controller tests and visual review passed, all four audio files fully decoded,
18 payload checks and real Core/optional install/reinstall passed. Music source
and bundled hashes match. Deployed through FTP .251 after the user re-enabled
access: SD library identity, previous app hash and clean shutdown verified.
Staged and final app read-back hashes match the tested build; all four missing
music files installed and verified. Eight saved configuration/text hashes are
unchanged. No writes were made to the earlier E: source card. Hardware button
and music playback retest remains pending.

Optional installer signature dependency (2026-10-01): bundle upstream
SigpatchesModuleWiiU 1.2 (marco-calautti, release tag 1.2), SHA-256
41f9409f0c615cf3276e647bcad44a4723389388342f0e2b8994527651a38776,
as setup/01_sigpatches.rpx. It runs after 00_mocha on a full restart. Pin the
source artifact in sync-native-payload.py and select it only with bit 17,
in keep mode so an existing user module is preserved. The app's installer
setting controls installer access, not this boot-time environment module.
Test selected/unselected fresh installs, existing module preservation and
reinstall. No package tickets are edited and no game installation is initiated.
The missing module matches the observed ticket rejection; successful NSMBU
installation remains a hardware retest after restarting.
Validation: native Setup rebuilt, 17 payload tests and actual Core-only plus
Core/title-installer install/reinstall tests passed, including preservation of
an existing signature module. FTP .110 SD identity and preceding Mocha hash
verified; staged module renamed into its absent final path and read-back hash
matched. Eight configuration/text hashes are unchanged. A full console reboot
and user-initiated NSMBU installation remain pending; no claim of install success.

Installer catalog and details update (2026-10-01): All excludes installation
packages; Install Content retains them. At startup and after every completed
installation job, fresh MCP title/version results merge installed base games
into the ordinary Wii U catalog and persist its cache without a library scan
or code/app.xml access. Updates/DLC remain package rows, partial jobs retain a
verified base game, repeated refreshes do not duplicate entries, and existing
unrelated games/media are preserved. Failed MCP queries do not create entries.
A recorded -265150 failure now explicitly says the ticket signature was rejected
and installation did not complete; do not infer later success from this trace.
The remappable Details action toggles closed in either skin, including installer
details (active installations still block closing). Favorite cover badges use a
filled gold star instead of an asterisk. Run title-installer and installed-catalog
regressions, both-skin details/installer host checks and favorite previews, rebuild
app and Setup, test payload sync and actual optional component install/reinstall.
Console verification of installation and controls remains separate from host tests.
Validation: package/worker sanitizer and catalog integration tests, installed
catalog/settings regressions, both-skin control and favorite preview checks passed.
Native app/Setup built, 16 payload tests and actual combined Core/title-installer
install/reinstall (mask 131073) passed. FTP .110 identity and clean app shutdown
verified. FTP reset during the final write; re-upload on a fresh connection
completed and the final SHA-256 matches the built/payload app. Own staging file
removed, all eight settings/text hashes unchanged. Hardware retest pending.

Repeat browsing freeze after storage/flip build (2026-09-30): syslog captured
00:25:30 on app a0b64c24. Artwork on core 0 and video-open/reentrancy allocation
on core 2 spin on the same mapped-heap lock. Both observers record FTPiiU
0x809f3b60, waiting in network poll/select with spinLockCount 0. The earlier
module-availability caching did not fix the underlying fault. No evidence yet
identifies a writer of stale/corrupt lock state, nor establishes RAM exhaustion.
Do not label the installer/animation changes or idle-poll patch a freeze fix.
Crash and saved log retrieved after restart. Installed ContentRedirection and
FTPiiU hashes match the expected patched releases; MemoryMapping SHA-256 is
fd44e5849ddb79e7446c5454741d49050a47856cba5fe71771d7a26df9e24e6f.
Next controlled test disables FTPiiU while leaving video/cover behavior unchanged;
verify its thread is absent and use independent syslog capture. No new code or
console settings were changed during this audit. Cause remains unproven.
Controlled test: user disabled FTPiiU; latest 41-thread snapshot confirms its
thread is absent. uFlow responds at 40/89/145/194 seconds, with no new captured
lock stall. This short test is not proof of a fix or sustained scrolling.
A separate user-triggered installation reports ES ticket-signature rejection
(-2011), MCP failure -265150 / 162-0000; uFlow remains responsive. The native
free-space preflight no longer blocks this attempt, but installation is not
verified successful. No ticket modification or verification bypass attempted.

Installer storage query and all-skin details flip (2026-09-30): query native
FSA free space on the selected `/vol/storage_mlc01/usr` or
`/vol/storage_usb01/usr` volume using a short-lived unlocked client, rather
than SD devoptab statvfs. Close the client on success and errors; failed queries
leave zero available bytes and block installation, reporting the native error.
Worker tests cover both destinations, 64-bit sizes, initialization/client/unlock/
query failures, cleanup and no installation submission when the query fails.
Installer details and normal Pixel details now use the existing Glass cover
rotation pose and reversible duration, preserving each layout�s landing point.
Both skins honor the configured animation speed; Pixel installer buttons use
its native colors with readable text. Native app/Setup builds, both-skin visual
and navigation checks, 16 payload tests, and real combined install/reinstall
tests passed. Deployed through FTP .110 after SD identity, prior app hash and
clean shutdown verification. Staged and final app hashes match, and all eight
saved configuration/text hashes are unchanged. Native storage-query and
animation hardware checks remain pending; console log capture is listening.

Browsing stall after installer release (2026-09-30): the captured heartbeat
trace has UI and artwork threads waiting for the mapped heap in ContentRedirection
file-open allocations. The UI path is AvailableLibraryTabs -> installer module
availability, called during rendering. Cache module availability once per console
app launch; component changes already require closing the app. The production
backend regression verifies availability stays cached after the descriptor is
removed. Host discovery tests retain dynamic fixtures. The observer records
FTPiiU as heap owner, with a poll/select stack, and the installed FTPiiU hash
matches the patched release. This does not identify why the lock remained held;
do not claim the general memory-lock freeze fixed by removing repeated opens.
Validation: production module-cache/worker sanitizer and library tests passed;
native app and Setup rebuilt, 16 payload tests and real combined Core/installer
install/reinstall passed. After FTP returned, the identified console received
the app; staged and final read-back SHA-256 match the release and all eight
saved configuration/text files are unchanged. Setup contains the same app.
Hardware retest remains pending; syslog capture is listening.

Optional title installer (2026-09-30): component bit 17 is separately selectable
and initially off in Setup. Its versioned module descriptor enables the built-in
MCP installation backend. Fresh selected installs create the Install Content
library and SD `/install` source; runtime defaults add these in memory on an
upgrade without rewriting user library/source configuration. General settings
can disable/enable the feature. Setup preserves saved settings on reinstall.

WUP folders are grouped by title ID across configured install sources. Details
list Game, Update and DLC with checkboxes, versions, sizes and installed status
for the selected MLC/USB destination. One version per type is selected, highest
by default. Source files are retained. Package/TMD and free-space preflight,
callback-owned progress and post-install title/version verification determine
success; a download badge becomes a check only from installed-title evidence.
On success the normal installed library refreshes too. Existing Wii U media
sources also provide installation-entry artwork. The library cache is version 7.

The backend uses the MCP/IOS installation sequence reviewed in WUP Installer
GX2 (`xhp-creations/WUP-Installer-GX2`, commit
`5105b58fa9ecb09add37d78eb1f1dd3976d21532`, `src/menu/InstallWindow.cpp`).
No upstream executable is bundled. Destination USB means Wii U-formatted
storage, not the FAT32 raw-game drive. UI input cannot leave an active install;
HOME is disabled until its completion callback. System-title packages are not
accepted by this game-library feature.

Run `tools/test-title-installer.ps1`, settings/library regressions and host
`titleinstallchecks`/`detailschecks` in both skins; rebuild the native app, sync
payload, rebuild Setup, run payload tests and actual Core/component install and
reinstall tests. Host tests are not hardware installation verification.
Validation: package/worker/library checks (including sanitizer checks), both-skin
details and navigation checks, native app and Setup builds, 16 payload checks,
and actual Core-only / title-installer-only installs and unchanged reinstalls
passed. The worker excludes disc titles from installed-destination detection.
Deployed the verified app and module descriptor to the identified E: AROMA SD;
staged and final hashes match and all eight saved configuration/text files are
unchanged. The supplied New Super Mario Bros. U base package (0005000010101d00,
v0) passes metadata/file-presence/size checks for 13 contents, 1,816,199,168 bytes.
No update or DLC is present. This is not a content-hash or hardware install test;
a real WUP installation remains pending, initiated by the user in details.

Raw file-path stack reduction (2026-09-30): port16 moves the eight native
open/free-space/flush path buffers from retail caller stacks into per-call
owned storage. Captain Toad's captured null virtual call reads `/raw` from a
thread object's name vtable slot; its executable also creates 4 KiB workers.
This motivates reducing loader stack pressure, but does not establish the
writer of the corrupt object or prove that this change fixes the launch.
Concurrent and nested calls retain separate buffers through native submission;
inactive titles allocate nothing. Allocation failure returns a fatal FS status
without forwarding a save request to the wrapper title's save directory.
Compiled hook frames are 48–72 bytes. Production open and save-space/flush
tests cover nested lifetime, allocation failure, native error/completion
ownership and inactive passthrough; path-routing and process-scope tests pass.
Rebuild/sync the module and native installer, run payload and actual Core
install/reinstall tests, and preserve settings, games and saves. Hardware
Captain Toad launch validation remains pending; do not label the crash fixed
from host results alone.
Validation: native module and installer builds passed; installer payload matches
the module, 15 payload checks and actual 16-file Core install/reinstall passed.
Candidate installed via FTP .110 after SD identity and app shutdown verification;
staged and final module read-back hashes match, all three configuration hashes
are unchanged. After a full restart, the captured log confirms port16 active
for title 0005000010180600 and the user confirms Captain Toad now launches
successfully. This verifies the reported launch failure is resolved on this
console; prolonged play and other titles are not covered by that result.
The exact corrupting write was not captured. FTP cannot rename over an existing file, so the verified candidate was
uploaded to the final path and read back before removing its staging file.

Faster alphabet navigation (2026-09-30): reduce A-Z hold delay from 350 to
250 ms and repeat interval from 120 to 90 ms (one third more letters per second).
Single-step input remains immediate. Search keyboard timing stays at 350/120 ms.
Run existing alphabet/search/navigation checks, rebuild native app and installer,
and verify payload and Core install/reinstall preservation before deployment.
Validation: existing alphabet/search/navigation checks passed; native app and
installer built, 15 payload tests and actual 16-file Core install/reinstall
passed. FTP .110 SD and preceding-app hashes matched; saved shutdown marker
and absent app response confirmed the app was closed. Staged and final installed
read-back verified; all three configuration hashes unchanged. Updated installer
contains the same app. Hardware scrolling feel remains for user verification.

Alphabet selector (2026-09-30): mirrors the local Aurora selector: hold R3,
left stick/D-pad to choose, release to apply; no movement cancels. L3 toggles
saved Browse/Filter mode (Browse by default). Browse jumps within the current
visible order; Filter intersects the current library and text search. ALL only
clears the letter filter. Leading whitespace is skipped; ASCII A-Z group by
initial, numbers/symbols/non-Latin titles are in #. The letter choice is retained
for the session. B clears an active letter filter first, then text search.
Find / Sort also offers Y A-Z for controllers without stick clicks, with A to
apply, X to switch mode and B to cancel. Pro Controller stick clicks are mapped.
Controls includes a remappable A-Z SELECTOR action appended after existing
actions, default R3; missing legacy bindings inherit R3 without resetting other
controls. Stick clicks can be captured, hints follow the binding, and the
Controls list scrolls to keep the entry and Reset Defaults visible. The held
activation button is consumed so remapping to a face/D-pad button cannot also
cancel, toggle mode or advance a letter on entry. L3 mode uses X if bound to L3.
The dimmed alphabet row animates in/out and uses the selected theme. No scans
or media-cache budget changes. Validate navigation, saved defaults, controller
mapping, search/Favorites composition, both skins, native build/payload and
Core install/reinstall before deployment; distinguish host from hardware tests.
Validation: both-skin alphabet/remap/search/navigation tests, controller mapping
and settings round trips passed. Native app and installer built; 15 payload
tests and actual 16-file Core install/reinstall passed with settings/saves kept.
The E: AROMA card and preceding app matched expected hashes. Staged/flushed
replacement and final read-back verified; all three user configuration hashes
are unchanged. Hardware selector and remap input verification remains pending.

Quick search clear (2026-09-30): the library Find binding (B by default)
clears an active search immediately and retains the selected title. The footer
switches to CLEAR while searching and back to FIND afterward. Empty results
show the current binding for clearing. With no query, Find still opens the
Find / Sort menu; modal cancel behavior is unchanged. No settings migration.
Validate quick clear with matching/empty results, selection and footer refresh,
then rebuild/sync the native installer and verify Core reinstall preservation.
The search keyboard also accepts the left stick with an engage/release dead
zone, immediate direction changes, 350 ms initial delay and 120 ms hold repeat.
D-pad shares repeat behavior. Opening search resets repeat state. Host checks
cover drift, hysteresis, both axes, reversal, hold timing and reopening.
Validation: host search/navigation checks passed with both skins; native app
and installer built, 15 payload tests and actual 16-file Core install/reinstall
passed with settings/save preservation. The E: AROMA SD and previous app were
identified by hash. The combined app was staged, flushed and read-back verified
after replacement; all three user configuration hashes are unchanged. Hardware
joystick feel and quick-clear behavior remain pending console verification.

Library search (2026-09-30): the existing Sort binding (B by default) is now
labelled FIND and opens Find / Sort. X opens title search; D-pad selects keys,
A types, X erases, Y clears, + applies and B cancels. Touch supports keys and
editing actions. Matching is case-insensitive for ASCII letters and uses a
substring of the title within the active library, including Favorites. An empty
query restores the library. Sorting and library switches retain the query;
selection is retained where possible, with a dedicated no-match state. Search
is session-only, requires no rescan and does not change saved settings or media.
The keyboard provides Latin letters, digits, spaces, apostrophe, hyphen and dot.
Preview playback is excluded while the search dialog is open. Build/sync this
app into the native installer; verify host search/navigation/rendering checks,
payload defaults and actual Core install/reinstall preservation. Hardware input
and console behavior must be verified separately from host checks.
Validation: search, selection retention, Favorites, sorting, keyboard touch,
empty results and both-skin rendering passed on the host, along with control
hints, launch options, layout and paging regressions. The native app built;
all 15 payload tests and actual 16-file Core install/reinstall passed, including
component defaults and settings/save preservation. Native installer build and
app/payload hash agreement are verified. After reseating the reader resolved
Windows I/O errors, the E: AROMA card matched the known library and installed-app
hashes. The search app was staged, flushed, activated and read-back verified;
all three user configuration hashes are unchanged. No settings, games or saves
were replaced. Hardware search input verification remains pending.


Neighbor artwork selection (2026-09-29): a resident thumbnail could return from
GetCover before its ready browse-sized fallback was checked. Prefer the ready
browse image while original artwork is pending. Protect original artwork for
the immediate previous/next titles from distant-prefetch eviction, and use those
originals during approach animation before selected styling turns on. Keep the
96 MiB detail limit and resident/worker budgets unchanged. Test actual texture
selection with resident+browse+pending-original, neighboring approach rendering,
33 focus changes under pressure, bounds, idle decode stability and cleanup.
The console source files were retrieved: Blue-Collar Astronaut is 600x846,
Bomberman '93 is 384x384 and Bomberman '94 is 768x1080. Preloading cannot create
detail absent from the source. No media files or user settings are replaced.
Rebuild/sync app and native installer and check Core defaults/reinstall and
settings/save preservation. Hardware reproduction remains pending.
Validation: final host tests prove a ready browse image wins over the resident
thumbnail while the original is pending, and a protected neighboring original
is selected during approach. The 33-title pressure test passed at the unchanged
96 MiB bound with stable idle decoding and cleanup; translation and layout
regressions pass. Native app build, 15 payload tests and actual Core
install/reinstall passed with defaults and settings/save preservation.
Deployment: confirmed the app was closed and SD identity matched, staged and
read-back verified the replacement, then activated and verified the installed
app. The installed app matches the native installer payload; all three user
config hashes are unchanged. Hardware browsing verification remains pending.

Scrolling stability (2026-09-29): calculate case render bounds in model space
before integer screen translation, keeping cached image size/anchor independent
of screen-coordinate rounding. Preserve fractional carousel X/Y through final
SDL_RenderCopyF compositing for both new and cached case images. Keep cache
budgets, decoder behavior and user settings unchanged. Test integer translation
pixel equivalence, visible subpixel motion, fresh/cache equivalence, all cover
layouts/spacings and launch animation. Rebuild/sync app and native installer,
then test actual Core defaults/install/reinstall and settings/save preservation.
Host rendering checks do not establish console frame pacing or freeze recovery.
Validation: exact integer translation, fractional-motion pixel changes and
fresh/cache equivalence passed. Layouts/spacings, navigation, control hints,
launch options and both-skin launch animation checks passed. Native app built;
15 payload tests and actual 16-file Core install/reinstall passed with component
defaults and settings/save preservation. Console scrolling remains unverified.
Deployment: confirmed the closed app and .147 SD identity; staged, activated and
read-back verified the scrolling build. User settings, media-source and library
config hashes are unchanged; installed app matches the rebuilt native installer.
Console movement smoothness remains pending user verification.

Nearby cover cache (2026-09-29): extend display-size artwork lookahead to 16
titles on each side, preload original artwork for the immediate previous/next
titles, and raise the bounded detail cache from 48 to 96 MiB. Keep the resident
thumbnail budget and 768-pixel browse size unchanged. Selected artwork and the
nearest visible cards are queued before more distant work. Refresh the LRU age
of wanted cached prefetch entries so side-card render caching does not make
useful source artwork look unused. Worker queues remain bounded and replace
obsolete scroll requests. Test 33 focus changes, both full-resolution neighbors,
idle decode stability, eviction/bounds and cleanup; rebuild/sync the native app
and installer and verify actual Core reinstall/defaults/settings/save retention.
This changes fresh-install behavior without rewriting user settings. Hardware
latency, worst-case media collections and the independent freeze remain unverified.
Validation: 33 focus changes across 198 MiB of source artwork passed with both
original-resolution neighbors ready, stable idle decode count, pinned artwork,
96 MiB cache bounds and cleanup. Host layout/paging/control checks, native app
build, all 15 payload-sync tests and actual 16-file Core install/reinstall passed,
including component defaults and preserved settings/saves.
Deployment: verified the closed app and .147 SD identity, staged/activated and
read-back verified the app, and confirmed all three user configuration hashes
are unchanged. Installed app matches the rebuilt installer payload. Hardware
scrolling/latency validation remains pending; no rescan or config migration needed.

Cover/preview handoff (2026-09-28): prioritize the selected original cover before
backgrounds, including during previews and Details; keep visible-neighbor
prefetch active during previews. Increase bounded browsing artwork from 512 to
768 pixels while retaining the 48 MiB detail-cache limit, resident-thumbnail
limit and original-resolution selected cover. Retain only the stock Glass
background's last GPU frame during preview decoder handoff, so buffering does
not reveal the procedural fallback; release its decoder before game playback.
Skin changes, shutdown and system backgrounding still release the theme image.
Test working-set eviction/bounds and theme pause/handoff/release, rebuild/sync
the native app and installer, and test actual Core install/reinstall preservation.
Hardware cover latency and the separate shared-heap freeze remain unverified.
Validation: final host layout/paging checks, 17 full-resolution focus changes
under a 102 MiB source working set, cache bounds/idle stability/cleanup, and
stock-theme pause/preview-handoff/system-release checks pass. Native app and
installer built; 15 payload tests and actual 16-file Core install/reinstall
pass, preserving settings/saves and component defaults. Deployment completed
after confirming uFlow was closed and FTP was available: SD identity, staged
and installed app hashes verified; settings, media-source and library config
hashes unchanged. Installed app matches the rebuilt native installer payload.
Console cover latency and appearance remain pending user verification; return
FTP to disabled for the independent freeze test.

Logo captions (2026-09-28): keep the plain game title beneath title artwork in
the library and Glass Details. Reserve a separate line above the creator in
Details, with a smaller contained logo; retain the existing text-only fallback.
No settings migration or media scan is required. Rebuild/sync the app and native
installer and verify Core install/reinstall and settings/save preservation.
The ARC STYLE: BASEBALL!!SP cover mismatch is a separate imported-media issue:
the SD JPEG matches the import manifest SHA256 for the original incorrectly
labelled Dragon Quest X image. The title ID and cached media path are correct.
No replacement was found in the local D: media collection; do not introduce a
title-specific application workaround or overwrite user artwork on reinstall.
Validation: inspected logo/title captures for library and Glass Details; host
layout, paging, control hints, launch options and Details navigation checks pass.
Native app and installer build; all 15 payload-sync tests and the actual 16-file
Core install/reinstall pass with component defaults and settings/save preservation.
Console rendering of the new captions remains unverified.
Deployment: checked the closed app and .147 SD identity, staged and read-back
verified the new app, and confirmed all three user configuration hashes are
unchanged. The installed app matches the rebuilt native installer payload.

Preview audio starvation (2026-09-28): live traces show 56,216–82,546 missing
48 kHz audio frames per five seconds while file reads and video decoding run
serially. Add a cancellable compressed-packet reader so storage and decode can
overlap; retain a single owner for demux, join before seek/close, and snapshot
stream metadata before handing off the format. Queue at most 96 packets within
a 512 KiB normal budget; an individual packet up to the existing 8 MiB limit may
occupy an otherwise empty queue. A reader can hold one pending packet. Do not
queue decoded frames or whole files in this new stage. Preserve existing bounded
image/audio queues, safe non-reference skipping, timestamps and async UI stop.
Start with 200 ms of audio when available, with a 400 ms fallback for sparse,
short or silent clips. Log reader identities and peak queued bytes to OSReport.
No user conversion or configuration change is needed. Test slow read/decode
overlap against the serial path, starvation, queue bounds, loops, short/silent
clips, suspension, cancellation and cleanup under ASan/UBSan. Rebuild/sync app
and native installer and check actual Core reinstall/defaults/preservation.
The synthetic comparison is not console playback validation, and this does not
establish a fix for the separate mapped-heap lock fault.
Validation: the final ASan/UBSan run missed 64,512 audio frames in the serial
slow-read/decode comparison and zero with read-ahead. Queue-bound, normal/silent
playback, loops, one-frame clips, suspension, cancellation, retained-frame and
delayed-cleanup tests passed. The native app built, all 15 payload tests passed,
and actual 16-file Core install/reinstall (30,973,851 bytes) passed with component
defaults and settings/save preservation. Console audio remains unverified.
Deployment: native installer build completed; its embedded app matches the
staged and read-back verified .147 installation. SD identity and closed app
state were checked, and settings/media-source/library configs are unchanged.
Existing video files and resident modules were not modified. Hardware audio
verification is pending reopening uFlow and playing the same previews.

Launch presentation (2026-09-28): selected cases make a full turn toward the
viewer, zoom forward and fade out before game dispatch. Start from the current
library/Details/Showcase case pose; reuse scene/lens targets instead of allocating
another full-screen texture. Pace against the display while pumping SDL system
events and controller cancellation. Restore renderer state and the ordinary UI
when interrupted or when dispatch fails. Cover every game-launch transport;
preparation/conversion dialogs still run before dispatch. The existing animation
speed controls the duration. Replace the skin/library label beside uFlow with
selected/visible count (including 0/0), remove its duplicate total badge and the
platform/storage/position subtitle beneath the title. Both skins use this layout.
No config migration or new defaults are needed. Test both skins, flat/case art,
library/Details/Showcase start poses, fade endpoints, cancellation, renderer/target
reuse, counter/empty state and launch/UI regressions. Rebuild/sync the app and
native installer and verify actual Core reinstall/settings/save preservation.
Host animation captures do not establish console timing or freeze recovery.
Validation: both skins and all four origin modes passed captures, cancellation,
selection/target identity and renderer blend/color/alpha restoration. All eight
final captures are fully black. Inspected intermediate library/Details turns
and the 32/1349 header. Layout, paging, control hints and launch-option checks
passed. Native app/installer builds, all 15 payload tests and actual 16-file
Core install/reinstall (30,972,779 bytes) passed, including component defaults
and settings/save preservation. Console appearance/timing remains unverified.
Deployment: confirmed uFlow was closed and the .147 SD identity matched; staged,
activated and read-back verified the app. Settings, media-source and library
config hashes are unchanged. The installed app matches the rebuilt native
installer payload; the resident launcher and other console files were unchanged.

Raw save-space routing (2026-09-28): The Amazing Spider-Man 2
(0005000010146a00) displays 155-0100 before creating a save. Its SD common/account
directories exist but are empty. The launch log confirms SD save redirection,
while the wrapper still mounts its native save quota. ContentRedirection does
not redirect free-space queries. Port15 routes both FSGetFreeSpaceSize entry
points through the same per-title/account native SD path as save opens and
flushes. Return actual capacity, including zero, and native errors; preserve
async output ownership, callback and queue. Restrict hooks to active raw titles
in the game process. No save files or quota metadata are deleted or fabricated.
The missing routing is confirmed in source; whether it causes this particular
game's error needs hardware validation, and actual SD capacity is not yet known.
Run the production hook sanitizer tests, USB/raw regression suite, open-hook
regressions and scope checks. Rebuild RPXLoadingModule, sync the existing module
input in tools/sync-native-payload.py, rebuild the native installer, and test
actual Core install/reinstall, component defaults and save/settings preservation.
Retain the current app and other system modules. A full console reboot is needed
after deployment to load the new resident module. The uFlow heap-lock freeze is
a separate unresolved investigation.
Validation: production save-space hooks passed ASan/UBSan for SD/account paths,
zero and 64-bit capacity, failure propagation and async ownership. The USB/raw
suite, game-only hook scope and all four file-open regressions passed. Updated
stale host fixtures for the existing HAI cleanup and process-scoped open hooks.
Module/installer builds, 15 payload-sync tests and actual 16-file Core
install/reinstall (30,971,051 bytes) passed, including component defaults and
preserved settings/saves. Console save creation remains unverified.
Deployment: checked .147 SD and existing loader identities, staged and read-back
verified port15, then activated and verified the final module hash. User settings,
media sources and library configs are unchanged. Only RPXLoadingModule was
replaced on the console; a full reboot is still required to load it. The native
installer payload matches the installed module.

Post-update freeze (2026-09-28): hardware froze again with the artwork working-set
change installed. That change did NOT resolve the mapped-heap lock fault. The
latest heartbeat trace has FTP, ContentRedirection and the UI waiting on lock
0x80000020, whose recorded owner is 0x1436C020 (not identified by the startup
thread list). The UI stack is StopVideo -> ReportVideo -> Trace -> fopen. Remove
diagnostic SD writes throughout the interactive loop and video stage/error
logging, retaining startup and the final clean-exit file checkpoint. Live events
go to OSReport. Log video worker identity/lifecycle there and let the opt-in owner
probe also sample from the UI, since a spinning I/O waiter can prevent its core-0
observer from scheduling. No forced heap unlocking, module replacement or FTP
configuration change. This removes a confirmed secondary blocking path and adds
diagnostics; the underlying allocator fault is still unresolved. Rebuild/sync
both app and native installer and check packaging/preservation before deployment.
Validation: native app and installer builds passed, as did video lifecycle and
heap-observer ASan/UBSan checks, all 15 payload-sync tests, and actual 16-file
Core install/reinstall (30,970,383 bytes). Component defaults and preserved user
settings/media passed. The installer embeds the same app candidate. These checks
do not validate console freeze recovery; the underlying lock fault remains open.
Deployment: verified the .147 SD identity, Menu startup after the frozen run and
inactive app interface. Staged and installed the candidate, then verified its
read-back hash and unchanged settings/media-source/library configs. The live
system-log receiver is connected. Hardware behavior remains unverified.

Artwork working set (2026-09-28): neighboring cases use 512-pixel browsing
textures while the selected case, Details and cover inspection retain the
existing original-detail decode limits. Pin the selected primary cover alongside
its background within the existing 48 MiB detail-cache budget. Keep resident
thumbnail fallbacks and asynchronous focus upgrades. This addresses repeated
large-cover allocation/eviction; it is not proof of a fix for the separate
mapped-heap lock stall seen in hardware traces. No FTP configuration change is
part of this update. Validate a 102 MiB source working set, all 17 focus changes,
stable idle decode counts, bounded memory, cleanup, backgrounds, video, both
skins and case rendering. Sync/rebuild the app and native installer and test
actual Core install/reinstall, component defaults and preservation. No settings
schema or fresh-install defaults change. Hardware freeze status remains open.
Validation: the 102 MiB working-set test passed all focus changes with no idle
source redecodes and bounded textures; asynchronous artwork, both-skin case
rendering, layout/paging, Showcase/video, backgrounds and Details checks passed.
Video lifecycle tests also passed ASan/UBSan on the host. The native app and
installer built, all 15 payload-sync tests passed, and the actual 16-file Core
bundle (30,969,247 bytes) passed install/reinstall and preserved settings/media,
with existing component-default checks intact. These are host/build results,
not confirmation that the console memory-lock freeze is fixed.
Deployment: verified the .147 SD identity and clean uFlow exit, then staged,
installed and read-back verified the app. User settings, media-source and
library-config hashes remained unchanged. The installed app matches the native
installer payload. Hardware behavior is pending; FTP and system modules were
not changed.

Accent colors (2026-09-28): Glass Flow must honor the selected accent instead of
overriding it with cyan. Use it for the shared glass wash, focused glass buttons,
Settings highlights/ink, library, Details, Showcase and popups. Retain clear
refraction and white bevels. Expand the palette to 16 colors plus Platform, with
IDs 0..3 unchanged, and show the selected color swatch in Display. Keep the
existing theme config key; load/save bounds follow the shared palette count.
Fresh installer configs explicitly default to theme=0; saved colors and all
other user settings/media survive reinstall. Validate every choice in both
skins, controller/touch wrap, Platform colors, focused buttons and representative
Details/scan/Showcase captures, settings migration/persistence, video/background
regressions, and actual Core installer/component-default/preservation checks.
Rebuild/sync the app and native installer. Host captures do not prove console
appearance or freeze recovery.
Validation: all 17 actual swatches in both skins, controller/touch wrap,
GameCube Platform color, representative library/Details/scan/Showcase/focused
button captures, background persistence, video transitions and layout/paging
checks passed. All color save/load/legacy/bounds cases, 15 payload-sync tests,
and actual 16-file Core install/reinstall (30,967,807 bytes) passed, with every
component's default and preserved explicit color/media/settings checked.
Console accent appearance remains unverified.
Deployment: verified the .147 console's SD identity and clean app exit, staged
and final app hashes, and unchanged user settings/media/library configs. The
installed app matches the rebuilt native installer payload. File installation
is verified; console appearance still requires hardware testing.

Selected backgrounds (2026-09-28): request the selected image before cover
lookahead, decode at a maximum edge of 1280 pixels, and pin only that title's
texture within the existing 48 MiB detail cache. Prefetch the nearest backgrounds.
Keep the same image opacity during Details and cover inspection; refresh the
cached Details scene if its asynchronous background arrives later. Display adds
BACKGROUND TRANSPARENCY in 5% steps: 0% shows the full image, 100% shows the theme,
and the default is 50%. Legacy configs use that default; explicit saved values
survive reinstall. Test async cache eviction, both-skin opacity/Details/zoom,
delayed arrival, settings controls/persistence, actual native Core install and
reinstall, and all component defaults. Host results do not establish SD loading
speed, console appearance or freeze recovery.
Validation: both-skin opacity endpoints/midpoint, Details entry/cached reuse,
front/back inspection, delayed background arrival, settings controls, asynchronous
cache eviction and video/Showcase regression checks passed. Settings persistence,
15 payload-sync tests and actual 16-file Core install/reinstall (30,965,287 bytes)
passed, including component defaults and preserved media/settings. Native app
and installer builds succeeded. Hardware validation remains pending.
Deployment: verified the console SD library identity, clean app exit, staged/final
app SHA-256 and unchanged settings/media/library configs through FTP at .147.
The installed app matches the app embedded in the rebuilt native installer.
This verifies file installation only; console behavior and freeze recovery are
not yet validated.

Latest freeze capture (2026-09-28): the owner observer captured 0x809401E0,
matching the FTPiiU thread, with saved state 4, spinLockCount 0, and a stack through
poll / Socket::poll / FtpSession::poll / FtpServer::loop into select, IOS_Ioctl and
OSWaitEvent. The video reader waits on mapped-heap lock 0x80000020 through
ContentRedirection's aligned allocation. Decoder initialization and first upload
completed before the stall. The recorded owner is sleeping in network polling;
this does not prove it is actively holding a valid allocator lock, or that RAM is
exhausted. Stale/corrupted lock state or other ownership problems remain possible.
Do not forcibly unlock the heap or reintroduce MemoryMapping's removed global
allocation mutex without establishing the cause. The FTP idle-allocation patch
did not eliminate the freeze. Background memory reductions are not a proven fix.

Glass Flow visual correction (2026-09-28): console screenshots showed the first
Showcase glass candidate as a gray slab with heavily outlined lettering. Use
the Settings selected-category lens as the reference instead: remove both solid
reading layers, retain the shared refracted glass material and its light cyan
wash, and use light text with the existing subtle video shadow. Recheck both
skins and animation reversal against host captures. Rebuild/sync the app and
native installer, then test actual Core install/reinstall, component defaults
and preserved media/settings. Console appearance requires user verification.
Validation: both-skin preview, entry/reversal/interruption, background-opacity
and standard layout/paging checks passed. All 15 payload-sync tests and actual
16-file Core install/reinstall (30,963,420 bytes) passed, including component
defaults, preserved settings/media and keyless setup. Console screenshots of
the prior build are the reason for this correction, not validation of it.

Glass Flow preview and decoder-open follow-up (2026-09-28): the Showcase panel
now shares the scan popup's glass material, shadow and text glow, with a live
blurred video backdrop using existing scratch textures. Preserve its smooth
entry/reversal and the Pixel Flow appearance. The decoder adapter validates
all hardware buffers and retries segment-crossing allocations once with stronger
alignment. Console firmware analysis maps the observed 0x01060000 init error
to a physical 256 MiB boundary check, including the one-past-end address.
This targets the intermittent decoder-open error; the memory-lock freeze remains
a separate unresolved investigation. Keep the final adapter source hash pinned
in tools/sync-native-payload.py, rebuild the FFmpeg image, app and native installer,
run sanitizer adapter tests, both-skin video/details/scan checks and payload sync
tests, then install/reinstall the actual Core bundle on a host fixture. Verify
component defaults, preserved media/settings and no bundled development key.
Host success does not establish console playback, visual quality or freeze recovery.
Validation: adapter ASan/UBSan and host playback checks passed. Glass/Pixel preview
captures, entry/dismissal/interruption, details/scan popup checks and standard
layout/paging checks passed. The app and native installer rebuilt successfully;
all 15 payload tests and actual 16-file Core install/reinstall (30,963,356 bytes)
passed, including component defaults and preserved settings/media. Console
validation of this candidate remains pending.

Nintendo Land freeze follow-up (2026-09-28): the new heartbeat capture identifies
mapped-heap lock owner 0x100457E0 as uFlow's main thread. Artwork on core 2 and
ContentRedirection on core 1 spin on 0x80000020; no core-0 timeout or owner's
stack was captured. The saved app log stops before the final preview-start
message reaches disk. This establishes a memory-lock stall, not RAM exhaustion
or a defective media file. Repeated H264 initialization errors are also observed;
their relationship to the stall remains unproven.

With an existing development key only, a statically allocated core-0 observer
samples the lock every 100 ms. A repeated nonzero owner for at least 250 ms emits
one bounded saved-context/back-chain capture through OSReport. It never acquires,
releases or alters the inspected lock, suspends threads, or writes to the SD.
Sampled ownership is not proof of uninterrupted ownership. Capture can fail if
all CPUs stop scheduling. Stop/join the observer before background/exit/launch;
restart it on foreground return. Fresh installations have no development key
and create no observer. This is diagnostic instrumentation, NOT a freeze fix.
Run the heap-owner probe under ASan/UBSan, compile the Wii U app, sync/rebuild the
native installer and test its actual Core bundle, component defaults, preserved
settings/media and absence of a bundled key. Console capture remains pending.
Validation: probe ASan/UBSan checks, native app/installer builds, all 15 payload
sync tests and actual 16-file Core install/reinstall (30,962,812 bytes) passed.
All component defaults, preserved settings/media and keyless defaults passed.
The memory-lock cause and hardware freeze fix are still unresolved.

Showcase preview (2026-09-28): third video mode, persisted as 2 in the existing
mode column; explicit legacy Background (0) and Foreground (1) choices survive.
Missing modes and fresh native installer preview directories default to Showcase.
The fullscreen video fades in while the selected case makes the details-page
turn into the lower-left corner. A theme-matched panel beside it shows title,
system/year/genre/players, creator and synopsis. Normal playback controls remain.
Rebuild the app and installer, run media persistence/migration tests, both-theme
video UI/transition checks, and actual Core install/reinstall plus every component's
default checks. Hardware validation of this presentation is pending.
Validation: media migration/persistence, frame retention and playback cleanup,
both-theme quarter-transition captures, dismissal reversal, details/navigation
interruption, existing background-opacity/layout/paging checks, all 15 payload
tests and the final 16-file Core install/reinstall passed on the host. The rebuilt
native installer contains the same app. Console animation testing remains pending.
Hardware follow-up: the user reported a freeze on Yoshi's Woolly World while the
standard cover was visible. Captured heartbeat traces again show all three cores
waiting on mapped-heap lock 0x80000020, this time with owner 0x12DFD340; the current
FTP thread is 0x80940120. Thus the FTP idle-allocation mitigation did not eliminate
the shared-heap stall. The shown stacks are file-request allocation/free and log
open waiters; they do not establish the owner's fault or an animation failure.
Playback-time thread identification and another hardware investigation are needed.

Playback-engine candidate (2026-09-28): users should not need to convert their
video libraries. Bulk media conversion was abandoned before it started. The
adapter now decodes into retained, aligned frame-pool buffers without a full-frame
copy and honors safe non-reference skips when playback falls behind. Rebuild the
FFmpeg builder, app and native installer; update the adapter hash pin only after
`tools/test-h264-adapter.ps1` and the normal playback suite pass. Test actual Core
installation/reinstallation, defaults and preserved user media/settings. Use an
unchanged 1080p60 source for console timing; the two externally converted previews
below are not evidence for the new engine. The user reported that playback works
after installing this build; exhaustive codec/performance validation is not claimed.

Preview media correction (2026-09-28): console measurements of the imported
1080p60 FullBlast/Funk of Titans files showed about 8-9 displayed frames/sec,
audio starvation and increasing timestamp lag. The PC conversion plan existed,
but its Wii U outputs had not been generated; the import used original files.
Auditing only imported cached-game Wii U previews found 283 unique sources:
281 over the 30 fps preparation budget, including 131 over 1280x720.
FullBlast and Funk of Titans were prepared with `tools/prepare-video-library.py`,
fully decoded/probed, and installed with FTP hash verification. Originals remain
on the PC; prepared copies and conversion-result hashes live in the user's
`Media/uFlow Compatible` tree for future imports. This is external user media,
not content embedded in the application or native installer. The installer must
retain these files and keep its standard per-library preview directories; the
actual-bundle test now covers a prepared preview across install and reinstall.
Validation: the actual 16-file Core bundle passed installation, reinstall,
prepared-media preservation, component defaults and unchanged settings checks.
Console playback validation of those two replacements is pending. The remaining
conversion batch was cancelled in favor of fixing playback in the application.
Do not describe host decoding as proof of console playback speed.

R2 freeze diagnostics (2026-09-28): buffered console heartbeat reports show all
three cores waiting in MemoryMapping_allocEx / MEMAllocFromExpHeapEx on the same
mapped-heap lock (0x80000020). Callers are artwork file open, video file read, and
the UI's video log write through ContentRedirection. This establishes the stall
location, not out-of-memory, a cover-count limit, or the reason the owner stopped.
Development-key opt-in now logs bounded thread identities at startup and through
the authenticated `threads` command. The same service accepts ordinary bounded
page-forward/back commands for controlled hardware reproduction. No diagnostic
key is installed by setup; normal fresh installs retain the same defaults.
Rebuild/sync the Core app and native installer, then test actual Core installation,
component defaults and preserved-settings reinstall. These diagnostics are not
a freeze fix, and native compilation/host checks do not establish hardware recovery.
Validation: Wii U app compilation, all 15 payload-sync tests, and actual Core
install/reinstall (16 files, mask 1) passed, including component defaults,
preserved settings and absence of a bundled development key. Hardware diagnosis
and freeze recovery were pending at that stage; see the hardware follow-up below.

Hardware follow-up: the thread inventory maps lock owner 0x8099D7A0 to FTPiiU.
A controlled run completed 80 ten-title pages with detail-cache eviction and
stable bounds, then froze with the same owner. FTP could still log in until a
file request stalled its ContentRedirection worker on that lock. FTPiiU's idle
listening poll allocates/frees a descriptor array every 16 ms. The candidate
mitigation uses 16 stack descriptors and retains dynamic storage for larger
connection sets. It removes the repeated idle allocation without changing
cover quality or cache budgets; the underlying lock-retention mechanism is not
yet proven. Reproduce with `tools/prepare-ftpiiu-poll.py --apply` against the
same pinned FTPiiU revision as `tools/prepare-ftpiiu-exit.py`. Both patches apply.
`tools/test-ftpiiu-poll.ps1` exercises the actual poll function under ASan/UBSan:
10,000 idle calls with allocation forbidden, real socket readiness/hangup/invalid
descriptors, and the 16/17-descriptor boundary. It and the existing exit lifecycle
test passed. Build with `uflow-ftpiiu-builder:0.4.5`, update the payload hash pin,
sync/rebuild/test the installer and preserve FTP configuration. A full reboot
is required to load a replaced plugin.
The candidate passed the actual Core bundle install/reinstall, all component
selection/default checks and all 15 payload-sync tests; FTP configuration is
preserved and only the pinned plugin binary is replaced.

Hardware validation after full reboot: all 1,346 unique covers prepared, followed
by 80 fast and 120 slower ten-title page movements, with a 60-second idle interval
after each pass. Both passes stayed responsive through detail-cache eviction;
the earlier build had frozen after the 80-page pass. Subsequent FTP reads of the
library, plugin and settings completed while uFlow remained responsive. Plugin
hash and unchanged settings were verified; the captured current boot had no
heartbeat timeout or spinlock fault reports. This validates the controlled
reproduction, not every possible freeze or the precise lock-retention mechanism.
The same pinned plugin is included in the rebuilt, host-tested native installer;
the installer itself was not run on the console. Jungle Beat console playback
remains a separate, unverified check.

Imported-cover preloading and video initialization (2026-09-28): PRELOAD COVERS
now prepares the preferred full sleeve/front/default sleeve for every cached
game. The legacy `preload_builtin_covers` key keeps existing ON/OFF choices.
Resident thumbnails have a 48 MiB budget, an adaptive maximum edge of 128 pixels,
and an 8,192-path cap. Detailed artwork keeps its separate 48 MiB LRU; eviction
falls back to the resident thumbnail. Source path/size/mtime and thumbnail size
key disposable PNGs under `cache/cover-thumbnails`; originals are never rewritten.
First preparation decodes source art; later launches reuse the cached thumbnails.
B still continues preparation in the background. Installer defaults enable this
for every component selection; keep-mode reinstall preserves an existing OFF.

The H.264 adapter asks `H264DECMemoryRequirement` for each stream instead of always
allocating a Level 4.2 workspace. Initialization failures retain allocation versus
platform errors, include stage/details in the log, and no longer all appear as
unsupported codecs. Loop counts and periodic video statistics remain available
after the initial 30 seconds. The source dependency is pinned in payload sync.
Rebuild `uflow-builder:video` with `tools/video-ffmpeg.Dockerfile`, then rebuild
the app. Run `tests/h264_adapter_test.c` with ASan/UBSan, `tools/test-video.sh`
(including the actual Jungle Beat clip through a loop), `asyncartworkchecks`,
`iconpreparationchecks`, `startupanimationchecks` and `mediagroupchecks` plus the
standard host layout checks. Rebuild/sync/test the native installer with its real
bundle. Host tests do not establish that the reported console freeze is fixed;
retest the GameCube preview and imported cover preparation on the Wii U.
Validation: the adapter fault-injection tests passed under ASan/UBSan, the host
video suite played the replacement Jungle Beat clip for 45 seconds through one
loop, and the artwork/startup/media and standard layout checks passed. All 15
payload-sync tests passed. The rebuilt installer passed the actual 987-file
bundle install, preserved-settings and unchanged-rerun test (mask 131071).
Console playback and imported-cover preload verification remain pending.

Preload speed follow-up: the console prepared 180/1346 covers at 76 seconds and
557/1346 at 212 seconds on its first pass. Thumbnail creation now requests JPEG
IDCT scaling (down to 1/8) and samples PNGs directly to the thumbnail dimensions,
instead of producing detailed artwork first. Disposable PNGs use fast encoding;
existing thumbnail cache keys remain compatible. Cache hits no longer attempt
directory creation. Full-detail decoding and source images are unchanged.
Run `artworkchecks` for exact regular/Adam7 thumbnail sampling, reduced JPEG
decoding, persistent-cache reuse, corruption recovery and source invalidation;
also run async artwork and preparation checks. Rebuild the app and native
installer and run the actual-bundle install/reinstall tests before deployment.
Hardware speed after this change must be measured separately from host timings.
The preparation counter uses `%` instead of `PERCENT`, including a percent glyph
in the pixel font. Host artwork/cache and async preparation tests passed; the
sample JPEG decoded in 90 ms at thumbnail resolution versus 132 ms for detail.

Cover-quality follow-up: native icons also upgrade from the resident thumbnail;
visible artwork continues decoding/uploading during video, one upload per frame,
while whole-library preparation waits. Side-card composites are cached only
after detailed art resolves, so a temporary thumbnail cannot become permanent.
Async tests cover built-in and imported upgrades with bulk preparation paused;
the video UI test upgrades a real resident thumbnail during active playback.
The console froze after starting Sochi 2014's preview (000500001010c700), before
its first performance report. That does not identify the failing operation.
The next build records audio/input/decoder initialization and first-upload
checkpoints, with no per-frame SD writes. Do not call the freeze fixed based on
host success; obtain the new checkpoint log and retest on hardware.
Validation: OpenGL host checks passed for asynchronous artwork, both preparation
themes, cover upgrades during background video, all four layouts/three spacings,
paging and library transitions. The actual Sochi clip completed one loop in a
40-second host playback test. The final Core payload passed install/reinstall
(16 files, mask 1), along with component selection/defaults and preserved settings
checks. The refreshed local installer contains the same app as the deployment.
Console speed, cover quality and freeze recovery remain pending hardware retest.

Library sorting (2026-09-26): B opens the sort picker in the library; the new
SORT LIBRARY action can be rebound in Controls and resets to B. Existing binding
indices are unchanged, and the footer follows the assigned button. A/touch
applies and B cancels. Display / SORT BY provides the same persistent selection:
title ascending/descending, recently played, year newest/oldest, publisher and
system. Unknown years/publishers sort last, ties use title and stable identity.
Sorting changes visible indices only, preserving the selected title and artwork
cache. Play history is recorded for accepted game launches, keyed to the original
title/ROM before injection; opening an emulator browser alone is not a play.
`play_history.cfg` uses atomic replacement and a monotonic sequence independent
of the console clock. It begins with this version, survives rescans/reinstall,
and is never bundled. Installer defaults include `sort_mode=0`, preserving user
settings on reinstall. Run library_sort_test with settings.cpp, `sortingchecks`
and the standard preview suite, and actual-bundle installer tests. Rebuild the
app and local installer; upload only the running app.

Angry Birds Trilogy save creation (2026-09-26): live console syslog identifies
ContentRedirection's FSRemoveWrapper mapping ENOENT for
`/vol/save/80000007/saves/tmp/fs0.fs` to MEDIA_ERROR, making the game's FS client
fatal while its rendering continues. Return NOT_FOUND for ENOENT; preserve
successful removal, read-only restrictions, layer routing and other errors.
Reproduce with `tools/prepare-content-remove-fix.py --apply` against
ContentRedirectionModule revision `31b319b6b4370caf4ab173f0db91cf838c6819d0`.
Build that checkout's Dockerfile and run `make -j4` in the resulting image.
Run `tools/test-content-remove.ps1`: it compiles the actual patched wrapper,
tests real missing/existing files and repeated cleanup, and injects other errors
under ASan/UBSan. Payload sync hash-pins this dependency as a Core replacement;
the native installer allowlist, defaults/component selection, actual-bundle
replacement and preserved-settings rerun tests include it. Rebuild/test the
installer but keep it local. Deploy only ContentRedirectionModule.wms to the
verified SD, fully reboot Aroma, then retest save creation and reopening the
game. Host success does not establish console success. Never delete game saves
to work around this missing-file error.
The user confirmed that Angry Birds passed save creation after this fix.

Startup reveal (2026-09-26): embed the user-supplied `uflow_startup.mp4` as
`app/content/ui/startup.mp4`, converted to H.264 High Level 3.1, 1280x720,
30 fps with the original AAC audio stream copied unchanged. STARTUP VIDEO is
an independent Display setting (`startup_video`, default ON), unrelated to
cover preloading or background motion. It plays the 7.2-second intro once, with
a 0.8-second fade through dark into the selected theme and a 0.65-second fade
into the library. B skips. Menu music waits until completion, skip or failure;
video playback explicitly disables looping. Cover loading continues while it
plays, if enabled. With preloading OFF, the transition says OPENING YOUR LIBRARY.
Installer defaults include the switch; reinstall preserves OFF. Test all four
startup/preload combinations, legacy defaults, and real audio decoding with
SDL_AUDIODRIVER=dummy in the host startup check.
Decoder failure/initial buffering timeout falls back to the
themed screen; cancel and foreground release use the existing video teardown.
The theme decoder retires before starting the reveal; game/theme playback waits
for the shared video player to retire afterwards. Run `startupanimationchecks`
with the actual packaged clip and inspect transition frames. Host validation is
not evidence of hardware playback smoothness. Keep the installer local.

Display menu and circular carousel (2026-09-26): the five former Media/Display
Options switches now live directly in Display, below Accent Theme. Along with Startup Video, twelve rows
scroll in a six-row viewport with matching controller and touch selection. Media
keeps directory, scan and help actions. Existing settings values are unchanged.
Wrap Navigation now applies to carousel neighbors and artwork lookahead as well
as selection; crossing the first/last boundary preserves the adjacent animation
instead of jumping across the library. OFF keeps finite ends. The existing page
button end-stop behavior is unchanged. Run `wrapdisplaychecks`,
`mediagroupchecks`, `iconpreparationchecks`, media_groups tests and normal layout/
paging checks. Rebuild/sync/test the local native installer; deploy only the app.
Console confirmation remains separate from host previews/tests.

Cover preparation preferences and design (2026-09-26): Media display settings
include PRELOAD BUILT-IN COVERS, enabled by default. The persisted
`preload_builtin_covers` switch applies on the next launch; disabling it skips
whole-library preparation and uses normal on-demand artwork loading. Existing
resident textures remain usable for the current session. Installer defaults
enable it for every component selection and preserve an existing OFF setting.
The preparation screen uses the selected skin/accent, the supplied logo embedded
at `/vol/content/ui/logo.png`, and lightweight ring/wave/logo animation respecting
BACKGROUND MOTION. No external SD logo is required. Run `iconpreparationchecks`,
settings persistence and actual-bundle installer tests; inspect both skins.
Rebuild the local installer, deploy only the app. The preceding resident-cache
change was confirmed by the user to keep up with fast scrolling on hardware.

Resident built-in covers (2026-09-26): prepare SD-cached native icons before
browsing and retain them in a separate, non-evicting 96 MiB texture pool (up to
2,048 unique paths). The user's 1,338 128x128 icons require about 84 MiB. Large
scanned artwork still uses the ordinary 48 MiB/128-entry LRU; it cannot evict
resident icons. Preparation shows progress and B skips the wait while loading
continues in the background. This happens on launch and after a rescan; no new
scan/cache format or settings migration is needed. Failed icons complete without
stalling; icons exceeding the resident budget retain ordinary on-demand behavior.
Renderer work stays on the UI thread and preparation continues pumping system
events. Run `asyncartworkchecks` for >128 resident icons, scanned-art eviction,
reverse traversal, duplicates, missing files, reset and the 96 MiB boundary.
Rebuild/sync/test the native installer locally; deploy only the running app.
Hardware startup time, memory use and fast scrolling remain to be retested.

Built-in USB icon caching (2026-09-26): library cache version 6 forces a
one-time rescan. Discovery copies supported `meta/iconTex.tga` artwork to the
SD `cache/icons` tree before publishing the library; source identity, size and
modification time select a cache entry, with 256 directory shards. Scrolling
then uses the SD path instead of resolving each USB game directory. Custom
covers retain precedence. Invalid/truncated icons or failed cache writes fall
back to the source without modifying game files. `builtin_icon_cache_test.cpp`
covers reuse, corruption, source isolation and write failure. Keep the rebuilt
native installer local for this update, per the user's deployment preference;
deploy only the app. Hardware scrolling performance is pending retest.

Artwork scrolling (2026-09-26): preload fronts for the animated on-screen
position first, then built-in icons up to 24 titles either side of the selected title before
background/back/spine work. The texture cache retains up to 128 entries with
the existing 48 MiB byte limit. The CPU decoder can produce four results
between UI frames, stopping when completed surfaces reach 8 MiB (plus at most
one bounded decode). Upload at most four results or 3 ms of work per frame;
one upload itself is not preemptible. Keep all renderer calls on the UI thread.
Run `tools/test-artwork-worker.ps1` and the `asyncartworkchecks` host preview;
rebuild the app and synchronize/rebuild this installer. This improves bounded
lookahead and reuse; fast scrolling through never-read SD artwork still requires
hardware measurement and cannot guarantee zero placeholders at every speed.

Every shipped uFlow fix must survive a fresh installation. A one-off SD edit is
not a completed release: keep the native payload, defaults, tests and packaging
recipe current. Preserve existing user settings, ROMs and saves on reinstall.

Review corrections (2026-09-26): rebuild the app, raw loader and Mocha together
before synchronizing this installer. HAI now validates the complete numbered NFS
set against its header and FAT volume bounds, and clears its one-shot arm on
cancel, unrelated title startup and SD homebrew preparation. Wii VC WBFS export
remains unavailable: both new conversions and legacy cached launches are blocked
until decrypted partition re-encryption is implemented and validated. The normal
HAI USB route remains available. This is not an app-only OTA upgrade.

Run `tools/test-review-regressions.ps1`, `tools/test-hai-companion.ps1`,
`tools/test-usb-raw.ps1`, `tools/test-wii-vc-cancel.ps1`, library/VC tests and
the actual-bundle installer tests. `tools/build-usb-raw.ps1` now uses
`module/build-native` so experimental build-directory dependencies do not leak
into the canonical release build. Library cache version 5 forces one rescan for
multilingual title fallback; user settings and game data remain intact.

WiiFlow requires all three matching `out/bins` files alongside its executable:
`app_booter.bin` (Nintendont/homebrew), `ext_loader.bin` and `ext_booter.bin`
(Wii games). Payload sync installs them under `apps/wiiflow/bins` in replace
mode for either Wii or GameCube selection (mask 6). Shipping `boot.dol` alone
passes the game's preflight but fails the subsequent loader handoff.

1. Build and test each changed component with its appropriate Wii U builder.
2. Run `tools/build-native-installer.ps1` from PowerShell. This synchronizes the
   latest app, launchers, raw loader/kernel, patched Region Free plugin, emulator
   backends and font, updates the manifest, and builds the Wii U installer.
3. Run `python tests/test_native_payload_sync.py` and the native installer host
   test with the actual content bundle. Cover clean install, replacement of stock
   dependencies, component selection, preserved settings and idempotent rerun.
4. Copy the resulting `native-installer/uFlow-Setup.wuhb` to the verified SD card
   at `wiiu/apps/uFlow-Setup.wuhb`, flush and compare complete SHA-256 readback.
   Updating the installer is not permission to erase the card or run console setup.

`native-installer/Makefile` refuses stale payloads. Direct Docker builds require
the project mounted at `/project`, workspace `tooling/sources` at `/sources`
(read-only), and `UFLOW_SOURCES_ROOT=/sources`. Run payload synchronization first;
the guard intentionally verifies, rather than silently changing release inputs.

The optional RetroArch components are bits 13-16; bits 11 (startup) and 12
(vWii tools) keep their meaning. Each ships one core, its `.info` and its
launcher module; the RetroArch frontend ships with any of them (mask 122880).
The installer allowlist names these exact paths, never all of `retroarch/`:

| Bit (mask) | Component | Core (keep) | Module (replace) |
|---|---|---|---|
| 13 (8192) | PlayStation | `pcsx_rearmed_libretro` | `retroarch-ps1.ufmod` |
| 14 (16384) | SNES | `snes9x_libretro` | `retroarch-snes.ufmod` |
| 15 (32768) | NES | `nestopia_libretro` | `retroarch-nes.ufmod` |
| 16 (65536) | Game Boy Advance | `mgba_libretro` | `retroarch-gba.ufmod` |

Cores are `retroarch/cores/<core>.rpx` with `retroarch/cores/info/<core>.info`;
the frontend is `wiiu/apps/retroarch.wuhb` (keep). Keep mode means an existing
RetroArch or core on the card is never overwritten. The inputs come from
`tooling/sources/retroarch-aroma/` (provenance in its `SOURCE.txt`) and
`sync-native-payload.py` pins their SHA-256, because only that community Aroma
build runs on this setup; see `docs/RETROARCH-LAUNCH.md`. RetroArch
configuration, BIOS, saves, history and core options are never shipped. Games
and BIOS are never bundled.

A fresh PS1 selection generates a `ps1` Files content type (`.cue,.chd,.pbp`,
launcher `ps1`, 142 x 125 x 10 mm jewel case) scanning `wiiu/roms/ps1`. SNES,
NES and GBA share their content type with the Virtual Console components
(bits 5, 4, 6): selecting either creates it, and when the RetroArch component
is selected its launcher (`snes-retroarch`, `nes-retroarch`, `gba-retroarch`)
is the default, with or without the VC injector. That system's default sleeve
ships with either. Generated configs are keep-mode, so an existing
`libraries.cfg`, such as a user's own `custom_1` PS1 type or a VC default,
survives a reinstall.

The Region Free patch is selected alongside Wii U or any VC system (mask1017).
It replaces stock `regionfree.wps` but never installs or overwrites its JSON
configuration. Wii/GC/NooDS-only installs leave the plugin alone. Auto-detection
is enabled by default in stock Aroma; intentionally disabled user settings stay
disabled. Bad/missing game metadata still produces a region prompt.

Region Free's tested binary hash is pinned in `sync-native-payload.py`; update
that pin only when a replacement plugin build has been tested. Every new external
patched dependency must be added to the same source-to-payload map and tests.
Original Nintendo VC bases are integrity-checked, not silently refreshed by sync.

N64 injection selects the base's configuration by normalized cartridge ID,
region and revision, not the donor ROM filename. A user ROM.ini takes precedence;
unknown games get generic options without donor-specific cheats/patches. Selected
profile/version participates in the build key; old saved N64 generations without
the small profile-version marker require a one-time rebuild. Normal launches
still do not hash the ROM or entire injection. Run tools/test-vc-formats.sh with
UFLOW_VC_SYSTEMS=n64 and UFLOW_TEST_N64_ROM pointing to a real cartridge for the
cross-game profile regression. Preserve the ROM/save namespace when repairing an
existing package, and validate every new generation before changing game.cfg.

Return-to-uFlow uses a one-shot SD request written only when uFlow launches a
game/emulator. The loader consumes it from Wii U Menu's foreground ProcUI pump,
after the previous title has closed. uFlow clears it at entry; launch errors
cancel it. Missing/malformed requests or a missing app leave the normal Menu.
This does not enable start-on-boot or change NAND/default system titles. Wii/GC
backends retain their Wii U return route; the request survives that transition.
RPXLoadingModule, its kernel dependency and Mocha now ship as Core, including
Wii/GC/NooDS-only installs. Test `tests/return_to_uflow_test.cpp` as well as the
actual installer bundle. Hardware game-close and explicit uFlow-exit tests remain
required after any return-hook change.

The loader shares `app/source/return_to_uflow.hpp`. With its existing Docker
dependency paths, mount `module` at `/project` and `app` read-only at `/app`, then
run `make -j4` in `/project` using `wiiu-loose-title-module-builder:0.1.0`.
Run `tools/test-wii-launch.sh` in the host test image for return/startup regressions.
`tools/test-return-hook.ps1` additionally compiles the actual production hook
against host stubs and checks its process restriction and foreground/exit behavior.

The old Windows-installer preparation scripts are historical, not the release
entry point. The current personal native bundle embeds user-supplied Nintendo VC
bases and must not be publicly redistributed. No PC-installed user ROMs or save
files belong in the release payload.

Default sleeves live in `native-installer/assets/default-covers/{system}.png`.
`sync-native-payload.py` copies them unchanged into each selected system's
`wiiu/apps/uFlow/media/{system}/fullcovers/Default.png`; these entries use keep
mode, not replace. DS art is included for either DS VC or NooDS. Add further
supplied systems to `DEFAULT_COVERS` with their component mask, then rebuild.
Generated media settings select the supplied default; existing settings stay
untouched. Full-cover sources with no explicit default now discover Default.png
(case-insensitive) during scanning, so copying the file and rescanning is enough.
An explicit different default retains precedence; disabling a source disables its
fallback too. No automatic fallback is introduced for video/audio/metadata.

Source DS icons and GameCube disc banners are decoded only when real artwork is
missing, fitted without distortion and cached under `cache/rom-art`. No injected
donor artwork is used. Wii banner extraction and older cartridge screenshot
generation are not implemented. The library cache version is bumped once for
the new artwork precedence. Test `rom_art`, `media` and the installer payload.

Separate cover faces: setup now creates `front`, `back`, `spine`, `fullcovers`
under each selected system. Existing `covers` sources remain supported as front
art; saved config tokens `front` and `full cover` are aliases. Existing configs
are kept; existing sibling face directories of registered full-sleeve sources
are discovered without duplicate entries or re-enabling explicit disabled rows.
`case_profiles.hpp` is shared by app and installer so dimensions agree. Legacy
DVD-size defaults upgrade on load; custom sizes and versioned saved sizes stay.
Keep `tests/cover_faces_test.cpp`, the actual native bundle tests and the
`coverfacechecks` host preview green when changing these paths.

All ten default PNGs have companion `.layout` authoring metadata, shipped in keep
mode as `Default.png.layout`. It records original byte length and normalized
back/spine/front cuts; wrong-size or invalid metadata is ignored. Never overwrite
a user's custom PNG to apply a layout. The scan reads each unique layout once,
caches the resolved cuts with media paths, and render does no metadata I/O.
Separate faces and sleeve regions are fitted to their case dimensions; aspect
differences over 8% are letterboxed rather than distorted. Full sleeves take
priority over separate faces; absent/failed faces fall back independently.

The bundled N64/SNES defaults are now recomposed as landscape sleeves, GBA as
a square-face sleeve, TurboGrafx-16 as a jewel-case sleeve, and MSX with its wider
spine. PNGs are the edited artwork, not padded portrait images. The authoring
cuts match the final files; `DefaultCoverGeometryTest` in
`tests/test_native_payload_sync.py` checks all ten real PNGs and all thirty face
regions against the renderer's fit tolerance, including byte-length guards.
Built-in image edit prompts are recorded in
`native-installer/assets/default-covers/format-edit-prompts.md`.

NES dimensions now follow the user's 3366x2100 sleeve reference, normalized to
130:178:25 (front width : height : spine), rather than the earlier assumed
124:178:25. These are image-derived ratios, not verified millimetres. `.nes`,
`.unf`, `.unif` and the NES VC launcher all select this preset. Existing artwork
already fits, so its PNG and authored cut metadata are unchanged.

Raw Wii U games with local RPL imports (e.g. Mario Kart 8's `mvplayer.rpl`)
need IOSU-side code-directory lookup before APPLICATION_STARTS/PPC redirection.
Mocha derives a one-launch scope from the prepared loose RPX and its matching
raw-profile executable. It serves only foreground process-code RPL requests;
CafeOS and explicit Aroma replacements retain precedence. The scope survives
the last RPX chunk but is cleared by a new title, preparation, cancellation or
profile change. MCP retains only the profile's executable token to stay within
its existing BSS allocation. Run `tools/test-raw-rpl.ps1` (production dispatch
and lifecycle under ASan/UBSan), rebuild `payload/00_mocha.rpx`, then follow the
installer workflow above. After SD deployment, a full reboot into Aroma is
required to activate this payload; relaunching uFlow alone is insufficient.

Rotating cover faces use `cover_projection.hpp`: interpolate planar samples
with their original clip W, then project a bounded, adaptive mesh for SDL's
affine triangle renderer. A single projected quad bends artwork along its
diagonal during yaw. `tests/cover_projection_test.cpp` checks 18,150 panel poses,
all system shapes, centre stability, cropped UVs and subpixel interpolation.
The host `casechecks` uses a ruled sleeve to expose bending, in both skins;
also run `coverfacechecks` for the fallback/spine layout. Rotation poses, zoom,
case geometry and source images are unchanged. Rebuild the app and installer.

The signed app updater is embedded in uFlow, including the public trust key and
TLS root certificates. Increment `app/source/update_model.hpp` for releases and
rebuild both app and native installer; payload sync carries the new executable
into fresh installs. Run `tools/test-updater.sh`, `tests/test_update_release.py`
and the host `updatechecks` UI suite before packaging. Version 1 intentionally
updates the app only; Aroma/backends still require the installer workflow.
The initial test SD uses sequence 2026092000 so it can accept server release
2026092001; the installer contains the latter. See `updates/README.md` for the
recovery entry and remaining hardware checks. Never ship the private signing key.

Updater correction: active WUHB files cannot be renamed while Aroma's content
mount holds them open. `app/Makefile` now builds `update-helper` first and embeds
its bare RPX. The app verifies/stages a signed request without renaming itself;
the helper installs after the app exits and reopens uFlow. Keep the helper build
dependency and handoff tests when changing packaging. The corrected test pair is
helper-enabled sequence 2026092001 on SD and release 2026092002 on the server;
the refreshed native installer contains 2026092002. Both the SD and installer
must include the embedded helper. Do not ship the previous no-helper bootstrap.

Test2 exposed Aroma's intentional inherited content remount for bare RPX hot
reloads. The helper must explicitly call RPXLoader_UnmountCurrentRunningBundle
before recovery/commit and stop on failure. Its font/code are self-contained.
Test3 checks this ordering and failure gating on host; the SD bootstrap now has
sequence 2026092002 and the server/installer have 2026092003. A host pass does not
establish that console bundle handles have been released; hardware retest is required.

The user confirmed test3 installs and reopens successfully on hardware. Test4
(sequence 2026092004) adds scoped white outer glow to GlassFlow update, injection
and welcome popup text. The glyph mask is cached and reused, with the crisp dark
face rendered last; Pixel Deck and non-popup styling are unchanged. Run the host
`updatechecks` preview (including Pixel Deck and scope-restoration checks), rebuild
the app and native installer, and publish the signed software-only OTA. Do not
replace the installed app on SD when the requested test is the OTA path itself.

Test5 (sequence 2026092005) adds explicit ../ entries to library/media browsers.
Back traverses to fs:/ and stays there; X/Cancel exits. Container roots cannot be
saved as library folders. Default-file selection remains independent of the
parent row. Run browser_path and browserchecks plus mediadefaultchecks before
release. This is navigation only, not new USB filesystem or mounting support.

Test6 adds a virtual vol entry at fs:/ even when namespace enumeration fails.
At fs:/vol, known external volume paths are probed with opendir and merged with
enumerated entries, without listing inaccessible drives or mounting anything.
Library and default-media pickers share this behavior. browser_path tests cover
unlistable parents, absent volumes and deduplication; browserchecks enters vol
from the real listing instead of injecting a synthetic root fixture.

Wii/GameCube selections also install wiiu/apps/WiiFlow.wuhb (mask 6).
tools/build-wiiflow-forwarder.ps1 builds Boot2vWii from tooling/sources/Boot2vWii
(upstream https://github.com/WiiDatabase/Boot2vWii, source commit
5c4f712dd1bfc66c833d27d9f1af64117feba936, Unlicense). It targets DWFA
0001000144574641 with BOTH screens, no forced resolution, and isolated objects.
This Wii U Menu shortcut needs the existing vWii DWFA channel and SD WiiFlow;
it skips Wii Menu/Wiimote prompts but does not emulate GamePad controls. Keep
normal vWii mode for the USB diagnostic, not Wii VC injection mode. No NAND
installation is performed when deploying this WUHB to an already configured console.

USB access test7 embeds a read-only FatFs driver in uFlow. It reads one FAT32
USB disk via libmocha raw-sector reads and exposes usb1:/ at the browser root.
It does not mount FAT over a system volume, write sectors, alter the SD mount,
or extend the game launchers yet. 512-byte sector disks are the initial target.
All filesystem calls are serialized; aligned sector buffers handle arbitrary
caller alignment. usb-storage.log on SD records mount/read failures. Use
tools/test-usb-storage.sh and browserchecks; test physical USB browsing before
claiming support. The embedded code follows the app into fresh native installs.

Direct raw USB test8 adds a module-owned read-only FatFs device (`ufusb:/`) for
retail content/code/meta redirection. Close its process-owned FSA handles on
APPLICATION_ENDS, then reopen and check the original disk on the target process.
The app's separate `usb1:/` browsing device must not own runtime callbacks.
Before launching, the module passes bounded RPX/RPL FAT allocation metadata to
MCP (64 code files, 4096 extents maximum). MCP reads executable bytes directly
from those USB sectors, including fragmented files; no game data is staged on SD.
Read-only maps are scoped to the selected raw launch and cleared on cancellation,
profile replacement, or the next title. MBR/boot fingerprints detect swapped disks.
Game saves continue in the existing SD raw-saves namespace.
Run tools/test-usb-raw.ps1, tools/build-usb-raw.ps1, rebuild the app, then the native
installer and actual-bundle host tests. The module build needs sibling /app and
/payload mounts; the script provides them. Deploy app, RPXLoadingModule.wms and
setup/00_mocha.rpx together and fully reboot into Aroma. Host tests do not prove
retail USB launch compatibility; test executable startup, content reads, HOME,
return-to-uFlow, and an SD raw launch on hardware. Do not publish an app-only OTA
as a complete direct-USB upgrade.

The first hardware USB launch reached the game but exposed an SD-only native
read bypass incorrectly prepending /vol/external01/ to usb1:/ content paths.
RawNativeReadPath now accepts only SD-relative roots; USB opens retain their
virtual paths for the module-owned ContentRedirection device. Native SD save
opens remain unchanged. tools/test-usb-raw.ps1 includes raw_native_path_test.cpp
with the captured alldata.psb.m/code paths and USB-title SD save regression.
This correction needs a rebuilt module and native installer; existing test8 app
and Mocha binaries remain compatible. Fully reboot Aroma after replacing the module.

Menu-exit investigation: hardware traces show completed application/USB cleanup
but a stall before Menu's app-entry callback, including with USB disconnected
and the Menu return hook disabled. Raw filesystem/save/frame hooks now target
FP_TARGET_PROCESS_GAME, not every system process. Their active-title guards
remain in place within that process. The Menu-only return hook is enabled again.
Run tests/test_raw_hook_scope.py and tools/test-return-hook.ps1 with USB/SD tests.
Retain startup/shutdown trace checkpoints until hardware confirms Menu entry and
game return; host tests alone do not establish that this fixes the observed hang.

The game-only hook candidate still hung on hardware. The user confirmed an older
install on another SD exits normally on the same console. The current capture
contains MCP Menu-load reset/lookup messages (some malformed console log bytes
made ordinary text search miss them); it stops before the replacement path is
reported. MCP now constructs environment/MLC/SD paths with bounded MCPJoinPath,
without the fixed-address snprintf import. Missing optional Menu overrides must
fall through to the installed Menu. Unterminated/oversized paths fail before I/O,
not after silently truncating to a different file. Diagnostics no longer use the
unsupported printf string-precision format. This is a hardware candidate, not a
confirmed resolution. tools/test-mcp-replacements.ps1 runs the actual replacement
loop and path/I/O routing with only firmware entrypoints/environment memory
mocked, including Menu bootstrap, missing overrides, all eleven lookup records,
MLC regional backups and malformed fields under ASan/UBSan. It is part of the USB
raw suite. Rebuild Mocha and the native installer; fully reboot for the retest.

Hardware confirmation (2026-09-20): after deploying the bounded-path payload
(SHA-256 816804F55F9C78E1E4B627546C9EEE23C120E95B21445A7B29DC28C4BAA81CAC)
and fully rebooting, the user confirmed both USB-game exit and explicit uFlow
exit work. This resolves the tested Menu-exit regression. The refreshed native
installer already embeds this exact payload and passed the actual-bundle install
and idempotent-rerun tests; do not revert it to the earlier formatter-based build.

Cover-browsing test9 (sequence 2026092009) moves artwork reads/decompression to
a low-priority CPU worker. Each frame replaces its bounded queue with currently
wanted covers; obsolete results are discarded. Only the UI thread uploads textures,
at most one completed image per frame. Neighbour prefetch no longer decodes their
unused backgrounds/logos. Cache reset invalidates in-flight results, and shutdown
joins the worker before renderer/storage teardown. Run tools/test-artwork-worker.ps1
(production worker under ASan/UBSan) and GPU host previews asyncartworkchecks,
artworkchecks, casechecks, coverfacechecks. Rebuild the app and native installer;
the hardware-confirmed USB payload/module must remain unchanged. V-sync/flip waits
and cover geometry are unchanged. Hardware checks still required: fast scrolling,
direction reversals, cover inspection, video transitions, and app/game exit.

Large-folder browser correction (sequence 2026092101): directory and default-file
pickers enumerate on a cancellable CPU worker, not the render/input thread.
Parent, Cancel and Use Folder remain usable during enumeration; obsolete listings
cannot replace a newer path. devkitPPC's readdir stat/type metadata is reused to
avoid a second FAT name lookup for every entry; unknown types retain stat fallback.
Show loading and read-error status. Cancel/join workers before storage teardown.
Run tools/test-browser-worker.ps1 (10,000-file fixture and ASan/UBSan lifecycle),
browser_path/library/media tests, and asyncbrowserchecks/browserchecks/
mediadefaultchecks host previews. Rebuild app and native installer; keep the tested
USB module and Mocha unchanged. Hardware retest target: usb1:/Decrypted; host tests
do not establish responsiveness of a failing physical disk or firmware I/O call.

Scan progress (sequence 2026092102): Scan Now and uncached startup run the scanner
on a worker. The modal reports stage, current path, games found and elapsed time;
unknown discovery totals use an animated bar, artwork matching uses completed /
total games. Do not fabricate a whole-scan percentage. Input/settings changes and
launches are blocked while scanning, but rendering and system events continue.
B requests cooperative cancellation before the cache commit; interrupted/failed
scans never replace the displayed library or save partial scan results. System
background/exit joins the worker before storage teardown. Final cache-save failure
is reported separately from successful in-memory discovery (including the existing
8192-entry cache bound). Directory handles are RAII-owned across observer unwinds.
Run tools/test-scan-worker.ps1, library/media/browser regressions and scanchecks
GPU preview in both skins; rebuild and test the native installer. The USB module
and Mocha are unchanged. Physical USB scan timing/progress requires console testing.

USB scan cache (sequence 2026092103): the app's read-only FatFs sector reader now
keeps a bounded 2 MiB, four-way cache. Absolute metadata paths previously reread
the large parent directory for each lookup. Cache only requested sectors, preserve
bounded multi-sector reads and aligned firmware buffers, and invalidate on failed
reads or raw-device close/remount. All access remains under the filesystem mutex.
This is app-only: do not change the hardware-confirmed game module or Mocha.
Scan-start/end diagnostics record cumulative cache hits and physical reads in
usb-storage.log. Run tools/test-usb-read-cache.ps1 (ASan/UBSan plus production
FatFs against 3,000 long-name folders), USB storage and scan-worker/library tests.
The synthetic fixture reduced physical read calls from 27,075,002 to 10,138 with
identical metadata; this is not a physical-console elapsed-time claim. Rebuild
the app and native installer, run actual-bundle install/rerun tests, and measure
the user's usb1:/Decrypted scan on hardware before claiming a timing improvement.

Wii VC authored-title handling: NEVER automatically convert an existing decrypted
Wii VC title to WBFS on Play. The user requires the original HAI wrapper and
controller configuration. The previous conversion fallback did not meet that
requirement. Installed titles retain their direct system launch. Loose Wii VC
titles currently stop with an explicit unsupported-backend message before any
conversion, return request or raw launch. ValidateRawGame independently rejects
HAI markers, preventing a caller from bypassing that restriction.

Direct HAI launch is still incomplete. The captured failure is a block-address
request for `/vol/content/hif_000000.nfs` returning -6, followed by fatal 160-0791.
Do not infer that WUP installation or conversion is fundamentally required: HAI
has a USB extent-map interface, but our ordinary content redirection does not
implement it. See `wii-vc-hai-investigation.md` for evidence and remaining work.
Run test-wii-vc-cancel (converter utility plus no-conversion launch guard),
test-usb-raw (including independent HAI validation), and rebuild/test the native
installer. A safe rejection is not evidence that GamePad-capable launch works.

Freedom Planet Event Preview exit investigation (2026-09-21): the reproduced
USB-game hang reaches every raw-loader cleanup checkpoint, including USB unmount,
but never reaches Menu application-start or consumes the return request. Loader
raw-port11 adds raw-session-latched OSReport checkpoints around ProcUIShutdown,
coreinit _Exit, module-end completion and the all-modules exit boundaries. The
hooks are game-process-only and forward calls, arguments and statuses unchanged.
Do not force termination, skip save flushing or alter USB/MCP routing based only
on the generic Menu icon. These diagnostics are NOT a confirmed hang fix.
Run tools/test-raw-exit-trace.ps1, tools/test-return-hook.ps1 and the USB raw suite;
rebuild/test the native installer. Deploy the module and installer to the verified
SD, then fully reboot Aroma and capture live syslog while reproducing the exit.
Leave the hardware-confirmed bounded-path Mocha payload unchanged. Once the
blocking boundary is identified, implement and hardware-test the actual fix.

Port11 hardware result: ProcUIShutdown returns twice, coreinit _Exit is entered,
and module-end-return is logged. all-module-end-callbacks-done is never reached.
The loaded module order puts WiiUPluginLoaderBackend after RPXLoadingModule.
The next diagnostic build adds per-plugin enter/return logs for APPLICATION_ENDS,
socket/devoptab finalization, and acquired-RPL release in PluginBackend 0.4.1.
It does not disable plugins, skip callbacks, change their order or force exit.
Reproduce the patch with tools/prepare-plugin-exit-trace.py against upstream
revision e3e67f8a8ebee77b351a43ff79561ecb6955dea6 (v0.4.1); --apply refuses any
non-stock/non-patched source. Without --apply it checks exact patch equivalence.
Build using that revision's Dockerfile and make -j4. The tested diagnostic binary
is hash-pinned by sync-native-payload.py, installed as Core in replace mode, and
covered by the native path allowlist and actual-bundle replacement/rerun tests.
Keep the port11 RPX loader and current Mocha unchanged for this isolated test.
This is still diagnostic: obtain the plugin-specific last checkpoint on hardware
before claiming the underlying exit hang is resolved.

Next hardware capture (plugin tracing enabled) stops inside raw-loader teardown,
before module-end-return or backend-end-enter. Plugin blame is not established;
the observed stopping boundary varies. Port12 removes five late SD log appends
from APPLICATION_ENDS and prevents profile-clear logging from opening the SD.
Those checkpoints now use OSReport. Real layer/file/device/profile cleanup and
its ordering are preserved; game saves are not skipped. Earlier callbacks already
stop ContentRedirection workers, so adding fresh diagnostic file I/O here is
unnecessary and prolongs the sensitive exit interval. Run
tools/test-raw-exit-cleanup.ps1 (actual callback and profile logging extracted and
compiled with ASan/UBSan), exit-trace/return-hook tests and USB raw regressions.
Rebuild/test the installer and deploy only the port12 loader plus installer.
Keep the plugin tracing backend for the next capture. This removes late logging;
whether it resolves the console hang still requires the user's hardware test.

Port12 capture identifies the blocking callback: raw-loader cleanup returns,
then Aroma Base and Homebrew on Menu end callbacks return, but FTPiiU's end
callback does not. FTPiiU 0.4.5 early-exit candidate stops/joins its server and
unmounts its paths on APPLICATION_REQUESTS_EXIT while the runtime is alive.
APPLICATION_ENDS retains its normal idempotent fallback; no worker is forcibly
terminated and no save flushing is skipped. OSReport marks join/unmount phases.
This identifies the callback, not yet the exact internal blocking operation;
the early-exit candidate still needs a full-reboot hardware test.

Reproduce with tools/prepare-ftpiiu-exit.py --apply against FTPiiU upstream
c2fc7310526124a9d63a8c28feaaca9356d504a6 (v0.4.5), initialize its pinned GSL
submodule, build its Dockerfile, then make -j4 in that image. Run
tools/test-ftpiiu-exit.ps1 for actual lifecycle extraction under ASan/UBSan.
Payload sync pins the tested binary. Setup replaces ftpiiu.wps only when that
exact plugin path already exists: fresh cards without FTP stay without FTP.
No FTP configuration is installed/overwritten; disabled service remains disabled.
Native host tests cover present/absent FTP, preserved configuration, real payload
replacement and idempotent rerun. Rebuild the installer and deploy only the FTP
plugin and refreshed installer; retain port12, tracing backend and current Mocha.

Hardware result: FTPiiU's early callback joined its server and returned in 18 ms,
but the same title still stalled after RPXLoadingModule entered its final callback
and before its first cleanup checkpoint. Port13 therefore performs the module's
existing cleanup sequence during APPLICATION_REQUESTS_EXIT, before FTPiiU and
before final filesystem teardown: remove bundle layers, close readers, remove the
ufusb device, unmount its read-only disk and stop logging. An atomic one-shot guard
makes APPLICATION_ENDS an idempotent fallback for exits without the early request.
It does not omit any prior cleanup, terminate a worker, alter saves, or change the
hardware-confirmed Mocha payload. The bundle unmount now reports mutex-entry and
acquisition boundaries. Run test-raw-exit-cleanup, exit-trace, return-hook and the
USB raw suite, rebuild the module/installer, and fully reboot before hardware test.
This remains a candidate until the Event Preview title returns successfully.

Port13 hardware result: every early uFlow and FTP cleanup checkpoint returned,
then Core2 immediately fetched instruction address 0 with LR 0 after ProcUI's
Exit message. This is a crash, even though the visible Menu icon resembles the
earlier hang. The title's other threads were still alive, so removing uFlow's
redirection/device at APPLICATION_REQUESTS_EXIT was unsafe. Port14 restores the
full uFlow cleanup to APPLICATION_ENDS. FTPiiU exit2 performs only server.reset
(worker join) during the early request, retaining its Mocha/devoptab mounts and
IOAbstraction state. Final ordering is therefore: join FTP worker early; uFlow
removes bundle layers/readers/ufusb during its earlier module-end callback; then
FTPiiU's later plugin-end callback releases its mounts. Its final stop remains a
fallback if no exit request was delivered. Host lifecycle tests assert this exact
split and settings preservation. Rebuild both FTPiiU and RPXLoadingModule, pin
the new FTP binary, rebuild/test the native installer, and fully reboot for the
Event Preview retest. Do not deploy or restore the crashing port13 module.

Native NV12 DMA upload (2026-10-19): Rock Zombie is smooth per the user after
native-plane deployment; Rock 'N Racing Off Road still stutters. Live counters
show about 36 ms/upload. Use one bounded DMA transfer per matching-pitch plane,
with source/destination cache maintenance and completion before releasing the
frame. Mismatched pitch/alignment retains row copies. Do not read last-row
padding. Source videos and configuration are unchanged. The backend source
is hash-pinned in VIDEO_NV12_INPUTS and ships through the Core app payload.
ASan/UBSan tests cover exact source bounds, padding, DMA completion, unaligned
fallback, allocation failure, surface locking and foreground suspension.
Build/deployment and installer verification evidence: artifacts/video-nv12-dma.
Console playback speed of this DMA change still requires a hardware retest.
Deployment complete: staged bytes matched SHA-256
773239ea969c30024c52064f895569966190ff903018dc8d83177c94a66d9056;
app replaced after closed-state checks, configuration unchanged. Backend
ASan/UBSan, 21 payload tests and actual Core/Core+WiiU fresh/reinstall checks
passed. New renderer dependencies are now included in payload test fixtures.

Decoder CPU accounting diagnostic (2026-10-20): DMA upload is confirmed around
3.5 ms/frame on the console, but Rock N Racing still spends about 22 ms per
native decode and accumulates audio starvation/video lag. Add five-second
worker CPU-accounting logs beside wall decode time to distinguish execution
from UVD waits. This does not change playback policy, priorities, system modules
or source media. Native system decoder binaries were read for local inspection
only; no system files are changed or distributed. Evidence and build/test logs:
artifacts/video-decode-budget. This diagnostic is not a playback fix.

The initial thread-structure accounting counter remained zero on hardware.
Corrected the diagnostic to use coreinit's exported OSGetThreadCoreTime(thread,
coreMask=7), returning scheduler ticks; convert through OSTicksToMilliseconds.
The symbol is present in the pinned libwut and console coreinit. The ABI was
checked against the console implementation. Do not interpret the earlier zero
counter as evidence that decoding consumes no CPU.

Corrected diagnostic hardware result (2026-10-20): title 0005000010148200
(Rock N Racing Off Road) confirmed by live videostats. Sustained 5-second
windows show 667-929 ms of decoder-worker CPU, 4919-4956 ms wall decode,
and about 190-217 returned frames. Example: wall 5014 ms, CPU 667 ms,
decode 4946 ms, 217 frames. Thus most elapsed decoder time is off CPU;
this does not by itself distinguish hardware execution from scheduling or
completion-event latency. Local UVD inspection shows synchronous completion
through OSWaitEvent. Do not claim a definitive silicon throughput ceiling.
The shared audio/video decode loop starves audio while video falls behind:
live snapshot shows 779894 missing audio frames and 7683 ms lag, while GPU
upload averages 3.54 ms. No new playback fix is claimed from this diagnostic.
Corrected app deployed SHA-256:
f0fea197bb844df56282decb0a1cbb0050f8261f6aaed5938ded8cdf1ae5230c.
Native installer rebuild completed; actual Core fresh/reinstall tests passed
(37 files, 38502496 bytes, existing settings preserved). The next direct-playback
investigation must distinguish UVD service time from completion/scheduling
latency; merely increasing reader buffering or changing CPU conversion cannot
explain or remove the measured synchronous wait. User rejects conversion copies.

Decoder wake scheduling trial (2026-10-20): the video worker now requests
WUT user priority 14 (native 78), ahead of rendering (80) and behind SDL audio
(77). No core affinity, system-thread priorities, media or boot configuration
changes. Authenticated live commands videowakeon/videowakeoff allow paired
measurements without replacing the app. This targets possible scheduling
latency after UVD completion; improvement is not established until measured on
Rock N Racing. CPU versus elapsed timing alone cannot prove hardware service
time. Host playback checks passed. Build/installer/deployment evidence is in
artifacts/video-wake-priority. Carry app changes through the Core installer;
preserve existing user configuration on reinstall.

Hardware A/B result: the deployed scheduling trial (SHA-256
64f125d807afafce0b11d6725efeebd82fe4dc18058a5843fc862bbf01bed655)
did not resolve stutter. Same-title native execute means were 22.05 ms (on),
22.13 ms (off), and 21.81 ms (on); aggregate decode means 23.87/23.97/23.47 ms.
Live switch restored normal scheduling after the comparison. Default in this
trial binary remains enabled on restart; do not describe it as a proven fix.
Bitstream inspection, without transcoding, counted 2343 slice NALs in the base
clip: 4 IDR, 1829 reference non-IDR, 510 non-reference. At its 39-second duration,
about 47 reference frames/second must be decoded even when non-reference
pictures are skipped. The existing platform skip check examines nal_ref_idc;
there is no missed class of safely skippable reference pictures identified.
H264DECExecute rejects null output buffers; no documented decode-without-output
mode is available in the inspected API. Removing its completion wait would
expose unfinished decoder state/buffers, not constitute a valid fix.
Native installer build and actual Core fresh/reinstall tests completed and
passed; SD app hash/readback/settings preservation verified before ejection.
Direct playback remains unresolved. Do not report this trial as a fix.

Direct NV12 surface import (2026-10-20): avoid the remaining full-frame DMA
copies by borrowing compatible decoder planes as GPU textures. The texture
holds an AVFrame lease until its last GPU-use fence. Existing owned surfaces
are freed upon first successful import; decoder storage replaces them instead
of adding three permanent full-frame textures. Alignment, complete padded
image sizes, pitch and foreground are checked before ownership transfer.
Rebinding flushes queued SDL commands and fences prior use; failed import keeps
ownership with caller and falls back to the existing DMA upload. Copy fallback
allocates both planes before releasing a previous import. Destruction releases
the decoder lease rather than freeing borrowed memory through GX2R.
Priority experiment defaults back to normal scheduling; measured benefit was
negligible. B-frame pruning was rejected: retained I/P-frame hashes differed
for both Rock N Racing clips when reference B-frames were omitted. No B-frame
shortcut, modified video copy, boot change or system-driver patch is shipped.
Backend ASan/UBSan checks cover imported-buffer lifetime, GPU fences, size and
alignment guards, foreground rejection and fallback-allocation failure. Evidence:
artifacts/video-plane-import. Console speed improvement remains unverified.

Hardware verification of direct import: active=1; texture upload averaged about
0.03 ms/frame versus roughly 3.5 ms before. Rock N Racing native execution
remained about 22 ms/frame and playback accumulated lag; do not claim smooth
playback from this optimization. Native Setup build and actual Core fresh/
reinstall checks passed for the direct-import app.

Audio during decoder waits (2026-10-20): decode audio on the packet-reader
thread, using a separate AVFrame and the existing bounded two-second PCM ring.
The video worker retains exclusive ownership of the video codec and reorder
queue. EOF joins the reader before draining/flushing audio or seeking; stop
interrupts audio backpressure and joins before callback captures are destroyed.
No extra decoded-video buffering or media conversion is introduced. Host test
injects repeated 400 ms decoder waits: serialized audio missed 6145 frames,
parallel audio missed zero. Existing loop, EOF, pause, rapid cancellation,
NV12 lease and slow-read tests passed. This addresses audio interruptions from
brief decoder stalls; sustained native-video throughput remains unresolved.
Evidence: artifacts/video-plane-import/audio-*.log. Core payload carries the
change with existing user settings preserved.

GPU-only cache handling and shared pool locking (2026-10-20): the native H264
adapter defaults to CPU-coherent output. uFlow explicitly opts in to GPU-only
output when native NV12 rendering is enabled, avoiding its redundant full-frame
CPU invalidation. CPU conversion and texture-upload fallbacks invalidate before
reading. Hardware completion waits and GPU texture invalidation remain intact.
Adapter ASan/UBSan tests verify the default, opt-in and return to CPU output.

The pinned FFmpeg configuration has HAVE_THREADS=0, so AVBufferPool's upstream
ff_mutex calls are no-ops. Cross-thread converter/GPU release can race decoder
pool acquisition. Patch only the buffer-pool mutex to coreinit OSMutex; do not
enable codec-internal threads or change global once initialization. Pool init
precedes publication; existing atomic lease counts keep the pool alive through
GPU retirement after decoder close. A host test compiles the actual patched
buffer.c with pthread-backed coreinit mutex stubs: 80000 concurrent leases,
pixel exclusivity, shared references and last release after pool teardown pass
under ASan/UBSan. This is a verified locking defect, not proof of the cause of
any earlier captured crash. Builder applies the pinned patch and header; both
are tracked by payload dependency hashes. Evidence: artifacts/video-plane-import
(gpu-cache-*, pool-tests.log, gpu-payload-tests.log). Console throughput still
requires measurement; do not describe hardware waits as eliminated.

Final deployed SHA-256 a2e4ed768696fa0e3fc6ea6214f212dde2723e4df11046ff6cdb4e183f38524f
(28951144 bytes), FTP readback verified at .116, settings unchanged. Native
Setup build and actual Core/WiiU fresh/reinstall checks passed (37 files,
38505840 bytes). Console retest: user reports "still stutters". 20 captured
samples show native 21.72 ms/frame, aggregate decode 22.91 ms/frame, roughly
41.15 decoded frames/sec including stalls/loops, and up to 7643 ms lag. Upload
is about 0.020 ms/presented frame. Audio underflow remains during sustained
under-throughput. First five seconds also show 4319.8 ms of packet-read time;
later windows have about 1.6-2.0 seconds read time overlapping 4.8 seconds
native execution. Host stall-test success does not establish smooth console
playback. Direct playback for this clip remains unresolved; no converted media,
reference-frame pruning, hardware-clock change or boot change was applied.
Detailed result: artifacts/video-gpu-cache-pool/result.json and stats.jsonl.

Video contention check (2026-10-20): pause library artwork prefetch and GPU
uploads while foreground/showcase video fully obscures the library. Normal
artwork work resumes on the first uncovered render; selected media and settings
are preserved. Authenticated videoisolateon/off draws only the current video
for an A/B measurement, preserving decoder/audio/timestamps. It expires after
45 seconds and cancels on overlays or stopped playback; no setting is saved.
This test separates UI/GPU contention from native decoding before further
changes. No video conversion or hardware-clock changes. Evidence and packaging
checks belong in artifacts/video-isolate; do not claim a console fix before
measurement. Core app carries the same code into fresh native installations.

Settled showcase optimization (2026-10-20): the four-phase comparison measured
native execution at 21.92/21.91 ms per frame with the showcase, versus
19.93/19.95 ms with video alone. Both still had audio starvation and lag.
Reuse the existing bounded 26 MiB case cache for the selected case only after
its animation settles; include selected lighting in its cache key. Reuse the
glass blur only for an unchanged video frame in the settled showcase. Gate
the earlier artwork-prefetch call as well as the late one behind full video.
No video files, decoder clocks, or queue memory limits change. Host checks
compare fresh/cached selected-case pixels and require repeated frames to issue
no mesh draws. Console playback is unverified until this build is measured.
Evidence: artifacts/video-isolate and artifacts/video-showcase-cache.
