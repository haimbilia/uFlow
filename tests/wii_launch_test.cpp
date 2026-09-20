#include "wii_launch.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
void Write(const std::string &path, const std::string &data) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path, std::ios::binary) << data;
}
int main() {
    const std::string root = "fs:/vol/external01";
    assert(!fs::exists(root)); // disposable working directory only
    WiiRequest::Request request{"RLUE4Q", "sd:/my games/Disney's Bolt [RLUE4Q].wbfs"}, parsed;
    const auto text = WiiRequest::Encode(request);
    assert(WiiRequest::Decode(text, parsed) && parsed.id == request.id && parsed.path == request.path);
    for (const auto &bad : {std::string(), text + "extra", text.substr(0, text.size()-1),
                           std::string(300, 'a'), text + std::string(1, '\0')})
        assert(!WiiRequest::Decode(bad, parsed) && parsed.id.empty());
    for (const auto *path : {"usb:/wbfs/foo.wbfs", "sd:/../foo.wbfs", "sd:/foo/../bar.wbfs",
                            "sd:/foo//bar.wbfs", "sd:/foo/./bar.wbfs", "sd:/foo\nbar.wbfs",
                            "sd:/foo:bar.wbfs", "sd:/foo\\bar.wbfs", "sd:/game.iso"})
        assert(!WiiRequest::ValidPath(path));
    assert(!WiiRequest::ValidPath("sd:/" + std::string(247, 'a') + ".wbfs"));
    assert(WiiRequest::ValidPath("sd:/" + std::string(246, 'a') + ".wbfs"));
    assert(!WiiRequest::ValidId("RLUE4"));
    assert(!WiiRequest::ValidId("RLUE4!"));
    assert(!WiiRequest::ValidId("rlue4q"));

    GameEntry game;
    game.platform = Platform::Wii;
    game.titleId = request.id;
    game.absolutePath = root + request.path.substr(3);
    std::string wbfs(1024, '\0');
    wbfs.replace(0, 4, "WBFS"); wbfs[8] = 9; wbfs[9] = 21; wbfs[12] = 1;
    wbfs.replace(512, 6, game.titleId);
    wbfs.replace(512 + 0x18, 4, "\x5d\x1c\x9e\xa3", 4);
    wbfs.replace(512 + 32, 12, "Disney: Bolt");
    Write(game.absolutePath, wbfs);
    assert(PrepareWiiLaunch(game, parsed).find("BACKEND") != std::string::npos);
    // Marker straddles the streaming-read boundary.
    Write(root + "/apps/wiiflow/boot.dol", std::string(8190, 'x') + WiiRequest::BackendMarker);
    assert(PrepareWiiLaunch(game, parsed).empty());
    assert(parsed.path == request.path);
    std::string title;
    assert(WiiRequest::CheckGame(game.absolutePath, game.titleId, &title).empty());
    assert(title == "Disney: Bolt");
    fs::permissions(game.absolutePath, fs::perms::owner_read);
    assert(PrepareWiiLaunch(game, parsed).find("READ-ONLY") != std::string::npos);
    fs::permissions(game.absolutePath, fs::perms::owner_read | fs::perms::owner_write);
    game.titleId = "RZTE01";
    assert(PrepareWiiLaunch(game, parsed).find("DOES NOT MATCH") != std::string::npos);
    game.titleId = request.id;
    wbfs[8] = 31; Write(game.absolutePath, wbfs);
    assert(!PrepareWiiLaunch(game, parsed).empty());
    Write(game.absolutePath, "WBFS");
    assert(!PrepareWiiLaunch(game, parsed).empty());
    game.absolutePath = "fs:/vol/usb01/wbfs/game.wbfs";
    assert(PrepareWiiLaunch(game, parsed).find("SD GAMES ONLY") != std::string::npos);

    Write(WiiRequest::CafeRequest, text);
    assert(WiiRequest::Consume(WiiRequest::CafeRequest, parsed));
    assert(!fs::exists(WiiRequest::CafeRequest));
    assert(!WiiRequest::Consume(WiiRequest::CafeRequest, parsed));
    Write(WiiRequest::CafeRequest, text + "stale");
    assert(!WiiRequest::Consume(WiiRequest::CafeRequest, parsed));
    assert(!fs::exists(WiiRequest::CafeRequest));
    Write(WiiRequest::CafeRequest, text);
    CancelWiiLaunchRequest();
    assert(!fs::exists(WiiRequest::CafeRequest));
    GameEntry gc;
    gc.platform = Platform::GameCube;
    gc.absolutePath = root + "/games/Test Game/game.iso";
    std::string iso(0x4000, '\0');
    iso.replace(0,6,"GMSE01");
    iso.replace(0x1c,4,"\xc2\x33\x9f\x3d",4);
    iso.replace(32,9,"Test Game");
    const auto word=[&](size_t p,uint32_t value) {
        for (int i=0;i<4;++i) iso[p+i]=static_cast<char>(value>>(24-i*8));
    };
    word(0x420,0x2500); word(0x424,0x3000); word(0x428,12);
    Write(gc.absolutePath,iso);
    assert(PrepareGameCubeLaunch(gc,parsed).find("BACKEND")!=std::string::npos);
    Write(root+"/apps/wiiflow/boot.dol",WiiRequest::GcBackendMarker);
    assert(PrepareGameCubeLaunch(gc,parsed).find("NINTENDONT")!=std::string::npos);
    Write(root+"/apps/nintendont/boot.dol",std::string(256,'x'));
    assert(PrepareGameCubeLaunch(gc,parsed).empty());
    assert(parsed.id=="GMSE01" && parsed.kind==WiiRequest::Kind::GameCube);
    const auto gcText=WiiRequest::Encode(parsed);
    assert(WiiRequest::Decode(gcText,parsed) && parsed.kind==WiiRequest::Kind::GameCube);
    assert(!WiiRequest::Decode(std::string(WiiRequest::Magic)+gcText.substr(std::strlen(WiiRequest::GcMagic)),parsed));
    Write(WiiRequest::CafeRequest,gcText);
    assert(WiiRequest::Consume(WiiRequest::CafeRequest,parsed));
    assert(!WiiRequest::Consume(WiiRequest::CafeRequest,parsed));
    fs::permissions(gc.absolutePath,fs::perms::owner_read);
    assert(PrepareGameCubeLaunch(gc,parsed).empty()); // ISO never requires rb+
    gc.launcher=GameLauncher::USBLoaderGX;
    assert(PrepareGameCubeLaunch(gc,parsed).find("BRIDGE")!=std::string::npos);
    Write(root+"/apps/wiiflow/boot.dol",std::string(WiiRequest::GcBackendMarker)+WiiRequest::SelectorMarker);
    assert(PrepareGameCubeLaunch(gc,parsed).find("USB LOADER GX")!=std::string::npos);
    Write(root+"/apps/usbloader_gx/boot.dol",WiiRequest::UsbMarker);
    assert(PrepareGameCubeLaunch(gc,parsed).empty());
    assert(parsed.launcher==WiiRequest::Launcher::USBLoaderGX);
    assert(WiiRequest::Decode(WiiRequest::Encode(parsed),parsed));
    assert(parsed.kind==WiiRequest::Kind::GameCube && parsed.launcher==WiiRequest::Launcher::USBLoaderGX);
    WiiRequest::Request usbWii{"RZTE01","sd:/wbfs/Sports/game.wbfs",WiiRequest::Kind::Wii,WiiRequest::Launcher::USBLoaderGX};
    assert(WiiRequest::Decode(WiiRequest::Encode(usbWii),parsed));
    assert(parsed.kind==WiiRequest::Kind::Wii && parsed.launcher==WiiRequest::Launcher::USBLoaderGX);
    assert(!WiiRequest::Decode("UFLOW-USB-BAD-1\nRZTE01\nsd:/games/a.iso\n",parsed));
    gc.launcher=GameLauncher::WiiFlow;
    fs::permissions(gc.absolutePath,fs::perms::owner_read|fs::perms::owner_write);
    gc.titleId="GLME01";
    assert(PrepareGameCubeLaunch(gc,parsed).find("DOES NOT MATCH")!=std::string::npos);
    gc.titleId.clear();
    iso[6]=1; Write(gc.absolutePath,iso);
    assert(PrepareGameCubeLaunch(gc,parsed).find("DISC 1")!=std::string::npos);
    iso[6]=0; word(0x428,0xffffffff); Write(gc.absolutePath,iso);
    assert(PrepareGameCubeLaunch(gc,parsed).find("TRUNCATED")!=std::string::npos);
    Write(gc.absolutePath,"RVZ");
    assert(!PrepareGameCubeLaunch(gc,parsed).empty());
    gc.absolutePath="fs:/vol/usb01/games/game.iso";
    assert(PrepareGameCubeLaunch(gc,parsed).find("SD GAMES ONLY")!=std::string::npos);
    for(const auto *path : {"sd:/../game.iso","sd:/game.rvz","sd:/foo//game.iso","usb:/game.iso"})
        assert(!WiiRequest::ValidPath(path,WiiRequest::Kind::GameCube));
    std::cout << "PASS: GameCube request kind, ISO header/ranges/disc ID, read-only ISO, Nintendont/backend checks\n";
    std::cout << "PASS: Wii preflight, header/ID, permissions, exact paths, request consumption\n";
}
