#pragma once
#include <cstdint>

// Submission intervals, not displayed FPS. Zero timestamp is the reset state.
struct RawFrameStats {
    uint64_t first = 0, last = 0;
    uint32_t intervals = 0, over25ms = 0, over50ms = 0, maxGapUs = 0;
    bool sample(uint64_t now) {
        if (!last || now < last) {
            *this = {};
            first = last = now;
            return false;
        }
        const uint64_t gap = now - last;
        last = now;
        ++intervals;
        if (gap > 25000) ++over25ms;
        if (gap > 50000) ++over50ms;
        const uint32_t boundedGap = gap > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(gap);
        if (boundedGap > maxGapUs) maxGapUs = boundedGap;
        return now - first >= 5000000;
    }
    void nextWindow() { first = last; intervals = over25ms = over50ms = maxGapUs = 0; }
};
