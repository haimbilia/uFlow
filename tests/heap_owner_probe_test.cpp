#include "heap_owner_probe.hpp"
#include <cassert>
#include <array>
#include <cstdio>

int main() {
    HeapOwnerProbe::Detector d;
    assert(!d.Sample(0,1000));
    assert(!d.Sample(0x1000,1100));
    assert(!d.Sample(0x1000,1349));
    assert(d.Sample(0x1000,1350));
    assert(!d.Sample(0x1000,9999)); // Bounded: no repeated log while stuck.
    assert(!d.Sample(0,10000));
    assert(!d.Sample(0x1000,10001));
    assert(d.Sample(0x1000,10251));
    assert(!d.Sample(0x2000,10300));
    assert(!d.Sample(0x2000,1)); // Clock reset is not a huge elapsed duration.
    assert(d.Sample(0x2000,251));
    std::array<unsigned,12> words{0x1010,0xaaa,0,0,0x1020,0xbbb,0,0,0,0xccc,0,0};
    unsigned reads=0;
    auto read=[&](unsigned address,unsigned &value) {
        assert(address>=0x1000 && address<0x1030 && !(address&3));
        ++reads;value=words[(address-0x1000)/4];return true;
    };
    HeapOwnerProbe::Frame frames[3]{};
    auto walk=[&](unsigned sp,unsigned low,unsigned high,unsigned capacity=3) {
        reads=0;return HeapOwnerProbe::Stack(sp,low,high,frames,capacity,read);
    };
    assert(walk(0x1000,0x1000,0x1030)==3 && reads==6 && frames[2].link==0xccc);
    assert(walk(0x1000,0x1000,0x1030,1)==1 && reads==2);
    assert(!walk(0x0ffc,0x1000,0x1030) && !reads);
    assert(!walk(0x1001,0x1000,0x1030) && !reads);
    assert(!walk(0x102c,0x1000,0x1030) && !reads);
    assert(!walk(0xfffffffc,0x1000,0xffffffff) && !reads);
    words[4]=0x1000;assert(walk(0x1000,0x1000,0x1030)==2); // Cycle.
    words[0]=0xfffffff0;assert(walk(0x1000,0x1000,0x1030)==1);
    assert(!HeapOwnerProbe::Stack(0x1000,0x1000,0x1030,frames,3,
        [](unsigned,unsigned &){return false;})); // Unmapped/owner changed.
    puts("PASS: owner-change/reset/one-shot detection and bounded corrupt/unmapped stack capture");
}
