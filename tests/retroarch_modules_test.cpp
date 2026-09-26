// The shipped RetroArch modules must agree with the installer that ships them:
// the launcher id is the component id, the core and its .info are exactly the
// paths the installer may write, and the content type it creates defaults to
// this launcher, which accepts every file type that type scans.
#include "launchers.hpp"
#include "installer.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char **argv) {
    assert(argc == 2); // launcher-modules directory
    struct Expected { const char *file; int component; const char *core, *type; };
    const Expected shipped[] = {
        {"retroarch-ps1.ufmod", Setup::PlayStation, "pcsx_rearmed", "ps1"},
        {"retroarch-snes.ufmod", Setup::SnesRetroArch, "snes9x", "snes"},
        {"retroarch-nes.ufmod", Setup::NesRetroArch, "nestopia", "nes"},
        {"retroarch-gba.ufmod", Setup::GbaRetroArch, "mgba", "gba"},
    };
    for (const auto &expected : shipped) {
        std::ifstream input(std::string(argv[1]) + "/" + expected.file);
        assert(input.is_open());
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        LauncherModule module;
        assert(ParseLauncherModule(text, module) && module.error.empty());
        assert(module.protocol == kRetroArchProtocol && module.contentBundle == "wiiu/apps/retroarch.wuhb");
        assert(module.id == Setup::components[expected.component].id);
        const std::string core = std::string("retroarch/cores/") + expected.core + "_libretro";
        assert(module.executable == core + ".rpx");
        assert(Setup::Allowed(module.executable) && Setup::Allowed(module.contentBundle));
        assert(Setup::Allowed(std::string("retroarch/cores/info/") + expected.core + "_libretro.info"));

        std::vector<std::string> dirs;
        std::istringstream libraries(Setup::Configs(1u << expected.component, dirs)[0].second);
        bool found = false;
        for (std::string line; std::getline(libraries, line);) {
            std::vector<std::string> fields;
            std::istringstream row(line);
            for (std::string field; std::getline(row, field, '\t');) fields.push_back(field);
            if (fields.size() < 8 || fields[0] != expected.type) continue;
            found = true;
            assert(fields[7] == module.id); // the type's default launcher
            const LauncherDefinition launcher{module.id, module.name, LauncherKind::Emulator, module.system,
                                              module.extensions, module.executable, true, module.id};
            std::istringstream extensions(fields[4]);
            for (std::string extension; std::getline(extensions, extension, ',');)
                assert(LauncherMatchesFile(launcher, "fs:/vol/external01/wiiu/roms/" + fields[0] + "/Game" + extension));
        }
        assert(found);
    }
    std::cout << "PASS: shipped RetroArch modules match installer components, core paths and content types\n";
}
