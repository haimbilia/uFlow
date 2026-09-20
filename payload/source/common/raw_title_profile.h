#pragma once

#include <stdint.h>

#define IPC_CUSTOM_SET_RAW_TITLE_PROFILE 0xF4
#define RAW_TITLE_PROFILE_MAGIC          0x52575031
#define RAW_TITLE_PROFILE_VERSION        1

typedef struct __attribute__((packed)) RawTitlePermission_t {
    uint32_t group;
    uint64_t mask;
} RawTitlePermission;

typedef struct __attribute__((packed)) RawTitleProfile_t {
    uint32_t magic;
    uint32_t profileVersion;
    uint32_t enabled;
    uint32_t cosVersion;
    uint64_t osVersion;
    uint64_t titleId;
    uint32_t appType;
    uint32_t groupId;
    uint32_t cmdFlags;
    char argstr[0x1000];
    uint32_t maxSize;
    uint32_t availSize;
    uint32_t codegenSize;
    uint32_t codegenCore;
    uint32_t maxCodeSize;
    uint32_t overlayArena;
    uint32_t numWorkareaHeapBlocks;
    uint32_t numCodeareaHeapBlocks;
    RawTitlePermission permissions[19];
    uint32_t defaultStack0Size;
    uint32_t defaultStack1Size;
    uint32_t defaultStack2Size;
    uint32_t defaultRedzone0Size;
    uint32_t defaultRedzone1Size;
    uint32_t defaultRedzone2Size;
    uint32_t exceptionStack0Size;
    uint32_t exceptionStack1Size;
    uint32_t exceptionStack2Size;
    uint32_t sdkVersion;
    uint32_t titleVersion;
} RawTitleProfile;

