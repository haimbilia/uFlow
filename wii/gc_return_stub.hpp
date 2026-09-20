#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Exact upstream WiiFlow 5.6.2 wfstub.bin, loaded at 0x80001800. Nintendont
// preserves this STUBHAXX program and restores/runs it when leaving a GC game.
// At +0xf30 it loads r3:r4 = 00010008:57494948 (WIIH), then calls ES_LaunchTitle.
// Keep its IOS58 reload, ticket handling, and System Menu fallback unchanged.
inline bool PatchGameCubeReturnStub(uint8_t *stub, size_t size) {
    constexpr uint8_t header[] = {0x48,0x00,0x00,0x10,'S','T','U','B','H','A','X','X'};
    constexpr uint8_t expected[] = {
        0x3c,0x60,0x00,0x01, // lis r3,1
        0x60,0x63,0x00,0x08, // ori r3,r3,8
        0x3c,0x80,0x57,0x49, // lis r4,0x5749
        0x60,0x84,0x49,0x48, // ori r4,r4,0x4948
        0x4b,0xff,0xfe,0x99, // bl ES_LaunchTitle
    };
    constexpr size_t offset = 0xf30;
    if (!stub || size != 0x1024 || std::memcmp(stub,header,sizeof(header)) != 0 ||
        std::memcmp(stub+offset,expected,sizeof(expected)) != 0) return false;
    // Full title 00010002-48435641 (HCVA: built-in Return to Wii U Menu).
    stub[offset+7]=0x02;
    stub[offset+10]=0x48; stub[offset+11]=0x43;
    stub[offset+14]=0x56; stub[offset+15]=0x41;
    return true;
}
