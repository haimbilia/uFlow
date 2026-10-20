#pragma once
#include <cstdint>
#include <bit>
// Release the activation button first, then retain the largest simultaneous chord.
struct BindingCapture {
    static constexpr uint32_t Allowed=0x0ff6fffcu;
    bool waitingRelease=true;
    uint32_t candidate=0;
    void Begin(){waitingRelease=true;candidate=0;}
    uint32_t Update(uint32_t held) {
        held &= Allowed;
        if(waitingRelease){if(!held)waitingRelease=false;return 0;}
        if(held){if(std::popcount(held)>=std::popcount(candidate))candidate=held;return 0;}
        const auto result=candidate;candidate=0;return result;
    }
};
