/* Compat Title Installer
 *   Copyright (C) 2026  zer00p
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef WAD_H
#define WAD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <vector>
#include "installer.h"
#include "downloader.h"

// Represents a loaded WAD file and its parsed components
typedef struct {
    uint8_t* rawData;
    size_t size;

    // Header info
    uint32_t certSize;
    uint32_t crlSize;
    uint32_t ticketSize;
    uint32_t tmdSize;
    uint32_t contentSize;
    uint32_t metaSize;

    // Pointers into rawData
    uint8_t* certData;
    uint8_t* crlData;
    uint8_t* ticketData;
    uint8_t* tmdData;
    uint8_t* contentData;
    uint8_t* metaData;

    // Parsed from Ticket
    uint8_t titleKey[16];     // Decrypted title key
    uint64_t ticketTitleId;

    // Parsed from TMD
    uint64_t tmdTitleId;
    uint32_t titleType;       // e.g., 0x00000001 (system), 0x00010001 (channel)
    uint16_t numContents;

    // Decrypted contents array (from wad-tools)
    CINS_Content* contentsArray;

} WADContext;

#include "title.h"

// Load a WAD file from SD card, parse it, and decrypt its contents.
// Returns a WADContext if successful, or NULL on failure.
WADContext* WAD_LoadAndDecrypt(const char* filepath);

// Download a title from NUS (handling vWii 00000007 logic and resigning).
DownloadResult NUS_DownloadTitle(uint64_t titleId, int32_t version, WADContext** outCtx);

// Get the latest version of a title from NUS. Returns -1 on failure.
int32_t NUS_GetLatestVersion(uint64_t titleId);

// Free a WADContext and its associated memory.
void WAD_Free(WADContext* ctx);

// Write the decrypted WAD contents and metadata (TMD/Ticket/Cert) to the slccmpt filesystem.
// Returns true on success, false on failure.
bool WAD_InstallToVWii(WADContext* ctx, int fsaFd);

// Validates safety and installs WADContext to vWii. Returns true on success, false on failure.
bool WAD_InstallSafe(WADContext* ctx);

// Downloads title from NUS, validates safety, installs to vWii, and frees context.
DownloadResult NUS_DownloadAndInstall(uint64_t titleId, int32_t version);

// Check if the given Title ID is a safe title type (blocks System Menu and critical IOS).
bool WAD_IsSafeTitle(WADContext* ctx);

// Reads the common key from OTP hardware
extern "C" bool GetCommonKeyFromOTP(uint8_t index, uint8_t outKey[16]);

typedef struct {
    uint64_t id;
    const char* name;
    bool regionSpecificId;
    bool regionSpecificVersion;
} NusTitle;

extern const NusTitle g_nusTitles[38];
extern const size_t g_numNusTitles;

// Resolves region-specific Title ID (e.g. adding 'E', 'P', 'J' character)
uint64_t NUS_ResolveTitleId(const NusTitle* title, int32_t regionCode);

// Resolves title version from NUS and applies region-specific versioning if needed
int32_t NUS_ResolveTitleVersion(const NusTitle* title, uint64_t resolvedTitleId, int32_t regionCode);

// Resolves version and installs a system title from NUS
DownloadResult NUS_InstallSystemTitle(const NusTitle* title, int32_t regionCode);

// Installs a list of NUS system titles in a loop, displaying progress and handling abort prompts.
// Returns true if all completed successfully, false if any failed or user aborted.
bool NUS_InstallTitlesBatch(const std::vector<const NusTitle*>& titles, int32_t regionCode, int& outSuccess, int& outFailed);

#endif // WAD_H
