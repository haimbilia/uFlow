#include "wii_return.hpp"
#include <gccore.h>
#include <ogc/ipc.h>

// d2x ES_SET_FAKE_SM_LAUNCH accepts a FULL 64-bit title ID. WiiFlow's
// D2X_PatchReturnTo helper hardcodes 00010001 (user channels), which is wrong
// for HCVA: the built-in Wii U Menu return channel is 00010002-48435641.
// This changes the running cIOS's state only; no NAND/config writes.
int UFlowPatchReturnToWiiU() {
    const s32 handle = IOS_Open("/dev/es", 0);
    if (handle < 0) return handle;
    static u64 title[4] ATTRIBUTE_ALIGN(32) = {};
    static ioctlv vector[4] ATTRIBUTE_ALIGN(32) = {};
    title[0] = 0x0001000248435641ULL;
    vector[0].data = title;
    vector[0].len = sizeof(u64);
    DCFlushRange(title, sizeof(title));
    DCFlushRange(vector, sizeof(vector));
    const s32 result = IOS_Ioctlv(handle, 0xA1, 1, 0, vector);
    IOS_Close(handle);
    return result;
}
