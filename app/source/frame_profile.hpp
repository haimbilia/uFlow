#pragma once
#include "saved_file.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

// UI-thread-only recorder. Fixed storage, no allocation, locks or disk I/O in
// Begin/Mark/End. A report is written explicitly after the render loop stops.
namespace FrameProfile {
enum Stage { Update, Artwork, Video, Draw, Compose, Present, Settings, Upload, Count };
inline constexpr const char *Names[]={"update","artwork_requests","video","scene_draw","compose_overlays","present_flip","settings","texture_upload"};
struct Sample {
    std::array<uint64_t,Count> us{};
    uint64_t inputUs=0,totalUs=0,number=0;
    int skin=0,view=0,layout=0,effects=-1;
};
class Recorder {
    Sample current_{};
    uint64_t last_=0;
    Stage stage_=Update;
    bool active_=false;
public:
    uint64_t menuSoundMaxUs=0;
    uint64_t frames=0,totalUs=0,maxUs=0,saveCount=0,saveWriteMaxUs=0,saveTraceMaxUs=0;
    std::array<uint64_t,Count> sums{},maxima{};
    std::array<uint64_t,5> buckets{};
    std::array<Sample,16> worst{};
    size_t worstCount=0;
    struct InputCounts { unsigned polls=0, samples=0, triggers=0, accepted=0; };
    std::array<InputCounts,2> controllerCounts{};
    bool sawProController=false;
    void ControllerInput(bool proSample,bool padSample,bool padTrigger,bool padAccepted){
        sawProController |= proSample;
        auto& counts=controllerCounts[sawProController ? 1 : 0];
        ++counts.polls; counts.samples+=padSample; counts.triggers+=padTrigger; counts.accepted+=padAccepted;
    }
    void Begin(uint64_t now,uint64_t input,int skin,int view,int layout,int effects=-1){
        current_={};current_.inputUs=input;current_.skin=skin;current_.view=view;current_.layout=layout;current_.effects=effects;
        last_=now;stage_=Update;active_=true;
    }
    void Mark(Stage next,uint64_t now){if(!active_)return;current_.us[stage_]+=now>=last_?now-last_:0;last_=now;stage_=next;}
    void End(uint64_t now){
        if(!active_)return;Mark(stage_,now);active_=false;current_.number=++frames;
        current_.totalUs=current_.inputUs;
        for(int i=0;i<Count;++i){current_.totalUs+=current_.us[i];sums[i]+=current_.us[i];maxima[i]=std::max(maxima[i],current_.us[i]);}
        totalUs+=current_.totalUs;maxUs=std::max(maxUs,current_.totalUs);
        ++buckets[current_.totalUs<=16667?0:current_.totalUs<=33334?1:current_.totalUs<=100000?2:current_.totalUs<=1000000?3:4];
        if(worstCount<worst.size())worst[worstCount++]=current_;
        else {auto low=std::min_element(worst.begin(),worst.end(),[](const auto&a,const auto&b){return a.totalUs<b.totalUs;});if(current_.totalUs>low->totalUs)*low=current_;}
    }
    void SaveTiming(uint64_t writeUs,uint64_t traceUs){++saveCount;saveWriteMaxUs=std::max(saveWriteMaxUs,writeUs);saveTraceMaxUs=std::max(saveTraceMaxUs,traceUs);}
    bool Write(const std::string &path) const {
        const auto temp=path+".tmp";auto *file=std::fopen(temp.c_str(),"wb");if(!file)return false;
        std::fprintf(file,"uFlow frame profile v1; times in microseconds; written after rendering stops\nframes=%llu total_us=%llu max_us=%llu\n",(unsigned long long)frames,(unsigned long long)totalUs,(unsigned long long)maxUs);
        std::fprintf(file,"buckets <=16.667ms <=33.334ms <=100ms <=1s >1s:");for(auto n:buckets)std::fprintf(file," %llu",(unsigned long long)n);std::fputs("\n",file);
        for(int i=0;i<Count;++i)std::fprintf(file,"stage=%s total_us=%llu max_us=%llu\n",Names[i],(unsigned long long)sums[i],(unsigned long long)maxima[i]);
        std::fprintf(file,"settings_saves=%llu write_max_us=%llu trace_max_us=%llu\n",(unsigned long long)saveCount,(unsigned long long)saveWriteMaxUs,(unsigned long long)saveTraceMaxUs);
        std::fprintf(file,"menu_sound_max_us=%llu\n",(unsigned long long)menuSoundMaxUs);
        for(unsigned i=0;i<controllerCounts.size();++i){
            const auto& c=controllerCounts[i];
            std::fprintf(file,"gamepad_%s_first_pro_sample polls=%u samples=%u trigger_frames=%u routed_press_frames=%u\n",
                i?"after":"before",c.polls,c.samples,c.triggers,c.accepted);
        }
        for(size_t i=0;i<worstCount;++i){const auto&s=worst[i];std::fprintf(file,"frame=%llu skin=%d view=%d layout=%d effects=%d total_us=%llu input_us=%llu",(unsigned long long)s.number,s.skin,s.view,s.layout,s.effects,(unsigned long long)s.totalUs,(unsigned long long)s.inputUs);for(int j=0;j<Count;++j)std::fprintf(file," %s=%llu",Names[j],(unsigned long long)s.us[j]);std::fputs("\n",file);}
        bool ok=std::ferror(file)==0;if(std::fclose(file)!=0)ok=false;
        if(ok&&SavedFile::Commit(temp,path))return true;
        std::remove(temp.c_str());return false;
    }
};
class Scope {
    Recorder &recorder_;uint64_t (*clock_)();
public:
    Scope(Recorder&r,uint64_t(*clock)(),uint64_t input,int skin,int view,int layout,int effects=-1):recorder_(r),clock_(clock){r.Begin(clock_(),input,skin,view,layout,effects);}
    ~Scope(){recorder_.End(clock_());}
    void Mark(Stage stage){recorder_.Mark(stage,clock_());}
};
}
