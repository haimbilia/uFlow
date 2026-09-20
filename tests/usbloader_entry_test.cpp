#include "usbloader_entry.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
MockSettings Settings;
int boots=0,mounts=0,mountResult=0;
void gprintf(const char *,...) {}
int MountGamePartition(bool) { assert(gUFlowActive && Settings.SDMode==1); ++mounts; return mountResult; }
int GameBooter::BootGame(discHdr *header) {
    assert(gUFlowActive && UFlowMatches(header->id));
    assert(header->path==gUFlowRequest.path);
    assert(header->tid==0 && header->is_ciso==0);
    assert(header->type==(gUFlowRequest.kind==WiiRequest::Kind::Wii?TYPE_GAME_WII_IMG:TYPE_GAME_GC_IMG));
    assert(!std::filesystem::exists(WiiRequest::UsbRequestPath));
    ++boots; return 42;
}
void Write(const std::string &path,const std::string &data) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream(path,std::ios::binary)<<data;
}
int main() {
    assert(UFlowUsbLaunch()==-1 && !gUFlowActive);
    std::string wbfs(1024,'\0');
    wbfs.replace(0,4,"WBFS"); wbfs[8]=9; wbfs[9]=21; wbfs[12]=1;
    wbfs.replace(512,6,"RZTE01"); wbfs.replace(512+0x18,4,"\x5d\x1c\x9e\xa3",4);
    WiiRequest::Request request{"RZTE01","sd:/custom/Wii/Sports.wbfs",WiiRequest::Kind::Wii,WiiRequest::Launcher::USBLoaderGX};
    Write(request.path,wbfs); Write(WiiRequest::UsbRequestPath,WiiRequest::Encode(request));
    assert(UFlowUsbLaunch()==42 && boots==1 && mounts==1 && !gUFlowActive);
    assert(UFlowUsbLaunch()==-1 && boots==1);
    mountResult=-1; Write(WiiRequest::UsbRequestPath,WiiRequest::Encode(request));
    assert(UFlowUsbLaunch()==-1 && boots==1 && !gUFlowActive);
    std::string iso(0x4000,'\0');
    iso.replace(0,6,"GMSE01"); iso.replace(0x1c,4,"\xc2\x33\x9f\x3d",4);
    iso[0x422]=0x25; iso[0x426]=0x30; iso[0x42b]=12;
    request={"GMSE01","sd:/custom/GC/Game Title.iso",WiiRequest::Kind::GameCube,WiiRequest::Launcher::USBLoaderGX};
    Write(request.path,iso); Write(WiiRequest::UsbRequestPath,WiiRequest::Encode(request));
    assert(UFlowUsbLaunch()==42 && boots==2 && mounts==2 && !gUFlowActive);
    request.id="GBAD01"; Write(WiiRequest::UsbRequestPath,WiiRequest::Encode(request));
    assert(UFlowUsbLaunch()==-1 && boots==2 && !gUFlowActive);
    Write(WiiRequest::UsbRequestPath,"bad request");
    assert(UFlowUsbLaunch()==-1 && !std::filesystem::exists(WiiRequest::UsbRequestPath));
    puts("PASS: USB Loader GX exact custom paths, one-shot consumption, preflight/mount failures, Wii/GC dispatch");
}
