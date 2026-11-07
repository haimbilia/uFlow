#pragma once
#include <atomic>
#include <cstdint>

namespace Controllers {
// WPADIsBusyForSync is not a completion notification. A sync request can be
// accepted before discovery starts. Only a STARTED -> FINISHED callback pair
// ends a request; the worker owns the deadline and cancellation.
class PairingSession {
public:
    enum class Result { Waiting, Finished, TimedOut };
    void Begin(uint32_t now){started_=now;phase_.store(1);}
    void Cancel(){phase_.store(0);}
    void Event(unsigned event){
        unsigned expected=event==0?1:2;
        if(event<=1)phase_.compare_exchange_strong(expected,event==0?2:3);
    }
    Result Poll(uint32_t now)const{
        if(phase_.load()==3)return Result::Finished;
        if(phase_.load()!=0&&uint32_t(now-started_)>=60000)return Result::TimedOut;
        return Result::Waiting;
    }
private:
    std::atomic<unsigned> phase_{0};uint32_t started_=0;
};
}
