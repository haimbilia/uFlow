#pragma once
#include <cstdint>
#include <limits>
#include <vector>

class DemoMode {
public:
    static constexpr int MinSeconds=5,MaxSeconds=600,DefaultSeconds=30;
    static constexpr std::size_t None=std::numeric_limits<std::size_t>::max();
    void Seed(std::uint32_t seed){random_=seed?seed:1;}
    void Reset(std::uint32_t now){since_=now;armed_=true;active_=false;}
    bool Active() const{return active_;}
    bool Tick(bool enabled,bool eligible,std::uint32_t now,int seconds) {
        if(!enabled || !eligible || !armed_){Reset(now);return false;}
        if(active_ || now-since_<std::uint32_t(seconds)*1000)return false;
        active_=true;return true;
    }
    std::size_t Choose(const std::vector<std::size_t> &candidates,std::size_t current) {
        if(candidates.empty())return None;
        const bool exclude=candidates.size()>1;
        std::size_t count=0;for(auto i:candidates)if(!exclude || i!=current)++count;
        random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;
        auto choice=random_%count;
        for(auto i:candidates)if(!exclude || i!=current){if(choice--==0)return i;}
        return None;
    }
private:
    std::uint32_t since_=0,random_=1;
    bool armed_=false,active_=false;
};
