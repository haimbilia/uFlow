#include "library.hpp"
#include "raw_launch.hpp"
#include "../module/src/RawSourcePath.h"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <iostream>
int main(){
    const std::string root="usb1:/Games/Test";
    std::filesystem::create_directories(root+"/code");
    std::filesystem::create_directories(root+"/content");
    std::ofstream(root+"/code/app.xml")<<"<app><title_id>0005000010157400</title_id></app>";
    std::ofstream(root+"/code/cos.xml")<<"<app><argstr>main.rpx</argstr></app>";
    unsigned char elf[20]={0x7f,'E','L','F',1,2};elf[19]=20;
    std::ofstream(root+"/code/main.rpx",std::ios::binary).write((char*)elf,20);
    GameEntry game;game.platform=Platform::WiiU;game.titleId="0005000010157400";
    game.absolutePath=root;game.rpxRelative=root+"/code/main.rpx";
    assert(ValidateRawGame(game).empty());
    assert(SaveContentSources({{Platform::WiiU,"usb1:/Games",true,3}}));
    const auto scan=ScanLibrary();assert(scan.games.size()==1);
    const auto &found=scan.games.front();
    assert(found.rpxRelative==root+"/code/main.rpx"&&found.contentRelative==root+"/content"&&found.codeRelative==root+"/code");
    assert(found.saveRelative.rfind("wiiu/raw-saves/",0)==0&&ValidateRawGame(found).empty());
    assert(RawSourcePath(game.rpxRelative.c_str())=="ufusb:/Games/Test/code/main.rpx");
    assert(RawSourcePath("Games/Test/code/main.rpx")=="fs:/vol/external01/Games/Test/code/main.rpx");
    // Each marker independently identifies a Wii VC wrapper. It launches
    // unchanged through HAI only when every file the USB handoff reads exists.
    for(const char *marker:{"/content/hif_000000.nfs","/code/nn_hai_user.rpl","/code/frisbiiU.rpx"}) {
        const auto path=root+marker;
        std::ofstream(path,std::ios::binary)<<"fixture";
        assert(ValidateRawGame(game)=="WII VC TITLE IS INCOMPLETE");
        assert(std::filesystem::file_size(path)==7);
        std::filesystem::remove(path);
        assert(ValidateRawGame(game).empty());
    }
    const char *haiFiles[]={"/code/nn_hai_user.rpl","/code/fw.img","/code/fw.tmd","/code/htk.bin",
                            "/code/rvlt.tik","/code/rvlt.tmd","/content/hif_000000.nfs"};
    for(const char *file:haiFiles)std::ofstream(root+file,std::ios::binary)<<"fixture";
    assert(ValidateRawGame(game).empty());
    std::filesystem::remove(root+"/code/rvlt.tmd");
    assert(ValidateRawGame(game)=="WII VC TITLE IS INCOMPLETE");
    std::ofstream(root+"/code/rvlt.tmd",std::ios::binary)<<"fixture";
    {
        const std::string sd="fs:/vol/external01/Games/VC";
        std::filesystem::create_directories(sd+"/code");
        std::ofstream(sd+"/code/nn_hai_user.rpl",std::ios::binary)<<"fixture";
        GameEntry vc=game;vc.absolutePath=sd;
        assert(ValidateRawGame(vc)=="WII VC DIRECT LAUNCH REQUIRES THE GAME ON USB");
    }
    for(const char *file:haiFiles)std::filesystem::remove(root+file);
    game.rpxRelative="missing";assert(!ValidateRawGame(game).empty());
    game.absolutePath="other:/Games/Test";assert(!ValidateRawGame(game).empty());
    std::cout<<"PASS: raw USB preflight and module USB/SD path resolution\n";
}
