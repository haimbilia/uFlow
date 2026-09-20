#pragma once

#include <stdint.h>

static inline uint16_t Read16BE(const uint8_t* p) {
    return (p[0] << 8) | p[1];
}

static inline uint32_t Read32BE(const uint8_t* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static inline uint64_t Read64BE(const uint8_t* p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  | ((uint64_t)p[7]);
}

static inline uint32_t GetPayloadOffset(const uint8_t* data) {
    uint32_t sigType = Read32BE(data);
    if (sigType == 0x00010000) return 0x240;
    if (sigType == 0x00010001) return 0x140;
    if (sigType == 0x00010002) return 0x80;
    return 0x140;
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define ToBE16(x) __builtin_bswap16(x)
#define ToBE32(x) __builtin_bswap32(x)
#define ToBE64(x) __builtin_bswap64(x)
#else
#define ToBE16(x) (x)
#define ToBE32(x) (x)
#define ToBE64(x) (x)
#endif

#define FromBE16(x) ToBE16(x)
#define FromBE32(x) ToBE32(x)
#define FromBE64(x) ToBE64(x)
