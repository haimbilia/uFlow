#pragma once
#include <stdint.h>
#include <string.h>

/* One-shot request for MCP's Wii VC HAI prepare handler. The original title
   stays on FAT32 USB; only its small code files are staged unchanged on SD
   because IOS cannot read FAT32 USB paths. */
#define UFLOW_HAI_ARM_COMMAND 0x55464841u
#define UFLOW_HAI_MAGIC 0x55464831u
#define UFLOW_HAI_SD_PREFIX "/vol/storage_iosu_homebrew/"
#define UFLOW_HAI_PATH_CAPACITY 56u /* MCPTitleListType::path */

typedef struct __attribute__((packed)) RawHaiArm_t {
    uint32_t magic;
    uint32_t device; /* /dev/usb01 or /dev/usb02, as in RawUsbMap */
    uint64_t titleId;
    char path[UFLOW_HAI_PATH_CAPACITY]; /* directory containing code/ */
} RawHaiArm;

static inline int RawHaiArmValid(const RawHaiArm *a) {
    if (a->magic != UFLOW_HAI_MAGIC || (a->device != 1 && a->device != 2) ||
        (uint32_t)(a->titleId >> 32) != 0x00050000u)
        return 0;
    const size_t prefix = sizeof(UFLOW_HAI_SD_PREFIX) - 1;
    if (memcmp(a->path, UFLOW_HAI_SD_PREFIX, prefix) != 0) return 0;
    for (size_t i = prefix, begin = prefix; i < UFLOW_HAI_PATH_CAPACITY; ++i) {
        const unsigned char c = (unsigned char) a->path[i];
        if (c == 0 || c == '/') {
            const size_t n = i - begin;
            if (!n || (n == 1 && a->path[begin] == '.') ||
                (n == 2 && a->path[begin] == '.' && a->path[begin + 1] == '.'))
                return 0;
            if (!c) return 1;
            begin = i + 1;
        } else if (c < 32 || c == ':' || c == '\\') {
            return 0;
        }
    }
    return 0; /* must be terminated inside the firmware field */
}
