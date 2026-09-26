#include "../app/source/hai_companion.hpp"
#include <cassert>
#include <cstdio>

using namespace HaiCompanion;
static std::uint32_t Word(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return (std::uint32_t(data.at(offset))<<24) | (std::uint32_t(data.at(offset+1))<<16) |
           (std::uint32_t(data.at(offset+2))<<8) | data.at(offset+3);
}
static void Reject(const std::vector<File>& files, std::uint64_t disk = 1ULL<<32) {
    try { (void)Build(files,disk); assert(false); }
    catch (const std::runtime_error&) {}
}
int main() {
    std::vector<std::uint8_t> header(512);
    std::memcpy(header.data(),"EGGS",4);std::memcpy(header.data()+508,"SGGE",4);
    const auto put=[&](std::size_t at,std::uint32_t n){for(int i=3;i>=0;--i){header[at+i]=n;n>>=8;}};
    put(16,2);put(20,0);put(24,8000);put(28,9000);put(32,16000);
    std::uint64_t bytes=0;
    assert(ImageBytes(header.data(),header.size(),bytes)&&bytes==512+24000ULL*0x8000);
    assert((bytes+ImageChunkBytes-1)/ImageChunkBytes==4); // require all four numbered files
    assert(!ImageBytes(header.data(),511,bytes));
    put(16,62);assert(!ImageBytes(header.data(),512,bytes));put(16,2);
    put(28,7999);assert(!ImageBytes(header.data(),512,bytes));put(28,9000);
    put(32,UINT32_MAX);assert(!ImageBytes(header.data(),512,bytes));put(32,16000);
    header[508]=0;assert(!ImageBytes(header.data(),512,bytes));
    // Actual read-only DKCR USB audit: 14 adjacent NFS files, beyond signed LBA.
    std::vector<File> files;
    for (unsigned i=0; i<14; ++i) {
        const unsigned count = i==13 ? 325632 : 512000;
        files.push_back({std::uint64_t(count)*512, {{2258768864U+i*512000,count}}});
    }
    const auto original = files[0].extents[0].sector;
    const auto result = Build(files,1ULL<<32);
    assert(result.size()==16 && Word(result,0)==1 && Word(result,4)==0x00090009);
    assert(Word(result,8)==2258768864U && Word(result,12)==6981632);
    assert(files[0].extents[0].sector==original);
    // Non-monotonic physical layout must retain logical file order.
    const auto fragmented = Build({{2048,{{200,2},{100,2}}},{512,{{500,1}}}},1000);
    assert(Word(fragmented,0)==3 && Word(fragmented,8)==200 && Word(fragmented,16)==100);
    assert(Word(fragmented,24)==500);
    assert(Build({{512,{{UINT32_MAX,1}}}},1ULL<<32).size()==16);
    Reject({}); Reject({{0,{{1,1}}}}); Reject({{513,{{1,2}}}});
    Reject({{512,{}}}); Reject({{512,{{0,1}}}}); Reject({{512,{{1,0}}}});
    Reject({{1024,{{1,1}}}}); Reject({{512,{{1,2}}}});
    Reject({{1024,{{UINT32_MAX,2}}}}); Reject({{512,{{10,1}}}},10);
    Reject({{512,{{1,1}}}},(1ULL<<32)+1);
    Reject({{2048,{{100,2},{101,2}}}});
    Reject({{512,{{100,1}}},{512,{{100,1}}}});
    File excess{(MaxExtents+1)*512,{}};
    for (std::size_t i=0;i<MaxExtents+1;++i) excess.extents.push_back({static_cast<unsigned>(i*2+1),1});
    Reject({excess});
    std::puts("PASS: HAI companion bytes, real DKCR geometry, fragmentation/order, unsigned LBAs, bounds and overlaps");
}
