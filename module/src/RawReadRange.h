#pragma once
#include <cstdint>

// FSReadFile returns an element count, not a byte count. Never flush beyond
// the requested/returned data, including on errors and malformed results.
constexpr uint32_t RawReadBytes(int32_t result, uint32_t size, uint32_t count) {
    if (result <= 0 || size == 0 || static_cast<uint32_t>(result) > count) return 0;
    const uint64_t bytes = static_cast<uint64_t>(result) * size;
    return bytes <= UINT32_MAX ? static_cast<uint32_t>(bytes) : 0;
}
