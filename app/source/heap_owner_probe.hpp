#pragma once
#include <cstdint>
#include <cstddef>

namespace HeapOwnerProbe {
// Observational only: a repeated address is evidence to capture, not proof that
// ownership was continuous between samples. Never repair or release the lock.
struct Detector {
    std::uint32_t owner=0;
    std::uint64_t since=0;
    bool reported=false;
    bool Sample(std::uint32_t current,std::uint64_t milliseconds) {
        if(!current || current!=owner || milliseconds<since) {
            owner=current;since=milliseconds;reported=false;return false;
        }
        if(reported || milliseconds-since<250)return false;
        reported=true;return true;
    }
};
struct Frame { std::uint32_t address,link; };
// Copy a bounded PPC back-chain, tolerating a stopped, changing or damaged stack.
// The reader validates mapped memory and continued ownership on every access.
template<class Reader>
std::size_t Stack(std::uint32_t sp,std::uint32_t low,std::uint32_t high,
                  Frame *output,std::size_t capacity,Reader read) {
    std::size_t count=0;
    while(count<capacity && low<high && sp>=low && sp<high &&
          !(sp&3u) && high-sp>=8) {
        std::uint32_t next=0,link=0;
        if(!read(sp,next) || !read(sp+4,link))break;
        output[count++]={sp,link};
        if(next<=sp)break;
        sp=next;
    }
    return count;
}
}
