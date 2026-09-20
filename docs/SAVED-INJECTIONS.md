# One-time injections

In Title details → GAME, right focuses the main panel. Up/down selects a row; left returns to the rail. Press A to edit Launcher, then left/right changes its value. A/B ends editing. Touch selects the same controls.

Before preparation, **Inject ROM** is available for DS games with the injector module enabled. No saved-injection launcher choice is offered yet. A on Inject ROM starts preparation only; it does not launch the game.

After success, the output directory and raw-launch paths are recorded persistently. The game automatically selects **Saved injected ROM**. PLAY launches that output directly, without rebuilding or needing the base. **Re-inject ROM** explicitly creates a replacement using current injection settings. The previous saved output stays selected until preparation and record commit succeed. Existing saves are not deleted; identical ROM/base title ID retains its save namespace.

NooDS remains selectable. Use default restores directory/type inheritance. Changing injector settings does not silently rebuild anything. Keep the source ROM in the scanned library so its entry remains discoverable; moving it changes the mapping key.

Records: `sd:/wiiu/apps/uFlow/injections/ds/`. Output remains under `sd:/wiiu/apps/uFlow/vc-cache/ds/`; despite the legacy folder name these saved copies are not automatically evicted. Saves remain under `sd:/wiiu/vc-saves/ds/`. Do not remove output folders that you still launch. Old replacements are retained for recovery and count toward the storage budget.

The old generic prepared-injection mapping options are no longer offered. Already installed or raw Wii U injections launch through the normal Wii U library.

Test: inject one game → confirm Saved injected ROM and Re-inject ROM appear → close/reopen uFlow → PLAY without preparation → switch to NooDS and back → re-inject, verifying the replacement is selected and saves remain intact. Console compatibility still needs hardware testing.
