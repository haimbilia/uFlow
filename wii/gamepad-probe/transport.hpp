#pragma once
#include "packet.hpp"

namespace GamePadProbe {
// Experimental foreground transport, reconstructed from HAI r569's I2C
// protocol. Caller must establish exclusive ownership; never use alongside
// HAI's polling thread or blindly attach to a game's running IOS.
enum class ReadResult { Ok, Unsupported, Timeout, BusError, FifoError, BadChecksum };
constexpr uint32_t Control = 0x00, WriteData = 0x04, Enqueue = 0x08,
                   ReadData = 0x0c, InterruptEnable = 0x10, Status = 0x14;
template<class IO> class Transport {
    IO &io;
    uint32_t expectedControl;
    ReadResult Wait(uint32_t done) {
        for (unsigned attempt = 0; attempt < 200; ++attempt) {
            const uint32_t state = io.Read(Status) & 0x1f;
            if (state & 0x1c) { io.Write(Status, state); return ReadResult::BusError; }
            if (state & done) { io.Write(Status, state); return ReadResult::Ok; }
            io.Delay(); // 500 us in the console adapter; <=100 ms total.
        }
        return ReadResult::Timeout;
    }
    void Queue(uint32_t value) { io.Write(WriteData, value); io.Write(Enqueue, 1); }
public:
    explicit Transport(IO &device, uint32_t control = 0x611e0003) : io(device), expectedControl(control) {}
    // Do not change bus speed, mode, interrupt routing or reset any hardware.
    // This is the exact configuration observed in the first IOS58 console test.
    bool Compatible() { return io.Read(Control) == expectedControl && io.Read(InterruptEnable) == 0; }
    ReadResult Read(Sample &sample, uint8_t (&packet)[11]) {
        if (!Compatible()) return ReadResult::Unsupported;
        // The count shares a destructive FIFO register with the data. This
        // check may consume ONE old byte; if occupied, stop instead of draining
        // or issuing a new request. Caller must already have exclusive ownership.
        if ((io.Read(ReadData) >> 16) & 0xff) return ReadResult::FifoError;
        io.Write(Status, 0x1f);
        Queue(0xe0); // 7-bit device 0x70, write register selector only.
        Queue(0x1f0); // selector F0, end transaction.
        auto result = Wait(2);
        if (result != ReadResult::Ok) return result;
        io.Write(Status, 0x1f);
        Queue(0xe1); // same device, read.
        for (unsigned i = 0; i < 11; ++i) Queue(i | (i == 10 ? 0x100 : 0));
        result = Wait(1);
        if (result != ReadResult::Ok) return result;
        for (unsigned i = 0; i < 11; ++i) {
            const uint32_t value = io.Read(ReadData);
            if (((value >> 16) & 0xff) != 11-i) return ReadResult::FifoError;
            packet[i] = static_cast<uint8_t>(value);
        }
        if ((io.Read(ReadData) >> 16) & 0xff) return ReadResult::FifoError;
        return Packet(packet, 11, sample) ? ReadResult::Ok : ReadResult::BadChecksum;
    }
};
// Separate, explicitly selected experiment. HAI's initialization passes mode=0
// to 0x138b5b7c, which clears bit 1. Keep the observed divider/timing unchanged.
template<class IO> class ModeExperiment {
    IO &io;
    bool active = false;
public:
    explicit ModeExperiment(IO &device) : io(device) {}
    bool Begin() {
        if (active || io.Read(Control) != 0x611e0003 || io.Read(InterruptEnable) != 0) return false;
        io.Write(Control, 0x611e0001); active = true;
        return io.Read(Control) == 0x611e0001;
    }
    void End() { if (active) { io.Write(Control, 0x611e0003); active = false; } }
    ~ModeExperiment() { End(); }
};
inline const char *Name(ReadResult result) {
    switch (result) {
    case ReadResult::Ok: return "ok";
    case ReadResult::Unsupported: return "unsupported-controller";
    case ReadResult::Timeout: return "timeout";
    case ReadResult::BusError: return "bus-error";
    case ReadResult::FifoError: return "unexpected-fifo";
    case ReadResult::BadChecksum: return "bad-checksum";
    }
    return "unknown";
}
}
