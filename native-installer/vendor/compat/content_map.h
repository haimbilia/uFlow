#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "title.h"
#include "FSAUtils.h"

#define VWII_SHARED_CONTENT_MAP_PATH "/vol/slccmpt01/shared1/content.map"
#define VWII_SHARED1_DIR_PATH        "/vol/slccmpt01/shared1"

struct ContentMapReport {
    bool mapFileMissing = false;
    bool mapFileDamaged = false;
    bool mapPermissionsInvalid = false;
    size_t totalSlots = 0;
    size_t activeEntries = 0;
    size_t verifiedEntries = 0;
    size_t missingFilesCount = 0;
    size_t hashMismatchCount = 0;
    size_t filePermissionErrorsCount = 0;
    size_t unindexedFilesCount = 0;
    size_t duplicateEntriesCount = 0;
    size_t duplicateFilesCount = 0;
    size_t titleMissingSharedAssetsCount = 0;
    std::vector<uint32_t> mismatchSlots;
    std::vector<uint32_t> missingSlots;
    std::vector<std::string> issues;

    bool IsClean() const {
        return !mapFileMissing && !mapFileDamaged && !mapPermissionsInvalid &&
               missingFilesCount == 0 && hashMismatchCount == 0 &&
               filePermissionErrorsCount == 0 && unindexedFilesCount == 0 &&
               duplicateEntriesCount == 0 && duplicateFilesCount == 0 &&
               titleMissingSharedAssetsCount == 0;
    }
};

// Finds the index in /vol/slccmpt01/shared1/content.map corresponding to the SHA-1 hash.
// Returns -1 if not found.
extern "C" {
int32_t FindSharedContentIndex(const uint8_t* expectedHash);
}
int32_t FindSharedContentIndex(const Sha1Hash& expectedHash);

// Returns an existing shared index if present, or allocates a new slot in content.map.
int32_t GetSharedContentIndex(const uint8_t* expectedHash);

// Zeroes out an entry in content.map by slot index.
bool CONTENTMAP_RemoveEntry(FSAClientHandle fsa, uint32_t slotIndex);
bool CONTENTMAP_RemoveEntries(FSAClientHandle fsa, const std::vector<uint32_t>& slotIndices);

// Performs a full consistency audit of /shared1/content.map and /shared1/*.app files.
bool CONTENTMAP_CheckConsistency(FSAClientHandle fsa, ContentMapReport& outReport);

// Reconstructs /shared1/content.map from disk files and title TMDs, repairing all permissions and cleaning duplicate shared files.
bool CONTENTMAP_Reconstruct(FSAClientHandle fsa, size_t* outRecoveredCount = nullptr, size_t* outDuplicatesRemovedCount = nullptr);
