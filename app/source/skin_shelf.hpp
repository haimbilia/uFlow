#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace SkinShelf {
// Bounded, frame-rate-independent transitions. Reversals continue from the
// current pose; rapid browsing never queues old selection animations.
struct Motion {
    std::array<float,60> progress{};
    void Reset(){progress.fill(0);}
    void Advance(int selected,int count,float seconds,int durationMs){
        const float step=std::clamp(seconds,0.f,.05f)*1000.f/std::max(1,durationMs);
        for(int i=0;i<60;++i){const float target=i<count&&i==selected?1.f:0.f;
            progress[i]+=std::clamp(target-progress[i],-step,step);}
    }
    float Focus(int slot)const {const float p=progress.at(slot);return p*p*(3-2*p);}
};
}
