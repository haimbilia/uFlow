#pragma once

#include <string>
#include <stdint.h>

enum class DownloadResult {
    SUCCESS,
    FAILED,
    CANCELLED
};

enum class DownloadAction {
    RETRY,
    SKIP,
    CANCEL
};

// Prompts user: Press (A) to Retry, (B) to Skip, (X) to Cancel.
DownloadAction PromptDownloadRetry();

// Downloads an app from Open Shop Channel and extracts it to the root of the SD card.
// appId is the internal name, e.g. "d2x-cios-installer".
DownloadResult DownloadAndExtractApp(const std::string& appId);

// Downloads a file from the given url to the given outPath on the filesystem.
DownloadResult DownloadFile(const std::string& url, const std::string& outPath);

// Downloads a file from the given url to a newly allocated memory buffer.
DownloadResult DownloadToMemory(const std::string& url, uint8_t** outData, size_t* outSize);

// Cleans up cURL resources. Should be called on exit.
void DeinitCurl();
