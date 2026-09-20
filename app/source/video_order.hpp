#pragma once
#include <array>
#include <cstddef>
#include <optional>

// Decode-order H.264 can return a future reference picture before the B pictures
// preceding it. DTS is a safe presentation watermark, not the picture's PTS.
class VideoFrameOrder {
public:
    static constexpr std::size_t Capacity=18;
    struct Entry { double pts; unsigned slot; };
    bool Push(double pts, unsigned slot) {
        if(count_==entries_.size()) return false;
        std::size_t at=count_++;
        while(at && entries_[at-1].pts>pts) { entries_[at]=entries_[at-1]; --at; }
        entries_[at]={pts,slot}; return true;
    }
    std::optional<Entry> PopReady(double watermark, bool flush=false) {
        if(!count_ || (!flush && entries_[0].pts>watermark+0.000001)) return {};
        const auto result=entries_[0];
        for(std::size_t i=1;i<count_;++i) entries_[i-1]=entries_[i];
        --count_; return result;
    }
    std::size_t Size() const { return count_; }
private:
    std::array<Entry,Capacity> entries_{};
    std::size_t count_=0;
};
