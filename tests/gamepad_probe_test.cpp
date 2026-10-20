#include "../wii/gamepad-probe/packet.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace GamePadProbe;
int main() {
    char query[]="--query", program[]="boot.dol", other[]="--query-extra", empty[]="";
    char *network[]={query}, *normal[]={program,query}, *unknown[]={other}, *blank[]={empty};
    assert(QueryRequested(1,network) && QueryRequested(2,normal));
    assert(!QueryRequested(0,nullptr) && !QueryRequested(3,normal));
    assert(!QueryRequested(1,unknown) && !QueryRequested(1,blank));
    const auto *check = reinterpret_cast<const uint8_t *>("123456789");
    assert(Crc(check, 9) == 0x6f91); // CRC-16/MCRF4XX check value.
    uint8_t wire[11] = {1, 0, 0, 2, 128, 128, 128, 128, 0, 0, 0};
    auto crc = Crc(wire, 9); wire[9] = crc; wire[10] = crc >> 8;
    Sample sample{};
    assert(Packet(wire, 11, sample) && sample.buttons == Home && !sample.shutdown);
    for (size_t byte = 0; byte < sizeof(wire); ++byte)
        for (unsigned bit = 0; bit < 8; ++bit) {
            wire[byte] ^= 1u << bit;
            assert(!Packet(wire, 11, sample));
            wire[byte] ^= 1u << bit;
        }
    for (size_t size = 0; size < 11; ++size) assert(!Packet(wire, size, sample));
    assert(!Packet(wire, 12, sample) && !Packet(nullptr, 11, sample));
    assert(!Payload(wire, 8, sample) && !Payload(nullptr, 9, sample));
    HomeEdges edges;
    assert(!edges.Update({Home, 1, 0, false})); // Initially held, not a new press.
    assert(!edges.Update({0, 2, 0, false}));
    assert(edges.Update({Home, 3, 0, false}));
    assert(!edges.Update({Home, 3, 0, false}));
    assert(!edges.Update({0, 3, 0, false})); // Inconsistent/stale sequence ignored.
    assert(!edges.Update({Home, 4, 0, false}));
    assert(!edges.Update({0, 255, 127, false}));
    assert(edges.Update({Home, 0, 0, false})); // Counter wrap.
    assert(!edges.Update({0, 1, 0, true})); // Shutdown is never a HOME event.
    assert(!edges.Update({Home, 2, 0, false}));
    edges.Reset();
    assert(!edges.Update({Home, 3, 0, false}));
    std::puts("PASS: GamePad packet CRC, malformed inputs, HOME edges, stale frames, wrap and shutdown isolation");
}
