#include "frame_profile.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
static uint64_t ticks=0;
static uint64_t Clock(){return ticks;}
int main(){
    FrameProfile::Recorder r;
    {FrameProfile::Scope frame(r,Clock,3000,5,3,1);ticks=1000;frame.Mark(FrameProfile::Draw);ticks=5000;frame.Mark(FrameProfile::Present);ticks=9000;}
    assert(r.frames==1&&r.maxUs==12000&&r.sums[FrameProfile::Update]==1000&&r.sums[FrameProfile::Draw]==4000&&r.sums[FrameProfile::Present]==4000);
    for(int i=0;i<100;++i){r.Begin(0,0,5,0,1);r.Mark(FrameProfile::Settings,100);r.End(i==50?6000100:1000);}
    assert(r.frames==101&&r.worstCount==16&&r.maxUs==6000100&&r.buckets[4]==1);
    assert(r.maxima[FrameProfile::Settings]==6000000);
    r.SaveTiming(5900000,80000);assert(r.saveWriteMaxUs==5900000&&r.saveTraceMaxUs==80000);
    const auto path=(std::filesystem::temp_directory_path()/"uflow-frame-profile-test.log").string();
    r.ControllerInput(false,true,true,true);
    r.ControllerInput(true,false,false,false);
    r.ControllerInput(false,true,true,false);
    assert(r.Write(path));std::ifstream input(path);std::string report((std::istreambuf_iterator<char>(input)),{});
    assert(report.find("settings=6000000")!=std::string::npos&&report.find("input_us=3000")!=std::string::npos);
    assert(report.find("gamepad_before_first_pro_sample polls=1 samples=1 trigger_frames=1 routed_press_frames=1")!=std::string::npos);
    assert(report.find("gamepad_after_first_pro_sample polls=2 samples=1 trigger_frames=1 routed_press_frames=0")!=std::string::npos);
    assert(!r.Write(path+"/missing/report"));std::filesystem::remove(path);
    std::cout<<"PASS: input/stage accounting, early scope exit, worst-frame retention, save versus trace attribution, safe report write\n";
}
