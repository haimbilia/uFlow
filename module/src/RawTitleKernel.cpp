// PrepareTitle integration adapted from Loadiine GX2 (GPL-3.0-or-later).
#include "RawTitleKernel.h"
#include "LoadiineTitleInfo.h"
#include <coreinit/dynload.h>
#include <coreinit/debug.h>
#include <coreinit/memorymap.h>

#include <coreinit/cache.h>
#include <coreinit/memory.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

// Cafe OS 5.5.x, matching Loadiine GX2's firmware table.
constexpr uint32_t kPrepareTitleHookAddress          = 0xFFF184E4;
constexpr uint32_t kPrepareTitleOriginalInstruction = 0x5587003E; // clrlwi r7,r12,0

bool sInitialized = false;
bool sHookInstalled = false;
const char *sInitializeError = nullptr;
char sInitializeErrorBuffer[160]{};
char sProbe[320]{};

using CopyPhysical = void (*)(uint32_t, uint32_t, uint32_t);
CopyPhysical sCopy = nullptr;
OSDynLoad_Module sKernelModule = {};
bool ResolveKernelCopy() {
    if (sCopy) return true;
    if (OSDynLoad_Acquire("homebrew_kernel", &sKernelModule) != OS_DYNLOAD_OK ||
        OSDynLoad_FindExport(sKernelModule, OS_DYNLOAD_EXPORT_FUNC,
                            "KernelCopyData", reinterpret_cast<void **>(&sCopy)) != OS_DYNLOAD_OK) {
        sInitializeError = "Raw launcher: KernelCopyData unavailable";
        return false;
    }
    return true;
}

uint32_t KernRead(const void *address) {
    alignas(0x40) uint32_t word[16]{};
    DCFlushRange(word, sizeof(word));
    sCopy(OSEffectiveToPhysical(reinterpret_cast<uint32_t>(word)), reinterpret_cast<uint32_t>(address), 4);
    // KernelModule's copy uses CPU loads/stores, not an uncached DMA write.
    // Invalidating here can discard the dirty copied word and reveal the old
    // zero-filled RAM. Match libkernel's KernelReadPTE post-copy flush.
    DCFlushRange(word, sizeof(word));
    return word[0];
}

bool CheckPhysicalCopy() {
    alignas(0x40) uint32_t source[16]{0x55464C57, 0xA53CC35A};
    alignas(0x40) uint32_t destination[16]{};
    const uint32_t sourcePhysical = OSEffectiveToPhysical(reinterpret_cast<uint32_t>(source));
    const uint32_t destinationPhysical = OSEffectiveToPhysical(reinterpret_cast<uint32_t>(destination));
    if (!sourcePhysical || !destinationPhysical) {
        sInitializeError = "Raw launcher: copy self-test address translation failed";
        return false;
    }
    DCFlushRange(source, sizeof(source));
    DCFlushRange(destination, sizeof(destination));
    sCopy(destinationPhysical, sourcePhysical, 2 * sizeof(uint32_t));
    DCFlushRange(destination, sizeof(destination));
    const bool valid = destination[0] == source[0] && destination[1] == source[1];
    snprintf(sProbe, sizeof(sProbe), "kernel-port2 copy-self-test=%s src=%08X dst=%08X values=%08X,%08X",
                valid ? "pass" : "fail", sourcePhysical, destinationPhysical,
                destination[0], destination[1]);
    if (!valid) sInitializeError = "Raw launcher: physical copy self-test failed; no kernel patch applied";
    return valid;
}

void KernWrite(void *address, uint32_t value) {
    alignas(0x40) uint32_t word[16]{};
    word[0] = value;
    DCFlushRange(word, sizeof(word));
    sCopy(reinterpret_cast<uint32_t>(address), OSEffectiveToPhysical(reinterpret_cast<uint32_t>(word)), 4);
    OSMemoryBarrier();
}

bool InitializeOnDemand();

} // namespace

extern "C" {
alignas(0x40) RawTitleProfile RawTitleKernelProfile
        __attribute__((section(".data.raw_title_kernel"), used)) = {};
alignas(0x40) volatile uint32_t RawTitleKernelArmed
        __attribute__((section(".data.raw_title_kernel"), used)) = 0;
void RawTitleKernelHookEntry();
}

// Runs in kernel context. The early module lives in the persistent low WUMS
// heap; the trampoline mirrors its instruction BAT temporarily for data access.
extern "C" __attribute__((noinline, section(".text.raw_title_kernel_code")))
void RawTitleKernel_Apply(void *rawInfo,
                          const RawTitleProfile *profile,
                          volatile uint32_t *armed) {
    auto *info = static_cast<KernelPrepareTitleInfo *>(rawInfo);
    if (!info || !profile || !armed) {
        return;
    }

    ApplyArmedRawTitleInfo(*info, *profile, *armed);
}

namespace {

bool InitializeOnDemand() {
    if (sInitialized) {
        return true;
    }

    if (!ResolveKernelCopy()) return false;
    if (!CheckPhysicalCopy()) return false;

    const uint32_t current = KernRead(reinterpret_cast<const void *>(kPrepareTitleHookAddress));
    const uint32_t hookAddress = reinterpret_cast<uint32_t>(&RawTitleKernelHookEntry);
    const uint32_t profileAddress = reinterpret_cast<uint32_t>(&RawTitleKernelProfile);
    OSReport("uFlow kernel probe: physical instruction=%08X hook=%08X profile=%08X\n",
             current, hookAddress, profileAddress);
    const size_t probeLength = __builtin_strlen(sProbe);
    snprintf(sProbe + probeLength, sizeof(sProbe) - probeLength,
             " address=%08X instruction=%08X expected=%08X hook=%08X profile=%08X",
             kPrepareTitleHookAddress,current,kPrepareTitleOriginalInstruction,hookAddress,profileAddress);
    if (hookAddress < 0x00800000 || hookAddress >= 0x01000000 ||
        profileAddress < 0x00800000 || profileAddress + sizeof(RawTitleKernelProfile) >= 0x01000000 ||
        (hookAddress & 3) != 0) {
        snprintf(sInitializeErrorBuffer, sizeof(sInitializeErrorBuffer),
                 "Wii U Raw Launcher: kernel hook/data out of range (%08X/%08X)", hookAddress, profileAddress);
        sInitializeError = sInitializeErrorBuffer;
        return false;
    }

    const uint32_t branchAbsoluteLinked = 0x48000003 | (hookAddress & 0x03FFFFFC);
    if (current != kPrepareTitleOriginalInstruction && current != branchAbsoluteLinked) {
        snprintf(sInitializeErrorBuffer, sizeof(sInitializeErrorBuffer),
                 "Wii U Raw Launcher: physical PrepareTitle mismatch (FFF184E4=%08X)", current);
        sInitializeError = sInitializeErrorBuffer;
        return false;
    }

    if (current == kPrepareTitleOriginalInstruction) {
        KernWrite(reinterpret_cast<void *>(kPrepareTitleHookAddress), branchAbsoluteLinked);
        if (KernRead(reinterpret_cast<const void *>(kPrepareTitleHookAddress)) != branchAbsoluteLinked) {
            sInitializeError = "Wii U Raw Launcher: failed to install PrepareTitle hook";
            return false;
        }
    }

    sHookInstalled = true;
    sInitialized = true;
    sInitializeError = nullptr;
    return true;
}

} // namespace

bool RawTitleKernel_Initialize() {
    // Boot-safe: privileged access is first used only when a raw title is selected.
    return true;
}

const char *RawTitleKernel_GetInitializeError() {
    return sInitializeError;
}

const char *RawTitleKernel_GetProbe() {
    static char snapshot[480];
    const uint32_t armed = RawTitleKernelArmed;
    snprintf(snapshot, sizeof(snapshot),
             "%s port6 armed=%u applied=%u sdk=%u code=%08X codegen=%08X",
             sProbe, armed != 0, armed ? armed - 1 : 0,
             RawTitleKernelProfile.sdkVersion, RawTitleKernelProfile.maxCodeSize,
             RawTitleKernelProfile.codegenSize);
    return snapshot;
}

bool RawTitleKernel_Arm(const RawTitleProfile &profile) {
    if (!InitializeOnDemand()) {
        return false;
    }

    RawTitleKernelProfile = profile;
    RawTitleKernelArmed = profile.enabled ? 1u : 0u;
    DCFlushRange(&RawTitleKernelProfile, sizeof(RawTitleKernelProfile));
    DCFlushRange(const_cast<uint32_t *>(&RawTitleKernelArmed), sizeof(RawTitleKernelArmed));
    OSMemoryBarrier();
    return sHookInstalled;
}

void RawTitleKernel_Clear() {
    if (!sInitialized) {
        return;
    }
    RawTitleKernelArmed = 0;
    DCFlushRange(const_cast<uint32_t *>(&RawTitleKernelArmed), sizeof(RawTitleKernelArmed));
    OSMemoryBarrier();
    const uint32_t branch = 0x48000003 | reinterpret_cast<uint32_t>(&RawTitleKernelHookEntry);
    if (sHookInstalled && KernRead(reinterpret_cast<void *>(kPrepareTitleHookAddress)) == branch) {
        KernWrite(reinterpret_cast<void *>(kPrepareTitleHookAddress), kPrepareTitleOriginalInstruction);
    }
    sHookInstalled = false;
    sInitialized = false;
}
