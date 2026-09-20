# Loadiine-derived raw launch work

Reference: [Loadiine GX2](https://github.com/dimok789/loadiine_gx2), commit
`2ae7fe4feaad633696149edcbcf3caa7f1eaeb1a`.
The adapted kernel code retains GPL-3.0-or-later attribution; see `LICENSE`.

Implemented locally:

- `LoadiineTitleInfo.h` follows Loadiine's PPC `CosAppXmlInfo` layout and field
  assignments. Offset 0x14 is **app type**, not the IOSU MCP group ID.
- Opaque kernel words are preserved instead of writing MCP heap/permission
  fields into them. Arguments retain Loadiine's NUL-separated representation.
- PrepareTitle reads/writes use physical copying through Aroma KernelModule,
  matching Loadiine's physical-copy approach instead of old kern_read/write.
- Trampoline additionally preserves CR, CTR, XER, and r0 around the C helper.
- Host regression test verifies layout, app type, argument splitting, and
  preservation of opaque words. Wii U module cross-compiles; disassembly of
  the kernel helper has no calls into userland string/memory routines.

Port1 is deployed as an experimental backend; hardware validation pending.

Remaining integration checks before deploying:

1. Verify runtime hook/data addresses. The helper now lives in `../kernel/`,
   an early WUMS module allocated in the low persistent heap, and temporarily
   mirrors IBAT4 into DBAT5 as in Loadiine. Direct low pointers replace the
   previous unverified C000 aliases. The high-memory RPX module uses a client.
2. Audit RPX/RPL replacement against Loadiine's imported-RPL staging and later
   dynamic loads. Current module requires paired custom Mocha ioctl 0xF4;
   stock Aroma lacks it. Port1 deploys the matched helper/module/payload set.
3. Verify title identity transition, redirection setup timing, account saves,
   and profile/hook teardown on return. Preserve the working SDL exit path.
4. Test FAST Racing NEO base-game startup and return on hardware. Matching
   update metadata in the library is not an update-content overlay.

Upstream reference paths: `src/common/kernel_defs.h`,
`src/kernel/kernel_functions.c`, `src/kernel/kernel_hooks.S`,
`src/kernel/syscalls.c`, `src/game/GameLauncher.cpp`,
`src/patcher/rplrpx_patcher.cpp` and pinned `src/dynamic_libs/os_functions.c`.
