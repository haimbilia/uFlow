#include "../wii/gamepad-probe/transport.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <utility>
using namespace GamePadProbe;
struct IO {
    uint32_t control=0x611e0003, interrupts=0, failStatus=0;
    unsigned phase=0, pos=0, delays=0;
    bool timeout=false, badCount=false, pending=false;
    uint8_t wire[11]={7,0,0,2,128,128,128,128,0,0,0};
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    IO() { const auto crc=Crc(wire,9);wire[9]=crc;wire[10]=crc>>8; }
    uint32_t Read(uint32_t offset) {
        if(offset==Control) return control;
        if(offset==InterruptEnable) return interrupts;
        if(offset==Status) return failStatus ? failStatus : timeout ? 0 : phase==1 ? 2 : 1;
        assert(offset==ReadData);
        if(pending) return 1<<16;
        if(phase<2 || pos==11) return 0;
        const auto value=((11-pos-(badCount?1:0))<<16)|wire[pos]; ++pos; return value;
    }
    void Write(uint32_t offset,uint32_t value) {
        assert(offset==Control || offset==Status || offset==WriteData || offset==Enqueue);
        writes.emplace_back(offset,value);
        if(offset==Control) control=value;
        if(offset==WriteData && (value&0x100)) ++phase;
    }
    void Delay() { ++delays; }
};
int main() {
    Sample sample;uint8_t packet[11]{};
    IO good;Transport<IO> reader(good);
    assert(reader.Read(sample,packet)==ReadResult::Ok && sample.buttons==Home);
    std::vector<uint32_t> commands;
    for(const auto &w:good.writes) if(w.first==WriteData) commands.push_back(w.second);
    assert((commands==std::vector<uint32_t>{0xe0,0x1f0,0xe1,0,1,2,3,4,5,6,7,8,9,0x10a}));
    IO disabled;disabled.control=0;assert(Transport<IO>(disabled).Read(sample,packet)==ReadResult::Unsupported && disabled.writes.empty());
    IO active;active.interrupts=1;assert(Transport<IO>(active).Read(sample,packet)==ReadResult::Unsupported && active.writes.empty());
    IO occupied;occupied.pending=true;assert(Transport<IO>(occupied).Read(sample,packet)==ReadResult::FifoError && occupied.writes.empty());
    IO timeout;timeout.timeout=true;assert(Transport<IO>(timeout).Read(sample,packet)==ReadResult::Timeout && timeout.delays==200 && timeout.phase==1);
    for(const auto bit:{4u,8u,16u}) { IO nack;nack.failStatus=bit;assert(Transport<IO>(nack).Read(sample,packet)==ReadResult::BusError && nack.phase==1); }
    IO count;count.badCount=true;assert(Transport<IO>(count).Read(sample,packet)==ReadResult::FifoError);
    IO crc;crc.wire[4]^=1;assert(Transport<IO>(crc).Read(sample,packet)==ReadResult::BadChecksum);
    IO mode;
    { ModeExperiment<IO> change(mode);assert(change.Begin() && mode.control==0x611e0001);
      mode.timeout=true;assert(Transport<IO>(mode,0x611e0001).Read(sample,packet)==ReadResult::Timeout); }
    assert(mode.control==0x611e0003);
    IO incompatible;incompatible.control=0x12345678;
    { ModeExperiment<IO> change(incompatible);assert(!change.Begin()); }
    assert(incompatible.writes.empty());
    IO normal;
    { ModeExperiment<IO> change(normal);assert(change.Begin());change.End();change.End(); }
    assert(normal.writes.size()==2 && normal.control==0x611e0003);
    std::puts("PASS: bounded input read, exact device/register commands, controller guards, timeout, bus errors, FIFO counts and CRC");
}
