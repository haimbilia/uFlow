#pragma once

#include <cstdint>
#include <rpxloader/rpxloader.h>

#ifdef __cplusplus
extern "C" {
#endif

// Launches a RetroArch core (.rpx under retroarch/cores) with RetroArch's own
// .wuhb supplying /vol/content, passing argumentBytes to the new process as
// native SYS arguments: exactly two NUL-terminated strings, argv[0] the core and
// argv[1] the content. On any synchronous failure the prepared launch is
// reverted and uFlow's pending loader state is restored.
RPXLoaderStatus RL_LaunchHomebrewWithArgsAndContent(const char *coreRelativePath,
                                                    const char *contentBundleRelativePath,
                                                    const char *argumentBytes,
                                                    uint32_t argumentByteCount);

#ifdef __cplusplus
}

namespace RetroArchLaunch {
bool SessionActive();
void ClearSession();
// Returns true when a same-wrapper relaunch is RetroArch switching cores with
// native arguments, in which case the caller must not re-prepare the old RPX.
bool AdoptCoreSwitch(void *standardArgs);
} // namespace RetroArchLaunch
#endif
