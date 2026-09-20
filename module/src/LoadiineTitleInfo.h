// Adapted from Loadiine GX2 src/common/kernel_defs.h and kernel_functions.c.
// Copyright the Loadiine GX2 contributors (dimok789/loadiine_gx2).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "RawTitleProfile.h"
#include <cstddef>

// This is the PPC PrepareTitle ABI, NOT the IOSU MCP title-info structure.
// Keep opaque fields intact, exactly as Loadiine does.
struct __attribute__((packed)) KernelPrepareTitleInfo {
    uint32_t version;
    uint64_t osVersion;
    uint64_t titleId;
    uint32_t appType;
    uint32_t cmdFlags;
    char argstr[0x1000];
    uint32_t unknown2[64];
    uint32_t maxSize, availSize, codegenSize, codegenCore, maxCodeSize, overlayArena;
    uint32_t unknown4[59];
    uint32_t defaultStack0Size, defaultStack1Size, defaultStack2Size;
    uint32_t defaultRedzone0Size, defaultRedzone1Size, defaultRedzone2Size;
    uint32_t exceptionStack0Size, exceptionStack1Size, exceptionStack2Size;
    uint32_t sdkVersion, titleVersion;
};
static_assert(offsetof(KernelPrepareTitleInfo, appType) == 0x14);
static_assert(offsetof(KernelPrepareTitleInfo, argstr) == 0x1C);
static_assert(offsetof(KernelPrepareTitleInfo, maxSize) == 0x111C);
static_assert(offsetof(KernelPrepareTitleInfo, defaultStack0Size) == 0x1220);
static_assert(offsetof(KernelPrepareTitleInfo, titleVersion) == 0x1248);
static_assert(sizeof(KernelPrepareTitleInfo) == 0x124C);

__attribute__((always_inline)) inline bool IsRawWrapperName(const char *name) {
    return (name[0] == 's' && name[1] == 'a' && name[2] == 'f' && name[3] == 'e' &&
            name[4] == '.' && name[5] == 'r' && name[6] == 'p' && name[7] == 'x' && name[8] == '\0') ||
           (name[0] == 'p' && name[1] == 'l' && name[2] == 'y' && name[3] == '.' &&
            name[4] == 'r' && name[5] == 'p' && name[6] == 'x' && name[7] == '\0');
}

// Must inline: the kernel trampoline cannot call userland string/memory helpers.
__attribute__((always_inline)) inline void ApplyLoadiineTitleInfo(
        KernelPrepareTitleInfo &info, const RawTitleProfile &profile) {
    bool ended = false;
    for (size_t i = 0; i < sizeof(info.argstr); ++i) {
        char c = profile.argstr[i];
        ended = ended || c == '\0';
        info.argstr[i] = ended || c == ' ' ? '\0' : c;
    }
    info.argstr[sizeof(info.argstr) - 1] = '\0';
    info.version = profile.cosVersion;
    info.osVersion = profile.osVersion;
    info.titleId = profile.titleId;
    info.appType = profile.appType;
    info.cmdFlags = profile.cmdFlags;
    info.maxSize = profile.maxSize;
    info.availSize = profile.availSize;
    info.codegenSize = profile.codegenSize;
    info.codegenCore = profile.codegenCore;
    info.maxCodeSize = profile.maxCodeSize;
    info.overlayArena = profile.overlayArena;
    info.defaultStack0Size = profile.defaultStack0Size;
    info.defaultStack1Size = profile.defaultStack1Size;
    info.defaultStack2Size = profile.defaultStack2Size;
    info.defaultRedzone0Size = profile.defaultRedzone0Size;
    info.defaultRedzone1Size = profile.defaultRedzone1Size;
    info.defaultRedzone2Size = profile.defaultRedzone2Size;
    info.exceptionStack0Size = profile.exceptionStack0Size;
    info.exceptionStack1Size = profile.exceptionStack1Size;
    info.exceptionStack2Size = profile.exceptionStack2Size;
    info.sdkVersion = profile.sdkVersion;
    info.titleVersion = profile.titleVersion;
}

// Preparation runs before recycle AND again for the actual load. Userland
// clears the hook at application start, not after the first preparation.
// 0 = disarmed, otherwise 1 + the number of matched preparations.
__attribute__((always_inline)) inline bool ApplyArmedRawTitleInfo(
        KernelPrepareTitleInfo &info, const RawTitleProfile &profile,
        volatile uint32_t &armed) {
    if (!armed || !profile.enabled || !IsRawWrapperName(info.argstr)) return false;
    ApplyLoadiineTitleInfo(info, profile);
    if (armed != UINT32_MAX) armed = armed + 1;
    return true;
}
