#include "browse_motion.hpp"
#include <cassert>
#include <iostream>

int main() {
    float worstStop=0,worstFastStop=0,worstFastRamp=0;
    for(int speed:{0,1,2})for(int fps:{10,15,30,60,120})for(int direction:{-1,1})
    for(float hold:{.9f,3.f,8.f})for(bool wrap:{false,true}) {
        BrowseMotion::Bookshelf motion;
        constexpr int count=1348;
        int target=direction>0?1320:28;
        float position=float(target),topAt=-1;
        const float dt=1.f/fps;
        std::uint32_t next=BrowseMotion::InitialDelay(speed);
        const auto move=[&](int amount){
            const int logical=target+amount;
            target=wrap?(logical%count+count)%count:std::clamp(logical,0,count-1);
            if(wrap)position+=target-logical;
        };
        move(direction);
        for(int frame=0;frame<int(hold*fps);++frame) {
            const auto now=std::uint32_t(std::lround(frame*1000.f/fps));
            if(frame && now>=next) {
                const auto repeat=BrowseMotion::HeldRepeat(now,speed);
                if(repeat.stride==4 && topAt<0)topAt=frame*dt;
                move(direction*repeat.stride);next=now+repeat.interval;
            }
            position=motion.Advance(position,float(target),dt,speed);
        }
        float stopped=0;
        for(int frame=0;position!=float(target) && frame<2*fps;++frame) {
            const float before=std::abs(target-position);
            position=motion.Advance(position,float(target),dt,speed);stopped+=dt;
            assert(std::abs(target-position)<=before);
        }
        assert(position==float(target) && !motion.rapid);
        assert(stopped<=1.f);
        worstStop=std::max(worstStop,stopped);
        if(speed==2){assert(topAt>=0 && topAt<=.6f);worstFastStop=std::max(worstFastStop,stopped);worstFastRamp=std::max(worstFastRamp,topAt);}
        // Reversing a moving shelf changes direction immediately, without overshoot.
        position=float(target)-direction*20;motion.rapid=true;
        const float reverse=position-direction*4;
        const float moved=motion.Advance(position,reverse,dt,speed);
        assert((moved-position)*direction<0);
    }
    // Even an exceptionally large backlog must stop within one second on Fast.
    for(int fps:{10,15,30,60,120})for(int direction:{-1,1}) {
        BrowseMotion::Bookshelf motion;float position=0,elapsed=0;
        for(int frame=0;position!=direction*1347.f && frame<fps;++frame) {
            position=motion.Advance(position,direction*1347.f,1.f/fps,2);elapsed+=1.f/fps;
        }
        assert(position==direction*1347.f && elapsed<=1.f && !motion.rapid);
    }
    // One deliberate step still has time to show the complete box roll.
    for(int speed:{0,1,2}) {
        BrowseMotion::Bookshelf motion;float position=0,elapsed=0;
        while(position!=1.f && elapsed<2.f){position=motion.Advance(position,1,.01f,speed);elapsed+=.01f;}
        const float expected=speed==0?1.6f:speed==1?1.3f:1.f;
        assert(std::abs(elapsed-expected)<.03f && !motion.rapid);
    }
    std::cout << "PASS: held scrolling, release, reverse, wrap/end stops, 10-120 fps, all speeds; worst stop "
              << worstStop << " s; Fast stop " << worstFastStop << " s, top-speed ramp " << worstFastRamp
              << " s; 1347-cover backlog settles within 1 s; single-step roll preserved\n";
}
