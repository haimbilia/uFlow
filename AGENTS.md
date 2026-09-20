# uFlow project requirements

- Every shipped feature and fix must survive a fresh installation, per the user.
  Update and test the native Wii U installer, not only the currently inserted SD.
  Follow `docs/native-installer-release.md`; keep new patched dependencies in
  `tools/sync-native-payload.py` and cover installer defaults/component selection.
- Preserve user settings, ROMs and saves on reinstall. Do not wipe a card or run
  console setup just because an installer update is requested. Verify the actual
  SD identity before writes; drive letters can change.
- Do not restore old builds or create unsolicited backups. Preserve unrelated
  worktree edits. Hardware test status must be distinguished from host test success.
