#pragma once
#include <cstddef>
#include <cstdint>

// Observed in the local HAI r569 OHCI1 poll routine, VA 0x138b5ce0.
// The bus returns nine payload bytes and a little-endian reflected CRC-16.
// The shared HAI memory buffer contains only the nine validated payload bytes.
namespace GamePadProbe {
// wiiload uses the explicit argument string as argv[0], whereas a normal
// launcher includes the executable name first. Accept only these two forms.
inline bool HasOption(int argc, char **argv, const char *expected) {
    if (!argv) return false;
    const char *option = argc == 1 ? argv[0] : argc == 2 ? argv[1] : nullptr;
    if (!option) return false;
    for (size_t i = 0; ; ++i) {
        if (option[i] != expected[i]) return false;
        if (!expected[i]) return true;
    }
}
inline bool QueryRequested(int argc, char **argv) { return HasOption(argc, argv, "--query"); }
constexpr uint16_t Home = 0x0002;
inline uint16_t Crc(const uint8_t *data, size_t size) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = static_cast<uint16_t>((crc >> 1) ^ ((crc & 1) ? 0x8408 : 0));
    }
    return crc;
}
struct Sample {
    uint16_t buttons = 0;
    uint8_t sequence = 0, sequenceExtra = 0;
    bool shutdown = false;
};
inline bool Payload(const uint8_t *data, size_t size, Sample &sample) {
    if (!data || size != 9) return false;
    sample = {static_cast<uint16_t>((data[2] << 8) | data[3]),
              data[0], static_cast<uint8_t>(data[1] & 0x7f), bool(data[1] & 0x80)};
    return true;
}
inline bool Packet(const uint8_t *data, size_t size, Sample &sample) {
    if (!data || size != 11 || Crc(data, 9) != uint16_t(data[9] | (data[10] << 8)))
        return false;
    return Payload(data, 9, sample);
}
// Logging only: never requests reset, title launch or shutdown. A held HOME at
// startup is not a press; stale/repeated frames cannot generate another event.
class HomeEdges {
    Sample last{};
    bool valid = false;
public:
    void Reset() { valid = false; }
    bool Update(const Sample &sample) {
        if (sample.shutdown) { Reset(); return false; }
        if (valid && sample.sequence == last.sequence && sample.sequenceExtra == last.sequenceExtra)
            return false;
        const bool pressed = valid && !(last.buttons & Home) && (sample.buttons & Home);
        last = sample; valid = true;
        return pressed;
    }
};
}
