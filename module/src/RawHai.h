#pragma once
#include <cstdint>

// Wii VC titles authored for Wii U run through Nintendo's HAI wrapper. Their
// disc images stay on FAT32 USB; this prepares the storage handoff that an
// installed title would get from WFS.
namespace RawHai {
enum class Result { NotHai, Ready, Failed };

// uFlow's process, after USBStorage::PrepareCode() succeeded.
Result Prepare(const char *codePath, const char *contentPath, uint64_t titleId);
void Clear();

// Game process.
void InstallCompanionPatch();
void RemoveCompanionPatch();
} // namespace RawHai
