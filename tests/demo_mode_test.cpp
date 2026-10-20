#include "demo_mode.hpp"
#include <cassert>
#include <set>
#include <iostream>
int main(){
    DemoMode demo;demo.Seed(123);demo.Reset(100);
    assert(!demo.Tick(false,true,30100,30));
    assert(!demo.Tick(true,true,60099,30));
    assert(demo.Tick(true,true,60100,30) && demo.Active());
    assert(!demo.Tick(true,true,999999,30)); // Playback duration never advances the cycle.
    assert(!demo.Tick(true,false,999999,30) && !demo.Active());
    assert(!demo.Tick(true,true,1000000,30));
    demo.Reset(0xfffffff0u);assert(!demo.Tick(true,true,4983,5));assert(demo.Tick(true,true,4984,5));
    assert(demo.Choose({},0)==DemoMode::None);
    assert(demo.Choose({7},7)==7);
    std::set<std::size_t> seen;std::size_t current=7;
    for(int i=0;i<100;++i){auto next=demo.Choose({2,7,19},current);assert(next!=current);assert(next==2||next==7||next==19);seen.insert(next);current=next;}
    assert(seen.size()==3);
    demo.Reset(10);assert(!demo.Active());assert(!demo.Tick(true,true,5009,5));assert(demo.Tick(true,true,5010,5));
    std::cout<<"PASS: inactivity/disable/menu reset, rollover, no timed video cutoff, empty/singleton/random video selection without immediate repeats\n";
}
