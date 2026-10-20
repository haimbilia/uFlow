// Compiled into upstream WiiFlow v5.6.2. The game-loading implementation is
// unchanged: this entry point supplies an exact path instead of rescanning by ID.
#include "menu/menu.hpp"
#include "defines.h"
#include "types.h"
#include "booter/external_booter.hpp"
#include "wii_request.hpp"
#include "wii_return.hpp"
#include "loader/cios.h"
#include "gc_return_stub.hpp"
#include "usb_launch_probe.hpp"
#include "devicemounter/usbthread.h"

void CMenu::launchUFlowGame(const char *id, const char *path, bool gameCube, bool usbLoader) {
    gprintf("%s\n", WiiRequest::SelectorMarker);
    gprintf("%s\n", WiiRequest::UsbStorageMarker);
    gprintf("%s\n", WiiUsbProbe::Marker);
    FILE *log = fopen("sd:/wiiu/apps/uFlow/wii-launch.log", "wb");
    if(log) {
        fprintf(log,"%s\nid=%s\nrequested_path=%s\nios=%u\nusb_port=%d\nusb_partitions=%u\n",
            WiiUsbProbe::Marker,id,path,IOS_GetVersion(),USBStorage2_GetPort(),DeviceHandle.GetUSBPartitionCount());
        fflush(log);
    }
    WiiRequest::Request resolved{id,path,gameCube?WiiRequest::Kind::GameCube:WiiRequest::Kind::Wii};
    const auto resolution=WiiUsbProbe::ResolveWithRetry(resolved,[](int partition) {
        return WiiUsbProbe::Partition{DeviceHandle.IsInserted(partition),DeviceHandle.GetFSType(partition)==PART_FS_FAT,DeviceHandle.GetFSName(partition)};
    },[]() {
        KillUSBKeepAliveThread();
        DeviceHandle.UnMountAllUSB();
        USBStorage2_Deinit();
        DeviceHandle.SetMountUSB(true);
        return DeviceHandle.MountAllUSB();
    },log);
    const bool found=resolution==WiiUsbProbe::Result::Ready;
    path=resolved.path.c_str();
    std::string title;
    const auto error = !found ? std::string(WiiUsbProbe::Name(resolution)) :
        (gameCube ? WiiRequest::CheckGameCube(path, id, &title) : WiiRequest::CheckGame(path, id, &title));
    if (log) {
        fprintf(log, "%s\nid=%s\npath=%s\npreflight=%s\n", gameCube ? WiiRequest::GcBackendMarker : WiiRequest::BackendMarker,
                id, path, error.empty() ? "passed" : error.c_str());
        fclose(log);
    }
    if (!error.empty()) {
        gprintf("uFlow preflight: %s\n", error.c_str());
        _error(found?L"uFlow: Game file check failed. See wii-launch.log.":WiiUsbProbe::Message(resolution));
        main();
        return;
    }
    if (usbLoader) {
        const auto request=WiiRequest::Encode({id,path,gameCube?WiiRequest::Kind::GameCube:WiiRequest::Kind::Wii,WiiRequest::Launcher::USBLoaderGX});
        FILE *file=fopen(WiiRequest::UsbRequestPath,"wb");
        bool ok=false;
        if(file) {
            ok=fwrite(request.data(),1,request.size(),file)==request.size();
            if(fclose(file)!=0) ok=false;
        }
        if(!ok) { remove(WiiRequest::UsbRequestPath); _error(L"uFlow: Cannot save USB Loader GX request."); main(); return; }
        _launchHomebrew("sd:/apps/usbloader_gx/boot.dol", {WiiRequest::IsUsbPath(resolved.path)?"-sdmode=0":"-sdmode=1","--uflow"});
        remove(WiiRequest::UsbRequestPath);
        return;
    }
    if (gameCube) {
        auto *stub = reinterpret_cast<uint8_t *>(0x80001800);
        const bool patched = PatchGameCubeReturnStub(stub, 0x1024);
        if (patched) {
            DCFlushRange(stub, 0x1800);
            ICInvalidateRange(stub, 0x1800);
        }
        if (FILE *result = fopen("sd:/wiiu/apps/uFlow/wii-launch.log", "ab")) {
            fprintf(result, "gc_return_target=0001000248435641\ngc_return_stub=%s\n", patched ? "patched" : "unexpected");
            fclose(result);
        }
        if (!patched) {
            _error(L"uFlow: Unexpected GameCube return stub. Update backend.");
            main();
            return;
        }
    }
    // Use the resolved device without changing the saved library partition.
    dir_discHdr hdr{};
    memcpy(hdr.id, id, 6);
    strncpy(hdr.path, path, sizeof(hdr.path) - 1);
    for (size_t i = 0; i < title.size() && i < 63; ++i)
        hdr.title[i] = static_cast<unsigned char>(title[i]);
    hdr.type = gameCube ? TYPE_GC_GAME : TYPE_WII_GAME;
    m_directLaunch = true;
    m_uflowReturnToWiiU = !gameCube;
    m_uflowGameCube = gameCube;
    m_gameList.clear();
    MusicPlayer.Stop();
    m_gcfg2.load(fmt("%s/" GAME_SETTINGS2_FILENAME, m_settingsDir.c_str()));
    currentPartition = DeviceHandle.PathToDriveType(path);
    gprintf("%s: %s %s\n", WiiRequest::BackendMarker, id, path);
    if (gameCube) _launchGC(&hdr, false);
    else _launchWii(&hdr, false);
    // Match the normal _launch failure path; no second menu after cleanup.
    cleanup();
    ShutdownBeforeExit();
    Sys_Exit();
}

bool CMenu::patchUFlowReturn() {
    // Must run AFTER _loadGameIOS: reloading IOS would discard this setting.
    const int result = IsOnWiiU() && CurrentIOS.Type == IOS_TYPE_D2X
        ? UFlowPatchReturnToWiiU() : -1;
    FILE *log = fopen("sd:/wiiu/apps/uFlow/wii-launch.log", "ab");
    if (log) {
        fprintf(log, "return_target=0001000248435641\nreturn_patch=%d\n", result);
        fclose(log);
    }
    if (result < 0) {
        _error(L"uFlow: Wii U Menu return setup failed. See wii-launch.log.");
        return false;
    }
    return true;
}
