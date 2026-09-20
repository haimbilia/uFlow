#pragma once

#include <coreinit/filesystem_fsa.h>
#include "PathRules.h"
#include <stddef.h>
#include <functional>
#include <string>
#include <string_view>

// vWii SLCCMPT mount point prefix
inline constexpr std::string_view VWII_MOUNT_POINT = "/vol/slccmpt01";

// Callback for filesystem removal events (e.g. progress logging)
using FSARemoveCallback = std::function<void(const std::string& path)>;

// Returns the full FSA path by prepending /vol/slccmpt01 if not already present
inline std::string VwiiFsaPath(std::string_view relPath) {
    if (relPath.starts_with(VWII_MOUNT_POINT)) {
        return std::string(relPath);
    }
    if (relPath.empty()) {
        return std::string(VWII_MOUNT_POINT);
    }
    if (relPath.front() == '/') {
        return std::string(VWII_MOUNT_POINT) + std::string(relPath);
    }
    return std::string(VWII_MOUNT_POINT) + "/" + std::string(relPath);
}

// Strips /vol/slccmpt01 prefix for display/logging if present
inline std::string_view VwiiCleanPath(std::string_view fullPath) {
    if (fullPath.starts_with(VWII_MOUNT_POINT)) {
        std::string_view stripped = fullPath.substr(VWII_MOUNT_POINT.size());
        return stripped.empty() ? "/" : stripped;
    }
    return fullPath;
}

// Writes a buffer to an FSA file handle using a 0x40 aligned internal buffer
bool FSAWriteAligned(FSAClientHandle fsa, FSAFileHandle fd, const void* buffer, size_t size);

// Recursively removes a directory tree or file using FSA. Set keepRoot=true to keep the root directory itself.
bool FSARemoveTree(FSAClientHandle fsaClient, const std::string& path, bool keepRoot = false, FSARemoveCallback onRemove = nullptr);

enum class UninstallResult {
    SUCCESS,
    NOT_PRESENT,
    FAILED
};

// Removes a file or directory tree, distinguishing between not present, success, and failure
UninstallResult FSARemovePathResult(FSAClientHandle fsaClient, const std::string& path, bool isDirectory = true);

// Changes ownership (UID and GID) of a file or directory via raw FSA IPC (ioctl 0x70)
// SLC (SLCCMPT) only — FAT32 (SD card) does not support ownership.
FSError FSA_ChangeOwner(FSAClientHandle fsaClient, const std::string& path, uint32_t uid, uint32_t gid);

// ---------------------------------------------------------------------------
// SLC (SLCCMPT / vWii NAND) helpers
// These wrappers resolve PathRules and apply correct ownership + permission
// modes via FSA_ChangeOwner / FSAChangeMode. Never use on SD card paths.
// ---------------------------------------------------------------------------

// Creates a directory using ownership and permissions resolved from PathRules.
FSError SlcMakeDir(FSAClientHandle fsaClient, const std::string& path, uint16_t tmdGroupId = 0);

// Creates a directory with explicit ownership. Chowns while the directory is still empty.
FSError SlcMakeDirWithOwner(FSAClientHandle fsaClient, const std::string& path, FSMode mode = (FSMode)0x664, uint32_t uid = 0, uint32_t gid = 0);

// Recursively creates a directory and all intermediate parent directories using PathRules.
bool SlcEnsureDir(FSAClientHandle fsaClient, const std::string& dirPath);

// Recursively creates all parent directories for a file path using PathRules.
bool SlcEnsureParentDir(FSAClientHandle fsaClient, const std::string& filePath);

// Creates a file using ownership and permissions resolved from PathRules.
bool SlcCreateFile(FSAClientHandle fsaClient, const std::string& path, const void* buffer, size_t size, uint16_t tmdGroupId = 0);

// Creates an empty file, chowns while empty, then writes payload and applies mode.
bool SlcCreateFileWithOwner(FSAClientHandle fsaClient, const std::string& path, const void* buffer, size_t size, FSMode mode, uint32_t uid, uint32_t gid);

// Writes a buffer to an SLC file setting proper ownership and permission mode.
bool SlcWriteFile(const std::string& path, const uint8_t* buf, uint32_t size, FSMode mode = (FSMode)0x660, uint32_t uid = 0, uint32_t gid = 0);

// Checks if an existing file on SLC has acceptable permissions, updating mode in-place or recreating with correct ownership if needed.
bool SlcRepairFilePermissions(FSAClientHandle fsaClient, const std::string& path, uint16_t tmdGroupId = 0);

// ---------------------------------------------------------------------------
// SD card (FAT32) helpers
// FAT32 has no ownership or permission concepts — never call FSA_ChangeOwner
// or FSAChangeMode on SD card paths. For a single mkdir on the SD card,
// call wut FSAMakeDir() directly.
// ---------------------------------------------------------------------------

// Recursively creates a directory and all intermediate parents on the SD card.
bool SdEnsureDir(FSAClientHandle fsa, const std::string& path);

// Recursively creates all parent directories for an SD card file path.
bool SdEnsureParentDir(FSAClientHandle fsa, const std::string& filePath);

// ---------------------------------------------------------------------------
// Common read / hash helpers
// ---------------------------------------------------------------------------

// Reads an entire file into a 0x40-aligned memory buffer using FSA. The caller is responsible for free()-ing outBuf.
bool ReadFileToBuffer(const std::string& path, uint8_t** outBuf, uint32_t* outSize);

// Computes the SHA-1 hash of a file on FSA using 64KB aligned streaming buffers. Returns true if file matches expectedSize and was hashed successfully.
bool FSAGetFileSha1(FSAClientHandle fsa, const std::string& path, uint8_t outHash[20], uint64_t expectedSize);

// Checks if a file exists on FSA, matches expectedSize, and has the matching SHA-1 hash.
bool FSACheckFileSha1(FSAClientHandle fsa, const std::string& path, const uint8_t expectedHash[20], uint64_t expectedSize);

// Re-creates standard stock SLCCMPT root directories with correct ownership and permission modes
bool FSA_InitStockRootDirs(FSAClientHandle fsa);

// Evaluates whether permissions are acceptable (stock or permissive like 0x666 from vWii NAND Restorer)
bool FSA_IsPermissionAcceptable(const FSStat& stat, FSMode stockMode, uint32_t expectedUid = 0, uint32_t expectedGid = 0);
