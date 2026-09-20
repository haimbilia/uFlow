# Launcher selection and testing

DS workflow update: see [Saved injections](SAVED-INJECTIONS.md). Preparation is now an explicit one-time GAME action. The older on-demand DS test steps below describe the previous build; use the saved-injection test instead. Left/right now switches details panes, and edits a selector only after A.

Launch-time priority: **game override → matching directory default → content-type default → legacy native/Wii adapter**. No rescan is required. The deepest enabled matching directory in the same content type wins; paths are matched at folder boundaries. Installed titles do not inherit a raw-game directory setting.

## Controls

- Settings → Library → select a directory → Launcher: choose an enabled compatible launcher or inherit the type default, then Save. Press A to edit selectors, left/right to change, B to finish editing.
- Title details → GAME → A: left/right selects the game-specific launcher, automatically saved. A or B finishes editing; up/down leaves editing and navigates the rail. Touch the left/right half of the launcher button to change it.
- Use default removes the game override. PLAY shows the resolved launcher. GAME right, outside editing, retains the synopsis pages.
- Missing/disabled/incompatible explicit selections produce an error, not silent fallback to another emulator.

Per-game preferences are stored centrally in `sd:/wiiu/apps/uFlow/game_launchers.cfg`, keyed by file path or installed title ID. Moving a ROM changes its key. Directory choices are the optional seventh field of `content_sources.cfg`; older configurations remain readable. Original ROMs and emulator save files are not modified by these settings.

## DS test

1. Reopen uFlow after updating. Set the DS directory launcher to NooDS and Save.
2. Open one DS title's details → GAME → A → select Nintendo DS - On-demand VC → A. PLAY should show that choice; another DS title should still show NooDS.
3. Restart uFlow and verify both selections remain correct.
4. Launch the overridden title. First preparation can take time and shows progress; B cancels preparation. A successful cached launch reuses verified output.
5. Return to GAME and choose Use default. PLAY should now show NooDS again.

On-demand VC is experimental; successful PC preparation is not proof of console compatibility. Configure its decrypted base in Settings → Launchers. The supplied test profile uses `sd:/wiiu/vc-bases/ds/nsmb-usa`, preserving the base's display settings, fast compression and a 1 GiB cache budget.

VC cache: `sd:/wiiu/apps/uFlow/vc-cache/ds/`. VC saves: `sd:/wiiu/vc-saves/ds/<ROM hash>/<base title ID>/`. Saves are separate from NooDS saves; switching methods does not convert save formats. The cache budget stops new preparation when full; it does not delete old games or saves automatically.
