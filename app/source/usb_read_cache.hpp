#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>

// Read-only, process-local sector cache. Caller serializes access and invalidates
// it when closing/changing the disk. Cache exact reads, never speculate past EOF.
template<std::size_t Sets=1024,std::size_t Ways=4>
class USBReadCache {
    static_assert(Sets>0 && Ways>0,"Sector cache needs at least one slot");
public:
    struct Stats {std::uint64_t requested=0,hits=0,reads=0,sectors=0,errors=0;};
    void Invalidate(){for(auto &set:sets_)for(auto &entry:set)entry.valid=false;}
    void Reset(){Invalidate();stats_={};clock_=0;}
    const Stats &Statistics() const{return stats_;}
    template<class Read> bool ReadSectors(unsigned char *out,std::uint32_t sector,std::uint32_t count,Read read){
        if(!out || !count || std::uint64_t(sector)+count>std::uint64_t(UINT32_MAX)+1)return false;
        stats_.requested+=count;
        while(count){
            if(auto *entry=Find(sector)){
                std::memcpy(out,entry->data.data(),512);entry->age=++clock_;++stats_.hits;
                ++sector;--count;out+=512;continue;
            }
            // Preserve multi-sector reads; split only at an already cached sector.
            std::uint32_t batch=1;
            while(batch<std::min(count,128u) && !Find(sector+batch))++batch;
            ++stats_.reads;stats_.sectors+=batch;
            if(!read(out,sector,batch)){++stats_.errors;Invalidate();return false;}
            for(std::uint32_t i=0;i<batch;++i)Store(sector+i,out+i*512);
            sector+=batch;count-=batch;out+=batch*512;
        }
        return true;
    }
private:
    struct Entry {std::array<unsigned char,512> data;std::uint32_t sector=0;std::uint64_t age=0;bool valid=false;};
    Entry *Find(std::uint32_t sector){
        for(auto &entry:sets_[sector%Sets])if(entry.valid && entry.sector==sector)return &entry;
        return nullptr;
    }
    void Store(std::uint32_t sector,const unsigned char *data){
        auto &set=sets_[sector%Sets];auto *victim=&set[0];
        for(auto &entry:set){if(!entry.valid){victim=&entry;break;}if(entry.age<victim->age)victim=&entry;}
        std::memcpy(victim->data.data(),data,512);victim->sector=sector;victim->age=++clock_;victim->valid=true;
    }
    std::array<std::array<Entry,Ways>,Sets> sets_;
    std::uint64_t clock_=0;
    Stats stats_;
};
