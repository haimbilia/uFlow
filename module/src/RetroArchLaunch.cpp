#include "RetroArchLaunch.h"

#include "globals.h"
#include "utils/logger.h"

#include "utils/ini.h"

#include <coreinit/cache.h>
#include <coreinit/memorymap.h>
#include <coreinit/memory.h>
#include <coreinit/title.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <dirent.h>
#include <mocha/mocha.h>
#include <algorithm>
#include <string>
#include <sys/stat.h>
#include <sysapp/args.h>
#include <sysapp/title.h>
#include <vector>
#include <wuhb_utils/utils.h>
#include <wums.h>

// RetroArch's Aroma port takes its ROM from native SYS arguments
// (frontend_wiiu_exec -> Mocha_LaunchHomebrewWrapperEx -> SYSDeserializeSysArgs).
// A core is a separately linked RetroArch executable under retroarch/cores, and
// its assets live in the RetroArch .wuhb, so the launch has to combine a bare
// RPX executable with a different bundle as the content provider. Preparing the
// RPX alone would inherit uFlow's own bundle and leave RetroArch without assets.

RPXLoaderStatus RL_PrepareLaunchFromSD(const char *bundle_path);

namespace {
// RetroArch's own default core folder; a core switch inside RetroArch may pick
// from here even when uFlow launched a core the user keeps elsewhere.
constexpr const char *kCoreRoot     = "retroarch/cores/";
constexpr const char *kVolumePrefix = "/vol/external01/";
constexpr const char *kCafePrefix   = "fs:/vol/external01/";
constexpr uint32_t kMaxArgumentBytes = 2048;

// What the same-wrapper relaunch hook may act on. Paths are copied; argument
// pointers from another process are never retained.
struct Session {
    bool active = false;
    char provider[sizeof(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath)] {};
    char core[0x280] {};
    char coreRoot[0x280] {}; // folder of the launched core, e.g. "retroarch/cores/"
};
Session sSession;

void Trace(const char *format, ...) {
    char line[320];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (FILE *file = fopen("fs:/vol/external01/wiiu/raw-launcher.log", "a")) {
        fprintf(file, "%s\n", line);
        fclose(file);
    }
    OSReport("uFlow retroarch: %s\n", line);
}

bool SafeRelativePath(const char *path, size_t limit) {
    if (path == nullptr) return false;
    const size_t length = strnlen(path, limit + 1);
    if (length == 0 || length > limit) return false;
    if (path[0] == '/' || strchr(path, ':') != nullptr || strchr(path, '\\') != nullptr) return false;
    for (size_t i = 0; i < length; ++i)
        if (static_cast<unsigned char>(path[i]) < 0x20 || static_cast<unsigned char>(path[i]) == 0x7F) return false;
    const std::string value(path, length);
    if (value.find("//") != std::string::npos) return false;
    return value != ".." && value.find("../") == std::string::npos && value.find("/..") == std::string::npos;
}

bool HasSuffix(const std::string &value, const char *suffix) {
    const std::string tail(suffix);
    return value.size() > tail.size() &&
           std::equal(tail.begin(), tail.end(), value.end() - tail.size(),
                      [](unsigned char a, unsigned char b) { return a == std::tolower(b); });
}

bool IsRegularFile(const std::string &sdRelative) {
    struct stat info {};
    return stat((std::string(kCafePrefix) + sdRelative).c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

// A core the user chose: an .rpx anywhere on the SD, present and short enough
// for MochaRPXLoadInfo::path.
bool ValidCorePath(const char *path) {
    if (!SafeRelativePath(path, sizeof(MochaRPXLoadInfo::path) - 1)) return false;
    const std::string value(path);
    return HasSuffix(value, ".rpx") && IsRegularFile(value);
}

// RetroArch's own bundle, wherever the user keeps it.
bool ValidProviderPath(const char *path) {
    if (!SafeRelativePath(path, sizeof(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath) - strlen(kVolumePrefix) - 1)) return false;
    const std::string value(path);
    if (!HasSuffix(value, ".wuhb")) return false;
    if (strlen(kVolumePrefix) + value.size() + 1 >
        sizeof(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath)) return false;
    return IsRegularFile(value);
}

// Exactly two NUL-terminated strings, the first naming the core being launched.
bool ValidArguments(const char *bytes, uint32_t size, const char *coreRelativePath) {
    if (bytes == nullptr || size == 0 || size > kMaxArgumentBytes || bytes[size - 1] != '\0') return false;
    uint32_t count = 0, offset = 0;
    while (offset < size) {
        const uint32_t length = static_cast<uint32_t>(strnlen(bytes + offset, size - offset));
        if (offset + length >= size) return false;
        offset += length + 1;
        ++count;
    }
    if (count != 2) return false;
    const std::string expected = std::string(kCafePrefix) + coreRelativePath;
    return expected == bytes;
}

// The provider's own presentation, so the wrapper shows RetroArch rather than
// uFlow. Read into caller-owned storage before anything global is touched.
struct ProviderMeta {
    MetaInformation meta {};
    bool metaLoaded = false;
    std::vector<uint8_t> icon;
};

int ProviderIniHandler(void *user, const char *section, const char *name, const char *value) {
    auto *info = static_cast<MetaInformation *>(user);
#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("menu", "longname")) {
        info->longname[0] = '\0';
        strncat(info->longname, value, sizeof(info->longname) - 1);
    } else if (MATCH("menu", "shortname")) {
        info->shortname[0] = '\0';
        strncat(info->shortname, value, sizeof(info->shortname) - 1);
    } else if (MATCH("menu", "author")) {
        info->author[0] = '\0';
        strncat(info->author, value, sizeof(info->author) - 1);
    } else {
        return 0;
    }
#undef MATCH
    return 1;
}

// Mounts the provider only to confirm it really carries content and to copy its
// metadata; the mount is always closed before the launch is prepared.
bool ReadProvider(const std::string &providerRelative, ProviderMeta &out, std::string &why) {
    const std::string volumePath = std::string(kVolumePrefix) + providerRelative;
    WUHBRPXInfo info {};
    if (WUHBUtils_GetRPXInfo(volumePath.c_str(), BundleSource_FileDescriptor_CafeOS, &info) != WUHB_UTILS_RESULT_SUCCESS) {
        why = "provider is not a bundle";
        return false;
    }
    int mounted = -1;
    if (WUHBUtils_MountBundle("rap", volumePath.c_str(), BundleSource_FileDescriptor_CafeOS, &mounted) != WUHB_UTILS_RESULT_SUCCESS ||
        mounted != 0) {
        why = "provider mount failed";
        return false;
    }
    // WUHB mounts serve files but do not always support directory listing, so a
    // failed probe is not proof that the bundle lacks assets.
    bool hasContent = false;
    if (DIR *dir = opendir("rap:/content")) {
        hasContent = readdir(dir) != nullptr;
        closedir(dir);
    }
    if (!hasContent) {
        struct stat info {};
        hasContent = stat("rap:/content", &info) == 0;
    }
    uint8_t *buffer  = nullptr;
    uint32_t bufferSize = 0;
    if (WUHBUtils_ReadWholeFile("rap:/meta/meta.ini", &buffer, &bufferSize) == WUHB_UTILS_RESULT_SUCCESS && buffer) {
        const std::string text(reinterpret_cast<const char *>(buffer), bufferSize);
        out.metaLoaded = bufferSize != 0 && ini_parse_string(text.c_str(), ProviderIniHandler, &out.meta) == 0;
        free(buffer);
    }
    buffer     = nullptr;
    bufferSize = 0;
    if (WUHBUtils_ReadWholeFile("rap:/meta/iconTex.tga", &buffer, &bufferSize) == WUHB_UTILS_RESULT_SUCCESS && buffer) {
        out.icon.assign(buffer, buffer + (bufferSize < ICON_SIZE ? bufferSize : ICON_SIZE));
        free(buffer);
    }
    int unmounted = 0;
    if (WUHBUtils_UnmountBundle("rap", &unmounted) != WUHB_UTILS_RESULT_SUCCESS || unmounted != 0) {
        Trace("provider unmount reported %d", unmounted);
        why = "provider unmount failed";
        return false;
    }
    if (!hasContent) Trace("provider content listing unavailable; mounting it anyway");
    return true;
}

// Everything this transaction can change, so a failed launch leaves uFlow as it was.
struct PendingSnapshot {
    bool willRPXBeReplaced = false;
    char lastFileLoaded[0x280] {};
    char toMountPath[sizeof(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath)] {};
    MetaInformation meta {};
    std::vector<uint8_t> icon;
};

void Capture(PendingSnapshot &snapshot) {
    snapshot.willRPXBeReplaced = gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced;
    memcpy(snapshot.lastFileLoaded, gReplacementInfo.lastFileLoaded, sizeof(snapshot.lastFileLoaded));
    memcpy(snapshot.toMountPath, gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath,
           sizeof(snapshot.toMountPath));
    memcpy(&snapshot.meta, &gReplacementInfo.rpxReplacementInfo.metaInformation, sizeof(snapshot.meta));
    snapshot.icon.assign(gReplacementInfo.rpxReplacementInfo.iconCache,
                         gReplacementInfo.rpxReplacementInfo.iconCache + ICON_SIZE);
}

void Restore(const PendingSnapshot &snapshot) {
    gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced = snapshot.willRPXBeReplaced;
    memcpy(gReplacementInfo.lastFileLoaded, snapshot.lastFileLoaded, sizeof(snapshot.lastFileLoaded));
    memcpy(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath, snapshot.toMountPath,
           sizeof(snapshot.toMountPath));
    memcpy(&gReplacementInfo.rpxReplacementInfo.metaInformation, &snapshot.meta, sizeof(snapshot.meta));
    if (snapshot.icon.size() == ICON_SIZE)
        memcpy(gReplacementInfo.rpxReplacementInfo.iconCache, snapshot.icon.data(), ICON_SIZE);
    OSMemoryBarrier();
}

void RevertPreparedLaunch(const char *coreRelativePath) {
    MochaRPXLoadInfo revert {};
    revert.target = LOAD_RPX_TARGET_EXTRA_REVERT_PREPARE;
    strncat(revert.path, coreRelativePath, sizeof(revert.path) - 1);
    const auto status = Mocha_PrepareRPXLaunch(&revert);
    if (status != MOCHA_RESULT_SUCCESS) Trace("revert-prepare failed: %s", Mocha_GetStatusStr(status));
    gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced = false;
}
} // namespace

RPXLoaderStatus RL_LaunchHomebrewWithArgsAndContent(const char *coreRelativePath,
                                                    const char *contentBundleRelativePath,
                                                    const char *argumentBytes,
                                                    uint32_t argumentByteCount) {
    if (!ValidCorePath(coreRelativePath)) {
        Trace("refused: core path");
        return RPX_LOADER_RESULT_INVALID_ARGUMENT;
    }
    if (!ValidProviderPath(contentBundleRelativePath)) {
        Trace("refused: content provider path");
        return RPX_LOADER_RESULT_INVALID_ARGUMENT;
    }
    if (!ValidArguments(argumentBytes, argumentByteCount, coreRelativePath)) {
        Trace("refused: arguments");
        return RPX_LOADER_RESULT_INVALID_ARGUMENT;
    }

    ProviderMeta provider;
    std::string why;
    if (!ReadProvider(contentBundleRelativePath, provider, why)) {
        Trace("refused: %s", why.c_str());
        return RPX_LOADER_RESULT_NOT_FOUND;
    }

    PendingSnapshot snapshot;
    Capture(snapshot);

    const auto prepared = RL_PrepareLaunchFromSD(coreRelativePath);
    if (prepared != RPX_LOADER_RESULT_SUCCESS) {
        Restore(snapshot);
        Trace("prepare failed: %d", prepared);
        return prepared;
    }

    // The core is a bare RPX, so the prepared mount currently points at uFlow's
    // own bundle (or nothing). Replace only the PENDING path; the live mount
    // keeps serving uFlow until it exits.
    auto &mount = gReplacementInfo.contentReplacementInfo.bundleMountInformation;
    mount.toMountPath[0] = '\0';
    strncat(mount.toMountPath, (std::string(kVolumePrefix) + contentBundleRelativePath).c_str(),
            sizeof(mount.toMountPath) - 1);
    if (provider.metaLoaded)
        memcpy(&gReplacementInfo.rpxReplacementInfo.metaInformation, &provider.meta,
               sizeof(gReplacementInfo.rpxReplacementInfo.metaInformation));
    if (provider.icon.size() == ICON_SIZE)
        memcpy(gReplacementInfo.rpxReplacementInfo.iconCache, provider.icon.data(), ICON_SIZE);
    OSMemoryBarrier();

    sSession = {};
    sSession.active = true;
    strncat(sSession.provider, mount.toMountPath, sizeof(sSession.provider) - 1);
    strncat(sSession.core, coreRelativePath, sizeof(sSession.core) - 1);
    const char *slash = strrchr(coreRelativePath, '/');
    if (slash != nullptr)
        strncat(sSession.coreRoot, coreRelativePath,
                std::min(sizeof(sSession.coreRoot) - 1, static_cast<size_t>(slash - coreRelativePath + 1)));

    SYSStandardArgsIn args {};
    args.argString = argumentBytes;
    args.size      = argumentByteCount;
    Trace("launching core=%s provider=%s bytes=%u", coreRelativePath, contentBundleRelativePath, argumentByteCount);
    const auto status = Mocha_LaunchHomebrewWrapperEx(&args);
    if (status == MOCHA_RESULT_SUCCESS) return RPX_LOADER_RESULT_SUCCESS;

    Trace("launch failed: %s", Mocha_GetStatusStr(status));
    RevertPreparedLaunch(coreRelativePath);
    Restore(snapshot);
    sSession = {};
    return status == MOCHA_RESULT_NOT_FOUND ? RPX_LOADER_RESULT_NOT_FOUND : RPX_LOADER_RESULT_UNKNOWN_ERROR;
}

namespace RetroArchLaunch {
bool SessionActive() { return sSession.active; }

void ClearSession() { sSession = {}; }

// RetroArch switches cores by preparing the new RPX itself and calling the
// wrapper again with native arguments. uFlow's same-wrapper hook would
// otherwise re-prepare the previous executable over that choice. Recognise only
// that case: our session, the provider still mounted, and a bounded argument
// buffer naming another core in the allowed directory.
bool AdoptCoreSwitch(void *standardArgs) {
    if (!sSession.active || standardArgs == nullptr) return false;
    auto &mount = gReplacementInfo.contentReplacementInfo.bundleMountInformation;
    if (!mount.isMounted || strncmp(mount.mountedPath, sSession.provider, sizeof(mount.mountedPath)) != 0) return false;

    const auto *args = static_cast<const SYSStandardArgsIn *>(standardArgs);
    if (args->argString == nullptr || args->size == 0 || args->size > kMaxArgumentBytes) return false;
    if (!OSIsAddressValid(reinterpret_cast<uint32_t>(args->argString))) return false;
    const char *bytes = args->argString;
    if (bytes[args->size - 1] != '\0') return false;

    const std::string first(bytes, strnlen(bytes, args->size));
    const std::string prefix(kCafePrefix);
    if (first.rfind(prefix, 0) != 0) return false;
    const std::string coreRelative = first.substr(prefix.size());
    if (!ValidCorePath(coreRelative.c_str())) return false;
    // Only cores next to the one uFlow launched, or in RetroArch's default folder.
    const bool besideLaunched = sSession.coreRoot[0] != '\0' && coreRelative.rfind(sSession.coreRoot, 0) == 0;
    if (!besideLaunched && coreRelative.rfind(kCoreRoot, 0) != 0) return false;

    // RetroArch already prepared this core; keep it and only restore the pieces
    // uFlow owns: the pending provider and the executable bookkeeping.
    mount.toMountPath[0] = '\0';
    strncat(mount.toMountPath, sSession.provider, sizeof(mount.toMountPath) - 1);
    gReplacementInfo.lastFileLoaded[0] = '\0';
    strncat(gReplacementInfo.lastFileLoaded, coreRelative.c_str(), sizeof(gReplacementInfo.lastFileLoaded) - 2);
    gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced = true;
    sSession.core[0] = '\0';
    strncat(sSession.core, coreRelative.c_str(), sizeof(sSession.core) - 1);
    OSMemoryBarrier();
    Trace("core switch adopted: %s", coreRelative.c_str());
    return true;
}
} // namespace RetroArchLaunch

WUMS_EXPORT_FUNCTION(RL_LaunchHomebrewWithArgsAndContent);
