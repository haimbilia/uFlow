#include "wii_launch.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#ifndef UFLOW_HOST_TEST
#include <malloc.h>
#include <nn/cmpt/cmpt.h>
#include <padscore/kpad.h>
#endif

namespace {
constexpr const char *Backend = "fs:/vol/external01/apps/wiiflow/boot.dol";
constexpr const char *Temporary = "fs:/vol/external01/wiiu/apps/uFlow/wii-launch.tmp";
bool BackendReady(const char *marker = WiiRequest::BackendMarker, const char *path = Backend) {
    std::ifstream file(path, std::ios::binary);
    char buffer[8192];
    std::string tail;
    while (file.read(buffer, sizeof(buffer)) || file.gcount()) {
        tail.append(buffer, static_cast<size_t>(file.gcount()));
        if (tail.find(marker) != std::string::npos) return true;
        if (tail.size() > 64) tail.erase(0, tail.size() - 64);
    }
    return false;
}
std::string SelectBackend(const GameEntry &game, WiiRequest::Request &request) {
    if(WiiRequest::IsUsbPath(request.path) && !BackendReady(WiiRequest::UsbStorageMarker))
        return "UPDATE UFLOW WIIFLOW BACKEND FOR USB GAMES";
    if(game.launcher != GameLauncher::USBLoaderGX) return {};
    if(!BackendReady(WiiRequest::SelectorMarker)) return "UPDATE UFLOW WIIFLOW BRIDGE FOR LAUNCHER SELECTION";
    if(!BackendReady(WiiRequest::UsbMarker,"fs:/vol/external01/apps/usbloader_gx/boot.dol"))
        return "INSTALL UFLOW USB LOADER GX BACKEND ON THIS SD";
    if(WiiRequest::IsUsbPath(request.path) && !BackendReady(WiiRequest::UsbStorageMarker,"fs:/vol/external01/apps/usbloader_gx/boot.dol"))
        return "UPDATE UFLOW USB LOADER GX BACKEND FOR USB GAMES";
    request.launcher=WiiRequest::Launcher::USBLoaderGX;
    return {};
}
}

std::string PrepareWiiLaunch(const GameEntry &game, WiiRequest::Request &request) {
    request = {};
    if (game.platform != Platform::Wii || game.installed) return "SELECT A WII WBFS GAME";
    const std::string root = "fs:/vol/external01/";
    const bool usb = game.absolutePath.rfind("usb1:/",0)==0;
    if (!usb && game.absolutePath.rfind(root, 0) != 0) return "SELECT A WII GAME ON SD OR FAT32 USB";
    WiiRequest::Request candidate{game.titleId, usb ? game.absolutePath : "sd:/" + game.absolutePath.substr(root.size())};
    if (!WiiRequest::ValidPath(candidate.path)) return "WII PATH INVALID OR TOO LONG (MAX 255 BYTES)";
    // The CafeOS FAT32 mount is deliberately read-only. The vWii backend does
    // its own writable preflight after the transition.
    const auto error = WiiRequest::CheckGame(game.absolutePath, candidate.id,nullptr,&candidate.id,!usb);
    if (!error.empty()) return error;
    if (!BackendReady()) return "INSTALL THE UFLOW WIIFLOW BACKEND ON THIS SD";
    const auto selectionError=SelectBackend(game,candidate);
    if(!selectionError.empty()) return selectionError;
    request = candidate;
    return {};
}

void CancelWiiLaunchRequest() {
    std::remove(WiiRequest::CafeRequest);
    std::remove(Temporary);
    std::remove("fs:/vol/external01/wiiu/apps/uFlow/usb-launch.req");
}

std::string PrepareGameCubeLaunch(const GameEntry &game, WiiRequest::Request &request) {
    request = {};
    if (game.platform != Platform::GameCube || game.installed) return "SELECT A GAMECUBE ISO";
    const std::string root = "fs:/vol/external01/";
    if (game.absolutePath.rfind(root,0) != 0) return "GAMECUBE LAUNCH CURRENTLY SUPPORTS SD GAMES ONLY";
    WiiRequest::Request candidate{game.titleId, "sd:/"+game.absolutePath.substr(root.size()), WiiRequest::Kind::GameCube};
    if (!WiiRequest::ValidPath(candidate.path,candidate.kind)) return "GAMECUBE ISO PATH INVALID OR TOO LONG";
    const auto error = WiiRequest::CheckGameCube(game.absolutePath,game.titleId,nullptr,&candidate.id);
    if (!error.empty()) return error;
    if (!BackendReady(WiiRequest::GcBackendMarker)) return "INSTALL THE UFLOW GAMECUBE BACKEND ON THIS SD";
    std::ifstream nin("fs:/vol/external01/apps/nintendont/boot.dol",std::ios::binary|std::ios::ate);
    if (!nin || nin.tellg() < 0x100) return "INSTALL NINTENDONT AT SD:/apps/nintendont/boot.dol";
    const auto selectionError=SelectBackend(game,candidate);
    if(!selectionError.empty()) return selectionError;
    request = candidate;
    return {};
}

std::string LaunchWiiGame(const WiiRequest::Request &request) {
    const auto text = WiiRequest::Encode(request);
    if (text.empty()) return "INVALID WII LAUNCH REQUEST";
#ifdef UFLOW_HOST_TEST
    return "VWII TRANSITION REQUIRES CONSOLE";
#else
    // SDL owns ProcUI. Do not initialize another WHBProc loop here.
    // KPAD is initialized once by uFlow at startup for Wii Remote navigation.
    int result = -1;
    for (const auto screen : {CMPT_SCREEN_TYPE_BOTH, CMPT_SCREEN_TYPE_DRC, CMPT_SCREEN_TYPE_TV}) {
        result = CMPTAcctSetScreenType(screen);
        if (result >= 0 && (result = CMPTCheckScreenState()) >= 0) break;
    }
    if (result < 0) return "VWII DISPLAY SETUP FAILED: " + std::to_string(result);
    uint32_t size = 0;
    result = CMPTGetDataSize(&size);
    if (result < 0 || size == 0 || size > 16 * 1024 * 1024)
        return "VWII SETUP FAILED: " + std::to_string(result);
    void *data = memalign(0x40, size);
    if (!data) return "NOT ENOUGH MEMORY FOR VWII LAUNCH";
    FILE *file = std::fopen(Temporary, "wb");
    bool written = false;
    if (file) {
        written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
        if (std::fclose(file) != 0) written = false;
    }
    if (!written || (std::remove(WiiRequest::CafeRequest) != 0 && errno != ENOENT) ||
        std::rename(Temporary, WiiRequest::CafeRequest) != 0) {
        std::free(data);
        CancelWiiLaunchRequest();
        return "CANNOT SAVE WII LAUNCH REQUEST TO SD";
    }
    // DWFA is installed by FIX94's vWii-aware WiiFlow Channel Installer.
    result = CMPTLaunchTitle(data, size, 0x0001000144574641ULL);
    std::free(data);
    if (result < 0) {
        CancelWiiLaunchRequest();
        return "WIIFLOW FORWARDER LAUNCH FAILED: " + std::to_string(result);
    }
    return {};
#endif
}
