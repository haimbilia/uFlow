#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// GamePad mapping in Nintendo's vWii HAI firmware (the title's code/fw.img).
// Stock HAI presents the GamePad as a Wii Remote with a Classic Controller
// attached, so games without Classic Controller support ignore it. The edits
// follow nfs2iso2nfs (FIX94): "-wiimote" always emits Wii Remote core buttons,
// "-horizontal" feeds them for sideways play and "-nocc" stops reporting the
// Classic Controller, which Wii Remote-only games otherwise wait on.
// nfs2iso2nfs targets r590; r569 (e.g. Donkey Kong Country Returns) has the
// mapper at other offsets but no known "-nocc" site. Running an r590 fw.img
// from another title with DKCR hard-froze the console, so each title keeps
// its own firmware.
// Only uFlow's staged copy is edited, never the title on USB.
namespace HaiGamePad {
enum class Mode { Classic, WiiRemote, Sideways };

// result = (original & ~writeMask) | (value & writeMask), big-endian halfword,
// applied only when (original & mask) == expect.
struct Edit {
    std::uint32_t offset;
    std::uint16_t mask, expect, writeMask, value;
};

// ES module version tag; its only use is a startup debug printf of the tag.
inline constexpr char kEsVersion[] = "$IOSVersion:  ES: 04/02/12 14:00:51 64M $";

// Streams fw.img once and records the anchors the edits need.
class Scanner {
public:
    std::uint32_t revision = 0; // "svn-r569" -> 569
    std::uint32_t wiimote = 0, wiimoteCount = 0;
    std::uint32_t horizontal = 0, horizontalCount = 0;
    std::uint32_t esVersion = 0, esVersionCount = 0; // HAI ES module anchor (r569)
    std::uint32_t nocc = 0, noccCount = 0;           // Classic Controller report (r590)

    // Chunks must be fed in order; call Finish() after the last one.
    void Feed(const std::uint8_t *data, std::size_t size) {
        window_.insert(window_.end(), data, data + size);
        const std::uint64_t base = position_ + size - window_.size(); // offset of window_[0]
        std::size_t i = 0;
        for (; i + Overlap <= window_.size(); ++i)
            Match(i, window_.size() - i, static_cast<std::uint32_t>(base + i));
        window_.erase(window_.begin(), window_.begin() + static_cast<std::ptrdiff_t>(i));
        position_ += size;
    }
    void Finish() {
        const std::uint64_t base = position_ - window_.size();
        for (std::size_t i = 0; i < window_.size(); ++i)
            Match(i, window_.size() - i, static_cast<std::uint32_t>(base + i));
        window_.clear();
    }

private:
    static constexpr std::size_t Overlap = 64; // longer than every pattern
    std::vector<std::uint8_t> window_;
    std::uint64_t position_ = 0;

    void Match(std::size_t i, std::size_t left, std::uint32_t at) {
        static const std::uint8_t wii[]   = {0x16, 0x13, 0x1C, 0x02, 0x40, 0x9A, 0x1C, 0x13};
        static const std::uint8_t horiz[] = {0x4A, 0x71, 0x42, 0x13, 0xD0, 0xD2, 0x9B, 0x00};
        static const std::uint8_t cc[]    = {0x78, 0x93, 0x21, 0x10, 0x2B, 0x02, 0xD1, 0xB7};
        const std::uint8_t *p = window_.data() + i;
        if (left >= sizeof(wii) && !std::memcmp(p, wii, sizeof(wii))) {
            if (!wiimoteCount++) wiimote = at;
        }
        if (left >= sizeof(horiz) && !std::memcmp(p, horiz, sizeof(horiz))) {
            if (!horizontalCount++) horizontal = at;
        }
        if (left >= sizeof(cc) && !std::memcmp(p, cc, sizeof(cc))) {
            if (!noccCount++) nocc = at;
        }
        if (left >= sizeof(kEsVersion) - 1 && !std::memcmp(p, kEsVersion, sizeof(kEsVersion) - 1)) {
            if (!esVersionCount++) esVersion = at;
        }
        if (!revision && left >= 8 && !std::memcmp(p, "svn-r", 5)) {
            std::uint32_t value = 0;
            std::size_t digits = 0;
            while (digits < 4 && 5 + digits < left && p[5 + digits] >= '0' && p[5 + digits] <= '9')
                value = value * 10 + (p[5 + digits++] - '0');
            if (digits >= 3) revision = value;
        }
    }
};

// Plans the edits. Returns false (with a reason) instead of guessing.
inline bool Plan(Mode mode, const Scanner &s, std::vector<Edit> &edits, std::string &why) {
    edits.clear();
    if (mode == Mode::Classic) return true;
    if (s.wiimoteCount != 1) {
        why = "wii-remote-anchor-count=" + std::to_string(s.wiimoteCount);
        return false;
    }
    // asrs r3,r2,#24 -> movs r3,#0: always take the Wii Remote core-button path.
    edits.push_back({s.wiimote, 0xFFFF, 0x1613, 0xFFFF, 0x2300});

    if (s.revision == 590) {
        if (s.noccCount != 1) {
            why = "r590-nocc-anchor-count=" + std::to_string(s.noccCount);
            edits.clear();
            return false;
        }
        // bne -> b: the Classic Controller is never reported as connected.
        edits.push_back({s.nocc + 6, 0xFFFF, 0xD1B7, 0xFFFF, 0xE0B7});
        if (mode == Mode::WiiRemote) return true;
        if (s.horizontalCount != 1) {
            why = "r590-sideways-anchor-count=" + std::to_string(s.horizontalCount);
            edits.clear();
            return false;
        }
        // nfs2iso2nfs writes the low byte of "ldr rX,[sp,#slot*4]" at these offsets.
        static const std::pair<std::uint32_t, std::uint8_t> r590[] = {
                {0x07, 0x02}, {0x0F, 0x03}, {0x1D, 0x01}, {0x2B, 0x00},
                {0x65, 0x07}, {0x75, 0x06}, {0x85, 0x04}, {0x95, 0x05}};
        for (const auto &[offset, slot] : r590)
            edits.push_back({s.horizontal + offset - 1, 0xFE00, 0x9A00, 0x00FF, slot});
        return true;
    }
    if (mode == Mode::WiiRemote) return true;
    if (s.revision == 569) {
        // Mapper at 0x138B298C: stack slots 0-3 = GamePad left/right/down/up,
        // 4-7 = B/A/X/Y; each Wii Remote bit tests one slot (twice in the chain).
        // Sideways: Left<-Down, Right<-Up, Down<-Right, Up<-Left, B<-Y, A<-X, 1<-B, 2<-A.
        static const struct { std::uint32_t offset; std::uint16_t from, to; } r569[] = {
                {0x062, 0x9B00, 0x9B02}, {0x068, 0x9A01, 0x9A03}, {0x06E, 0x9B02, 0x9B01},
                {0x074, 0x9A03, 0x9A00}, {0x08C, 0x9A04, 0x9A07}, {0x092, 0x9B05, 0x9B06},
                {0x098, 0x9A06, 0x9A04}, {0x09E, 0x9B07, 0x9B05}, {0x0BA, 0x9B07, 0x9B05},
                {0x0CA, 0x9A06, 0x9A04}, {0x0DE, 0x9B05, 0x9B06}, {0x0EC, 0x9A04, 0x9A07},
                {0x12E, 0x9A03, 0x9A00}, {0x140, 0x9B02, 0x9B01}, {0x148, 0x9A01, 0x9A03}};
        for (const auto &e : r569)
            edits.push_back({s.wiimote + e.offset, 0xFFFF, e.from, 0xFFFF, e.to});
        return true;
    }
    why = "sideways-unsupported-revision=r" + std::to_string(s.revision);
    edits.clear();
    return false;
}

// "Wii Menu" in a Wii game's HOME Menu launches the System Menu, which strands a
// GamePad-only player in vWii. Like uFlow's WiiFlow/Nintendont paths, send it to
// the built-in Return to Wii U Menu channel (HCVA) instead. The ES ioctl
// handlers for GetTicketViewCnt, GetTicketViews and LaunchTitle load the title
// ID with two ldr instructions; each pair becomes a call to a 28-byte routine
// (placed over the ES version tag) that substitutes 00010002-48435641 for
// 00000001-00000002. The game then fetches HCVA's own ticket view and launches
// it, so ES keeps validating the real HCVA ticket.
inline bool PlanReturnToWiiU(const Scanner &s, std::vector<Edit> &edits, std::string &why) {
    edits.clear();
    if (s.esVersionCount != 1) {
        why = "es-anchor-count=" + std::to_string(s.esVersionCount);
        return false;
    }
    constexpr std::uint32_t tag = 0x2010BB1C; // virtual address of kEsVersion (r569 ES)
    const auto at = [&](std::uint32_t va) { return s.esVersion + va - tag; };
    // Routine at tag: NUL first so the startup printf prints an empty line.
    static const std::uint16_t routine[] = {
            0x0000,                 // bb1c: "" terminator (never executed)
            0x6810, 0x6851, 0xE001, // bb1e: remap_r2: ldr r0,[r2]; ldr r1,[r2,#4]; b common
            0x6818, 0x6859,         // bb24: remap_r3: ldr r0,[r3]; ldr r1,[r3,#4]
            0x2801, 0xD105,         // bb28: common: cmp r0,#1; bne ret
            0x2902, 0xD103,         //       cmp r1,#2; bne ret
            0x2001, 0x0400, 0x3002, //       r0 = 0x00010002
            0x4903,                 //       ldr r1,[pc,#12] -> 0x48435641
            0x4770,                 // bb38: ret: bx lr
            0x46C0, 0x46C0, 0x46C0, 0x46C0, 0x46C0, // padding (nop)
            0x4843, 0x5641};        // bb44: literal 0x48435641 ("HCVA")
    static_assert(sizeof(routine) == sizeof(kEsVersion) - 1 + 3, "routine replaces tag + padding");
    for (std::size_t i = 0; i < sizeof(routine) / 2; ++i) {
        const auto c0 = static_cast<std::uint8_t>(i * 2 < sizeof(kEsVersion) - 1 ? kEsVersion[i * 2] : 0);
        const auto c1 = static_cast<std::uint8_t>(i * 2 + 1 < sizeof(kEsVersion) - 1 ? kEsVersion[i * 2 + 1] : 0);
        edits.push_back({at(tag) + std::uint32_t(i * 2), 0xFFFF, std::uint16_t(c0 << 8 | c1), 0xFFFF, routine[i]});
    }
    const auto bl = [&](std::uint32_t from, std::uint32_t to, std::uint16_t first, std::uint16_t second) {
        const std::int32_t offset = std::int32_t(to) - std::int32_t(from + 4);
        edits.push_back({at(from), 0xFFFF, first, 0xFFFF, std::uint16_t(0xF000 | ((offset >> 12) & 0x7FF))});
        edits.push_back({at(from) + 2, 0xFFFF, second, 0xFFFF, std::uint16_t(0xF800 | ((offset >> 1) & 0x7FF))});
    };
    bl(0x2010041E, 0x2010BB24, 0x6818, 0x6859); // LaunchTitle ioctl (0x08)
    bl(0x2010059C, 0x2010BB24, 0x6818, 0x6859); // GetTicketViewCnt (0x12)
    bl(0x201005F4, 0x2010BB1E, 0x6810, 0x6851); // GetTicketViews (0x13)
    return true;
}

// Applies edits to an in-memory image (host tests); all-or-nothing.
inline bool Apply(std::vector<std::uint8_t> &image, const std::vector<Edit> &edits) {
    for (const auto &e : edits) {
        if (e.offset + 2 > image.size()) return false;
        const std::uint16_t v = std::uint16_t(image[e.offset] << 8 | image[e.offset + 1]);
        if ((v & e.mask) != e.expect) return false;
    }
    for (const auto &e : edits) {
        const std::uint16_t v = std::uint16_t(image[e.offset] << 8 | image[e.offset + 1]);
        const std::uint16_t r = std::uint16_t((v & ~e.writeMask) | (e.value & e.writeMask));
        image[e.offset] = std::uint8_t(r >> 8);
        image[e.offset + 1] = std::uint8_t(r);
    }
    return true;
}

inline const char *Name(Mode mode) {
    return mode == Mode::Sideways ? "sideways" : mode == Mode::WiiRemote ? "wiimote" : "classic";
}
inline bool Parse(const std::string &text, Mode &mode) {
    if (text == "classic") mode = Mode::Classic;
    else if (text == "wiimote") mode = Mode::WiiRemote;
    else if (text == "sideways") mode = Mode::Sideways;
    else return false;
    return true;
}
// Wii games (rvlt.tmd game code) that only read a sideways Wii Remote.
inline Mode Default(const char gameCode[4]) {
    return !std::memcmp(gameCode, "SF8", 3) ? Mode::Sideways : Mode::Classic; // Donkey Kong Country Returns
}
} // namespace HaiGamePad
