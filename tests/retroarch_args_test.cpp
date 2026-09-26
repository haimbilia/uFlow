#include "../app/source/retroarch_args.hpp"
#include <cassert>
#include <iostream>

using namespace RetroArchArgs;

int main() {
    const std::string core = "fs:/vol/external01/retroarch/cores/pcsx_rearmed_libretro.rpx";
    const std::string game = "fs:/vol/external01/wiiu/roms/ps1/Crash Bandicoot/Crash Bandicoot.cue";
    std::string why;

    // Exactly two NUL-terminated strings, both terminators counted.
    auto bytes = Pack(core, game, &why);
    assert(why.empty() && bytes.size() == core.size() + game.size() + 2);
    assert(bytes[core.size()] == '\0' && bytes.back() == '\0');
    assert(std::string(bytes.data()) == core);                       // no quoting around the space
    assert(std::string(bytes.data() + core.size() + 1) == game);
    std::vector<std::string> argv;
    assert(Unpack(bytes.data(), bytes.size(), argv) && argv.size() == 2 && argv[0] == core && argv[1] == game);

    // Apostrophes and UTF-8 survive byte for byte.
    const std::string fancy = "fs:/vol/external01/wiiu/roms/ps1/Tomb Raider II - L'aventure/\xE3\x82\xBC\xE3\x83\xAB\xE3\x83\x80.cue";
    bytes = Pack(core, fancy, &why);
    assert(why.empty() && Unpack(bytes.data(), bytes.size(), argv) && argv[1] == fancy);

    // Path forms convert both ways.
    assert(VolumePath("wiiu/apps/retroarch.wuhb") == "/vol/external01/wiiu/apps/retroarch.wuhb");
    assert(CafePath("retroarch/cores/x.rpx") == "fs:/vol/external01/retroarch/cores/x.rpx");
    assert(SdRelative(core) == "retroarch/cores/pcsx_rearmed_libretro.rpx");
    assert(SdRelative("relative/stays") == "relative/stays");

    // Refusals: never a partly packed buffer.
    for (const std::string &bad : {std::string("sd:/wiiu/roms/x.cue"),                    // wrong prefix
                                   std::string("fs:/vol/external01/"),                     // no file
                                   std::string("fs:/vol/external01/a/../b.cue"),           // traversal
                                   std::string("fs:/vol/external01/a//b.cue"),             // empty segment
                                   std::string("fs:/vol/external01/a\\b.cue"),             // backslash
                                   std::string("fs:/vol/external01/a\nb.cue"),             // control byte
                                   std::string("")}) {
        assert(Pack(core, bad, &why).empty() && !why.empty());
        assert(Pack(bad, game, &why).empty() && !why.empty());
    }

    // Size boundary: the largest accepted block, then one byte over.
    const std::string base = "fs:/vol/external01/";
    const std::size_t room = kMaxArgumentBytes - core.size() - 2 - base.size();
    const std::string longest = base + std::string(room, 'a');
    bytes = Pack(core, longest, &why);
    assert(why.empty() && bytes.size() == kMaxArgumentBytes);
    assert(Pack(core, longest + "a", &why).empty() && !why.empty());

    // An executable too long for MochaRPXLoadInfo::path is refused.
    const std::string longCore = base + std::string(kMaxExecutableRelative, 'c') + ".rpx";
    assert(Pack(longCore, game, &why).empty() && !why.empty());

    // Unpack rejects what the receiver could not read back.
    assert(!Unpack(nullptr, 4, argv));
    assert(!Unpack("abc", 3, argv));                                  // not terminated
    assert(!Unpack(bytes.data(), 0, argv));
    std::vector<char> oversized(kMaxArgumentBytes + 1, '\0');
    assert(!Unpack(oversized.data(), oversized.size(), argv));
    // An embedded NUL simply ends a string: three strings is not our two.
    const char three[] = "a\0b\0c\0";
    assert(Unpack(three, sizeof(three) - 1, argv) && argv.size() == 3);

    std::cout << "PASS: RetroArch native argument packing, path forms, refusals and size boundaries\n";
}
