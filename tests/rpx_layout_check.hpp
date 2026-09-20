#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Independent on-disk check: do not use Rpx's decoder or packing-order helper.
// Wii U loader grouping follows devkitPro/wut-tools calculateSectionOffsets.
inline void CheckRpxLayout(const std::string &file) {
    auto need=[](bool ok){if(!ok)throw std::runtime_error("invalid Wii U RPX physical section layout");};
    auto be=[&](size_t at,size_t n){need(at<=file.size()&&n<=file.size()-at);uint32_t v=0;
        while(n--)v=(v<<8)|uint8_t(file[at++]);return v;};
    const auto table=be(32,4),count=be(48,2);need(count>0&&be(46,2)==40);
    struct Entry{uint32_t offset,size,type;int group;};std::vector<Entry> physical;
    for(size_t i=0;i<count;++i){const auto at=table+i*40;
        const auto type=be(at+4,4),flags=be(at+8,4),offset=be(at+16,4),size=be(at+20,4);
        if(type==0||type==8||size==0){need(offset==0);continue;}
        need(offset>=table+count*40&&offset<=file.size()&&size<=file.size()-offset);
        int group=-1;
        if(type==0x80000003)group=0;
        else if(type==0x80000004)group=1;
        else if(type!=0x80000002&&(flags&7)==3)group=2;
        else if((flags&2)&&(type==0x80000001||type==0x80000002||!(flags&5)))group=3;
        else if((flags&4)&&type!=0x80000001&&type!=0x80000002)group=4;
        else if(!(flags&6)&&type!=0x80000002)group=5;
        need(group>=0);physical.push_back({offset,size,type,group});
    }
    std::sort(physical.begin(),physical.end(),[](const auto &a,const auto &b){return a.offset<b.offset;});
    need(physical.size()>=2&&physical[0].type==0x80000003&&physical[1].type==0x80000004);
    need(physical[1].offset+physical[1].size<=table+count*40+4096);
    for(size_t i=1;i<physical.size();++i){need(physical[i-1].group<=physical[i].group);
        need(uint64_t(physical[i-1].offset)+physical[i-1].size<=physical[i].offset);}
}
