#pragma once
#include <cstddef>
#include <cstring>

inline bool RawNativeSavePath(char *out, size_t capacity, bool active,
                              const char *path, const char *saveRoot) {
    if (!active || !out || !path || !saveRoot || !*saveRoot) return false;
    constexpr char virtualRoot[] = "/vol/save";
    constexpr char sdRoot[] = "/vol/external01/";
    constexpr size_t virtualLen = sizeof(virtualRoot) - 1, sdLen = sizeof(sdRoot) - 1;
    if (std::strncmp(path, virtualRoot, virtualLen) != 0 ||
        (path[virtualLen] != '\0' && path[virtualLen] != '/')) return false;
    const size_t rootLen = std::strlen(saveRoot), tailLen = std::strlen(path + virtualLen);
    if (sdLen + rootLen + tailLen + 1 > capacity) return false;
    std::memcpy(out, sdRoot, sdLen);
    std::memcpy(out + sdLen, saveRoot, rootLen);
    std::memcpy(out + sdLen + rootLen, path + virtualLen, tailLen + 1);
    return true;
}

// A read-only content/code open can use Cafe FS directly on the mounted SD.
inline bool RawNativeReadPath(char *out, size_t capacity, bool active,
                              const char *path, const char *mode,
                              const char *contentRoot, const char *codeRoot) {
    if (!active || !out || !path || !mode ||
        (std::strcmp(mode, "r") != 0 && std::strcmp(mode, "rb") != 0)) return false;
    const char *root = nullptr;
    const char *tail = nullptr;
    if (std::strncmp(path, "/vol/content/", 13) == 0) {
        root = contentRoot;
        tail = path + 13;
    } else if (std::strncmp(path, "/vol/code/", 10) == 0) {
        root = codeRoot;
        tail = path + 10;
    } else return false;
    if (!root || !*root) return false;
    constexpr char prefix[] = "/vol/external01/";
    const size_t rootLen = std::strlen(root), tailLen = std::strlen(tail);
    const size_t prefixLen = sizeof(prefix) - 1;
    if (prefixLen + rootLen + 1 + tailLen + 1 > capacity) return false;
    std::memcpy(out, prefix, prefixLen);
    std::memcpy(out + prefixLen, root, rootLen);
    out[prefixLen + rootLen] = '/';
    std::memcpy(out + prefixLen + rootLen + 1, tail, tailLen + 1);
    return true;
}

// Save file opens must also return native handles: a ContentRedirection
// virtual handle can escape to native WRITE_FILE in a raw title (150-3039).
// Map reads AND writes to the same existing per-title/account save directory.
// Native FS still validates modes and returns the real open/I/O errors.
inline bool RawNativeOpenPath(char *out, size_t capacity, bool active,
                              const char *path, const char *mode,
                              const char *contentRoot, const char *codeRoot,
                              const char *saveRoot) {
    if (!mode) return false;
    return RawNativeSavePath(out, capacity, active, path, saveRoot) ||
           RawNativeReadPath(out, capacity, active, path, mode, contentRoot, codeRoot);
}
