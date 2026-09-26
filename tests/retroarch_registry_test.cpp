// The saved PS1 launcher predates the RetroArch module. Loading must attach the
// module to that entry without disturbing the user's naming, enabled state or
// the content type pointing at it. Module discovery is cached per process, so
// this fixture is built before the first LoadLaunchers() call.
#include "launchers.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

void Put(const std::string &path, const std::string &body) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << body;
}

int main() {
    const std::string root = "fs:/vol/external01";
    assert(!fs::exists(root));
    const std::string manifest =
        "UFLOW-MODULE-1\nid=ps1\nname=PlayStation (RetroArch)\nversion=1.0.0\n"
        "system=PlayStation\nextensions=.cue,.chd,.pbp\n"
        "executable=retroarch/cores/pcsx_rearmed_libretro.rpx\n"
        "protocol=retroarch-sysargs-v1\ncontent_bundle=wiiu/apps/retroarch.wuhb\n";
    Put(root + "/wiiu/apps/uFlow/launchers/retroarch-ps1.ufmod", manifest);
    auto duplicate=manifest;duplicate.replace(duplicate.find("id=ps1"),6,"id=duplicate");
    Put(root + "/wiiu/apps/uFlow/launchers/duplicate-a.ufmod",duplicate);
    Put(root + "/wiiu/apps/uFlow/launchers/duplicate-b.ufmod",duplicate);
    Put(root + "/wiiu/apps/uFlow/launchers/noods.ufmod",
        "UFLOW-MODULE-1\nid=noods\nname=NooDS\nversion=1\nsystem=DS\nextensions=.nds\n"
        "executable=wiiu/apps/noods/noods.wuhb\nprotocol=rom-request-v1\n"
        "request=wiiu/apps/uFlow/noods.req\ndependency=wiiu/apps/noods/capabilities.txt\ncapability=rom-request-v1\n");
    Put(root + "/retroarch/cores/pcsx_rearmed_libretro.rpx", "core");
    Put(root + "/wiiu/apps/retroarch.wuhb", "bundle");

    // What this session wrote for the user: a generic emulator entry aimed at
    // the RetroArch bundle, renamed and still enabled.
    const std::string legacy =
        "native\tWii U / Raw\t0\tWii U\t.rpx\t\t1\t\n"
        "wiiflow\tWiiFlow (Wii / GameCube)\t1\tWii / GameCube\t.wbfs,.iso\t\t1\t\n"
        "usbloadergx\tUSB Loader GX (Wii / GameCube)\t2\tWii / GameCube\t.wbfs,.iso\t\t1\t\n"
        "noods\tNooDS\t4\tNintendo DS\t.nds\twiiu/apps/noods/noods.wuhb\t1\tnoods\n"
        "nintendont\tNintendont via WiiFlow\t1\tGameCube\t.iso\t\t1\t\n"
        "ps1\tMy PlayStation\t4\tPlayStation\t.cue,.chd,.pbp\twiiu/apps/retroarch.wuhb\t1\t\n";
    Put(root + "/wiiu/apps/uFlow/launchers.cfg", legacy);

    auto launchers = LoadLaunchers();
    LauncherDefinition broken{"custom","Duplicate",LauncherKind::Emulator,"PS1",".cue",
                             "retroarch/cores/pcsx_rearmed_libretro.rpx",true,"duplicate"};
    broken.contentBundle="wiiu/apps/retroarch.wuhb";
    assert(LauncherReadiness(broken)=="DUPLICATE MODULE ID");
    const auto find = [&](const std::string &id) {
        return std::find_if(launchers.begin(), launchers.end(), [&](const auto &l) { return l.id == id; });
    };
    auto ps1 = find("ps1");
    assert(ps1 != launchers.end());
    assert(ps1->moduleId == "ps1");                                   // attached
    assert(ps1->executable == "retroarch/cores/pcsx_rearmed_libretro.rpx"); // now the core
    assert(ps1->name == "My PlayStation" && ps1->enabled);            // the user's choices survive
    assert(ps1->kind == LauncherKind::Emulator);                      // no new persisted kind
    assert(ps1->contentBundle == "wiiu/apps/retroarch.wuhb");            // module default filled in
    const auto count = launchers.size();

    // Idempotent: saving and loading again changes nothing and adds nothing.
    assert(SaveLaunchers(launchers));
    auto again = LoadLaunchers();
    assert(again.size() == count && find("ps1")->moduleId == "ps1");
    assert(std::count_if(again.begin(), again.end(), [](const auto &l) { return l.id == "ps1"; }) == 1);
    assert(ModuleReadiness(*FindLauncherModule("ps1")).empty());        // ready
    assert(LauncherReadiness(*find("ps1")) == "READY - ONE-PRESS LAUNCH");

    // The user keeps their core and RetroArch elsewhere: those choices win over
    // the module defaults on every later load.
    const std::string movedCore = "wiiu/apps/Libretro_Cores/pcsx_rearmed_libretro.rpx";
    const std::string movedBundle = "wiiu/apps/RetroArch Aroma/retroarch.wuhb";
    auto chosen = again;
    auto ps1Entry = std::find_if(chosen.begin(), chosen.end(), [](const auto &l) { return l.id == "ps1"; });
    ps1Entry->executable = movedCore;
    ps1Entry->contentBundle = movedBundle;
    assert(LauncherReadiness(*ps1Entry) == "CORE NOT FOUND - CHOOSE ITS .RPX");
    Put(root + "/" + movedCore, "core");
    assert(LauncherReadiness(*ps1Entry) == "RETROARCH NOT FOUND - CHOOSE ITS .WUHB");
    Put(root + "/" + movedBundle, "bundle");
    assert(LauncherReadiness(*ps1Entry) == "READY - ONE-PRESS LAUNCH");
    assert(SaveLaunchers(chosen));
    auto reloaded = LoadLaunchers();
    auto kept = std::find_if(reloaded.begin(), reloaded.end(), [](const auto &l) { return l.id == "ps1"; });
    assert(kept->executable == movedCore && kept->contentBundle == movedBundle && kept->moduleId == "ps1");
    // Only the RetroArch line carries the ninth field; others keep their format.
    std::ifstream saved(root + "/wiiu/apps/uFlow/launchers.cfg");
    for (std::string line; std::getline(saved, line);) {
        const auto tabs = std::count(line.begin(), line.end(), '\t');
        assert(tabs == (line.rfind("ps1\t", 0) == 0 || line.rfind("duplicate\t", 0) == 0 ? 8 : 7));
    }
    // Extensions are the user's: an added type survives reloads.
    kept->extensions = ".cue,.chd,.pbp,.m3u";
    assert(SaveLaunchers(reloaded));
    auto withM3u = LoadLaunchers();
    auto m3u = std::find_if(withM3u.begin(), withM3u.end(), [](const auto &l) { return l.id == "ps1"; });
    assert(m3u->extensions == ".cue,.chd,.pbp,.m3u" && m3u->moduleId == "ps1");
    assert(LauncherMatchesFile(*m3u, "fs:/vol/external01/wiiu/roms/ps1/Final Fantasy VII/FF7.m3u"));

    // LAUNCH MODULE set to NONE sticks: saved as "-", never re-attached.
    m3u->moduleId.clear();
    m3u->moduleOptOut = true;
    m3u->executable = movedBundle; // "opens the emulator" now means RetroArch itself
    m3u->contentBundle.clear();
    assert(SaveLaunchers(withM3u));
    {
        std::ifstream cfg(root + "/wiiu/apps/uFlow/launchers.cfg");
        bool marked = false;
        for (std::string line; std::getline(cfg, line);)
            if (line.rfind("ps1\t", 0) == 0) marked = line.size() > 2 && line.substr(line.size() - 2) == "\t-";
        assert(marked);
    }
    auto detached = LoadLaunchers();
    auto none = std::find_if(detached.begin(), detached.end(), [](const auto &l) { return l.id == "ps1"; });
    assert(none->moduleId.empty() && none->moduleOptOut && none->executable == movedBundle);
    assert(!IsRetroArchLauncher(*none));
    assert(LauncherReadiness(*none) == "APP FOUND / ROM BROWSER");

    // Which modules a launcher may pick: RetroArch's protocol works with any
    // emulator entry; a rom-request module only with its own emulator.
    LauncherModule noodsModule;
    noodsModule.id = "noods";
    noodsModule.protocol = "rom-request-v1";
    LauncherDefinition custom{"launcher_7", "RetroArch SNES", LauncherKind::Emulator, "SNES", ".sfc,.smc", "", true, ""};
    const LauncherModule &retroArchModule = *FindLauncherModule("ps1");
    assert(CompatibleLauncherModule(retroArchModule, custom));
    assert(!CompatibleLauncherModule(noodsModule, custom));
    custom.id = "noods";
    assert(CompatibleLauncherModule(noodsModule, custom));
    custom.kind = LauncherKind::WiiFlow;
    assert(!CompatibleLauncherModule(retroArchModule, custom));

    // Choosing RetroArch on an existing NooDS entry must survive discovery of
    // NooDS's own installed module. Never silently switch the user's backend.
    auto reassigned = detached;
    auto noods = std::find_if(reassigned.begin(), reassigned.end(), [](const auto &l) { return l.id == "noods"; });
    noods->moduleId = "ps1";
    noods->executable = movedCore;
    noods->contentBundle = movedBundle;
    assert(SaveLaunchers(reassigned));
    auto reopened = LoadLaunchers();
    const auto reassignedNoods = std::find_if(reopened.begin(), reopened.end(), [](const auto &l) { return l.id == "noods"; });
    assert(reassignedNoods->moduleId == "ps1");
    assert(reassignedNoods->executable == movedCore && reassignedNoods->contentBundle == movedBundle);

    // A bundle that is not a .wuhb, or a path escaping the SD, is refused.
    ps1Entry->contentBundle = "wiiu/apps/retroarch.rpx";
    assert(!SaveLaunchers(chosen));
    ps1Entry->contentBundle = "../retroarch.wuhb";
    assert(!SaveLaunchers(chosen));

    // A customized entry that is not the documented legacy shape is left alone.
    const LauncherModule *module = FindLauncherModule("ps1");
    LauncherDefinition other{"ps1", "Something else", LauncherKind::Emulator, "PlayStation", ".iso",
                             "wiiu/apps/other/other.wuhb", true, ""};
    assert(!LegacyRetroArchEntry(other, *module));                    // different executable
    other.executable = "wiiu/apps/retroarch.wuhb";
    assert(!LegacyRetroArchEntry(other, *module));                    // different extensions
    other.extensions = ".CUE, .chd,.pbp";                             // spelling only
    assert(LegacyRetroArchEntry(other, *module));
    other.kind = LauncherKind::WiiFlow;
    assert(!LegacyRetroArchEntry(other, *module));
    other.kind = LauncherKind::Emulator;
    other.id = "ps1-custom";
    assert(!LegacyRetroArchEntry(other, *module));                    // only the module's own id

    std::cout << "PASS: legacy PS1 entry attaches, user paths/extensions persist, NONE sticks, module compatibility, custom entries untouched\n";
}
