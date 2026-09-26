#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

// HAI's companion contains physical ranges in disc-file order, not file bytes.
// The existing NFS encryption/header/wrapper are retained. This is only the
// serializer: it must not enable launch before device/firmware handoff exists.
namespace HaiCompanion {
struct Extent {
    std::uint32_t sector;
    std::uint32_t count;
};
struct File {
    std::uint64_t bytes;
    std::vector<Extent> extents;
};
constexpr std::size_t MaxExtents = 16384;
constexpr std::uint64_t ImageChunkBytes = 0xfa00000;

// The unencrypted NFS header describes the complete packed image. A directory
// walk ending at the first missing filename cannot distinguish EOF from a gap
// or a truncated copy, so validate against this before exposing physical LBAs.
inline bool ImageBytes(const std::uint8_t *header, std::size_t size, std::uint64_t &bytes) {
    bytes = 0;
    if (!header || size != 512 || std::memcmp(header, "EGGS", 4) || std::memcmp(header + 508, "SGGE", 4)) return false;
    const auto word = [&](std::size_t at) {
        return std::uint32_t(header[at]) << 24 | std::uint32_t(header[at+1]) << 16 |
               std::uint32_t(header[at+2]) << 8 | header[at+3];
    };
    const auto count = word(16);
    if (!count || count > 61) return false;
    std::uint64_t end = 0, sectors = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto start = word(20 + i * 8), length = word(24 + i * 8);
        if (!length || start < end || std::uint64_t(start) + length > 286864) return false;
        end = std::uint64_t(start) + length;
        sectors += length;
    }
    bytes = 512 + sectors * 0x8000;
    return true;
}

inline std::vector<std::uint8_t> Build(const std::vector<File>& files,
                                      std::uint64_t deviceSectors) {
    if (files.empty() || !deviceSectors || deviceSectors > (std::uint64_t{1} << 32))
        throw std::runtime_error("invalid HAI source geometry");
    std::vector<Extent> logical;
    for (const auto& file : files) {
        if (!file.bytes || file.bytes % 512 || file.extents.empty())
            throw std::runtime_error("invalid HAI file size");
        std::uint64_t sectors = 0;
        for (const auto& extent : file.extents) {
            const auto end = std::uint64_t(extent.sector) + extent.count;
            if (!extent.sector || !extent.count || end > deviceSectors)
                throw std::runtime_error("HAI extent outside device");
            sectors += extent.count;
            if (sectors > file.bytes / 512)
                throw std::runtime_error("HAI extent exceeds file");
            if (!logical.empty() && std::uint64_t(logical.back().sector) + logical.back().count == extent.sector &&
                    std::uint64_t(logical.back().count) + extent.count <= UINT32_MAX) {
                logical.back().count += extent.count;
            } else {
                if (logical.size() == MaxExtents)
                    throw std::runtime_error("too many HAI extents");
                logical.push_back(extent);
            }
        }
        if (sectors != file.bytes / 512)
            throw std::runtime_error("incomplete HAI file allocation");
    }
    auto physical = logical;
    std::sort(physical.begin(), physical.end(), [](auto a, auto b) { return a.sector < b.sector; });
    for (std::size_t i = 1; i < physical.size(); ++i)
        if (std::uint64_t(physical[i-1].sector) + physical[i-1].count > physical[i].sector)
            throw std::runtime_error("overlapping HAI source allocation");

    std::vector<std::uint8_t> result;
    result.reserve(8 + logical.size() * 8);
    const auto word = [&](std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
            result.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    word(static_cast<std::uint32_t>(logical.size()));
    word(0x00090009); // 2^9-byte address unit, repeated by Nintendo's builder.
    for (const auto& extent : logical) {
        word(extent.sector);
        word(extent.count);
    }
    return result; // exact buffer size passed to MCP, no additional padding.
}
}
