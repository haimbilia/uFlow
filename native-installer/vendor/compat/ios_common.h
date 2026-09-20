#pragma once

#include "title.h"
#include <memory>
#include <string>
#include <vector>
#include <stdint.h>

struct MemContent {
    uint32_t cid;
    uint32_t size;
    uint8_t* data;
};

struct MemIOS {
    TitleTmd* tmd = nullptr;
    uint32_t tmdSize = 0;
    TitleTicket* ticket = nullptr;
    uint32_t ticketSize = 0;
    MemContent* contents = nullptr;
    uint32_t numContents = 0;
    uint32_t maxContents = 0;

    ~MemIOS();
    MemIOS() = default;
    MemIOS(const MemIOS&) = delete;
    MemIOS& operator=(const MemIOS&) = delete;
};

void Patcher_Log(const std::string& msg);
std::string ToHexString(uint32_t val, int width = 8);

void Write16BE(uint8_t* p, uint16_t v);
void Write32BE(uint8_t* p, uint32_t v);
void Write64BE(uint8_t* p, uint64_t v);

std::string GetTmdBackupPath(uint32_t ios_ver);
std::string GetTikBackupPath(uint32_t ios_ver);
void RemoveBackupFiles(uint32_t ios_ver);
std::unique_ptr<MemIOS> ReadBaseIOS(uint32_t baseIos);
bool WritePatchedIOS(uint32_t titleIdLow, MemIOS& ios);

bool IsOriginalNintendoSignature(const uint8_t* signature, size_t size);
bool HasPristineBackup(uint32_t ios_ver);
void BackupPristineTmdAndTicket(uint32_t ios_ver, MemIOS* ios);

bool RestoreIOSFromNUS(uint32_t ios_ver);
bool LoadPristineSharedContents(MemIOS* ios, const TitleTmd* origTmd);
bool VerifyAndInstallRestoredIOS(uint32_t ios_ver, MemIOS* ios, const uint8_t* origTmdBuf, uint32_t origTmdSize, const uint8_t* origTikBuf, uint32_t origTikSize);

int ReplacePattern(uint8_t *buf, uint32_t size, const uint8_t* search, const uint8_t* replace, uint32_t len, bool revert = false);
