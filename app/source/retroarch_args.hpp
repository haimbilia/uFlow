#pragma once

// Native argument packing for RetroArch's Aroma port.
//
// The Aroma frontend does not read a low-memory block: frontend_wiiu_exec()
// builds a buffer of NUL-separated argument strings, puts it in
// SYSStandardArgsIn and calls Mocha_LaunchHomebrewWrapperEx(). The launched
// process recovers it through SYSDeserializeSysArgs()/_SYSDeserializeStandardArg()
// and rebuilds argc/argv for rarch_main(). uFlow sends the same two strings:
// argv[0] is the core executable, argv[1] is the content to run.
//
// Cores are separately linked RetroArch executables (retroarch/cores/*.rpx), so
// no "-L core" argument exists; the chosen RPX is the core. RetroArch's assets
// come from its own .wuhb, which the loader mounts as /vol/content.
#include <cstddef>
#include <string>
#include <vector>

namespace RetroArchArgs {
// uFlow's own cap on the serialized argument block. Not a documented system
// limit; keeps a malformed library path from pushing a huge buffer into SYS.
inline constexpr std::size_t kMaxArgumentBytes = 2048;
// MochaRPXLoadInfo::path is 256 bytes, so an SD-relative executable path plus
// its terminator must fit; bundleMountInformation::toMountPath is 255 bytes and
// holds "/vol/external01/" + the provider path.
inline constexpr std::size_t kMaxExecutableRelative = 255;
inline constexpr std::size_t kMaxMountPath = 254;
inline constexpr char kSdPrefixCafe[] = "fs:/vol/external01/";   // newlib/Cafe paths used in argv
inline constexpr char kSdPrefixVolume[] = "/vol/external01/";    // loader/mount API paths

// "wiiu/apps/x.wuhb" -> "/vol/external01/wiiu/apps/x.wuhb"
inline std::string VolumePath(const std::string &sdRelative) { return std::string(kSdPrefixVolume) + sdRelative; }
// "retroarch/cores/x.rpx" -> "fs:/vol/external01/retroarch/cores/x.rpx"
inline std::string CafePath(const std::string &sdRelative) { return std::string(kSdPrefixCafe) + sdRelative; }
// "fs:/vol/external01/x" -> "x"; anything else stays as it is.
inline std::string SdRelative(const std::string &cafePath) {
    const std::string prefix(kSdPrefixCafe);
    return cafePath.rfind(prefix, 0) == 0 ? cafePath.substr(prefix.size()) : cafePath;
}

// A path uFlow may hand to another process: no control bytes, no backslashes,
// no traversal, no empty segments. Spaces, apostrophes and UTF-8 are kept.
inline bool ValidArgumentPath(const std::string &path) {
    if (path.empty() || path.size() + 2 > kMaxArgumentBytes) return false;
    if (path.rfind(kSdPrefixCafe, 0) != 0) return false;
    for (unsigned char c : path)
        if (c < 0x20 || c == 0x7F || c == '\\') return false;
    std::size_t start = std::string(kSdPrefixCafe).size();
    if (start >= path.size()) return false;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto segment = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

// The exact bytes SYSStandardArgsIn carries: two NUL-terminated strings, both
// terminators counted, no pointer array and no shell quoting. Empty on refusal.
inline std::vector<char> Pack(const std::string &coreCafePath, const std::string &contentCafePath,
                              std::string *why = nullptr) {
    const auto fail = [&](const char *reason) {
        if (why) *why = reason;
        return std::vector<char>{};
    };
    if (!ValidArgumentPath(coreCafePath)) return fail("core path is not a valid SD path");
    if (!ValidArgumentPath(contentCafePath)) return fail("content path is not a valid SD path");
    if (SdRelative(coreCafePath).size() > kMaxExecutableRelative) return fail("core path is too long");
    const std::size_t total = coreCafePath.size() + contentCafePath.size() + 2;
    if (total > kMaxArgumentBytes) return fail("arguments are too long");
    std::vector<char> bytes;
    bytes.reserve(total);
    bytes.insert(bytes.end(), coreCafePath.begin(), coreCafePath.end());
    bytes.push_back('\0');
    bytes.insert(bytes.end(), contentCafePath.begin(), contentCafePath.end());
    bytes.push_back('\0');
    if (why) why->clear();
    return bytes;
}

// Reads a packed block back, as the receiving process does. Used by tests and
// by the loader to check what it was handed.
inline bool Unpack(const char *bytes, std::size_t size, std::vector<std::string> &out) {
    out.clear();
    if (!bytes || size == 0 || size > kMaxArgumentBytes) return false;
    if (bytes[size - 1] != '\0') return false;
    std::size_t start = 0;
    while (start < size) {
        const std::string value(bytes + start);
        out.push_back(value);
        start += value.size() + 1;
    }
    return !out.empty();
}
} // namespace RetroArchArgs
