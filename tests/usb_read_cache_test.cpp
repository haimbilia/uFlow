#include "usb_read_cache.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
static void Pattern(unsigned char *out,std::uint32_t sector,std::uint32_t count){
    for(std::uint32_t i=0;i<count*512;++i)out[i]=static_cast<unsigned char>((sector+i/512)*31+i%512);
}
int main(){
    USBReadCache<2,2> cache;
    std::vector<unsigned char> data(300*512+7),expected(data.size());unsigned reads=0;bool fail=false;
    auto raw=[&](unsigned char *out,std::uint32_t sector,std::uint32_t count){assert(count<=128);++reads;if(fail)return false;Pattern(out,sector,count);return true;};
    auto read=[&](unsigned sector,unsigned count){assert(cache.ReadSectors(data.data()+3,sector,count,raw));Pattern(expected.data()+3,sector,count);assert(data==expected);};
    read(1,1);read(1,1);assert(reads==1 && cache.Statistics().hits==1);
    read(3,1);read(1,1);read(5,1);auto before=reads;read(1,1);assert(reads==before);read(3,1);assert(reads==before+1);
    read(1,260); // multi-batch and unaligned destination
    for(unsigned i=0;i<400;++i)read((i*7919)%1000,1+i%24);
    cache.Invalidate();before=reads;read(10,1);assert(reads==before+1);
    fail=true;assert(!cache.ReadSectors(data.data(),8000,2,raw));fail=false;
    before=reads;read(10,1);assert(reads==before+1); // failed I/O invalidates old cached data
    assert(!cache.ReadSectors(data.data(),UINT32_MAX,2,raw));
    assert(!cache.ReadSectors(nullptr,0,1,raw));assert(!cache.ReadSectors(data.data(),0,0,raw));
    cache.Reset();assert(cache.Statistics().reads==0 && cache.Statistics().hits==0);
    unsigned char value=21;
    auto swapped=[&](unsigned char *out,std::uint32_t,std::uint32_t count){std::memset(out,value,count*512);return true;};
    assert(cache.ReadSectors(data.data(),0,1,swapped) && data[0]==21);
    cache.Reset();value=42;assert(cache.ReadSectors(data.data(),0,1,swapped) && data[0]==42);
    puts("PASS: exact cached reads, hits, LRU collisions, multi-sector batches, arbitrary alignment, errors, overflow and disk-change invalidation");
}
