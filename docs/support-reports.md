# About & Support reports

Settings has a small `i` button at the bottom left. It opens About & Support,
including the build, karotech.io and diagnostic actions. On a controller,
press Down from Launchers to focus it, then A. Touch opens it directly. Existing
unsaved directory/content-type changes still require discard confirmation.

## User flow

1. Start problem recording, return to the library and reproduce the issue.
2. Return to About & Support and choose Send report.
3. Review the disclosure, then choose Yes (No is selected by default).
4. Share the returned `UF-...` ID and explain what happened.

The confirmed action creates a fresh local report before uploading. If sending
fails, the message shows `SD:/wiiu/apps/uFlow/support-report.json`; send that file
from a PC. If local saving fails, no upload is attempted.
The site administrator can import that file into the same inbox. The local
report remains after a failed upload. Another confirmed Send replaces the local
report before retrying; nothing is sent automatically. Recording starts off each time uFlow starts.

Recording is a bounded list of 64 app-status and scan-stage events, flushed in
the background about every two seconds to `support-session.log`. After a crash,
restart and send a report **before starting another recording** to include the
previous session. The last unflushed events may be missing. It is not a complete
memory dump or an IOSU log stream, and does not guarantee a cause for every crash.

## Contents and limits

- Build sequence, library/game/media counts, numeric app settings.
- Configured source types, launcher IDs, enabled/depth settings; directory paths
  are replaced with SD/FAT32 USB/omitted markers. Custom display names are omitted.
- SHA-256 of the installed RPX loader, Mocha and ContentRedirection modules.
- Bounded tails of uflow.log, installed-scan.log, usb-storage.log, wii-launch.log
  and support-session.log, when available. Missing files are explicitly marked.
- Path/address-bearing log lines are omitted. Remaining logs can name games.

No directory-wide collection, ROMs, saves, tickets, keys, account files or play
history. Collection and transfer run on a worker thread. The report has a 256 KiB
limit. HTTPS uses the bundled CA certificate, hostname verification, no redirects,
and a 30-second transfer timeout. There is no diagnostic network listener or
remote command execution. The security release's IOSU service removal remains.

## Private inbox

WordPress: Karo → uFlow Reports, admin capability `manage_options`.
Supports report-ID search, New/Investigating/Resolved, last event, JSON download,
deletion and importing an SD export. Reports live in a non-public private post
type, not in public uploads. All displayed data is escaped and modifying/downloading
admin actions require nonces and capabilities.

The anonymous POST-only endpoint is `/wp-json/karo-uflow/v1/reports`. It accepts
only schema 1 and allowlisted fields/log names. Limits: six reports per peer IP
per hour, 100 daily globally, 2,000 retained reports. Peer addresses are HMACed
for temporary rate counters, not included in reports. Normal server access logs
still apply. Daily WP-Cron expires reports older than 90 days; keep WP-Cron running.
The WordPress plugin source is in the Karotech Web repository under
`app/public/wp-content/plugins/karo-uflow-support/`.

## Validation / release status

Host collector tests cover redaction, allowlists, bounded recording, offline
failure, replacement and write failure. UI tests cover all four skins,
touch/controller access, default No consent and dirty-editor protection. Actual
native Core and Core+Wii U install/reinstall tests preserve diagnostic files,
settings, games and saves. Fresh installs start with recording disabled.

Live endpoint/admin tests cover schema, size, content type, private storage,
escaped content, exact JSON round-trip, search/status and expiry. Synthetic
reports used for verification are removed. App and Setup are built locally;
no new public release/version bump. The first iteration was deployed to 192.168.1.116 with full FTP readback.
The combined Send action and simplified About/Yes/No labels passed host tests
and were deployed app-only to .116 with full FTP readback verification.
SHA-256: ca164da2c3be4e46708af362953b7592a7bcefc77eba883d5f7e00df3f46197c.
Setup stays local. Console upload and reproduction remain pending hardware
verification.
