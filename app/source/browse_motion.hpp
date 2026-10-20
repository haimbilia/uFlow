#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

inline float AdvanceBookshelfPosition(float position,float target,float seconds,int speed,bool rapid=false) {
    const float duration=rapid?(speed==0?.14f:speed==1?.12f:.10f):(speed==0?1.6f:speed==1?1.3f:1.f);
    const float distance=target-position;
    const float amount=std::min(std::abs(distance),std::max(1.f,std::abs(distance))*std::max(0.f,seconds)/duration);
    return position+std::copysign(amount,distance);
}

namespace BrowseMotion {
struct Bookshelf {
    bool rapid=false;
    float Advance(float position,float target,float seconds,int speed) {
        // Repeated inputs/pages must not queue seconds of individual spins.
        // Keep catch-up active through the last cover, until it is fully at rest.
        rapid=rapid || std::abs(target-position)>1.01f;
        position=AdvanceBookshelfPosition(position,target,seconds,speed,rapid);
        if(std::abs(target-position)<.002f){position=target;rapid=false;}
        return position;
    }
};
inline std::uint32_t InitialDelay(int speed) {return speed==0?210:speed==1?170:130;}
struct Repeat {int stride;std::uint32_t interval;};
inline Repeat HeldRepeat(std::uint32_t elapsed,int speed) {
    const std::uint32_t top=speed==0?900:speed==1?650:450;
    return elapsed>=top?Repeat{4,38}:elapsed>=top/2?Repeat{2,62}:Repeat{1,105};
}
}
