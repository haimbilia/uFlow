#pragma once
#include <cstdint>

// Input/system events must be serviced even when a progress frame is skipped.
class PreparationProgress {
public:
    template<class Poll,class Render> bool Tick(std::uint32_t now,Poll poll,Render render){
        if(!poll()){render();return false;}
        if(!rendered_ || std::uint32_t(now-lastFrame_)>=80){
            rendered_=true;lastFrame_=now;render();
        }
        return true;
    }
private:
    bool rendered_=false;
    std::uint32_t lastFrame_=0;
};
