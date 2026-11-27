#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Bounded, non-blocking recording. No allocations or filesystem calls in Record.
class RawPerformanceTrace {
    struct Event { uint32_t ms, us, bytes; int32_t handle, result; char kind[24],path[224]; };
    Event events[2048]{};
    Event pauses[256]{};
    uint32_t pauseWritten=0;
    std::atomic<bool> enabled{false};
    std::atomic_flag guard=ATOMIC_FLAG_INIT;
    std::atomic<uint32_t> dropped{0};
    uint32_t written=0;
public:
    std::atomic<uint32_t> syncReads{0},asyncSubmits{0},opens{0};
    std::atomic<uint32_t> frameCalls{0},frameGuardDrops{0};
    void Begin(bool active) { enabled=false; written=0; pauseWritten=0; dropped=0; syncReads=0; asyncSubmits=0; opens=0; frameCalls=0; frameGuardDrops=0; guard.clear(); enabled=active; }
    bool Enabled() const { return enabled.load(std::memory_order_relaxed); }
    void Record(const char *kind,uint32_t ms,uint32_t us,int handle,int result,uint32_t bytes,const char *path="") {
        if(!Enabled())return;
        if(guard.test_and_set(std::memory_order_acquire)){++dropped;return;}
        if(Enabled()) {
            auto &e=events[written++%2048]; e.ms=ms;e.us=us;e.handle=handle;e.result=result;e.bytes=bytes;
            std::snprintf(e.kind,sizeof(e.kind),"%s",kind);std::snprintf(e.path,sizeof(e.path),"%s",path?path:"");
            if(!std::strcmp(kind,"frame-gap") || us>=50000) pauses[pauseWritten++%256]=e;
        }
        guard.clear(std::memory_order_release);
    }
    void StopAndWrite(const char *path) {
        if(!enabled.exchange(false))return;
        if(guard.test_and_set(std::memory_order_acquire))return; // Never stall process exit for diagnostics.
        FILE *f=std::fopen(path,"a");
        if(f) {
            std::fprintf(f,"BEGIN raw-performance-v1 sync_reads=%u async_submits=%u opens=%u overwritten=%u contention_drops=%u\nAsync times measure submission only; sync and async counts may overlap through nested APIs. Paths truncated at 223 bytes.\nms\tkind\tduration_us\thandle\tresult\tbytes\tpath\n",syncReads.load(),asyncSubmits.load(),opens.load(),written>2048?written-2048:0,dropped.load());
            const auto first=written>2048?written-2048:0;
            for(auto i=first;i<written;++i){const auto &e=events[i%2048];std::fprintf(f,"%u\t%s\t%u\t%08x\t%d\t%u\t%s\n",e.ms,e.kind,e.us,e.handle,e.result,e.bytes,e.path);}
            std::fprintf(f,"FRAME_PROBE calls=%u guard_drops=%u; PAUSES retained=%u overwritten=%u (duplicates from main ring possible)\n",frameCalls.load(),frameGuardDrops.load(),pauseWritten<256?pauseWritten:256,pauseWritten>256?pauseWritten-256:0);
            for(auto i=pauseWritten>256?pauseWritten-256:0;i<pauseWritten;++i){const auto &e=pauses[i%256];std::fprintf(f,"PAUSE\t%u\t%s\t%u\t%08x\t%d\t%u\t%s\n",e.ms,e.kind,e.us,e.handle,e.result,e.bytes,e.path);}
            std::fprintf(f,"END raw-performance-v1\n");std::fclose(f);
        }
        guard.clear(std::memory_order_release);
    }
};
void RawPerformanceBegin(bool active);
void RawPerformanceWrite();
