# Update handoff

Aroma's loader mounts the running WUHB for content access and holds its file open.
The foreground app must not rename that file. It downloads and verifies the new
app, copies this bundled bare RPX to its update cache and writes a signed `ready`
request. It then launches the RPX through RPXLoadingModule, leaving the normal
title-exit sequence to release the old WUHB mount.

Only this separate process performs the recoverable rename transaction. It
rechecks the signature and downloaded hash, installs to the fixed uFlow path,
consumes the request and launches the new app. Startup reconciles the journal
and shows release notes. Failed helper operations include paths and errno and
are recorded in `cache/update/helper-error.txt`; no ROM/config paths are targets.

`app/Makefile` builds this helper and embeds its RPX automatically. Native payload
sync therefore includes it in every app/installer update, not as a separately
installed public menu entry. Aroma deliberately remounts the previous WUHB when
launching a bare RPX for hot-reloading support. The helper therefore calls
`RPXLoader_UnmountCurrentRunningBundle` before any recovery or file replacement.
Detaching only by exiting the previous process is insufficient. Its embedded
font remains usable without the inherited content mount. Detach failure stops
installation before it touches the target.

Host tests cover signed handoff creation/consumption, tampering, unchanged active
app before handoff and journal recovery. Actual Aroma title transitions and
power-cut recovery still require hardware tests.
