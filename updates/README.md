# Karotech update service

Endpoint: `https://karotech.io/updates/uFlow/`

Server directory: `/var/www/karotech.io/updates/uFlow`

The client checks the stable channel once on startup, without blocking browsing.
When a newer signed release exists, it waits until other dialogs close and offers
Yes/No (No is selected initially). Yes shows download/install progress, followed
by version changes. No dismisses the offer until the next startup. Offline checks
are silent. A bundled helper installs after uFlow closes, then reopens the new app.

Version 1 updates **only** `wiiu/apps/uFlow/uFlow.wuhb`. Module/backend updates
remain installer-managed; the client rejects multi-component manifests. It never
updates ROMs, VC bases, settings, saves or other console files. SSH access
as `root@82.25.101.144` passed strict verification against the existing local
known-host entry. The domain's SSH entry differs; do not overwrite or bypass it.
`index.html` and `status.json` are deployed and their HTTPS readback was verified.
The existing Caddy configuration already serves `/updates/*` as static files;
no global web-server settings were changed.

## Layout

```
uFlow/
  stable.json           # Signed envelope for the current stable release
  testing.json          # Signed envelope for the current test release
  releases/<release>/
    manifest.json       # Same signed envelope, immutable
    files/<sha256>      # Individual software components
    NOTICES.txt         # Licenses and matching source references
    public-key.txt      # Informational; never establishes client trust
```

Serve manifests with `Cache-Control: no-cache`, and immutable release files with
`Cache-Control: public, max-age=31536000, immutable`. Disable directory listing.
No database or PHP handler is needed. Upload into an unpublished staging directory,
verify remote hashes, then rename it into `releases/<release>`. Atomically rename
the channel envelope last. Never overwrite an existing immutable release.

## Publisher preparation

`tools/build-update-release.py` builds the signed envelope and content-addressed
files from a separately prepared software artifact directory. It requires Python
and `cryptography`. Keep the Ed25519 private signing key outside the project,
artifact tree and web root; never upload it. Provision its public key in the
client build. A channel switch does not change the trust key.

The personal installer must not be uploaded: it embeds Nintendo VC bases.
The current personal app also embeds a background video credited to Imora;
prepare a distributable app build with appropriately licensed background assets
before public release. The package builder restricts destinations to software
components, but cannot determine rights to embedded binary assets. Review those
and include dependency licenses and matching source before publication.

The client verifies Ed25519 signatures before parsing trusted manifest
fields, enforce the destination allowlist, HTTPS host and path, maximum lengths,
component sizes and sequence, and verify SHA-256 downloads. Update installed
optional components only; WiiFlow and its three booters form a dependency group.
Never include ROMs, VC bases, saves or settings. Stage all changed files first,
then use a recoverable commit journal before replacing any installed software.
Reboot after updating Aroma modules. Do not advance the installed release marker
until the whole selected transaction is complete.

## Tests and recovery

`tools/test-updater.sh` exercises the production parser and file transaction:
wrong key/channel, tampering, invalid paths, corrupt downloads, and interruptions
at each commit boundary. `tests/test_update_release.py` checks publishing inputs.
The host renderer's `updatechecks` covers the actual dialogs and input handling.
These are host tests, not proof of successful Wii U networking or power-cut recovery.

The update is downloaded under `wiiu/apps/uFlow/cache/update`, verified, then
handed to a separate bare-RPX helper. Aroma holds the active WUHB open; replacing
it inside the original process fails. The helper starts after that process exits
and replaces the app using a journal. The previous executable temporarily remains as
`wiiu/apps/uFlow-Recovery.wuhb`. If power is lost between file renames and uFlow
is missing, launch this recovery entry from the Wii U Menu. Startup reconciles
the transaction before checking for another update. Do not delete this entry or
the pending journal during recovery. Normal startup removes the recovery file
after validating the committed update.

Build with an increasing `UFLOW_UPDATE_SEQUENCE` (the default is recorded in
`update_model.hpp`). Sign the software-only release with
`tools/build-update-release.py`, passing a short UTF-8 `--changes` file. Publish
using `tools/publish-update-release.py <release-directory> --identity <ssh-key>`;
it verifies upload hashes and promotes the channel manifest last. Private signing
keys never leave the PC. Do not publish an older sequence to an existing channel.

For the corrected console test, the SD receives a helper-enabled bootstrap at
sequence 2026092001 and the server offers 2026092002 (`2026.09.20-test2`). The
native installer contains the latest app with the helper embedded. The first
test release exposed the running-WUHB rename failure; it was not a successful
hardware update. Do not deploy the original no-helper bootstrap again.
