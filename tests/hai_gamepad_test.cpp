#include "../app/source/hai_gamepad.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace HaiGamePad;

static void Put16(std::vector<std::uint8_t> &b, std::size_t at, std::uint16_t v) {
    b[at] = std::uint8_t(v >> 8); b[at + 1] = std::uint8_t(v);
}
static std::uint16_t Get16(const std::vector<std::uint8_t> &b, std::size_t at) {
    return std::uint16_t(b[at] << 8 | b[at + 1]);
}
static Scanner Scan(const std::vector<std::uint8_t> &image, std::size_t chunk) {
    Scanner s;
    for (std::size_t at = 0; at < image.size(); at += chunk)
        s.Feed(image.data() + at, std::min(chunk, image.size() - at));
    s.Finish();
    return s;
}
static void Text(std::vector<std::uint8_t> &b, std::size_t at, const char *text) {
    for (std::size_t i = 0; text[i]; ++i) b[at + i] = std::uint8_t(text[i]);
}

// Stock r569 mapper chain (offsets relative to the Wii Remote anchor).
static std::vector<std::uint8_t> R569(std::size_t anchor) {
    std::vector<std::uint8_t> b(0x4000, 0);
    Text(b, 0x20, "[OHCI1] Hai 0.9-svn-r569");
    const std::uint8_t wii[] = {0x16, 0x13, 0x1C, 0x02, 0x40, 0x9A, 0x1C, 0x13};
    std::copy(std::begin(wii), std::end(wii), b.begin() + anchor);
    const std::pair<unsigned, std::uint16_t> stock[] = {
            {0x062, 0x9B00}, {0x068, 0x9A01}, {0x06E, 0x9B02}, {0x074, 0x9A03}, {0x08C, 0x9A04},
            {0x092, 0x9B05}, {0x098, 0x9A06}, {0x09E, 0x9B07}, {0x0BA, 0x9B07}, {0x0CA, 0x9A06},
            {0x0DE, 0x9B05}, {0x0EC, 0x9A04}, {0x12E, 0x9A03}, {0x140, 0x9B02}, {0x148, 0x9A01}};
    for (const auto &[offset, value] : stock) Put16(b, anchor + offset, value);
    return b;
}

int main(int argc, char **argv) {
    std::vector<Edit> edits;
    std::string why;
    // Chunk boundaries must not hide or duplicate anchors.
    for (std::size_t chunk : {1u, 7u, 15u, 16u, 17u, 4096u, 0x10000u}) {
        auto image = R569(0x1B33);
        const auto s = Scan(image, chunk);
        assert(s.revision == 569 && s.wiimoteCount == 1 && s.wiimote == 0x1B33 && s.horizontalCount == 0);
        assert(Plan(Mode::Classic, s, edits, why) && edits.empty());
        assert(Plan(Mode::WiiRemote, s, edits, why) && edits.size() == 1);
        assert(Plan(Mode::Sideways, s, edits, why) && edits.size() == 16);
        assert(Apply(image, edits));
        assert(Get16(image, 0x1B33) == 0x2300);
        assert(Get16(image, 0x1B33 + 0x062) == 0x9B02 && Get16(image, 0x1B33 + 0x074) == 0x9A00);
        assert(Get16(image, 0x1B33 + 0x09E) == 0x9B05 && Get16(image, 0x1B33 + 0x148) == 0x9A03);
        assert(!Apply(image, edits)); // already applied: expectations no longer hold, nothing changes
        assert(Get16(image, 0x1B33) == 0x2300);
    }
    {   // A changed instruction at any site rejects the whole plan, untouched.
        auto image = R569(0x800);
        Put16(image, 0x800 + 0x0DE, 0x9B04);
        const auto before = image;
        assert(Plan(Mode::Sideways, Scan(image, 64), edits, why) && !Apply(image, edits) && image == before);
    }
    {   // Unknown revision: Wii Remote only; sideways refused rather than guessed.
        auto image = R569(0x800);
        Text(image, 0x20, "[OHCI1] Hai 0.9-svn-r600");
        const auto s = Scan(image, 99);
        assert(Plan(Mode::WiiRemote, s, edits, why) && edits.size() == 1);
        assert(!Plan(Mode::Sideways, s, edits, why) && edits.empty() && why == "sideways-unsupported-revision=r600");
    }
    {   // Missing or duplicated Wii Remote anchor.
        std::vector<std::uint8_t> image(0x1000, 0);
        Text(image, 0, "svn-r569");
        assert(!Plan(Mode::WiiRemote, Scan(image, 33), edits, why) && why == "wii-remote-anchor-count=0");
        auto twice = R569(0x400);
        const std::uint8_t wii[] = {0x16, 0x13, 0x1C, 0x02, 0x40, 0x9A, 0x1C, 0x13};
        std::copy(std::begin(wii), std::end(wii), twice.begin() + 0x3000);
        assert(!Plan(Mode::Sideways, Scan(twice, 33), edits, why) && why == "wii-remote-anchor-count=2");
    }
    {   // r590 layout from nfs2iso2nfs: low bytes of the ldr immediates.
        std::vector<std::uint8_t> image(0x2000, 0);
        Text(image, 0x10, "svn-r590");
        const std::uint8_t wii[]   = {0x16, 0x13, 0x1C, 0x02, 0x40, 0x9A, 0x1C, 0x13};
        const std::uint8_t horiz[] = {0x4A, 0x71, 0x42, 0x13, 0xD0, 0xD2, 0x9B, 0x00};
        std::copy(std::begin(wii), std::end(wii), image.begin() + 0x100);
        std::copy(std::begin(horiz), std::end(horiz), image.begin() + 0x600);
        for (unsigned offset : {0x0Eu, 0x1Cu, 0x2Au, 0x64u, 0x74u, 0x84u, 0x94u}) Put16(image, 0x600 + offset, 0x9A00);
        {   // without the Classic Controller report site r590 is refused, never half-mapped
            const auto s = Scan(image, 5);
            assert(!Plan(Mode::WiiRemote, s, edits, why) && edits.empty() && why == "r590-nocc-anchor-count=0");
        }
        const std::uint8_t cc[] = {0x78, 0x93, 0x21, 0x10, 0x2B, 0x02, 0xD1, 0xB7};
        std::copy(std::begin(cc), std::end(cc), image.begin() + 0xA00);
        const auto s = Scan(image, 5);
        assert(s.noccCount == 1 && s.nocc == 0xA00);
        assert(Plan(Mode::WiiRemote, s, edits, why) && edits.size() == 2);
        assert(Plan(Mode::Sideways, s, edits, why) && edits.size() == 10 && Apply(image, edits));
        assert(Get16(image, 0x100) == 0x2300 && Get16(image, 0xA06) == 0xE0B7);
        assert(image[0x607] == 0x02 && image[0x60F] == 0x03 && image[0x61D] == 0x01 && image[0x62B] == 0x00);
        assert(image[0x665] == 0x07 && image[0x675] == 0x06 && image[0x685] == 0x04 && image[0x695] == 0x05);
    }
    char dkcr[4] = {'S', 'F', '8', 'E'}, other[4] = {'R', 'M', 'G', 'E'};
    assert(Default(dkcr) == Mode::Sideways && Default(other) == Mode::Classic);
    Mode mode{};
    assert(Parse("sideways", mode) && mode == Mode::Sideways && !Parse("SIDEWAYS", mode));
    {   // Return to Wii U: routine over the ES tag + three call sites, all verified.
        const std::uint32_t tag = 0xBB1C + 0x2000; // arbitrary file position of the tag
        const auto site = [&](std::uint32_t va) { return tag + va - 0x2010BB1C; };
        const auto make = [&](std::uint16_t lastLoad) {
            std::vector<std::uint8_t> image(0x10000, 0);
            Text(image, tag, kEsVersion);
            Put16(image, site(0x2010041E), 0x6818); Put16(image, site(0x20100420), 0x6859);
            Put16(image, site(0x2010059C), 0x6818); Put16(image, site(0x2010059E), 0x6859);
            Put16(image, site(0x201005F4), 0x6810); Put16(image, site(0x201005F6), lastLoad);
            return image;
        };
        auto image = make(0x6851);
        const auto s = Scan(image, 37);
        assert(s.esVersionCount == 1 && s.esVersion == tag);
        assert(PlanReturnToWiiU(s, edits, why) && edits.size() == 28 && Apply(image, edits));
        assert(image[tag] == 0 && Get16(image, tag + 0x28) == 0x4843 && Get16(image, tag + 0x2A) == 0x5641);
        assert(Get16(image, site(0x2010041E)) == 0xF00B && Get16(image, site(0x20100420)) == 0xFB81);
        assert(Get16(image, site(0x201005F4)) == 0xF00B && Get16(image, site(0x201005F6)) == 0xFA93);
        auto changed = make(0x6852); // a different instruction refuses the whole plan
        const auto before = changed;
        assert(PlanReturnToWiiU(Scan(changed, 64), edits, why) && !Apply(changed, edits) && changed == before);
        std::vector<std::uint8_t> none(0x1000, 0);
        assert(!PlanReturnToWiiU(Scan(none, 64), edits, why) && why == "es-anchor-count=0");
    }
    if (argc > 1) { // optional: a locally dumped title fw.img (never committed)
        std::ifstream in(argv[1], std::ios::binary);
        std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(in)), {});
        const auto s = Scan(image, 0x10000);
        assert(s.revision == 569 && s.wiimoteCount == 1 && s.wiimote == 0x11B34);
        assert(Plan(Mode::Sideways, s, edits, why) && Apply(image, edits));
        assert(s.esVersionCount == 1 && s.esVersion == 0x169F74);
        assert(PlanReturnToWiiU(s, edits, why) && Apply(image, edits));
        std::cout << "real r569 fw.img: 16 GamePad + 28 return edits verified\n";
    }
    if (argc > 2) { // optional: a locally dumped stock r590 fw.img (never committed)
        std::ifstream in(argv[2], std::ios::binary);
        std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(in)), {});
        const auto s = Scan(image, 0x10000);
        assert(s.revision == 590 && s.noccCount == 1 && s.horizontalCount == 1 && s.esVersionCount == 1);
        assert(Plan(Mode::Sideways, s, edits, why) && edits.size() == 10);
        std::vector<Edit> all = edits;
        assert(PlanReturnToWiiU(s, edits, why));
        all.insert(all.end(), edits.begin(), edits.end());
        assert(Apply(image, all));
        std::cout << "real r590 fw.img: 10 GamePad (incl. no-CC) + 28 return edits verified\n";
    }
    std::cout << "PASS: HAI GamePad mapping scan, plan, all-or-nothing apply, r569/r590, refusals\n";
}
