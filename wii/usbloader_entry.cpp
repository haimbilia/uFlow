// Integration for the pinned USB Loader GX revision; original game boot code retained.
#include "usbloader_entry.hpp"
#ifndef UFLOW_HOST_TEST
#include "usbloader/GameBooter.hpp"
#include "usbloader/MountGamePartition.h"
#include "settings/CSettings.h"
#include "settings/SettingsEnums.h"
#include "gecko.h"
#endif
WiiRequest::Request gUFlowRequest;
bool gUFlowActive=false;

int UFlowUsbLaunch() {
    gprintf("%s\n",WiiRequest::UsbMarker);
    if(!WiiRequest::Consume(WiiRequest::UsbRequestPath,gUFlowRequest) ||
        gUFlowRequest.launcher!=WiiRequest::Launcher::USBLoaderGX) return -1;
    const bool gc=gUFlowRequest.kind==WiiRequest::Kind::GameCube;
    const auto error=gc?WiiRequest::CheckGameCube(gUFlowRequest.path,gUFlowRequest.id):
                        WiiRequest::CheckGame(gUFlowRequest.path,gUFlowRequest.id);
    if(FILE *log=fopen("sd:/wiiu/apps/uFlow/usb-launch.log","wb")) {
        fprintf(log,"%s\nid=%s\npath=%s\npreflight=%s\n",WiiRequest::UsbMarker,
                gUFlowRequest.id.c_str(),gUFlowRequest.path.c_str(),error.empty()?"passed":error.c_str());
        fclose(log);
    }
    if(!error.empty()) return -1;
    discHdr header{};
    FILE *file=fopen(gUFlowRequest.path.c_str(),"rb");
    if(!file) return -1;
    unsigned char wbfs[32]{};
    bool ok=true;
    if(!gc) ok=fread(wbfs,1,32,file)==32 && fseek(file,1L<<wbfs[8],SEEK_SET)==0;
    // Read the real header, including audio streaming fields; path is internal GX data.
    if(ok) ok=fread(&header,1,0x60,file)==0x60;
    fclose(file);
    if(!ok) return -1;
    header.tid=0;
    header.is_ciso=0;
    if(gc) header.magic=header.gc_magic;
    header.type=gc?TYPE_GAME_GC_IMG:TYPE_GAME_WII_IMG;
    snprintf(header.path,sizeof(header.path),"%s",gUFlowRequest.path.c_str());
    Settings.SDMode=1;
    gUFlowActive=true;
    if(!gc && MountGamePartition(false)<0) { gUFlowActive=false; return -1; }
    const int result=GameBooter::BootGame(&header);
    gUFlowActive=false;
    return result;
}
