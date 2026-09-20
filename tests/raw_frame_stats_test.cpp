#include "RawFrameStats.h"
#include <cassert>
#include <iostream>

int main() {
    RawFrameStats stats;
    assert(!stats.sample(1000000));
    assert(!stats.sample(1016667));
    assert(stats.intervals == 1 && stats.over25ms == 0);
    assert(!stats.sample(1050000));
    assert(stats.over25ms == 1 && stats.over50ms == 0);
    assert(stats.sample(6000000));
    assert(stats.intervals == 3 && stats.over50ms == 1 && stats.maxGapUs == 4950000);
    stats.nextWindow();
    assert(stats.first == 6000000 && stats.intervals == 0 && stats.maxGapUs == 0);
    assert(!stats.sample(1));
    assert(stats.first == 1 && stats.intervals == 0);
    assert(stats.sample(1ULL + UINT32_MAX + 10));
    assert(stats.maxGapUs == UINT32_MAX);
    std::cout << "PASS: frame intervals, thresholds, windows, reset and saturation\n";
}
