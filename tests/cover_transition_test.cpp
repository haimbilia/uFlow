#include "cover_transition.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    constexpr float tau = 6.28318530718f;
    const auto start = CoverTransition(0), end = CoverTransition(1);
    assert(start.travel == 0 && std::abs(start.yaw-.055f) < .00001f);
    assert(end.travel == 1 && std::abs(end.yaw-tau-.48f) < .00001f);
    assert(end.roll > 0 && end.pitch < 0);
    assert(std::cos(CoverTransition(.5f).yaw) < 0); // real back-facing midpoint
    float previous = start.yaw;
    for (int i=1; i<=100; ++i) {
        const auto pose = CoverTransition(i/100.f);
        assert(pose.yaw >= previous); previous = pose.yaw;
    }
    for (int fps : {30,60,120}) {
        assert(CoverTransitionDuration(0) > CoverTransitionDuration(1));
        assert(CoverTransitionDuration(1) > CoverTransitionDuration(2));
        assert(CoverTransitionDuration(1) == .45f);
        float p = 0;
        const float duration = CoverTransitionDuration(1);
        for (int i=0;i<std::ceil(fps*duration)+1;++i) p = AdvanceCoverTransition(p,true,1.f/fps,duration);
        assert(p == 1);
        for (int i=0;i<std::ceil(fps*duration)+1;++i) p = AdvanceCoverTransition(p,false,1.f/fps,duration);
        assert(p == 0);
    }
    const float reversed = AdvanceCoverTransition(.5f,false,.016f,.8f);
    assert(reversed < .5f && reversed > .45f);
    const auto front = CoverInspect(1,0), back = CoverInspect(1,1), halfway = CoverInspect(1,.5f);
    assert(front.travel == 1 && std::cos(front.yaw) > .99f);
    assert(std::cos(back.yaw) < -.99f && std::cos(halfway.yaw) < .1f);
    assert(back.pitch < 0 && back.roll == 0);
    std::cout << "PASS cover pose, full turn, inspect front/back, timing at 30/60/120 FPS, reverse\n";
}
